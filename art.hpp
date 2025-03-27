// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_ART_HPP
#define UNODB_DETAIL_ART_HPP

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stack>
#include <type_traits>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "art_internal_impl.hpp"
#include "assert.hpp"
#include "in_fake_critical_section.hpp"
#include "node_type.hpp"

namespace unodb {

namespace detail {

template <typename Key, typename Value>
class inode;  // IWYU pragma: keep

template <typename Key, typename Value>
class inode_4;

template <typename Key, typename Value>
class inode_16;

template <typename Key, typename Value>
class inode_48;

template <typename Key, typename Value>
class inode_256;

struct [[nodiscard]] node_header {};

static_assert(std::is_empty_v<node_header>);

using node_ptr = basic_node_ptr<node_header>;

struct impl_helpers;

template <typename Key, typename Value>
using inode_defs = basic_inode_def<inode<Key, Value>, inode_4<Key, Value>,
                                   inode_16<Key, Value>, inode_48<Key, Value>,
                                   inode_256<Key, Value>>;

template <typename Key, typename Value, class INode>
using db_inode_deleter = basic_db_inode_deleter<INode, unodb::db<Key, Value>>;

template <typename Key, typename Value>
using art_policy =
    basic_art_policy<Key, Value, unodb::db, unodb::in_fake_critical_section,
                     unodb::fake_lock, unodb::fake_read_critical_section,
                     node_ptr, inode_defs, db_inode_deleter,
                     basic_db_leaf_deleter>;

template <typename Key, typename Value>
using inode_base = basic_inode_impl<art_policy<Key, Value>>;

template <typename Key>
using leaf_type = basic_leaf<Key, node_header>;

template <typename Key, typename Value>
class inode : public inode_base<Key, Value> {};

}  // namespace detail

template <typename Key, typename Value>
class mutex_db;

/// A non-thread-safe implementation of the Adaptive Radix Tree (ART).
///
/// \sa olc_art for a highly concurrent thread-safe ART implementation.
template <typename Key, typename Value>
class db final {
  friend class mutex_db<Key, Value>;

 public:
  /// The type of the keys in the index.
  using key_type = Key;
  /// The type of the value associated with the keys in the index.
  using value_type = Value;
  using value_view = unodb::value_view;
  using get_result = std::optional<value_view>;
  using inode_base = detail::inode_base<Key, Value>;

  // TODO(laurynas): added temporarily during development
  static_assert(std::is_same_v<value_type, unodb::value_view>);

 private:
  using art_key_type = detail::basic_art_key<Key>;
  using leaf_type = detail::leaf_type<Key>;
  using db_type = db<Key, Value>;

  /// Query for a value associated with an encoded key.
  [[nodiscard, gnu::pure]] get_result get_internal(
      art_key_type search_key) const noexcept;

  /// Insert a value under an encoded key iff there is no entry for that key.
  ///
  /// \note Cannot be called during stack unwinding with
  /// `std::uncaught_exceptions() > 0`.
  ///
  /// \return true iff the key value pair was inserted.
  [[nodiscard]] bool insert_internal(art_key_type insert_key, value_type v);

  /// Remove the entry associated with the encoded key.
  ///
  /// \return true if the delete was successful (i.e. the key was found in the
  /// tree and the associated index entry was removed).
  [[nodiscard]] bool remove_internal(art_key_type remove_key);

 public:
  // Creation and destruction
  db() noexcept = default;

  ~db() noexcept;

  // TODO(laurynas): implement copy and move operations
  db(const db&) = delete;
  db(db&&) = delete;
  db& operator=(const db&) = delete;
  db& operator=(db&&) = delete;

  /// Query for a value associated with a key.
  ///
  /// \param search_key If Key is a simple primitive type, then it is converted
  /// into a binary comparable key.  If Key is unodb::key_view, then it is
  /// assumed to already be a binary comparable key, e.g., as produced by
  /// unodb::key_encoder.
  [[nodiscard, gnu::pure]] get_result get(Key search_key) const noexcept {
    const art_key_type k{search_key};
    return get_internal(k);
  }

  /// Return true iff the index is empty.
  [[nodiscard, gnu::pure]] auto empty() const noexcept {
    return root == nullptr;
  }

  /// Insert a value under a key iff there is no entry for that key.
  ///
  /// \note Cannot be called during stack unwinding with
  /// `std::uncaught_exceptions() > 0`.
  ///
  /// \param insert_key If Key is a simple primitive type, then it is converted
  /// into a binary comparable key.  If Key is unodb::key_view, then it is
  /// assumed to already be a binary comparable key, e.g., as produced by
  /// unodb::key_encoder.
  ///
  /// \param v The value of type `value_type` to be inserted under that key.
  ///
  /// \return true iff the key value pair was inserted.
  ///
  /// \sa key_encoder, which provides for encoding text and multi-field records
  /// when Key is unodb::key_view.
  [[nodiscard]] bool insert(Key insert_key, value_type v) {
    const art_key_type k{insert_key};
    return insert_internal(k, v);
  }

  /// Remove the entry associated with the key.
  ///
  /// \param search_key If Key is a simple primitive type, then it is converted
  /// into a binary comparable key.  If Key is unodb::key_view, then it is
  /// assumed to already be a binary comparable key, e.g., as produced by
  /// unodb::key_encoder.
  ///
  /// \return true if the delete was successful (i.e. the key was found in the
  /// tree and the associated index entry was removed).
  [[nodiscard]] bool remove(Key search_key) {
    const art_key_type k{search_key};
    return remove_internal(k);
  }

  // Removes all entries in the index.
  void clear() noexcept;

  ///
  /// iterator (the iterator is an internal API, the public API is scan()).
  ///
  class iterator {
    // Note: The iterator is backed by a std::stack. This means that
    // the iterator methods accessing the stack can not be declared as
    // [[noexcept]].
    friend class db;
    template <class>
    friend class visitor;

    /// Alias used for the elements of the stack.
    using stack_entry = typename inode_base::iter_result;

   protected:
    /// Construct an empty iterator (one that is logically not
    /// positioned on anything and which will report !valid()).
    explicit iterator(db& tree) noexcept : db_(tree) {}

    // iterator is not flyweight. disallow copy and move.
    iterator(const iterator&) = delete;
    iterator(iterator&&) = delete;
    iterator& operator=(const iterator&) = delete;
    // iterator& operator=(iterator&&) = delete; // test_only_iterator()

   public:
    using key_type = Key;
    using value_type = Value;

    // EXPOSED TO THE TESTS

    /// Position the iterator on the first entry in the index.
    iterator& first();

    /// Advance the iterator to next entry in the index.
    iterator& next();

    /// Position the iterator on the last entry in the index, which
    /// can be used to initiate a reverse traversal.
    iterator& last();

    /// Position the iterator on the previous entry in the index.
    iterator& prior();

    /// Position the iterator on, before, or after the caller's key.  If the
    /// iterator can not be positioned, it will be invalidated.  For example, if
    /// `fwd:=true` and the \a search_key is GT any key in the index then the
    /// iterator will be invalidated since there is no index entry greater than
    /// the search key.  Likewise, if `fwd:=false` and the \a search_key is LT
    /// any key in the index, then the iterator will be invalidated since there
    /// is no index entry LT the \a search_key.
    ///
    /// \param search_key The internal key used to position the iterator.
    ///
    /// \param match Will be set to true iff the search key is an exact match in
    /// the index data.  Otherwise, the match is not exact and the iterator is
    /// positioned either before or after the search_key.
    ///
    /// \param fwd When true, the iterator will be positioned first entry which
    /// orders GTE the search_key and invalidated if there is no such entry.
    /// Otherwise, the iterator will be positioned on the last key which orders
    /// LTE the search_key and invalidated if there is no such entry.
    iterator& seek(art_key_type search_key, bool& match, bool fwd = true);

    /// Return the key_view associated with the current position of
    /// the iterator.
    ///
    /// \pre The iterator MUST be valid().
    [[nodiscard]] key_view get_key() noexcept;

    /// Return the value_view associated with the current position of
    /// the iterator.
    ///
    /// \pre The iterator MUST be valid().
    [[nodiscard, gnu::pure]] value_view get_val() const noexcept;

    /// Debugging
    // LCOV_EXCL_START
    [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream& os) const {
      if (empty()) {
        os << "iter::stack:: empty\n";
        return;
      }
      // Dump the key buffer maintained by the iterator.
      os << "keybuf=";
      detail::dump_key(os, keybuf_.get_key_view());
      os << "\n";
      // Create a new stack and copy everything there.  Using the new
      // stack, print out the stack in top-bottom order.  This avoids
      // modifications to the existing stack for the iterator.
      auto tmp = stack_;
      auto level = tmp.size() - 1;
      while (!tmp.empty()) {
        const auto& e = tmp.top();
        const auto& np = e.node;
        os << "iter::stack:: level = " << level << ", key_byte=0x" << std::hex
           << std::setfill('0') << std::setw(2)
           << static_cast<std::uint64_t>(e.key_byte) << std::dec
           << ", child_index=0x" << std::hex << std::setfill('0')
           << std::setw(2) << static_cast<std::uint64_t>(e.child_index)
           << std::dec << ", prefix(" << e.prefix.length() << ")=";
        detail::dump_key(os, e.prefix.get_key_view());
        os << ", ";
        art_policy::dump_node(os, np, false /*recursive*/);
        if (np.type() != node_type::LEAF) os << '\n';
        tmp.pop();
        level--;
      }
    }
    // LCOV_EXCL_STOP

    /// Debugging
    // LCOV_EXCL_START
    [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump() const { dump(std::cerr); }
    // LCOV_EXCL_STOP

    /// Return true unless the stack is empty (exposed to tests).
    [[nodiscard]] bool valid() const noexcept { return !stack_.empty(); }

   protected:
    /// Push the given node onto the stack and traverse from the
    /// caller's node to the left-most leaf under that node, pushing
    /// nodes onto the stack as they are visited.
    iterator& left_most_traversal(detail::node_ptr node);

    /// Descend from the current state of the stack to the right most
    /// child leaf, updating the state of the iterator during the
    /// descent.
    iterator& right_most_traversal(detail::node_ptr node);

    /// Compare the given key (e.g., the to_key) to the current key in
    /// the internal buffer.
    ///
    /// \return -1, 0, or 1 if this key is LT, EQ, or GT the other
    /// key.
    [[nodiscard]] int cmp(art_key_type akey) const noexcept {
      // TODO(thompsonbry) : variable length keys.  Explore a cheaper
      // way to handle the exclusive bound case when developing
      // variable length key support based on the maintained key
      // buffer.
      UNODB_DETAIL_ASSERT(!stack_.empty());
      auto& node = stack_.top().node;
      UNODB_DETAIL_ASSERT(node.type() == node_type::LEAF);
      const auto* const leaf{node.template ptr<leaf_type*>()};
      return unodb::detail::compare(leaf->get_key_view(), akey.get_key_view());
    }

    //
    // stack access methods.
    //

    /// Return true iff the stack is empty.
    [[nodiscard]] bool empty() const noexcept { return stack_.empty(); }

    /// Push a non-leaf entry onto the stack.
    void push(detail::node_ptr node, std::byte key_byte,
              std::uint8_t child_index, detail::key_prefix_snapshot prefix) {
      // For variable length keys we need to know the number of bytes
      // associated with the node's key_prefix.  In addition there is
      // one byte for the descent to the child node along the
      // child_index.  That information needs to be stored on the
      // stack so we can pop off the right number of bytes even for
      // OLC where the node might be concurrently modified.
      UNODB_DETAIL_ASSERT(node.type() != node_type::LEAF);
      stack_.push({node, key_byte, child_index, prefix});
      keybuf_.push(prefix.get_key_view());
      keybuf_.push(key_byte);
    }

    /// Push a leaf onto the stack.
    void push_leaf(detail::node_ptr aleaf) {
      // Mock up a stack entry for the leaf.
      stack_.push({
          aleaf,
          static_cast<std::byte>(0xFFU),     // ignored for leaf
          static_cast<std::uint8_t>(0xFFU),  // ignored for leaf
          detail::key_prefix_snapshot(0)     // ignored for leaf
      });
      // No change in the key_buffer.
    }

    /// Push an entry onto the stack.
    void push(const typename inode_base::iter_result& e) {
      const auto node_type = e.node.type();
      if (UNODB_DETAIL_UNLIKELY(node_type == node_type::LEAF)) {
        push_leaf(e.node);
      }
      push(e.node, e.key_byte, e.child_index, e.prefix);
    }

    /// Pop an entry from the stack and truncate the key buffer.
    void pop() noexcept {
      UNODB_DETAIL_ASSERT(!empty());

      const auto prefix_len = top().prefix.length();
      keybuf_.pop(prefix_len);
      stack_.pop();
    }

    /// Return the entry on the top of the stack.
    [[nodiscard]] const stack_entry& top() const noexcept {
      UNODB_DETAIL_ASSERT(!stack_.empty());
      return stack_.top();
    }

    /// Return the node on the top of the stack and nullptr if the
    /// stack is empty (similar to top(), but handles an empty stack).
    [[nodiscard]] detail::node_ptr current_node() const noexcept {
      return stack_.empty() ? detail::node_ptr(nullptr) : stack_.top().node;
    }

   private:
    /// Invalidate the iterator (pops everything off of the stack).
    iterator& invalidate() noexcept {
      while (!stack_.empty()) stack_.pop();  // clear the stack
      keybuf_.reset();                       // clear the key buffer
      return *this;
    }

    /// The outer db instance.
    db& db_;

    /// A stack reflecting the parent path from the root of the tree
    /// to the current leaf.  An empty stack corresponds to a
    /// logically empty iterator and the iterator will report
    /// !valid().  The iterator for an empty tree is an empty stack.
    //
    /// The stack is made up of (node_ptr, key, child_index) entries.
    //
    /// The [node_ptr] is never [nullptr] and points to the internal
    /// node or leaf for that step in the path from the root to some
    /// leaf.  For the bottom of the stack, [node_ptr] is the root.
    /// For the top of the stack, [node_ptr] is the current leaf. In
    /// the degenerate case where the tree is a single root leaf, then
    /// the stack contains just that leaf.
    //
    /// The [key] is the [std::byte] along which the path descends
    /// from that [node_ptr].  The [key] has no meaning for a leaf.
    /// The key byte may be used to reconstruct the full key (along
    /// with any prefix bytes in the nodes along the path).  The key
    /// byte is tracked to avoid having to search the keys of some
    /// node types (N48) when the [child_index] does not directly
    /// imply the key byte.
    //
    /// The [child_index] is the [std::uint8_t] index position in the
    /// parent at which the [child_ptr] was found.  The [child_index]
    /// has no meaning for a leaf.  In the special case of N48, the
    /// [child_index] is the index into the [child_indexes[]].  For
    /// all other internal node types, the [child_index] is a direct
    /// index into the [children[]].  When finding the successor (or
    /// predecessor) the [child_index] needs to be interpreted
    /// according to the node type.  For N4 and N16, you just look at
    /// the next slot in the children[] to find the successor.  For
    /// N256, you look at the next non-null slot in the children[].
    /// N48 is the oddest of the node types.  For N48, you have to
    /// look at the child_indexes[], find the next mapped key value
    /// greater than the current one, and then look at its entry in
    /// the children[].
    std::stack<stack_entry> stack_{};

    /// A buffer into which visited encoded (binary comparable) keys
    /// are materialized by during the iterator traversal.  Bytes are
    /// pushed onto this buffer when we push something onto the
    /// iterator stack and popped off of this buffer when we pop
    /// something off of the iterator stack.
    detail::key_buffer keybuf_{};
  };  // class iterator

  //
  // end of the iterator API, which is an internal API.
  //

  //
  // public scan API
  //

  // Note: The scan() interface is public.  The iterator and the
  // methods to obtain an iterator are protected (except for tests).
  // This encapsulation makes it easier to define methods which
  // operate on external keys (scan()) and those which operate on
  // internal keys (seek() and the iterator). It also makes life
  // easier for mutex_db since scan() can take the lock.

  /// Scan the tree, applying the caller's lambda to each visited leaf.
  ///
  /// \param fn A function `f(unodb::visitor<unodb::db::iterator>&)' returning
  /// `bool`.  The traversal will halt if the function returns \c true.
  ///
  /// \param fwd When \c true perform a forward scan, otherwise perform a
  /// reverse scan.
  template <typename FN>
  void scan(FN fn, bool fwd = true);

  /// Scan in the indicated direction, applying the caller's lambda to each
  /// visited leaf.
  ///
  /// \param from_key is an inclusive lower bound for the starting point of the
  /// scan.
  ///
  /// \param fn A function `f(unodb::visitor<unodb::db::iterator>&)` returning
  /// `bool`.  The traversal will halt if the function returns \c true.
  ///
  /// \param fwd When \c true perform a forward scan, otherwise perform a
  /// reverse scan.
  template <typename FN>
  void scan_from(Key from_key, FN fn, bool fwd = true);

  /// Scan a half-open key range, applying the caller's lambda to each visited
  /// leaf.  The scan will proceed in lexicographic order iff \a from_key is
  /// less than \a to_key and in reverse lexicographic order iff \a to_key is
  /// less than \a from_key.  When `from_key < to_key`, the scan will visit all
  /// index entries in the half-open range `[from_key,to_key)` in forward order.
  /// Otherwise the scan will visit all index entries in the half-open range
  /// `(from_key,to_key]` in reverse order.
  ///
  /// \param from_key is an inclusive bound for the starting point of the scan.
  ///
  /// \param to_key is an exclusive bound for the ending point of the scan.
  ///
  /// \param fn A function `f(unodb::visitor<unodb::db::iterator>&)` returning
  /// `bool`.  The traversal will halt if the function returns \c true.
  template <typename FN>
  void scan_range(Key from_key, Key to_key, FN fn);

  //
  // TEST ONLY METHODS
  //

  // Used to write the iterator tests.
  auto test_only_iterator() noexcept { return iterator(*this); }

  // Stats

#ifdef UNODB_DETAIL_WITH_STATS

  // Return current memory use by tree nodes in bytes.
  [[nodiscard, gnu::pure]] constexpr auto get_current_memory_use()
      const noexcept {
    return current_memory_use;
  }

  template <node_type NodeType>
  [[nodiscard, gnu::pure]] constexpr auto get_node_count() const noexcept {
    return node_counts[as_i<NodeType>];
  }

  // cppcheck-suppress returnByReference
  [[nodiscard, gnu::pure]] constexpr auto get_node_counts() const noexcept {
    return node_counts;
  }

  template <node_type NodeType>
  [[nodiscard, gnu::pure]] constexpr auto get_growing_inode_count()
      const noexcept {
    return growing_inode_counts[internal_as_i<NodeType>];
  }

  // cppcheck-suppress returnByReference
  [[nodiscard, gnu::pure]] constexpr auto get_growing_inode_counts()
      const noexcept {
    return growing_inode_counts;
  }

  template <node_type NodeType>
  [[nodiscard, gnu::pure]] constexpr auto get_shrinking_inode_count()
      const noexcept {
    return shrinking_inode_counts[internal_as_i<NodeType>];
  }

  // cppcheck-suppress returnByReference
  [[nodiscard, gnu::pure]] constexpr auto get_shrinking_inode_counts()
      const noexcept {
    return shrinking_inode_counts;
  }

  [[nodiscard, gnu::pure]] constexpr auto get_key_prefix_splits()
      const noexcept {
    return key_prefix_splits;
  }

#endif  // UNODB_DETAIL_WITH_STATS

  // Public utils
  [[nodiscard, gnu::const]] static constexpr auto key_found(
      const get_result& result) noexcept {
    return static_cast<bool>(result);
  }

  // Debugging
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream& os) const;
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump() const;

 private:
  using art_policy = detail::art_policy<Key, Value>;
  using header_type = typename art_policy::header_type;
  using inode_type = detail::inode<Key, Value>;
  using inode_4 = detail::inode_4<Key, Value>;
  using tree_depth_type = detail::tree_depth<art_key_type>;
  using visitor_type = visitor<db_type::iterator>;
  using inode_defs_type = detail::inode_defs<Key, Value>;

  void delete_root_subtree() noexcept;

#ifdef UNODB_DETAIL_WITH_STATS

  constexpr void increase_memory_use(std::size_t delta) noexcept {
    UNODB_DETAIL_ASSERT(delta > 0);
    UNODB_DETAIL_ASSERT(
        std::numeric_limits<decltype(current_memory_use)>::max() - delta >=
        current_memory_use);

    current_memory_use += delta;
  }

  constexpr void decrease_memory_use(std::size_t delta) noexcept {
    UNODB_DETAIL_ASSERT(delta > 0);
    UNODB_DETAIL_ASSERT(delta <= current_memory_use);

    current_memory_use -= delta;
  }

  constexpr void increment_leaf_count(std::size_t leaf_size) noexcept {
    increase_memory_use(leaf_size);
    ++node_counts[as_i<node_type::LEAF>];
  }

  constexpr void decrement_leaf_count(std::size_t leaf_size) noexcept {
    decrease_memory_use(leaf_size);

    UNODB_DETAIL_ASSERT(node_counts[as_i<node_type::LEAF>] > 0);
    --node_counts[as_i<node_type::LEAF>];
  }

  template <class INode>
  constexpr void increment_inode_count() noexcept;

  template <class INode>
  constexpr void decrement_inode_count() noexcept;

  template <node_type NodeType>
  constexpr void account_growing_inode() noexcept;

  template <node_type NodeType>
  constexpr void account_shrinking_inode() noexcept;

#endif  // UNODB_DETAIL_WITH_STATS

  detail::node_ptr root{nullptr};

#ifdef UNODB_DETAIL_WITH_STATS

  std::size_t current_memory_use{0};

  node_type_counter_array node_counts{};
  inode_type_counter_array growing_inode_counts{};
  inode_type_counter_array shrinking_inode_counts{};

  std::uint64_t key_prefix_splits{0};

#endif  // UNODB_DETAIL_WITH_STATS

  friend auto detail::make_db_leaf_ptr<Key, Value, db>(art_key_type, value_view,
                                                       db&);

  template <class>
  friend class detail::basic_db_leaf_deleter;

  template <typename,                             // Key
            typename,                             // Value
            template <typename, typename> class,  // Db
            template <class> class,               // CriticalSectionPolicy
            class,                                // Fake lock implementation
            class,  // Fake read_critical_section implementation
            class,  // NodePtr
            template <typename, typename> class,         // INodeDefs
            template <typename, typename, class> class,  // INodeReclamator
            template <class> class>                      // LeafReclamator
  friend struct detail::basic_art_policy;

  template <typename, class>
  friend class detail::basic_db_inode_deleter;

  friend struct detail::impl_helpers;
};

namespace detail {

struct impl_helpers {
  // GCC 10 diagnoses parameters that are present only in uninstantiated if
  // constexpr branch, such as node_in_parent for inode_256.
  UNODB_DETAIL_DISABLE_GCC_10_WARNING("-Wunused-parameter")

  template <typename Key, typename Value, class INode>
  [[nodiscard]] static detail::node_ptr* add_or_choose_subtree(
      INode& inode, std::byte key_byte, basic_art_key<Key> k, value_view v,
      db<Key, Value>& db_instance, tree_depth<basic_art_key<Key>> depth,
      detail::node_ptr* node_in_parent);

  UNODB_DETAIL_RESTORE_GCC_10_WARNINGS()

  template <typename Key, typename Value, class INode>
  [[nodiscard]] static std::optional<detail::node_ptr*>
  remove_or_choose_subtree(INode& inode, std::byte key_byte,
                           basic_art_key<Key> k, db<Key, Value>& db_instance,
                           detail::node_ptr* node_in_parent);

  impl_helpers() = delete;
};

template <typename Key, typename Value>
using inode_4_parent = basic_inode_4<art_policy<Key, Value>>;

template <typename Key, typename Value>
class [[nodiscard]] inode_4 final : public inode_4_parent<Key, Value> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  using inode_4_parent<Key, Value>::inode_4_parent;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args&&... args) {
    return impl_helpers::add_or_choose_subtree(*this,
                                               std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args&&... args) {
    return impl_helpers::remove_or_choose_subtree(*this,
                                                  std::forward<Args>(args)...);
  }
};

using inode_4_test_type = inode_4<std::uint64_t, unodb::value_view>;
#ifndef _MSC_VER
static_assert(sizeof(inode_4_test_type) == 48);
#else
// MSVC pads the first field to 8 byte boundary even though its natural
// alignment is 4 bytes, maybe due to parent class sizeof
static_assert(sizeof(inode_4_test_type) == 56);
#endif

template <typename Key, typename Value>
using inode_16_parent = basic_inode_16<art_policy<Key, Value>>;

template <typename Key, typename Value>
class [[nodiscard]] inode_16 final : public inode_16_parent<Key, Value> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  using inode_16_parent<Key, Value>::inode_16_parent;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args&&... args) {
    return impl_helpers::add_or_choose_subtree(*this,
                                               std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args&&... args) {
    return impl_helpers::remove_or_choose_subtree(*this,
                                                  std::forward<Args>(args)...);
  }
};

static_assert(sizeof(inode_16<std::uint64_t, unodb ::value_view>) == 160);

template <typename Key, typename Value>
using inode_48_parent = basic_inode_48<art_policy<Key, Value>>;

template <typename Key, typename Value>
class [[nodiscard]] inode_48 final : public inode_48_parent<Key, Value> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  using inode_48_parent<Key, Value>::inode_48_parent;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args&&... args) {
    return impl_helpers::add_or_choose_subtree(*this,
                                               std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args&&... args) {
    return impl_helpers::remove_or_choose_subtree(*this,
                                                  std::forward<Args>(args)...);
  }
};

using inode_48_test_type = inode_48<std::uint64_t, unodb::value_view>;
#ifdef UNODB_DETAIL_AVX2
static_assert(sizeof(inode_48_test_type) == 672);
#else
static_assert(sizeof(inode_48_test_type) == 656);
#endif

template <typename Key, typename Value>
using inode_256_parent = basic_inode_256<art_policy<Key, Value>>;

template <typename Key, typename Value>
class [[nodiscard]] inode_256 final : public inode_256_parent<Key, Value> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  using inode_256_parent<Key, Value>::inode_256_parent;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args&&... args) {
    return impl_helpers::add_or_choose_subtree(*this,
                                               std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args&&... args) {
    return impl_helpers::remove_or_choose_subtree(*this,
                                                  std::forward<Args>(args)...);
  }
};

static_assert(sizeof(inode_256<std::uint64_t, unodb::value_view>) == 2064);

// Because we cannot dereference, load(), & take address of - it is a temporary
// by then
UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)
inline auto* unwrap_fake_critical_section(
    unodb::in_fake_critical_section<unodb::detail::node_ptr>* ptr) noexcept {
  return reinterpret_cast<unodb::detail::node_ptr*>(ptr);
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <typename Key, typename Value, class INode>
detail::node_ptr* impl_helpers::add_or_choose_subtree(
    INode& inode, std::byte key_byte, basic_art_key<Key> k, value_view v,
    db<Key, Value>& db_instance, tree_depth<basic_art_key<Key>> depth,
    detail::node_ptr* node_in_parent) {
  auto* const child =
      unwrap_fake_critical_section(inode.find_child(key_byte).second);

  if (child != nullptr) return child;

  auto leaf = art_policy<Key, Value>::make_db_leaf_ptr(k, v, db_instance);
  const auto children_count = inode.get_children_count();

  if constexpr (!std::is_same_v<INode, inode_256<Key, Value>>) {
    if (UNODB_DETAIL_UNLIKELY(children_count == INode::capacity)) {
      auto larger_node{INode::larger_derived_type::create(
          db_instance, inode, std::move(leaf), depth)};
      *node_in_parent =
          node_ptr{larger_node.release(), INode::larger_derived_type::type};
#ifdef UNODB_DETAIL_WITH_STATS
      db_instance
          .template account_growing_inode<INode::larger_derived_type::type>();
#endif  // UNODB_DETAIL_WITH_STATS
      return child;
    }
  }
  inode.add_to_nonfull(std::move(leaf), depth, children_count);
  return child;
}

template <typename Key, typename Value, class INode>
std::optional<detail::node_ptr*> impl_helpers::remove_or_choose_subtree(
    INode& inode, std::byte key_byte, basic_art_key<Key> k,
    db<Key, Value>& db_instance, detail::node_ptr* node_in_parent) {
  const auto [child_i, child_ptr]{inode.find_child(key_byte)};

  if (child_ptr == nullptr) return {};

  const auto child_ptr_val{child_ptr->load()};
  if (child_ptr_val.type() != node_type::LEAF)
    return unwrap_fake_critical_section(child_ptr);

  const auto* const leaf{
      child_ptr_val.template ptr<typename db<Key, Value>::leaf_type*>()};
  if (!leaf->matches(k)) return {};

  if (UNODB_DETAIL_UNLIKELY(inode.is_min_size())) {
    if constexpr (std::is_same_v<INode, inode_4<Key, Value>>) {
      auto current_node{art_policy<Key, Value>::make_db_inode_unique_ptr(
          &inode, db_instance)};
      *node_in_parent = current_node->leave_last_child(child_i, db_instance);
    } else {
      auto new_node{
          INode::smaller_derived_type::create(db_instance, inode, child_i)};
      *node_in_parent =
          node_ptr{new_node.release(), INode::smaller_derived_type::type};
    }
#ifdef UNODB_DETAIL_WITH_STATS
    db_instance.template account_shrinking_inode<INode::type>();
#endif  // UNODB_DETAIL_WITH_STATS
    return nullptr;
  }

  inode.remove(child_i, db_instance);
  return nullptr;
}

}  // namespace detail

template <typename Key, typename Value>
db<Key, Value>::~db() noexcept {
  delete_root_subtree();
}

template <typename Key, typename Value>
typename db<Key, Value>::get_result db<Key, Value>::get_internal(
    art_key_type k) const noexcept {
  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) return {};

  auto node{root};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      const auto* const leaf{node.template ptr<leaf_type*>()};
      if (leaf->matches(k)) return leaf->get_value_view();
      return {};
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);

    auto* const inode{node.template ptr<inode_type*>()};
    const auto& key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    if (key_prefix.get_shared_length(remaining_key) < key_prefix_length)
      return {};
    remaining_key.shift_right(key_prefix_length);
    const auto* const child{
        inode->find_child(node_type, remaining_key[0]).second};
    if (child == nullptr) return {};

    node = *child;
    remaining_key.shift_right(1);
  }
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(26430)
template <typename Key, typename Value>
bool db<Key, Value>::insert_internal(art_key_type k, value_type v) {
  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) {
    auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
    root = detail::node_ptr{leaf.release(), node_type::LEAF};
    return true;
  }

  auto* node = &root;
  tree_depth_type depth{};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node->type();
    if (node_type == node_type::LEAF) {
      auto* const leaf{node->template ptr<leaf_type*>()};
      const auto existing_key{leaf->get_key_view()};
      const auto cmp = k.cmp(existing_key);
      if (UNODB_DETAIL_UNLIKELY(cmp == 0)) {
        return false;  // exists
      }
      // Replace the existing leaf with a new N4 and put the existing
      // leaf and the leaf for the caller's key and value under the
      // new inode as its direct children.
      auto new_leaf = art_policy::make_db_leaf_ptr(k, v, *this);
      auto new_node{inode_4::create(*this, existing_key, remaining_key, depth,
                                    leaf, std::move(new_leaf))};
      *node = detail::node_ptr{new_node.release(), node_type::I4};
#ifdef UNODB_DETAIL_WITH_STATS
      account_growing_inode<node_type::I4>();
#endif  // UNODB_DETAIL_WITH_STATS
      return true;
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);

    auto* const inode{node->template ptr<inode_type*>()};
    const auto& key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_len{key_prefix.get_shared_length(remaining_key)};
    if (shared_prefix_len < key_prefix_length) {
      // We have reached an existing inode whose key_prefix is greater
      // than the desired match.  We need to split this inode into a
      // new N4 whose children are the existing inode and a new child
      // leaf.
      auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
      auto new_node = inode_4::create(*this, *node, shared_prefix_len, depth,
                                      std::move(leaf));
      *node = detail::node_ptr{new_node.release(), node_type::I4};
#ifdef UNODB_DETAIL_WITH_STATS
      account_growing_inode<node_type::I4>();
      ++key_prefix_splits;
      UNODB_DETAIL_ASSERT(growing_inode_counts[internal_as_i<node_type::I4>] >
                          key_prefix_splits);
#endif  // UNODB_DETAIL_WITH_STATS
      return true;
    }
    // key_prefix bytes were absorbed during the descent.  Now we need
    // to either descend along an existing child or create a new child.
    UNODB_DETAIL_ASSERT(shared_prefix_len == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    node = inode->template add_or_choose_subtree<detail::node_ptr*>(
        node_type, remaining_key[0], k, v, *this, depth, node);

    if (node == nullptr) return true;

    ++depth;
    remaining_key.shift_right(1);
  }
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <typename Key, typename Value>
bool db<Key, Value>::remove_internal(art_key_type k) {
  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) return false;

  if (root.type() == node_type::LEAF) {
    auto* const root_leaf{root.ptr<leaf_type*>()};
    if (root_leaf->matches(k)) {
      const auto r{art_policy::reclaim_leaf_on_scope_exit(root_leaf, *this)};
      root = nullptr;
      return true;
    }
    return false;
  }

  auto* node = &root;
  tree_depth_type depth{};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node->type();
    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);

    auto* const inode{node->template ptr<inode_type*>()};
    const auto& key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_len{key_prefix.get_shared_length(remaining_key)};
    if (shared_prefix_len < key_prefix_length) return false;

    UNODB_DETAIL_ASSERT(shared_prefix_len == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    const auto remove_result{inode->template remove_or_choose_subtree<
        std::optional<detail::node_ptr*>>(node_type, remaining_key[0], k, *this,
                                          node)};
    if (UNODB_DETAIL_UNLIKELY(!remove_result)) return false;

    auto* const child_ptr{*remove_result};
    if (child_ptr == nullptr) return true;

    node = child_ptr;
    ++depth;
    remaining_key.shift_right(1);
  }
}

///
/// ART Iterator Implementation
///

// Traverse to the left-most leaf. The stack is cleared first and then
// re-populated as we step down along the path to the left-most leaf.
// If the tree is empty, then the result is the same as end().
// TODO(laurynas): the method pairs first, last; next, prior;
// left_most_traversal, right_most_traversal are identical except for a couple
// lines. Extract helper methods templatized on the differences.
template <typename Key, typename Value>
typename db<Key, Value>::iterator& db<Key, Value>::iterator::first() {
  invalidate();  // clear the stack
  if (UNODB_DETAIL_UNLIKELY(db_.root == nullptr)) return *this;  // empty tree.
  const auto node{db_.root};
  return left_most_traversal(node);
}

// Traverse to the right-most leaf. The stack is cleared first and then
// re-populated as we step down along the path to the right-most leaf.
// If the tree is empty, then the result is the same as end().
template <typename Key, typename Value>
typename db<Key, Value>::iterator& db<Key, Value>::iterator::last() {
  invalidate();  // clear the stack
  if (UNODB_DETAIL_UNLIKELY(db_.root == nullptr)) return *this;  // empty tree.
  const auto node{db_.root};
  return right_most_traversal(node);
}

// Position the iterator on the next leaf in the index.
template <typename Key, typename Value>
typename db<Key, Value>::iterator& db<Key, Value>::iterator::next() {
  while (!empty()) {
    const auto& e = top();
    const auto node{e.node};
    UNODB_DETAIL_ASSERT(node != nullptr);
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      pop();     // pop off the leaf
      continue;  // falls through loop if just a root leaf since stack now
                 // empty.
    }
    auto* inode{node.template ptr<inode_type*>()};
    const auto nxt = inode->next(node_type,
                                 e.child_index);  // next child of that parent.
    if (!nxt.has_value()) {
      pop();     // Nothing more for that inode.
      continue;  // We will look for the right sibling of the parent inode.
    }
    // Fix up stack for new parent node state and left-most descent.
    const auto& e2 = nxt.value();
    pop();
    push(e2);
    const auto child = inode->get_child(node_type, e2.child_index);  // descend
    return left_most_traversal(child);
  }
  return *this;  // stack is empty, so iterator == end().
}

// Position the iterator on the prior leaf in the index.
template <typename Key, typename Value>
typename db<Key, Value>::iterator& db<Key, Value>::iterator::prior() {
  while (!empty()) {
    const auto& e = top();
    const auto node{e.node};
    UNODB_DETAIL_ASSERT(node != nullptr);
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      pop();     // pop off the leaf
      continue;  // falls through loop if just a root leaf since stack now
                 // empty.
    }
    auto* inode{node.template ptr<inode_type*>()};
    auto nxt = inode->prior(node_type, e.child_index);  // parent's prev child
    if (!nxt) {
      pop();     // Nothing more for that inode.
      continue;  // We will look for the left sibling of the parent inode.
    }
    // Fix up stack for new parent node state and right-most descent.
    UNODB_DETAIL_ASSERT(nxt.has_value());  // value exists for std::optional
    const auto& e2 = nxt.value();
    pop();
    push(e2);
    auto child = inode->get_child(node_type, e2.child_index);  // descend
    return right_most_traversal(child);
  }
  return *this;  // stack is empty, so iterator == end().
}

// Push the given node onto the stack and traverse from the caller's
// node to the left-most leaf under that node, pushing nodes onto the
// stack as they are visited.
template <typename Key, typename Value>
typename db<Key, Value>::iterator&
db<Key, Value>::iterator::left_most_traversal(detail::node_ptr node) {
  while (true) {
    UNODB_DETAIL_ASSERT(node != nullptr);
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      push_leaf(node);
      return *this;  // done
    }
    // recursive descent.
    auto* const inode{node.ptr<inode_type*>()};
    const auto e =
        inode->begin(node_type);  // first child of current internal node
    push(e);                      // push the entry on the stack.
    node = inode->get_child(node_type, e.child_index);  // get the child
  }
  UNODB_DETAIL_CANNOT_HAPPEN();
}

// Push the given node onto the stack and traverse from the caller's
// node to the right-most leaf under that node, pushing nodes onto the
// stack as they are visited.
template <typename Key, typename Value>
typename db<Key, Value>::iterator&
db<Key, Value>::iterator::right_most_traversal(detail::node_ptr node) {
  while (true) {
    UNODB_DETAIL_ASSERT(node != nullptr);
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      push_leaf(node);
      return *this;  // done
    }
    // recursive descent.
    auto* const inode{node.ptr<inode_type*>()};
    auto e = inode->last(node_type);  // first child of current internal node
    push(e);                          // push the entry on the stack.
    node = inode->get_child(node_type, e.child_index);  // get the child
  }
  UNODB_DETAIL_CANNOT_HAPPEN();
}

// Note: The basic seek() logic is similar to ::get() as long as the
// search_key exists in the data.  However, the iterator is positioned
// instead of returning the value for the key.  Life gets a lot more
// complicated when the search_key is not in the data and we have to
// consider the cases for both forward traversal and reverse traversal
// from a key that is not in the data.  See method declartion for
// details.
template <typename Key, typename Value>
typename db<Key, Value>::iterator& db<Key, Value>::iterator::seek(
    art_key_type search_key, bool& match, bool fwd) {
  invalidate();   // invalidate the iterator (clear the stack).
  match = false;  // unless we wind up with an exact match.
  if (UNODB_DETAIL_UNLIKELY(db_.root == nullptr)) return *this;  // aka end()

  auto node{db_.root};
  const auto k = search_key;
  auto remaining_key{k};

  while (true) {
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      const auto* const leaf{node.template ptr<leaf_type*>()};
      push_leaf(node);
      const auto cmp_ = leaf->cmp(k);
      if (cmp_ == 0) {
        match = true;
        return *this;
      }
      if (fwd) {  // GTE semantics
        // if search_key < leaf, use the leaf, else next().
        return (cmp_ < 0) ? *this : next();
      }
      // LTE semantics: if search_key > leaf, use the leaf, else prior().
      return (cmp_ > 0) ? *this : prior();
    }
    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    auto* const inode{node.template ptr<inode_type*>()};  // some internal node.
    const auto key_prefix{inode->get_key_prefix().get_snapshot()};  // prefix
    const auto key_prefix_length{key_prefix.length()};  // length of that prefix
    const auto shared_length = key_prefix.get_shared_length(
        remaining_key.get_u64());  // #of prefix bytes matched.
    if (shared_length < key_prefix_length) {
      // We have visited an internal node whose prefix is longer than
      // the bytes in the key that we need to match.  To figure out
      // whether the search key would be located before or after the
      // current internal node, we need to compare the respective key
      // spans lexicographically.  Since we have [shared_length] bytes
      // in common, we know that the next byte will tell us the
      // relative ordering of the key vs the prefix. So now we compare
      // prefix and key and the first byte where they differ.
      const auto cmp_ = static_cast<int>(remaining_key[shared_length]) -
                        static_cast<int>(key_prefix[shared_length]);
      UNODB_DETAIL_ASSERT(cmp_ != 0);
      if (fwd) {
        if (cmp_ < 0) {
          // FWD and the search key is ordered before this node.  We
          // want the left-most leaf under the node.
          return left_most_traversal(node);
        }
        // FWD and the search key is ordered after this node.  Right
        // most descent and then next().
        return right_most_traversal(node).next();
      }
      // reverse traversal
      if (cmp_ < 0) {
        // REV and the search key is ordered before this node.  We
        // want the preceeding key.
        return left_most_traversal(node).prior();
      }
      // REV and the search key is ordered after this node.
      return right_most_traversal(node);
    }
    remaining_key.shift_right(key_prefix_length);
    const auto res = inode->find_child(node_type, remaining_key[0]);
    if (res.second == nullptr) {
      // We are on a key byte during the descent that is not mapped by
      // the current node.  Where we go next depends on whether we are
      // doing forward or reverse traversal.
      if (fwd) {
        // FWD: Take the next child_index that is mapped in the data
        // and then do a left-most descent to land on the key that is
        // the immediate successor of the desired key in the data.
        //
        // Note: We are probing with a key byte which does not appear
        // in our list of keys (this was verified above) so this will
        // always be the index the first entry whose key byte is
        // greater-than the probe value and [false] if there is no
        // such entry.
        //
        // Note: [node] has not been pushed onto the stack yet!
        auto nxt = inode->gte_key_byte(node_type, remaining_key[0]);
        if (!nxt) {
          // Pop entries off the stack until we find one with a
          // right-sibling of the path we took to this node and then
          // do a left-most descent under that right-sibling. If there
          // is no such parent, we will wind up with an empty stack
          // (aka the end() iterator) and return that state.
          if (!empty()) pop();
          while (!empty()) {
            const auto& centry = top();
            const auto cnode{centry.node};  // possible parent from the stack
            auto* const icnode{cnode.template ptr<inode_type*>()};
            const auto cnxt = icnode->next(
                cnode.type(), centry.child_index);  // right-sibling.
            if (cnxt) {
              auto nchild = icnode->get_child(cnode.type(), centry.child_index);
              return left_most_traversal(nchild);
            }
            pop();
          }
          return *this;  // stack is empty (aka end()).
        }
        const auto& tmp = nxt.value();  // unwrap.
        const auto child_index = tmp.child_index;
        const auto child = inode->get_child(node_type, child_index);
        push(node, tmp.key_byte, child_index, tmp.prefix);  // the path we took
        return left_most_traversal(child);  // left most traversal
      }
      // REV: Take the prior child_index that is mapped and then do
      // a right-most descent to land on the key that is the
      // immediate precessor of the desired key in the data.
      auto nxt = inode->lte_key_byte(node_type, remaining_key[0]);
      if (!nxt) {
        // Pop off the current entry until we find one with a
        // left-sibling and then do a right-most descent under that
        // left-sibling.  In the extreme case there is no such
        // previous entry and we will wind up with an empty stack.
        if (!empty()) pop();
        while (!empty()) {
          const auto& centry = top();
          const auto cnode{centry.node};  // possible parent from stack
          auto* const icnode{cnode.template ptr<inode_type*>()};
          const auto cnxt =
              icnode->prior(cnode.type(), centry.child_index);  // left-sibling.
          if (cnxt) {
            auto nchild = icnode->get_child(cnode.type(), centry.child_index);
            return right_most_traversal(nchild);
          }
          pop();
        }
        return *this;  // stack is empty (aka end()).
      }
      const auto& tmp = nxt.value();  // unwrap.
      const auto child_index{tmp.child_index};
      const auto child = inode->get_child(node_type, child_index);
      push(node, tmp.key_byte, child_index, tmp.prefix);  // the path we took
      return right_most_traversal(child);  // right most traversal
    }
    // Simple case. There is a child for the current key byte.
    const auto child_index{res.first};
    const auto* const child{res.second};
    push(node, remaining_key[0], child_index, key_prefix);
    node = *child;
    remaining_key.shift_right(1);
  }  // while ( true )
  UNODB_DETAIL_CANNOT_HAPPEN();
}

UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=pure")
template <typename Key, typename Value>
key_view db<Key, Value>::iterator::get_key() noexcept {
  UNODB_DETAIL_ASSERT(valid());  // by contract
  // TODO(thompsonbry) : variable length keys. The simplest case
  // where this does not work today is a single root leaf.  In that
  // case, there is no inode path and we can not properly track the
  // key in the key_buffer.
  //
  // return keybuf_.get_key_view();
  const auto& e = stack_.top();
  const auto& node = e.node;
  UNODB_DETAIL_ASSERT(node.type() == node_type::LEAF);      // On a leaf.
  const auto* const leaf{node.template ptr<leaf_type*>()};  // current leaf.
  return leaf->get_key_view();
}
UNODB_DETAIL_RESTORE_GCC_WARNINGS()

template <typename Key, typename Value>
value_view db<Key, Value>::iterator::get_val() const noexcept {
  UNODB_DETAIL_ASSERT(valid());  // by contract
  const auto& e = stack_.top();
  const auto& node = e.node;
  UNODB_DETAIL_ASSERT(node.type() == node_type::LEAF);      // On a leaf.
  const auto* const leaf{node.template ptr<leaf_type*>()};  // current leaf.
  return leaf->get_value_view();
}

///
/// ART scan implementations.
///

template <typename Key, typename Value>
template <typename FN>
void db<Key, Value>::scan(FN fn, bool fwd) {
  if (fwd) {
    iterator it(*this);
    it.first();
    const visitor_type v{it};
    while (it.valid()) {
      if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
      it.next();
    }
  } else {
    iterator it(*this);
    it.last();
    const visitor_type v{it};
    while (it.valid()) {
      if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
      it.prior();
    }
  }
}

template <typename Key, typename Value>
template <typename FN>
void db<Key, Value>::scan_from(Key from_key, FN fn, bool fwd) {
  art_key_type from_key_{from_key};  // convert to internal key
  bool match{};
  if (fwd) {
    iterator it(*this);
    it.seek(from_key_, match, true /*fwd*/);
    visitor_type v{it};
    while (it.valid()) {
      if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
      it.next();
    }
  } else {
    iterator it(*this);
    it.seek(from_key_, match, false /*fwd*/);
    visitor_type v{it};
    while (it.valid()) {
      if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
      it.prior();
    }
  }
}

template <typename Key, typename Value>
template <typename FN>
void db<Key, Value>::scan_range(Key from_key, Key to_key, FN fn) {
  // TODO(thompsonbry) : variable length keys. Explore a cheaper way
  // to handle the exclusive bound case when developing variable
  // length key support based on the maintained key buffer.
  constexpr bool debug = false;             // set true to debug scan.
  const art_key_type from_key_{from_key};   // convert to internal key
  const art_key_type to_key_{to_key};       // convert to internal key
  const auto ret = from_key_.cmp(to_key_);  // compare the internal keys
  const bool fwd{ret < 0};                  // from_key is less than to_key
  if (ret == 0) return;                     // NOP
  bool match{};
  if (fwd) {
    iterator it(*this);
    it.seek(from_key_, match, true /*fwd*/);
    if constexpr (debug) {
      std::cerr << "scan_range:: fwd"
                << ", from_key=" << from_key_ << ", to_key=" << to_key_ << "\n";
      it.dump(std::cerr);
    }
    const visitor_type v{it};
    while (it.valid() && it.cmp(to_key_) < 0) {
      if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
      it.next();
      if constexpr (debug) {
        std::cerr << "scan_range:: next()\n";
        it.dump(std::cerr);
      }
    }
  } else {  // reverse traversal.
    iterator it(*this);
    it.seek(from_key_, match, false /*fwd*/);
    if constexpr (debug) {
      std::cerr << "scan_range:: rev"
                << ", from_key=" << from_key_ << ", to_key=" << to_key_ << "\n";
      it.dump(std::cerr);
    }
    const visitor_type v{it};
    while (it.valid() && it.cmp(to_key_) > 0) {
      if (UNODB_DETAIL_UNLIKELY(fn(v))) break;
      it.prior();
      if constexpr (debug) {
        std::cerr << "scan_range:: prior()\n";
        it.dump(std::cerr);
      }
    }
  }
}

template <typename Key, typename Value>
void db<Key, Value>::delete_root_subtree() noexcept {
  if (root != nullptr) art_policy::delete_subtree(root, *this);

#ifdef UNODB_DETAIL_WITH_STATS
  // It is possible to reset the counter to zero instead of decrementing it for
  // each leaf, but not sure the savings will be significant.
  UNODB_DETAIL_ASSERT(node_counts[as_i<node_type::LEAF>] == 0);
#endif  // UNODB_DETAIL_WITH_STATS
}

template <typename Key, typename Value>
void db<Key, Value>::clear() noexcept {
  delete_root_subtree();

  root = nullptr;
#ifdef UNODB_DETAIL_WITH_STATS
  current_memory_use = 0;
  node_counts[as_i<node_type::I4>] = 0;
  node_counts[as_i<node_type::I16>] = 0;
  node_counts[as_i<node_type::I48>] = 0;
  node_counts[as_i<node_type::I256>] = 0;
#endif  // UNODB_DETAIL_WITH_STATS
}

#ifdef UNODB_DETAIL_WITH_STATS

template <typename Key, typename Value>
template <class INode>
constexpr void db<Key, Value>::increment_inode_count() noexcept {
  static_assert(inode_defs_type::template is_inode<INode>());

  ++node_counts[as_i<INode::type>];
  increase_memory_use(sizeof(INode));
}

template <typename Key, typename Value>
template <class INode>
constexpr void db<Key, Value>::decrement_inode_count() noexcept {
  static_assert(inode_defs_type::template is_inode<INode>());
  UNODB_DETAIL_ASSERT(node_counts[as_i<INode::type>] > 0);

  --node_counts[as_i<INode::type>];
  decrease_memory_use(sizeof(INode));
}

template <typename Key, typename Value>
template <node_type NodeType>
constexpr void db<Key, Value>::account_growing_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  // NOLINTNEXTLINE(google-readability-casting)
  ++growing_inode_counts[internal_as_i<NodeType>];
  UNODB_DETAIL_ASSERT(growing_inode_counts[internal_as_i<NodeType>] >=
                      node_counts[as_i<NodeType>]);
}

template <typename Key, typename Value>
template <node_type NodeType>
constexpr void db<Key, Value>::account_shrinking_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  ++shrinking_inode_counts[internal_as_i<NodeType>];
  UNODB_DETAIL_ASSERT(shrinking_inode_counts[internal_as_i<NodeType>] <=
                      growing_inode_counts[internal_as_i<NodeType>]);
}

#endif  // UNODB_DETAIL_WITH_STATS

template <typename Key, typename Value>
void db<Key, Value>::dump(std::ostream& os) const {
#ifdef UNODB_DETAIL_WITH_STATS
  os << "db dump, current memory use = " << get_current_memory_use() << '\n';
#else
  os << "db dump\n";
#endif  // UNODB_DETAIL_WITH_STATS
  art_policy::dump_node(os, root);
}

// LCOV_EXCL_START
template <typename Key, typename Value>
void db<Key, Value>::dump() const {
  dump(std::cerr);
}
// LCOV_EXCL_STOP

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_HPP
