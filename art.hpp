// Copyright 2019-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_HPP
#define UNODB_DETAIL_ART_HPP

#include "global.hpp"

// IWYU pragma: no_include <__fwd/ostream.h>
// IWYU pragma: no_include <ostream>

#include <cstddef>
#include <cstdint>
#include <iosfwd>  // IWYU pragma: keep
#include <limits>
#include <optional>
#include <stack>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "art_internal_impl.hpp"
#include "assert.hpp"
#include "in_fake_critical_section.hpp"
#include "node_type.hpp"

namespace unodb {

namespace detail {

class inode;

class inode_4;
class inode_16;
class inode_48;
class inode_256;

struct [[nodiscard]] node_header {};

static_assert(std::is_empty_v<node_header>);

template <class,  // Db
          template <class> class,  // CriticalSectionPolicy
          class,                   // Fake lock implementation
          class,                   // Fake read_critical_section implementation
          class,                   // NodePtr
          class,                   // INodeDefs
          template <class> class,  // INodeReclamator
          template <class, class> class>  // LeadReclamator
struct basic_art_policy;  // IWYU pragma: keep

using node_ptr = basic_node_ptr<node_header>;

struct impl_helpers;

using inode_defs = unodb::detail::basic_inode_def<inode, inode_4, inode_16,
                                                  inode_48, inode_256>;

template <class INode>
using db_inode_deleter =
    unodb::detail::basic_db_inode_deleter<INode, unodb::db>;

using art_policy = unodb::detail::basic_art_policy<
  unodb::db,
  unodb::in_fake_critical_section,
  unodb::fake_lock,
  unodb::fake_read_critical_section,
  unodb::detail::node_ptr,
  inode_defs,
  db_inode_deleter,
  unodb::detail::basic_db_leaf_deleter>;

using inode_base = unodb::detail::basic_inode_impl<art_policy>;

using leaf = unodb::detail::basic_leaf<unodb::detail::node_header>;

class inode : public inode_base {};

}  // namespace detail

// A non-thread-safe implementation of the Adaptive Radix Tree (ART).
class db final {
 public:
  using get_result = std::optional<value_view>;

  // Creation and destruction
  db() noexcept = default;

  ~db() noexcept;

  // TODO(laurynas): implement copy and move operations
  db(const db &) = delete;
  db(db &&) = delete;
  db &operator=(const db &) = delete;
  db &operator=(db &&) = delete;

  // Querying for a value associated with a key.
  [[nodiscard, gnu::pure]] get_result get(key search_key) const noexcept;

  // Return true iff the index is empty.
  [[nodiscard, gnu::pure]] auto empty() const noexcept {
    return root == nullptr;
  }

  // Insert a value under a key iff there is no entry for that key.
  //
  // Note: Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  //
  // @return true iff the key value pair was inserted.
  [[nodiscard]] bool insert(key insert_key, value_view v);

  // Remove the entry associated with the key.
  //
  // @return true if the delete was successful (i.e. the key was found
  // in the tree and the associated index entry was removed).
  [[nodiscard]] bool remove(key remove_key);

  // Removes all entries in the index.
  void clear() noexcept;

  ///
  /// iterator (the iterator is an internal API, the public API is scan()).
  ///
  class iterator {
    friend class db;
    template <class> friend class visitor;

  protected:

    // Construct an empty iterator.
    inline iterator(db& tree):db_(tree) {}

   public: // EXPOSED TO THE TESTS
    
    // Position the iterator on the first entry in the index.
    iterator& first() noexcept;
    
    // Advance the iterator to next entry in the index.
    iterator& next() noexcept;
    
    // Position the iterator on the last entry in the index, which can
    // be used to initiate a reverse traversal.
    //
    // Note: This is NOT the same as end(), which does not position
    // the iterator on anything.
    iterator& last() noexcept;
    
    // Position the iterator on the previous entry in the index.
    iterator& prior() noexcept;

    // Makes this the "end()" iterator (by clearing the stack).
    inline iterator& end() noexcept {return invalidate();}
    
    // Position the iterator on, before, or after the caller's key.
    // If the iterator can not be positioned, it will be set to end().
    // For example, if [fwd:=true] and the [search_key] is GT any key
    // in the index then the iterator will be positioned to end()
    // since there is no index entry greater than the search key.
    // Likewise, if [fwd:=false] and the [search_key] is LT any key in
    // the index, then the iterator will be positioned to end() since
    // there is no index entry LT the search key.
    //
    // @param search_key The internal key used to position the iterator.
    //
    // @param match Will be set to true iff the search key is an exact
    // match in the index data.  Otherwise, the match is not exact and
    // the iterator is positioned either before or after the
    // search_key.
    //
    // @param fwd When true, the iterator will be positioned first
    // entry which orders GTE the search_key and end() if there is no
    // such entry.  Otherwise, the iterator will be positioned on the
    // last key which orders LTE the search_key and end() if there is
    // no such entry.
    iterator& seek(const detail::art_key& search_key, bool& match, bool fwd = true) noexcept;

    // Iff the iterator is positioned on an index entry, then returns
    // the decoded key associated with that index entry.
    inline std::optional<const key> get_key() noexcept;
    
    // Iff the iterator is positioned on an index entry, then returns
    // the value associated with that index entry.
    inline std::optional<const value_view> get_val() const noexcept;
    
    inline bool operator==(const iterator& other) const noexcept;
    inline bool operator!=(const iterator& other) const noexcept;

    // Debugging
    [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const;

   protected:
    
    // Return true unless the stack is empty.
    inline bool valid() const noexcept { return ! stack_.empty(); }

    // Push the given node onto the stack and traverse from the
    // caller's node to the left-most leaf under that node, pushing
    // nodes onto the stack as they are visited.
    iterator& left_most_traversal(detail::node_ptr node) noexcept;

    // Descend from the current state of the stack to the right most
    // child leaf, updating the state of the iterator during the
    // descent.
    iterator& right_most_traversal(detail::node_ptr node) noexcept;

    // Return the node on the top of the stack, which will wrap
    // nullptr if the stack is empty.
    inline detail::node_ptr current_node() noexcept {
      return stack_.empty()
          ? detail::node_ptr(nullptr)
          : std::get<NP>( stack_.top() );
      ;
    }

    // Compare the given key (e.g., the toKey) to the current key in
    // the internal buffer.
    //
    // @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
    inline int cmp(const detail::art_key& akey) const noexcept {
      // TODO Explore a cheaper way to handle the exclusive bound case
      // when developing variable length key support based on the
      // maintained key buffer.
      UNODB_DETAIL_ASSERT( !stack_.empty() );
      auto node = std::get<NP>( stack_.top() );
      UNODB_DETAIL_ASSERT( node.type() == node_type::LEAF);
      const auto *const leaf{node.ptr<detail::leaf *>()};
      return leaf->get_key().cmp( akey );
    }
    
   private:

    static constexpr int NP = detail::inode_base::NP; // node pointer
    static constexpr int KB = detail::inode_base::KB; // key byte (on descent from NP)
    static constexpr int CI = detail::inode_base::CI; // child_index (on descent from NP)(
    
    // invalidate the iterator (pops everything off of the stack).
    inline iterator& invalidate() noexcept {
      while ( ! stack_.empty() ) stack_.pop(); // clear the stack
      return *this;
    }

    // The element (0) is the key byte, element (1) is child index in
    // the node, element (2) is the pointer to the child or nullptr if
    // the iter_result is invalid (e.g., end()).
    using stack_entry = detail::inode_base::iter_result;
    
    // The outer db instance.
    db& db_;

    // A stack reflecting the parent path from the root of the tree to
    // the current leaf.  An empty stack corresponds to the end()
    // iterator.  The iterator for an empty tree is an empty stack.
    //
    // The stack is made up of (node_ptr, key, child_index) entries
    // defined by [iter_result].
    //
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
    std::stack<stack_entry> stack_ {};

    // A buffer into which visited keys are decoded and materialized
    // by get_key().
    //
    // Note: The internal key is a sequence of unsigned bytes having
    // the appropriate lexicographic ordering.  The internal key needs
    // to be decoded to the external key.
    //
    // FIXME The current implementation stores the entire key in the
    // leaf. This works fine for simple primitive keys.  However, it
    // needs to be modified when the implementation is modified to
    // support variable length keys. In that situation, the full
    // internal key needs to be constructed using the [key] byte from
    // the path stack plus the prefix bytes from the internal nodes
    // along that path.
    key key_ {};
    
  }; // class iterator
  
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
  // @param fn A function f(unodb::visitor<unodb::db::iterator>&)
  // returning [bool::halt].  The traversal will halt if the function
  // returns [true].
  //
  // @param fwd When [true] perform a forward scan, otherwise perform
  // a reverse scan.
  template <typename FN>
  inline void scan(FN fn, bool fwd = true) noexcept;

  // Scan in the indicated direction, applying the caller's lambda to
  // each visited leaf.
  //
  // @param fromKey is an inclusive lower bound for the starting point
  // of the scan.
  //
  // @param fn A function f(unodb::visitor<unodb::db::iterator>&)
  // returning [bool::halt].  The traversal will halt if the function
  // returns [true].
  //
  // @param fwd When [true] perform a forward scan, otherwise perform
  // a reverse scan.
  template <typename FN>
  inline void scan_from(const key fromKey, FN fn, bool fwd = true) noexcept;

  // Scan a half-open key range, applying the caller's lambda to each
  // visited leaf.  The scan will proceed in lexicographic order iff
  // fromKey is less than toKey and in reverse lexicographic order iff
  // toKey is less than fromKey.  When fromKey < toKey, the scan will
  // visit all index entries in the half-open range [fromKey,toKey) in
  // forward order.  Otherwise the scan will visit all index entries
  // in the half-open range (fromKey,toKey] in reverse order.
  //
  // @param fromKey is an inclusive bound for the starting point of
  // the scan.
  //
  // @param toKey is an exclusive bound for the ending point of the
  // scan.
  //
  // @param fn A function f(unodb::visitor<unodb::db::iterator>&)
  // returning [bool::halt].  The traversal will halt if the function
  // returns [true].
  template <typename FN>
  inline void scan_range(const key fromKey, const key toKey, FN fn) noexcept;

  //
  // TEST ONLY METHODS
  //

  // Used to write the iterator tests.
  auto __test_only_iterator__() noexcept {return iterator(*this);}
  
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
      const get_result &result) noexcept {
    return static_cast<bool>(result);
  }

  // Debugging
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const;

 private:
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

  friend auto detail::make_db_leaf_ptr<detail::node_header, db>(detail::art_key,
                                                                value_view,
                                                                db &);

  template <class, class>
  friend class detail::basic_db_leaf_deleter;

  template <class, template <class> class, class, class, class, template <class> class,
            template <class, class> class>
  friend struct detail::basic_art_policy;

  template <class, class>
  friend class detail::basic_db_inode_deleter;

  friend struct detail::impl_helpers;

};

///
/// ART Iterator Implementation
///

inline std::optional<const key> db::iterator::get_key() noexcept {
  // FIXME Eventually this will need to use the stack to reconstruct
  // the key from the path from the root to this leaf.  Right now it
  // is relying on the fact that simple fixed width keys are stored
  // directly in the leaves.
  if ( ! valid() ) return {}; // not positioned on anything.
  const auto& e = stack_.top();
  const auto& node = std::get<NP>( e );
  UNODB_DETAIL_ASSERT( node.type() == node_type::LEAF ); // On a leaf.
  const auto *const leaf{ node.ptr<detail::leaf *>() }; // current leaf.
  key_ = leaf->get_key().decode(); // decode key into buffer.
  return key_; // return pointer to the internal key buffer.
}

inline std::optional<const value_view> db::iterator::get_val() const noexcept {
  if ( ! valid() ) return {}; // not positioned on anything.
  const auto& e = stack_.top();
  const auto& node = std::get<NP>( e );
  UNODB_DETAIL_ASSERT( node.type() == node_type::LEAF ); // On a leaf.
  const auto *const leaf{ node.ptr<detail::leaf *>() }; // current leaf.
  return leaf->get_value_view();
}

inline bool db::iterator::operator==(const iterator& other) const noexcept {
  if ( &db_ != &other.db_ ) return false;  // different tree?
  if ( stack_.empty() != other.stack_.empty() )
    return false;  // one stack is empty and the other is not?
  if ( stack_.empty() ) return true;  // both empty.
  const auto& a = stack_.top();
  const auto& b = other.stack_.top();
  return a == b; // top of stack is same (inode, key, and child_index).
}

inline bool db::iterator::operator!=(const iterator& other) const noexcept {
  return !(*this == other);
}

///
/// ART scan implementations.
///

template <typename FN>
inline void db::scan(FN fn, bool fwd) noexcept {
  if ( empty() ) return;
  if ( fwd ) {
    auto it { iterator(*this).first() };
    visitor v{ it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.next();
    }
  } else {
    auto it { iterator(*this).last() };
    visitor v { it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.prior();
    }
  }
}

template <typename FN>
inline void db::scan_from(const key fromKey_, FN fn, bool fwd) noexcept {
  if ( empty() ) return;
  const detail::art_key fromKey{fromKey_};  // convert to internal key
  bool match {};
  if ( fwd ) {
    auto it { iterator(*this).seek( fromKey, match, true/*fwd*/ ) };
    visitor v { it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.next();
    }
  } else {
    auto it { iterator(*this).seek( fromKey, match, false/*fwd*/ ) };
    visitor v { it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.prior();
    }
  }
}

template <typename FN>
inline void db::scan_range(const key fromKey_, const key toKey_, FN fn) noexcept {
  constexpr bool debug = false;  // set true to debug scan.
  if ( empty() ) return;
  const detail::art_key fromKey{fromKey_};  // convert to internal key
  const detail::art_key toKey{toKey_};      // convert to internal key
  const auto ret = fromKey.cmp( toKey );    // compare the internal keys.
  const bool fwd { ret < 0 };               // fromKey is less than toKey
  if ( ret == 0 ) return;                   // NOP.
  bool match {};
  if ( fwd ) {
    auto it1 { iterator(*this).seek( fromKey, match, true/*fwd*/ ) }; // lower bound
    if constexpr ( debug ) {
      std::cerr<<"scan:: fwd"<<std::endl;
      std::cerr<<"scan:: fromKey="<<fromKey_<<std::endl; it1.dump(std::cerr);
    }
    visitor v { it1 };
    while ( it1.valid() && it1.cmp( toKey ) < 0 ) { // compares internal keys
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it1.next();
      if constexpr( debug ) {
        std::cerr<<"scan: next()"<<std::endl; it1.dump( std::cerr );
      }
    }
  } else { // reverse traversal.
    auto it1 { iterator(*this).seek( fromKey, match, true/*fwd*/ ) }; // upper bound
    if constexpr( debug ) {
      std::cerr<<"scan:: rev"<<std::endl;
      std::cerr<<"scan:: fromKey="<<fromKey_<<std::endl; it1.dump(std::cerr);
    }
    visitor v { it1 };
    while ( it1.valid() && it1.cmp( toKey ) < 0 ) { // compares internal keys.
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      // if ( UNODB_DETAIL_UNLIKELY( it1.current_node() == it2.current_node() ) ) break;
      it1.prior();
      if constexpr( debug ) {
      std::cerr<<"scan: prior()"<<std::endl; it1.dump( std::cerr );
      }
    }
  }
}

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_HPP
