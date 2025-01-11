// Copyright 2019-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_OLC_ART_HPP
#define UNODB_DETAIL_OLC_ART_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__fwd/ostream.h>
// IWYU pragma: no_include <ostream>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>  // IWYU pragma: keep
#include <optional>
#include <stack>
#include <tuple>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "art_internal_impl.hpp"
#include "assert.hpp"
#include "node_type.hpp"
#include "optimistic_lock.hpp"
#include "portability_arch.hpp"
#include "qsbr_ptr.hpp"

namespace unodb {

class olc_db;

namespace detail {

// The OLC header contains an [optimistic_lock].
struct [[nodiscard]] olc_node_header {
  // Return a reference to the [optimistic_lock].
  [[nodiscard]] constexpr optimistic_lock& lock() const noexcept {
    return m_lock;
  }

#ifndef NDEBUG
  static void check_on_dealloc(const void* ptr) noexcept {
    static_cast<const olc_node_header*>(ptr)->m_lock.check_on_dealloc();
  }
#endif

 private:
  mutable optimistic_lock m_lock;  // The lock.
};
static_assert(std::is_standard_layout_v<olc_node_header>);

class olc_inode;
class olc_inode_4;
class olc_inode_16;
class olc_inode_48;
class olc_inode_256;

using olc_inode_defs =
    unodb::detail::basic_inode_def<olc_inode, olc_inode_4, olc_inode_16,
                                   olc_inode_48, olc_inode_256>;

using olc_node_ptr = basic_node_ptr<olc_node_header>;

template <class>
class db_inode_qsbr_deleter;  // IWYU pragma: keep

template <class, class>
class db_leaf_qsbr_deleter;  // IWYU pragma: keep

template <class Header, class Db>
[[nodiscard]] auto make_db_leaf_ptr(art_key, value_view, Db&);

struct olc_impl_helpers;

using olc_art_policy = unodb::detail::basic_art_policy<
    unodb::olc_db, unodb::in_critical_section, unodb::optimistic_lock,
    unodb::optimistic_lock::read_critical_section, unodb::detail::olc_node_ptr,
    olc_inode_defs, unodb::detail::db_inode_qsbr_deleter,
    unodb::detail::db_leaf_qsbr_deleter>;

using olc_db_leaf_unique_ptr = olc_art_policy::db_leaf_unique_ptr;

using olc_inode_base = unodb::detail::basic_inode_impl<olc_art_policy>;

class olc_inode : public olc_inode_base {};

using olc_leaf = unodb::detail::olc_art_policy::leaf_type;

//
//
//

template <class AtomicArray>
using non_atomic_array =
    std::array<typename AtomicArray::value_type::value_type,
               std::tuple_size<AtomicArray>::value>;

template <class T>
inline non_atomic_array<T> copy_atomic_to_nonatomic(T& atomic_array) noexcept {
  non_atomic_array<T> result;
  for (typename decltype(result)::size_type i = 0; i < result.size(); ++i) {
    result[i] = atomic_array[i].load(std::memory_order_relaxed);
  }
  return result;
}

using olc_leaf_unique_ptr =
    detail::basic_db_leaf_unique_ptr<detail::olc_node_header, olc_db>;

// Declare internal methods to quiet compiler warnings.
void create_leaf_if_needed(olc_db_leaf_unique_ptr& cached_leaf,
                           unodb::detail::art_key k, unodb::value_view v,
                           unodb::olc_db& db_instance);

}  // namespace detail

using qsbr_value_view = qsbr_ptr_span<const std::byte>;

// A concurrent Adaptive Radix Tree that is synchronized using optimistic lock
// coupling. At any time, at most two directly-related tree nodes can be
// write-locked by the insert algorithm and three by the delete algorithm. The
// lock used is optimistic lock (see optimistic_lock.hpp), where only writers
// lock and readers access nodes optimistically with node version checks. For
// deleted node reclamation, Quiescent State-Based Reclamation is used.
class olc_db final {
 public:
  using value_view = unodb::qsbr_value_view;
  using get_result = std::optional<value_view>;

  // Creation and destruction
  olc_db() noexcept = default;

  ~olc_db() noexcept;

  // Querying for a value given a key.
  [[nodiscard]] get_result get(key search_key) const noexcept;

  // Return true iff the tree is empty (no root leaf).
  [[nodiscard]] auto empty() const noexcept { return root == nullptr; }

  // Insert a value under a key iff there is no entry for that key.
  //
  // Note: Cannot be called during stack unwinding with
  // std::uncaught_exceptions() > 0
  //
  // @return true if the key value pair was inserted.
  //
  // FIXME There should be a lambda variant of this to handle
  // conflicts and support upsert or delete-upsert semantics. This
  // would call the caller's lambda once the method was positioned on
  // the leaf.  The caller could then update the value or perhaps
  // delete the entry under the key.
  [[nodiscard]] bool insert(key insert_key, unodb::value_view v);

  // Remove the entry associated with the key.
  //
  // @return true if the delete was successful (i.e. the key was found
  // in the tree and the associated index entry was removed).
  [[nodiscard]] bool remove(key remove_key);

  // Removes all entries in the index.
  //
  // Note: Only legal in single-threaded context, as destructor
  void clear() noexcept;

  ///
  /// iterator (the iterator is an internal API, the public API is scan()).
  ///

  // The OLC scan() logic tracks the version tag (a read_critical_section)
  // for each node in the stack.  This information is required because the
  // iter_result tuples already contain physical information read from
  // nodes which may have been invalidated by subsequent mutations.  The
  // scan is built on iterator methods for seek(), next(), prior(), etc.
  // These methods must restart (rebuilding the stack and redoing the work)
  // if they encounter a version tag for an element on the stack which is
  // no longer valid.  Restart works by performing a seek() to the key for
  // the leaf on the bottom of the stack.  Restarts can be full (from the
  // root) or partial (from the first element in the stack which was not
  // invalidated by the structural modification).
  //
  // During scan(), the iterator seek()s to some key and then invokes the
  // caller's lambda passing a reference to a visitor object.  That visitor
  // allows the caller to access the key and/or value associated with the
  // leaf.  If the leaf is concurrently deleted from the structure, the
  // visitor relies on epoch protection to return the data from the now
  // invalidated leaf.  This is still the state that the caller would have
  // observed without the concurrent structural modification.  When next()
  // is called, it will discover that the leaf on the bottom of the stack
  // is not valid (it is marked as obsolete) and it will have to restart by
  // seek() to the key for that leaf and then invoking next() if the key
  // still exists and otherwise we should already be on the successor of
  // that leaf.
  //
  // Note: The OLC thread safety mechanisms permit concurrent non-atomic
  // (multi-work) mutations to be applied to nodes.  This means that a
  // thread can read junk in the middle of some reorganization of a node
  // (e.g., the keys or children are being reordered to maintain an
  // invariant for I16).  To protect against reading such junk, the thread
  // reads the version tag before and after it accesses data in the node
  // and restarts if the version information has changed.  The thread must
  // not act on information that it had read until it verifies that the
  // version tag remained unchanged across the read operation.
  //
  // Note: The iterator is backed by a std::stack. This means that the
  // iterator methods accessing the stack can not be declared as
  // [[noexcept]].
  class iterator {
    friend class olc_db;
    template <class>
    friend class visitor;

    // The [node_ptr] is never [nullptr] and points to the internal
    // node or leaf for that step in the path from the root to some
    // leaf.  For the bottom of the stack, [node_ptr] is the root.
    // For the top of the stack, [node_ptr] is the current leaf. In
    // the degenerate case where the tree is a single root leaf, then
    // the stack contains just that leaf.
    //
    // The [key] is the [std::byte] along which the path descends from
    // that [node_ptr].  The [key] has no meaning for a leaf.  The key
    // byte may be used to reconstruct the full key (along with any
    // prefix bytes in the nodes along the path).  The key byte is
    // tracked to avoid having to search the keys of some node types
    // (N48) when the [child_index] does not directly imply the key
    // byte.
    //
    // The [child_index] is the [std::uint8_t] index position in the
    // parent at which the [child_ptr] was found.  The [child_index]
    // has no meaning for a leaf.  In the special case of N48, the
    // [child_index] is the index into the [child_indexes[]].  For all
    // other internal node types, the [child_index] is a direct index
    // into the [children[]].  When finding the successor (or
    // predecessor) the [child_index] needs to be interpreted
    // according to the node type.  For N4 and N16, you just look at
    // the next slot in the children[] to find the successor.  For
    // N256, you look at the next non-null slot in the children[].
    // N48 is the oddest of the node types.  For N48, you have to look
    // at the child_indexes[], find the next mapped key value greater
    // than the current one, and then look at its entry in the
    // children[].
    //
    // The [tag] is the [version_tag_type] from the read_critical_section
    // and contains the version information that must be valid to use
    // the [key_byte] and [child_index] data read from the [node].
    // The version tag is cached when when those data are read from
    // the node along with the [key_byte] and [child_index] values
    // that were read.
    struct stack_entry : public detail::olc_inode_base::iter_result {
      // The version tag invariant for the node.
      //
      // Note: This is just the data for the version tag and not the
      // read_critical_section (RCS).  Moving the RCS onto the stack
      // creates problems in the while(...) loops that use parent and
      // node lock chaining since the RCS in the loop is invalid as
      // soon as it is moved onto the stack.  Hence, this is just the
      // data and the while loops continue to use the normal OLC
      // pattern for lock chaining.
      version_tag_type version;

      [[nodiscard]] inline bool operator==(
          const stack_entry& other) const noexcept {
        return node == other.node && key_byte == other.key_byte &&
               child_index == other.child_index && version == other.version;
      }
    };

   protected:
    // Construct an empty iterator (one that is logically not
    // positioned on anything and which will report !valid()).
    explicit iterator(olc_db& tree) : db_(tree) {}

    // iterator is not flyweight. disallow copy and move.
    iterator(const iterator&) = delete;
    iterator(iterator&&) = delete;
    iterator& operator=(const iterator&) = delete;
    // iterator& operator=(iterator&&) = delete; // test_only_iterator()

   public:  // EXPOSED TO THE TESTS
    // Position the iterator on the first entry in the index.
    iterator& first();

    // Advance the iterator to next entry in the index.
    iterator& next();

    // Position the iterator on the last entry in the index, which can
    // be used to initiate a reverse traversal.
    iterator& last();

    // Position the iterator on the previous entry in the index.
    iterator& prior();

    // Position the iterator on, before, or after the caller's key.
    // If the iterator can not be positioned, it will be invalidated.
    // For example, if [fwd:=true] and the [search_key] is GT any key
    // in the index then the iterator will be invalidated since there
    // is no index entry greater than the search key.  Likewise, if
    // [fwd:=false] and the [search_key] is LT any key in the index,
    // then the iterator will be invalidated since there is no index
    // entry LT the search key.
    //
    // @param search_key The internal key used to position the iterator.
    //
    // @param match Will be set to true iff the search key is an exact
    // match in the index data.  Otherwise, the match is not exact and
    // the iterator is positioned either before or after the
    // search_key.
    //
    // @param fwd When true, the iterator will be positioned first
    // entry which orders GTE the search_key and will be !valid() if
    // there is no such entry.  Otherwise, the iterator will be
    // positioned on the last key which orders LTE the search_key and
    // !valid() if there is no such entry.
    iterator& seek(detail::art_key search_key, bool& match, bool fwd = true);

    // Iff the iterator is positioned on an index entry, then returns
    // the decoded key associated with that index entry.
    [[nodiscard]] std::optional<const key> get_key();

    // Iff the iterator is positioned on an index entry, then returns
    // the value associated with that index entry.
    [[nodiscard, gnu::pure]] std::optional<const qsbr_value_view> get_val()
        const;

    // Debugging
    [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream& os) const;
    [[gnu::cold]] void dump() const;

    // Return true unless the stack is empty (exposed to tests)
    [[nodiscard]] bool valid() const { return !stack_.empty(); }

   protected:
    // Compare the given key (e.g., the to_key) to the current key in
    // the internal buffer.
    //
    // @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
    [[nodiscard]] int cmp(const detail::art_key& akey) const;

    //
    // stack access methods.
    //

    // Return true iff the stack is empty.
    [[nodiscard]] bool empty() const { return stack_.empty(); }

    // Push an entry onto the stack.
    //
    // TODO(thompsonbry) handle variable length keys here.
    void push(const detail::olc_inode_base::iter_result& e,
              const optimistic_lock::read_critical_section& rcs) {
      push(e.node, e.key_byte, e.child_index, rcs);
    }

    // Push an entry onto the stack.
    void push(detail::olc_node_ptr node, std::byte key_byte,
              std::uint8_t child_index,
              const optimistic_lock::read_critical_section& rcs) {
      stack_.push({{node, key_byte, child_index}, rcs.get()});
    }

    // Push a leaf onto the stack.
    void push_leaf(detail::olc_node_ptr aleaf,
                   const optimistic_lock::read_critical_section& rcs) {
      // Mock up an iter_result for the leaf. The [key] and
      // [child_index] are ignored for a leaf.
      push(aleaf, static_cast<std::byte>(0xFFU),
           static_cast<std::uint8_t>(0xFFU), rcs);
    }

    // Pop an entry from the stack.
    //
    // TODO(thompsonbry) handle variable length keys here.
    void pop() { stack_.pop(); }

    // Return the entry (if any) on the top of the stack.
    [[nodiscard]] stack_entry& top() { return stack_.top(); }

    // Return the node on the top of the stack and nullptr if the
    // stack is empty (similar to top(), but handles an empty stack).
    [[nodiscard]] detail::olc_node_ptr current_node() {
      return stack_.empty() ? detail::olc_node_ptr(nullptr) : stack_.top().node;
    }

   private:
    // Invalidate the iterator (pops everything off of the stack).
    //
    // post-condition: The iterator is !valid().
    iterator& invalidate() {
      while (!stack_.empty()) stack_.pop();  // clear the stack
      return *this;
    }

    //
    // Core logic invoked from retry loops.
    //

    [[nodiscard]] bool try_first();
    [[nodiscard]] bool try_last();
    [[nodiscard]] bool try_next();
    [[nodiscard]] bool try_prior();

    // Push the given node onto the stack and traverse from the
    // caller's node to the left-most leaf under that node, pushing
    // nodes onto the stack as they are visited.
    [[nodiscard]] bool try_left_most_traversal(
        detail::olc_node_ptr node,
        optimistic_lock::read_critical_section& parent_critical_section);

    // Descend from the current state of the stack to the right most
    // child leaf, updating the state of the iterator during the
    // descent.
    [[nodiscard]] bool try_right_most_traversal(
        detail::olc_node_ptr node,
        optimistic_lock::read_critical_section& parent_critical_section);

    // Core logic invoked from retry loop.
    [[nodiscard]] bool try_seek(detail::art_key search_key, bool& match,
                                bool fwd);

    // The outer db instance.
    olc_db& db_;

    // A stack reflecting the parent path from the root of the tree to
    // the current leaf.  An empty stack corresponds to a logically
    // empty iterator and can be detected using !valid().  The
    // iterator for an empty tree is an empty stack.
    //
    std::stack<stack_entry> stack_{};

    // A buffer into which visited keys are decoded and materialized
    // by get_key().
    //
    // Note: The internal key is a sequence of unsigned bytes having
    // the appropriate lexicographic ordering.  The internal key needs
    // to be decoded to the external key.
    //
    // TODO(thompsonbry) The current implementation stores the entire
    // key in the leaf. This works fine for simple primitive keys.
    // However, it needs to be modified when the implementation is
    // modified to support variable length keys. In that situation,
    // the full internal key needs to be constructed using the [key]
    // byte from the path stack plus the prefix bytes from the
    // internal nodes along that path.
    key key_{};
  };  // class iterator

  //
  // end of the iterator API, which is an internal API.
  //

 public:
  ///
  /// public scan API
  ///

  // Note: The scan() interface is public.  The iterator and the
  // methods to obtain an iterator are protected (except for tests).
  // This encapsulation makes it easier to define methods which
  // operate on external keys (scan()) and those which operate on
  // internal keys (seek() and the iterator). It also makes life
  // easier for mutex_db since scan() can take the lock.

  // Scan the tree, applying the caller's lambda to each visited leaf.
  //
  // @param fn A function f(unodb::visitor<unodb::olc_db::iterator>&)
  // returning [bool:halt].  The traversal will halt if the function
  // returns [true].
  //
  // @param fwd When [true] perform a forward scan, otherwise perform
  // a reverse scan.
  template <typename FN>
  inline void scan(FN fn, bool fwd = true) noexcept {
    if (fwd) {
      iterator it(*this);
      it.first();
      visitor<olc_db::iterator> v{it};
      while (it.valid()) {
        if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
        it.next();
      }
    } else {
      iterator it(*this);
      it.last();
      visitor<olc_db::iterator> v{it};
      while (it.valid()) {
        if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
        it.prior();
      }
    }
  }

  // Scan in the indicated direction, applying the caller's lambda to
  // each visited leaf.
  //
  // @param from_key is an inclusive lower bound for the starting
  // point of the scan.
  //
  // @param fn A function f(unodb::visitor<unodb::olc_db::iterator>&)
  // returning [bool:halt].  The traversal will halt if the function
  // returns [true].
  //
  // @param fwd When [true] perform a forward scan, otherwise perform
  // a reverse scan.
  template <typename FN>
  inline void scan_from(key from_key, FN fn, bool fwd = true) noexcept {
    const detail::art_key from_key_{from_key};  // convert to internal key
    bool match{};
    if (fwd) {
      iterator it(*this);
      it.seek(from_key_, match, true /*fwd*/);
      visitor<olc_db::iterator> v{it};
      while (it.valid()) {
        if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
        it.next();
      }
    } else {
      iterator it(*this);
      it.seek(from_key_, match, false /*fwd*/);
      visitor<olc_db::iterator> v{it};
      while (it.valid()) {
        if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
        it.prior();
      }
    }
  }

  // Scan a half-open key range, applying the caller's lambda to each
  // visited leaf.  The scan will proceed in lexicographic order iff
  // from_key is less than to_key and in reverse lexicographic order iff
  // to_key is less than from_key.  When from_key < to_key, the scan will
  // visit all index entries in the half-open range [from_key,to_key) in
  // forward order.  Otherwise the scan will visit all index entries
  // in the half-open range (from_key,to_key] in reverse order.
  //
  // @param from_key is an inclusive bound for the starting point of
  // the scan.
  //
  // @param to_key is an exclusive bound for the ending point of the
  // scan.
  //
  // @param fn A function f(unodb::visitor<unodb::olc_db::iterator>&)
  // returning [bool:halt].  The traversal will halt if the function
  // returns [true].
  template <typename FN>
  inline void scan_range(key from_key, key to_key, FN fn) noexcept {
    // TODO(thompsonbry) Explore a cheaper way to handle the exclusive
    // bound case when developing variable length key support based on
    // the maintained key buffer.
    constexpr bool debug = false;               // set true to debug scan.
    const detail::art_key from_key_{from_key};  // convert to internal key
    const detail::art_key to_key_{to_key};      // convert to internal key
    const auto ret = from_key_.cmp(to_key_);    // compare the internal keys.
    const bool fwd{ret < 0};                    // from key is less than to key
    if (ret == 0) return;                       // NOP
    bool match{};
    if (fwd) {
      iterator it(*this);
      it.seek(from_key_, match, true /*fwd*/);
      if constexpr (debug) {
        std::cerr << "scan_range:: fwd"
                  << ", from_key=" << from_key << ", to_key=" << to_key << "\n";
        it.dump(std::cerr);
      }
      visitor<olc_db::iterator> v{it};
      while (it.valid() && it.cmp(to_key_) < 0) {
        if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
        it.next();
        if constexpr (debug) {
          std::cerr << "scan: next()\n";
          it.dump(std::cerr);
        }
      }
    } else {  // reverse traversal.
      iterator it(*this);
      it.seek(from_key_, match, false /*fwd*/);
      if constexpr (debug) {
        std::cerr << "scan_range:: rev"
                  << ", from_key=" << from_key << ", to_key=" << to_key << "\n";
        it.dump(std::cerr);
      }
      visitor<olc_db::iterator> v{it};
      while (it.valid() && it.cmp(to_key_) > 0) {
        if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
        it.prior();
        if constexpr (debug) {
          std::cerr << "scan: prior()\n";
          it.dump(std::cerr);
        }
      }
    }
  }

  //
  // TEST ONLY METHODS
  //

  // Used to write the iterator tests.
  auto test_only_iterator() noexcept { return iterator(*this); }

  // Stats

#ifdef UNODB_DETAIL_WITH_STATS

  // Return current memory use by tree nodes in bytes
  [[nodiscard]] auto get_current_memory_use() const noexcept {
    return current_memory_use.load(std::memory_order_relaxed);
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_node_count() const noexcept {
    return node_counts[as_i<NodeType>].load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_node_counts() const noexcept {
    return detail::copy_atomic_to_nonatomic(node_counts);
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_growing_inode_count() const noexcept {
    return growing_inode_counts[internal_as_i<NodeType>].load(
        std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_growing_inode_counts() const noexcept {
    return detail::copy_atomic_to_nonatomic(growing_inode_counts);
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_shrinking_inode_count() const noexcept {
    return shrinking_inode_counts[internal_as_i<NodeType>].load(
        std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_shrinking_inode_counts() const noexcept {
    return detail::copy_atomic_to_nonatomic(shrinking_inode_counts);
  }

  [[nodiscard]] auto get_key_prefix_splits() const noexcept {
    return key_prefix_splits.load(std::memory_order_relaxed);
  }

#endif  // UNODB_DETAIL_WITH_STATS

  // Public utils
  [[nodiscard]] static constexpr auto key_found(
      const get_result& result) noexcept {
    return static_cast<bool>(result);
  }

  // Debugging
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream& os) const;
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump() const;

  olc_db(const olc_db&) noexcept = delete;
  olc_db(olc_db&&) noexcept = delete;
  olc_db& operator=(const olc_db&) noexcept = delete;
  olc_db& operator=(olc_db&&) noexcept = delete;

 private:
  // If get_result is not present, the search was interrupted. Yes, this
  // resolves to std::optional<std::optional<value_view>>, but IMHO both
  // levels of std::optional are clear here
  using try_get_result_type = std::optional<get_result>;

  using try_update_result_type = std::optional<bool>;

  [[nodiscard]] try_get_result_type try_get(detail::art_key k) const noexcept;

  [[nodiscard]] try_update_result_type try_insert(
      detail::art_key k, unodb::value_view v,
      detail::olc_leaf_unique_ptr& cached_leaf);

  [[nodiscard]] try_update_result_type try_remove(detail::art_key k);

  void delete_root_subtree() noexcept;

#ifdef UNODB_DETAIL_WITH_STATS
  void increase_memory_use(std::size_t delta) noexcept;
  void decrease_memory_use(std::size_t delta) noexcept;

  void increment_leaf_count(std::size_t leaf_size) noexcept {
    increase_memory_use(leaf_size);
    node_counts[as_i<node_type::LEAF>].fetch_add(1, std::memory_order_relaxed);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(4189)
  void decrement_leaf_count(std::size_t leaf_size) noexcept {
    decrease_memory_use(leaf_size);

    const auto old_leaf_count UNODB_DETAIL_USED_IN_DEBUG =
        node_counts[as_i<node_type::LEAF>].fetch_sub(1,
                                                     std::memory_order_relaxed);
    UNODB_DETAIL_ASSERT(old_leaf_count > 0);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  template <class INode>
  constexpr void increment_inode_count() noexcept;

  template <class INode>
  constexpr void decrement_inode_count() noexcept;

  template <node_type NodeType>
  constexpr void account_growing_inode() noexcept;

  template <node_type NodeType>
  constexpr void account_shrinking_inode() noexcept;

#endif  // UNODB_DETAIL_WITH_STATS

  // optimistic lock guarding the [root].
  alignas(
      detail::hardware_destructive_interference_size) mutable optimistic_lock
      root_pointer_lock;

  // The root of the tree, guarded by the [root_pointer_lock].
  in_critical_section<detail::olc_node_ptr> root{detail::olc_node_ptr{nullptr}};

  static_assert(sizeof(root_pointer_lock) + sizeof(root) <=
                detail::hardware_constructive_interference_size);

#ifdef UNODB_DETAIL_WITH_STATS

  // Current logically allocated memory that is not scheduled to be reclaimed.
  // The total memory currently allocated is this plus the QSBR deallocation
  // backlog (qsbr::previous_interval_total_dealloc_size +
  // qsbr::current_interval_total_dealloc_size).
  alignas(detail::hardware_destructive_interference_size)
      std::atomic<std::size_t> current_memory_use{0};

  alignas(detail::hardware_destructive_interference_size)
      std::atomic<std::uint64_t> key_prefix_splits{0};

  template <class T>
  using atomic_array = std::array<std::atomic<typename T::value_type>,
                                  std::tuple_size<T>::value>;

  alignas(detail::hardware_destructive_interference_size)
      atomic_array<node_type_counter_array> node_counts{};
  alignas(detail::hardware_destructive_interference_size)
      atomic_array<inode_type_counter_array> growing_inode_counts{};
  alignas(detail::hardware_destructive_interference_size)
      atomic_array<inode_type_counter_array> shrinking_inode_counts{};

#endif  // UNODB_DETAIL_WITH_STATS

  friend auto detail::make_db_leaf_ptr<detail::olc_node_header, olc_db>(
      detail::art_key, unodb::value_view, olc_db&);

  template <class, class>
  friend class detail::basic_db_leaf_deleter;

  template <class, class>
  friend class detail::db_leaf_qsbr_deleter;

  template <class>
  friend class detail::db_inode_qsbr_deleter;

  template <class, template <class> class, class, class, class, class,
            template <class> class, template <class, class> class>
  friend struct detail::basic_art_policy;

  template <class, class>
  friend class detail::basic_db_inode_deleter;

  friend struct detail::olc_impl_helpers;
};

///
/// ART iterator implementation.
///

inline std::optional<const unodb::key> olc_db::iterator::get_key() {
  // Note: If the iterator is on a leaf, we return the key for that
  // leaf regardless of whether the leaf has been deleted.  This is
  // part of the design semantics for the OLC ART scan.
  //
  // TODO(thompsonbry) Eventually this will need to use the stack to
  // reconstruct the key from the path from the root to this leaf.
  // Right now it is relying on the fact that simple fixed width keys
  // are stored directly in the leaves.
  if (!valid()) return {};  // not positioned on anything.
  const auto& e = stack_.top();
  const auto& node = e.node;
  UNODB_DETAIL_ASSERT(node.type() == node_type::LEAF);     // On a leaf.
  const auto* const aleaf{node.ptr<detail::olc_leaf*>()};  // current leaf.
  key_ = aleaf->get_key().decode();  // decode key into iterator's buffer.
  return key_;  // return pointer to the internal key buffer.
}

inline std::optional<const qsbr_value_view> olc_db::iterator::get_val() const {
  // Note: If the iterator is on a leaf, we return the value for
  // that leaf regardless of whether the leaf has been deleted.
  // This is part of the design semantics for the OLC ART scan.
  if (!valid()) return {};  // not positioned on anything.
  const auto& e = stack_.top();
  const auto& node = e.node;
  UNODB_DETAIL_ASSERT(node.type() == node_type::LEAF);     // On a leaf.
  const auto* const aleaf{node.ptr<detail::olc_leaf*>()};  // current leaf.
  return qsbr_ptr_span{aleaf->get_value_view()};
}

inline int olc_db::iterator::cmp(const detail::art_key& akey) const {
  // TODO(thompsonbry) Explore a cheaper way to handle the exclusive
  // bound case when developing variable length key support based on
  // the maintained key buffer.
  UNODB_DETAIL_ASSERT(!stack_.empty());
  auto& node = stack_.top().node;
  UNODB_DETAIL_ASSERT(node.type() == node_type::LEAF);
  const auto* const leaf{node.ptr<detail::olc_leaf*>()};
  return leaf->get_key().cmp(akey);
}

///
/// OLC scan implementation
///

}  // namespace unodb

#endif  // UNODB_DETAIL_OLC_ART_HPP
