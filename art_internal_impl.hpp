// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_ART_INTERNAL_IMPL_HPP
#define UNODB_DETAIL_ART_INTERNAL_IMPL_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#ifdef UNODB_DETAIL_X86_64
#include <emmintrin.h>
#ifdef UNODB_DETAIL_AVX2
#include <immintrin.h>
#elif defined(UNODB_DETAIL_SSE4_2)
#include <smmintrin.h>
#endif
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "art_common.hpp"
#include "art_internal.hpp"
#include "assert.hpp"
#include "heap.hpp"
#include "node_type.hpp"
#include "portability_builtins.hpp"

namespace unodb {
class db;
class olc_db;
}  // namespace unodb

namespace unodb::detail {

#ifdef UNODB_DETAIL_X86_64

// Idea from https://stackoverflow.com/a/32945715/80458
[[nodiscard, gnu::const]] inline auto _mm_cmple_epu8(__m128i x,
                                                     __m128i y) noexcept {
  return _mm_cmpeq_epi8(_mm_max_epu8(y, x), y);
}

#elif !defined(__aarch64__)

// From public domain
// https://graphics.stanford.edu/~seander/bithacks.html
[[nodiscard, gnu::const]] constexpr std::uint32_t has_zero_byte(
    std::uint32_t v) noexcept {
  return ((v - 0x01010101UL) & ~v & 0x80808080UL);
}

[[nodiscard, gnu::const]] constexpr std::uint32_t contains_byte(
    std::uint32_t v, std::byte b) noexcept {
  return has_zero_byte(v ^ (~0U / 255 * static_cast<std::uint8_t>(b)));
}

#endif  // #ifdef UNODB_DETAIL_X86_64

template <class Header>
class [[nodiscard]] basic_leaf final : public Header {
 public:
  using value_size_type = std::uint32_t;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
  constexpr basic_leaf(art_key k, value_view v) noexcept
      : key{k}, value_size{static_cast<value_size_type>(v.size())} {
    UNODB_DETAIL_ASSERT(v.size() <= max_value_size);

    if (!v.empty()) std::memcpy(&value_start[0], v.data(), value_size);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // return the binary comparable key stored in the leaf
  //
  // TODO(thompsonbry) : variable length keys.  Where possible this
  // must be changed to a method comparing a caller's key suffix
  // against the key suffix stored in the leaf.  Calling contexts
  // which assume that they can recover the entire key from the leaf
  // are trouble for variable length keys.  Instead, the key must be
  // buffered during the traversal down to the leaf and the leaf might
  // have a tail fragment of the key.  That buffer can be wrapped and
  // exposed as a gsl::span<const std::byte> (aka key_view).  If used,
  // it should be renamed to get_key_view() and conditionally compiled
  // depending on how we template the db, mutex_db, and olc_db (e.g.,
  // iff they support storing the key in the leaf as a time over space
  // optimization)
  [[nodiscard, gnu::pure]] constexpr auto get_key() const noexcept {
    return key;
  }

  // TODO(thompsonbry) : variable length keys.  This will go away. It
  // is here as a shim.  The iterator needs to handle this and buffer
  // the key during traversal.  get()/insert()/remove() need to do
  // something similar.  The only time we can get the key_view from
  // the leaf is if the binary comparable key is stored in the leaf as
  // an optimization of time over space.
  [[nodiscard, gnu::pure]] constexpr auto get_key_view() const noexcept {
    return key.get_key_view();
  }

  // Return true iff the two keys are the same.
  //
  // TODO(thompsonbry) : variable length keys.  This should be changed
  // to a method comparing a caller's key suffix against the key
  // suffix stored in the leaf.
  [[nodiscard, gnu::pure]] constexpr auto matches(art_key k) const noexcept {
    return cmp(k) == 0;
  }

  // Return LT ZERO (0) if this key is less than the caller's key.
  // Return GT ZERO (0) if this key is greater than the caller's key.
  // Return ZERO (0) if the two keys are the same.
  //
  // TODO(thompsonbry) : variable length keys.  This should be changed
  // to a method comparing a caller's key suffix against the key
  // suffix stored in the leaf.
  [[nodiscard, gnu::pure]] constexpr auto cmp(art_key k) const noexcept {
    return k.cmp(get_key());
  }

  [[nodiscard, gnu::pure]] constexpr auto get_value_view() const noexcept {
    return value_view{&value_start[0], value_size};
  }

#ifdef UNODB_DETAIL_WITH_STATS

  [[nodiscard, gnu::pure]] constexpr auto get_size() const noexcept {
    return compute_size(value_size);
  }

#endif  // UNODB_DETAIL_WITH_STATS

  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os,
                                                bool /*recursive*/) const {
    os << ", " << get_key() << ", value size: " << value_size << '\n';
  }

  [[nodiscard, gnu::const]] static constexpr auto compute_size(
      value_size_type val_size) noexcept {
    return sizeof(basic_leaf<Header>) + val_size - 1;
  }

  static constexpr std::size_t max_value_size =
      std::numeric_limits<value_size_type>::max();

 private:
  const art_key key;  // TODO(thompsonbry) : variable length keys.  The key
                      // should be optional on the leaf.
  const value_size_type value_size;
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  std::byte value_start[1];
};  // class basic_leaf

template <class Header, class Db>
[[nodiscard]] auto make_db_leaf_ptr(art_key k, value_view v,
                                    Db &db UNODB_DETAIL_LIFETIMEBOUND) {
  using leaf_type = basic_leaf<Header>;

  if (UNODB_DETAIL_UNLIKELY(v.size() > leaf_type::max_value_size)) {
    throw std::length_error("Value length must fit in std::uint32_t");
  }

  const auto size = leaf_type::compute_size(
      static_cast<typename leaf_type::value_size_type>(v.size()));

  auto *const leaf_mem = static_cast<std::byte *>(
      allocate_aligned(size, alignment_for_new<leaf_type>()));

#ifdef UNODB_DETAIL_WITH_STATS
  db.increment_leaf_count(size);
#endif  // UNODB_DETAIL_WITH_STATS

  return basic_db_leaf_unique_ptr<Header, Db>{
      new (leaf_mem) leaf_type{k, v}, basic_db_leaf_deleter<Header, Db>{db}};
}

// basic_inode_def is a metaprogramming construct to list all concrete
// inode types for a particular flavor.
template <class INode, class Node4, class Node16, class Node48, class Node256>
struct basic_inode_def final {
  using inode = INode;
  using n4 = Node4;
  using n16 = Node16;
  using n48 = Node48;
  using n256 = Node256;

  template <class Node>
  [[nodiscard]] static constexpr bool is_inode() noexcept {
    return std::is_same_v<Node, n4> || std::is_same_v<Node, n16> ||
           std::is_same_v<Node, n48> || std::is_same_v<Node, n256>;
  }

  basic_inode_def() = delete;
};

// Implementation of things declared in art_internal.hpp
template <class Header, class Db>
inline void basic_db_leaf_deleter<Header, Db>::operator()(
    leaf_type *to_delete) const noexcept {
#ifdef UNODB_DETAIL_WITH_STATS
  const auto leaf_size = to_delete->get_size();
#endif  // UNODB_DETAIL_WITH_STATS

  free_aligned(to_delete);

#ifdef UNODB_DETAIL_WITH_STATS
  db.decrement_leaf_count(leaf_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

template <class INode, class Db>
inline void basic_db_inode_deleter<INode, Db>::operator()(
    INode *inode_ptr) noexcept {
  static_assert(std::is_trivially_destructible_v<INode>);

  free_aligned(inode_ptr);

#ifdef UNODB_DETAIL_WITH_STATS
  db.template decrement_inode_count<INode>();
#endif  // UNODB_DETAIL_WITH_STATS
}

// The basic_art_policy encapsulates differences between plain and OLC
// ART, such as extra header field, and node access critical section
// type.
template <class Db, template <class> class CriticalSectionPolicy,
          class LockPolicy, class ReadCriticalSection, class NodePtr,
          class INodeDefs, template <class> class INodeReclamator,
          template <class, class> class LeafReclamator>
struct basic_art_policy final {
  using node_ptr = NodePtr;
  using header_type = typename NodePtr::header_type;
  using lock_policy = LockPolicy;
  using read_critical_section = ReadCriticalSection;
  using inode_defs = INodeDefs;
  using inode = typename inode_defs::inode;
  using inode4_type = typename inode_defs::n4;
  using inode16_type = typename inode_defs::n16;
  using inode48_type = typename inode_defs::n48;
  using inode256_type = typename inode_defs::n256;

  using leaf_type = basic_leaf<header_type>;

  using db = Db;

 private:
  template <class INode>
  using db_inode_deleter = basic_db_inode_deleter<INode, Db>;

  using leaf_reclaimable_ptr =
      std::unique_ptr<leaf_type, LeafReclamator<header_type, Db>>;

 public:
  template <typename T>
  using critical_section_policy = CriticalSectionPolicy<T>;

  template <class INode>
  using db_inode_unique_ptr = std::unique_ptr<INode, db_inode_deleter<INode>>;

  using db_inode4_unique_ptr = db_inode_unique_ptr<inode4_type>;
  using db_inode16_unique_ptr = db_inode_unique_ptr<inode16_type>;
  using db_inode48_unique_ptr = db_inode_unique_ptr<inode48_type>;
  using db_inode256_unique_ptr = db_inode_unique_ptr<inode256_type>;

  template <class INode>
  using db_inode_reclaimable_ptr =
      std::unique_ptr<INode, INodeReclamator<INode>>;

  using db_leaf_unique_ptr = basic_db_leaf_unique_ptr<header_type, Db>;

  [[nodiscard]] static auto make_db_leaf_ptr(
      art_key k, value_view v, Db &db_instance UNODB_DETAIL_LIFETIMEBOUND) {
    return ::unodb::detail::make_db_leaf_ptr<header_type, Db>(k, v,
                                                              db_instance);
  }

  [[nodiscard]] static auto reclaim_leaf_on_scope_exit(
      leaf_type *leaf UNODB_DETAIL_LIFETIMEBOUND,
      Db &db_instance UNODB_DETAIL_LIFETIMEBOUND) noexcept {
    return leaf_reclaimable_ptr{leaf,
                                LeafReclamator<header_type, Db>{db_instance}};
  }

  UNODB_DETAIL_DISABLE_GCC_11_WARNING("-Wmismatched-new-delete")
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26409)
  template <class INode, class... Args>
  [[nodiscard]] static auto make_db_inode_unique_ptr(
      Db &db_instance UNODB_DETAIL_LIFETIMEBOUND, Args &&...args) {
    auto *const inode_mem = static_cast<std::byte *>(
        allocate_aligned(sizeof(INode), alignment_for_new<INode>()));

#ifdef UNODB_DETAIL_WITH_STATS
    db_instance.template increment_inode_count<INode>();
#endif  // UNODB_DETAIL_WITH_STATS

    return db_inode_unique_ptr<INode>{
        new (inode_mem) INode{db_instance, std::forward<Args>(args)...},
        db_inode_deleter<INode>{db_instance}};
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  UNODB_DETAIL_RESTORE_GCC_11_WARNINGS()

  template <class INode>
  [[nodiscard]] static auto make_db_inode_unique_ptr(
      INode *inode_ptr UNODB_DETAIL_LIFETIMEBOUND,
      Db &db_instance UNODB_DETAIL_LIFETIMEBOUND) noexcept {
    return db_inode_unique_ptr<INode>{inode_ptr,
                                      db_inode_deleter<INode>{db_instance}};
  }

  template <class INode>
  [[nodiscard]] static auto make_db_inode_reclaimable_ptr(
      INode *inode_ptr UNODB_DETAIL_LIFETIMEBOUND,
      Db &db_instance UNODB_DETAIL_LIFETIMEBOUND) noexcept {
    return db_inode_reclaimable_ptr<INode>{inode_ptr,
                                           INodeReclamator<INode>{db_instance}};
  }

 private:
  [[nodiscard]] static auto make_db_leaf_ptr(
      leaf_type *leaf UNODB_DETAIL_LIFETIMEBOUND,
      Db &db_instance UNODB_DETAIL_LIFETIMEBOUND) noexcept {
    return basic_db_leaf_unique_ptr<header_type, Db>{
        leaf, basic_db_leaf_deleter<header_type, Db>{db_instance}};
  }

  struct delete_db_node_ptr_at_scope_exit final {
    constexpr explicit delete_db_node_ptr_at_scope_exit(
        NodePtr node_ptr_ UNODB_DETAIL_LIFETIMEBOUND,
        Db &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
        : node_ptr{node_ptr_}, db{db_} {}

    ~delete_db_node_ptr_at_scope_exit() noexcept {
      switch (node_ptr.type()) {
        case node_type::LEAF: {
          const auto r{
              make_db_leaf_ptr(node_ptr.template ptr<leaf_type *>(), db)};
          return;
        }
        case node_type::I4: {
          const auto r{make_db_inode_unique_ptr(
              node_ptr.template ptr<inode4_type *>(), db)};
          return;
        }
        case node_type::I16: {
          const auto r{make_db_inode_unique_ptr(
              node_ptr.template ptr<inode16_type *>(), db)};
          return;
        }
        case node_type::I48: {
          const auto r{make_db_inode_unique_ptr(
              node_ptr.template ptr<inode48_type *>(), db)};
          return;
        }
        case node_type::I256: {
          const auto r{make_db_inode_unique_ptr(
              node_ptr.template ptr<inode256_type *>(), db)};
          return;
        }
      }
      UNODB_DETAIL_CANNOT_HAPPEN();  // LCOV_EXCL_LINE
    }

    delete_db_node_ptr_at_scope_exit(const delete_db_node_ptr_at_scope_exit &) =
        delete;
    delete_db_node_ptr_at_scope_exit(delete_db_node_ptr_at_scope_exit &&) =
        delete;
    auto &operator=(const delete_db_node_ptr_at_scope_exit &) = delete;
    auto &operator=(delete_db_node_ptr_at_scope_exit &&) = delete;

   private:
    const NodePtr node_ptr;
    Db &db;
  };

 public:
  static void delete_subtree(NodePtr node, Db &db_instance) noexcept {
    delete_db_node_ptr_at_scope_exit delete_on_scope_exit{node, db_instance};

    switch (node.type()) {
      case node_type::LEAF:
        return;
      case node_type::I4: {
        auto *const subtree_ptr{node.template ptr<inode4_type *>()};
        subtree_ptr->delete_subtree(db_instance);
        return;
      }
      case node_type::I16: {
        auto *const subtree_ptr{node.template ptr<inode16_type *>()};
        subtree_ptr->delete_subtree(db_instance);
        return;
      }
      case node_type::I48: {
        auto *const subtree_ptr{node.template ptr<inode48_type *>()};
        subtree_ptr->delete_subtree(db_instance);
        return;
      }
      case node_type::I256: {
        auto *const subtree_ptr{node.template ptr<inode256_type *>()};
        subtree_ptr->delete_subtree(db_instance);
        return;
      }
    }
  }

  [[gnu::cold]] UNODB_DETAIL_NOINLINE static void dump_node(
      std::ostream &os, const NodePtr &node, bool recursive = true) {
    os << "node at: " << node.template ptr<void *>() << ", tagged ptr = 0x"
       << std::hex << node.raw_val() << std::dec;
    if (node == nullptr) {
      os << '\n';
      return;
    }
    os << ", type = ";
    switch (node.type()) {
      case node_type::LEAF:
        os << "LEAF";
        node.template ptr<leaf_type *>()->dump(os, recursive);
        break;
      case node_type::I4:
        os << "I4";
        node.template ptr<inode4_type *>()->dump(os, recursive);
        break;
      case node_type::I16:
        os << "I16";
        node.template ptr<inode16_type *>()->dump(os, recursive);
        break;
      case node_type::I48:
        os << "I48";
        node.template ptr<inode48_type *>()->dump(os, recursive);
        break;
      case node_type::I256:
        os << "I256";
        node.template ptr<inode256_type *>()->dump(os, recursive);
        break;
    }
  }

  basic_art_policy() = delete;
};  // class basic_art_policy

using key_prefix_size = std::uint8_t;
static constexpr key_prefix_size key_prefix_capacity = 7;

// A helper class used to expose a consistent snapshot of the
// key_prefix to the iterator for use in tracking the data on the
// iterator's stack.  This method exposes a key_view over its internal
// data.  We can't do that directly with the key_prefix due to (a)
// thread safety; and (b) the key_view being a non-owned view. So the
// data are atomically copied into this structure and it can expose
// the key_view over those data.
class key_prefix_snapshot {
 private:
  using key_prefix_data = std::array<std::byte, key_prefix_capacity>;
  union {
    struct {
      key_prefix_data key_prefix;         // The prefix.
      key_prefix_size key_prefix_length;  // The #of bytes in the prefix.
    } f;
    std::uint64_t u64;  // The same thing as a machine word.
  };

 public:
  explicit key_prefix_snapshot(std::uint64_t v) : u64(v) {}
  // Return a view onto the key_prefix.
  [[nodiscard]] key_view get_key_view() const noexcept {
    return key_view(f.key_prefix.data(), f.key_prefix_length);
  }
  // Return the length of the key_prefix.
  [[nodiscard]] key_prefix_size size() const noexcept {
    return f.key_prefix_length;
  }
};

// The key_prefix is a sequence of zero or more bytes for a given node
// that are a common prefix shared by all children of that node and
// supports prefix compression in the index.
template <template <class> class CriticalSectionPolicy>
union [[nodiscard]] key_prefix {
 private:
  template <typename T>
  using critical_section_policy = CriticalSectionPolicy<T>;

  using key_prefix_data =
      std::array<critical_section_policy<std::byte>, key_prefix_capacity>;

  struct [[nodiscard]] inode_fields {
    key_prefix_data key_prefix;
    critical_section_policy<key_prefix_size> key_prefix_length;
  } f;
  critical_section_policy<std::uint64_t> u64;

 public:
  key_prefix(art_key k1, art_key shifted_k2, tree_depth depth) noexcept
      : u64{make_u64(k1, shifted_k2, depth)} {}

  key_prefix(unsigned key_prefix_len,
             const key_prefix &source_key_prefix) noexcept
      : u64{(source_key_prefix.u64 & key_bytes_mask) |
            length_to_word(key_prefix_len)} {
    UNODB_DETAIL_ASSERT(key_prefix_len <= key_prefix_capacity);
  }

  key_prefix(const key_prefix &other) noexcept : u64{other.u64.load()} {}

  ~key_prefix() noexcept = default;

  // Return the number of bytes in common between this key_prefix and
  // a view of some internal key (shifted_key) from which any leading
  // bytes already matched by the traversal path have been discarded.
  [[nodiscard]] constexpr auto get_shared_length(
      unodb::detail::art_key shifted_key) const noexcept {
    return shared_len(shifted_key.get_internal_key(), u64, length());
  }

  // A snapshot of the key_prefix data.
  [[nodiscard]] constexpr key_prefix_snapshot get_snapshot() const noexcept {
    return key_prefix_snapshot(u64);
  }

  // The number of prefix bytes.
  [[nodiscard]] constexpr unsigned length() const noexcept {
    const auto result = f.key_prefix_length.load();
    UNODB_DETAIL_ASSERT(result <= key_prefix_capacity);
    return result;
  }

  constexpr void cut(unsigned cut_len) noexcept {
    UNODB_DETAIL_ASSERT(cut_len > 0);
    UNODB_DETAIL_ASSERT(cut_len <= length());

    u64 = ((u64 >> (cut_len * 8)) & key_bytes_mask) |
          length_to_word(length() - cut_len);

    UNODB_DETAIL_ASSERT(f.key_prefix_length.load() <= key_prefix_capacity);
  }

  constexpr void prepend(const key_prefix &prefix1,
                         std::byte prefix2) noexcept {
    UNODB_DETAIL_ASSERT(length() + prefix1.length() < key_prefix_capacity);

    const auto prefix1_bit_length = prefix1.length() * 8U;
    const auto prefix1_mask = (1ULL << prefix1_bit_length) - 1;
    const auto prefix3_bit_length = length() * 8U;
    const auto prefix3_mask = (1ULL << prefix3_bit_length) - 1;
    const auto prefix3 = u64 & prefix3_mask;
    const auto shifted_prefix3 = prefix3 << (prefix1_bit_length + 8U);
    const auto shifted_prefix2 = static_cast<std::uint64_t>(prefix2)
                                 << prefix1_bit_length;
    const auto masked_prefix1 = prefix1.u64 & prefix1_mask;

    u64 = shifted_prefix3 | shifted_prefix2 | masked_prefix1 |
          length_to_word(length() + prefix1.length() + 1);

    UNODB_DETAIL_ASSERT(f.key_prefix_length.load() <= key_prefix_capacity);
  }

  // Return the byte at the specified index.
  [[nodiscard]] constexpr auto operator[](std::size_t i) const noexcept {
    UNODB_DETAIL_ASSERT(i < length());
    return f.key_prefix[i].load();
  }

  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    const auto len = length();
    os << ", key prefix len = " << len;
    if (len > 0) {
      os << ", key prefix =";
      for (std::size_t i = 0; i < len; ++i) dump_byte(os, f.key_prefix[i]);
    }
  }

  key_prefix(key_prefix &&) = delete;
  key_prefix &operator=(const key_prefix &) = delete;
  key_prefix &operator=(key_prefix &&) = delete;

 private:
  static constexpr auto key_bytes_mask = 0x00FF'FFFF'FFFF'FFFFULL;

  [[nodiscard, gnu::const]] static constexpr std::uint64_t length_to_word(
      unsigned length) {
    UNODB_DETAIL_ASSERT(length <= key_prefix_capacity);
    return static_cast<std::uint64_t>(length) << 56U;
  }

  [[nodiscard, gnu::const]] static constexpr unsigned shared_len(
      std::uint64_t k1, std::uint64_t k2, unsigned clamp_byte_pos) noexcept {
    UNODB_DETAIL_ASSERT(clamp_byte_pos < 8);

    const auto diff = k1 ^ k2;
    const auto clamped = diff | (1ULL << (clamp_byte_pos * 8U));
    return static_cast<unsigned>(detail::ctz(clamped) >> 3U);
  }

  [[nodiscard, gnu::const]] static constexpr std::uint64_t make_u64(
      art_key k1, art_key shifted_k2, tree_depth depth) noexcept {
    k1.shift_right(depth);

    const auto k1_u64 = k1.get_internal_key() & key_bytes_mask;

    return k1_u64 |
           length_to_word(shared_len(k1_u64, shifted_k2.get_internal_key(),
                                     key_prefix_capacity));
  }
};  // class key_prefix

// A class used as a sentinel for basic_inode template args: the
// larger node type for the largest node type and the smaller node type for
// the smallest node type.
class fake_inode final {
 public:
  fake_inode() = delete;
};

// A template class extending the common header and defining some
// methods common to all internal node types.  The common header type
// is specific to the thread-safety policy.  In particular, for the
// OLC implementation, the common header includes the lock and version
// tag metadata.
//
// Note: basic_inode_impl contains generic inode code, key prefix,
// children count, and dispatch for add/remove/find towards specific
// types. basic_inode has several template args giving it a capacity,
// so it can implement is_full / is_min etc, and is immediate parent
// for 4 16 ... classes.
template <class ArtPolicy>
class basic_inode_impl : public ArtPolicy::header_type {
 public:
  using node_ptr = typename ArtPolicy::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

  using lock_policy = typename ArtPolicy::lock_policy;
  using read_critical_section = typename ArtPolicy::read_critical_section;

  using db_leaf_unique_ptr = typename ArtPolicy::db_leaf_unique_ptr;

  using db = typename ArtPolicy::db;

  // The first element is the child index in the node, the second
  // element is a pointer to the child.  If there is no such child,
  // the pointer is nullptr, and the child_index is undefined.
  using find_result = std::pair<std::uint8_t  // child_index
                                ,
                                critical_section_policy<node_ptr> *  // child
                                >;

  // A struct that is returned by the iterator visitation pattern
  // which represents a path in the tree for an internal node.
  //
  // Note: The node is a pointer to either an internal node or a leaf.
  //
  // Note: The key_byte is the byte from the key that was consumed as
  // you step down to the child node. This is the same as the child
  // index (if you convert std::uint8_t to std::byte) for N48 and
  // N256, but it is different for N4 and N16 since they use a sparse
  // encoding of the keys.  It is represented explicitly to avoid
  // searching for the key byte in the N48 and N256 cases.
  //
  // Note: The child_index is the index of the child node within that
  // internal node (except for N48, where it is the index into the
  // child_indexes[] and is in fact the same data as the key byte).
  // Overflow for the child_index can only occur for N48 and N256.
  // When overflow happens, the iter_result is not defined and the
  // outer std::optional will return false.
  struct iter_result {
    node_ptr node;             // node pointer
    std::byte key_byte;        // key byte
    std::uint8_t child_index;  // child-index

    // [[nodiscard]] constexpr bool operator==(
    //     const iter_result &other) const noexcept {
    //   return node == other.node && key_byte == other.key_byte &&
    //          child_index == other.child_index;
    // }
  };
  using iter_result_opt = std::optional<iter_result>;

 protected:
  using inode_type = typename ArtPolicy::inode;
  using db_inode4_unique_ptr = typename ArtPolicy::db_inode4_unique_ptr;
  using db_inode16_unique_ptr = typename ArtPolicy::db_inode16_unique_ptr;
  using db_inode48_unique_ptr = typename ArtPolicy::db_inode48_unique_ptr;

 private:
  using header_type = typename ArtPolicy::header_type;
  using inode4_type = typename ArtPolicy::inode4_type;
  using inode16_type = typename ArtPolicy::inode16_type;
  using inode48_type = typename ArtPolicy::inode48_type;
  using inode256_type = typename ArtPolicy::inode256_type;

 public:
  [[nodiscard]] constexpr const auto &get_key_prefix() const noexcept {
    return k_prefix;
  }

  [[nodiscard]] constexpr auto &get_key_prefix() noexcept { return k_prefix; }

  // Only for unodb::detail use.
  [[nodiscard]] constexpr auto get_children_count() const noexcept {
    return children_count.load();
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26491)

  template <typename ReturnType, typename... Args>
  [[nodiscard]] ReturnType add_or_choose_subtree(node_type type,
                                                 Args &&...args) {
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->add_or_choose_subtree(
            std::forward<Args>(args)...);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->add_or_choose_subtree(
            std::forward<Args>(args)...);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->add_or_choose_subtree(
            std::forward<Args>(args)...);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->add_or_choose_subtree(
            std::forward<Args>(args)...);
        // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  template <typename ReturnType, typename... Args>
  [[nodiscard]] ReturnType remove_or_choose_subtree(node_type type,
                                                    Args &&...args) {
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->remove_or_choose_subtree(
            std::forward<Args>(args)...);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->remove_or_choose_subtree(
            std::forward<Args>(args)...);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->remove_or_choose_subtree(
            std::forward<Args>(args)...);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->remove_or_choose_subtree(
            std::forward<Args>(args)...);
        // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  ///
  /// access methods
  ///
  //
  // Note: Because of the parallel updates, the callees below may work
  // on inconsistent nodes and must not assert, just produce results,
  // which are OK to be incorrect/inconsistent as the node state will
  // be checked before acting on them.
  //

  // TODO(laurynas) make const?
  [[nodiscard]] constexpr find_result find_child(node_type type,
                                                 std::byte key_byte) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->find_child(key_byte);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->find_child(key_byte);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->find_child(key_byte);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->find_child(key_byte);
        // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Return the child node at the specified child index.
  //
  // Note: For N48, the child_index is the index into the
  // child_indices[].  For all other node types, it is a direct index
  // into the children[].  This method hides this distinction.
  //
  // TODO(laurynas) make const?
  [[nodiscard]] constexpr node_ptr get_child(
      node_type type, std::uint8_t child_index) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->get_child(child_index);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->get_child(child_index);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->get_child(child_index);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->get_child(child_index);
        // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Dispatch logic for begin().
  [[nodiscard]] constexpr iter_result begin(node_type type) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->begin();
      case node_type::I16:
        return static_cast<inode16_type *>(this)->begin();
      case node_type::I48:
        return static_cast<inode48_type *>(this)->begin();
      case node_type::I256:
        return static_cast<inode256_type *>(this)->begin();
      // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Dispatch logic for last() which returns the iter_result for the
  // last valid child of the node.
  [[nodiscard]] constexpr iter_result last(node_type type) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->last();
      case node_type::I16:
        return static_cast<inode16_type *>(this)->last();
      case node_type::I48:
        return static_cast<inode48_type *>(this)->last();
      case node_type::I256:
        return static_cast<inode256_type *>(this)->last();
      // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Dispatch logic for next()
  //
  // @param type The type of this internal node.
  //
  // @param child_index The current position within the that internal node.
  //
  // @return A wrapped iter_result for the next child of this node iff
  // such a child exists.
  [[nodiscard]] constexpr iter_result_opt next(
      node_type type, std::uint8_t child_index) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->next(child_index);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->next(child_index);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->next(child_index);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->next(child_index);
      // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Dispatch logic for prior()
  //
  // @param type The type of this internal node.
  //
  // @param child_index The current position within the that internal node.
  //
  // @return A wrapped iter_result for the previous child of this node
  // iff such a child exists.
  [[nodiscard]] constexpr iter_result_opt prior(
      node_type type, std::uint8_t child_index) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->prior(child_index);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->prior(child_index);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->prior(child_index);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->prior(child_index);
      // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Return an iter_result for the greatest key byte which orders
  // lexicographically less than or equal to (LTE) the give key byte.
  // This method is used by seek() to find the path before a key when
  // the key is not mapped in the data.
  [[nodiscard]] constexpr iter_result_opt lte_key_byte(
      node_type type, std::byte key_byte) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->lte_key_byte(key_byte);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->lte_key_byte(key_byte);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->lte_key_byte(key_byte);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->lte_key_byte(key_byte);
        // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  // Return an iter_result for the smallest key byte which orders
  // lexicographically greater than or equal to (GTE) the given
  // key_byte.  This method is used by seek() to find the path before
  // a key when the key is not mapped in the data.
  [[nodiscard]] constexpr iter_result_opt gte_key_byte(
      node_type type, std::byte key_byte) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    switch (type) {
      case node_type::I4:
        return static_cast<inode4_type *>(this)->gte_key_byte(key_byte);
      case node_type::I16:
        return static_cast<inode16_type *>(this)->gte_key_byte(key_byte);
      case node_type::I48:
        return static_cast<inode48_type *>(this)->gte_key_byte(key_byte);
      case node_type::I256:
        return static_cast<inode256_type *>(this)->gte_key_byte(key_byte);
        // LCOV_EXCL_START
      case node_type::LEAF:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr basic_inode_impl(unsigned children_count_, art_key k1,
                             art_key shifted_k2, tree_depth depth) noexcept
      : k_prefix{k1, shifted_k2, depth},
        children_count{static_cast<std::uint8_t>(children_count_)} {}

  constexpr basic_inode_impl(unsigned children_count_, unsigned key_prefix_len,
                             const inode_type &key_prefix_source_node) noexcept
      : k_prefix{key_prefix_len, key_prefix_source_node.get_key_prefix()},
        children_count{static_cast<std::uint8_t>(children_count_)} {}

  constexpr basic_inode_impl(unsigned children_count_,
                             const basic_inode_impl &other) noexcept
      : k_prefix{other.k_prefix},
        children_count{static_cast<std::uint8_t>(children_count_)} {}

 protected:
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os,
                                                bool /*recursive*/) const {
    k_prefix.dump(os);
    const auto children_count_ = this->children_count.load();
    os << ", # children = "
       << (children_count_ == 0 ? 256 : static_cast<unsigned>(children_count_));
  }

 private:
  key_prefix<critical_section_policy> k_prefix;
  static_assert(sizeof(k_prefix) == 8);

  critical_section_policy<std::uint8_t> children_count;

  static constexpr std::uint8_t child_not_found_i = 0xFFU;

 protected:
  // Represents the find_result when no such child was found.
  static constexpr find_result child_not_found{child_not_found_i, nullptr};

  // Represents the std::optional<iter_result> for end(), which is [false].
  static constexpr iter_result_opt end_result{};

  using leaf_type = basic_leaf<header_type>;

  friend class unodb::db;
  friend class unodb::olc_db;
  friend struct olc_inode_immediate_deleter;

  template <class, unsigned, unsigned, node_type, class, class, class>
  friend class basic_inode;

  template <class>
  friend class basic_inode_4;

  template <class>
  friend class basic_inode_16;

  template <class>
  friend class basic_inode_48;

  template <class>
  friend class basic_inode_256;
};  // class basic_inode_impl

// The class basic_inode is the last common ancestor (both for
// templates and inheritance) for all inode types for both OLC and
// regular.
template <class ArtPolicy, unsigned MinSize, unsigned Capacity,
          node_type NodeType, class SmallerDerived, class LargerDerived,
          class Derived>
class [[nodiscard]] basic_inode : public basic_inode_impl<ArtPolicy> {
  static_assert(NodeType != node_type::LEAF);
  static_assert(!std::is_same_v<Derived, LargerDerived>);
  static_assert(!std::is_same_v<SmallerDerived, Derived>);
  static_assert(!std::is_same_v<SmallerDerived, LargerDerived>);
  static_assert(MinSize < Capacity);

 public:
  using typename basic_inode_impl<ArtPolicy>::db_leaf_unique_ptr;
  using typename basic_inode_impl<ArtPolicy>::db;
  using typename basic_inode_impl<ArtPolicy>::node_ptr;

  template <typename... Args>
  [[nodiscard]] static constexpr auto create(db &db_instance, Args &&...args) {
    return ArtPolicy::template make_db_inode_unique_ptr<Derived>(
        db_instance, std::forward<Args>(args)...);
  }

#ifndef NDEBUG

  [[nodiscard]] constexpr bool is_full_for_add() const noexcept {
    return this->children_count == capacity;
  }

#endif

  [[nodiscard]] constexpr bool is_min_size() const noexcept {
    return this->children_count == min_size;
  }

  static constexpr auto min_size = MinSize;
  static constexpr auto capacity = Capacity;
  static constexpr auto type = NodeType;

  using larger_derived_type = LargerDerived;
  using smaller_derived_type = SmallerDerived;
  using inode_type = typename basic_inode_impl<ArtPolicy>::inode_type;

 protected:
  constexpr basic_inode(art_key k1, art_key shifted_k2,
                        tree_depth depth) noexcept
      : basic_inode_impl<ArtPolicy>{MinSize, k1, shifted_k2, depth} {
    UNODB_DETAIL_ASSERT(is_min_size());
  }

  constexpr basic_inode(unsigned key_prefix_len,
                        const inode_type &key_prefix_source_node) noexcept
      : basic_inode_impl<ArtPolicy>{MinSize, key_prefix_len,
                                    key_prefix_source_node} {
    UNODB_DETAIL_ASSERT(is_min_size());
  }

  explicit constexpr basic_inode(const SmallerDerived &source_node) noexcept
      : basic_inode_impl<ArtPolicy>{MinSize, source_node} {
    // Cannot assert that source_node.is_full_for_add because we are creating
    // this node optimistically in the case of OLC.
    UNODB_DETAIL_ASSERT(is_min_size());
  }

  explicit constexpr basic_inode(const LargerDerived &source_node) noexcept
      : basic_inode_impl<ArtPolicy>{Capacity, source_node} {
    // Cannot assert that source_node.is_min_size because we are creating this
    // node optimistically in the case of OLC.
    UNODB_DETAIL_ASSERT(is_full_for_add());
  }
};

template <class ArtPolicy>
using basic_inode_4_parent =
    basic_inode<ArtPolicy, 2, 4, node_type::I4, fake_inode,
                typename ArtPolicy::inode16_type,
                typename ArtPolicy::inode4_type>;

// An internal node with up to 4 children.  There is an array of (4)
// bytes for the keys and keys are maintained in lexicographic order.
// There is a corresponding array of (4) child pointers.  Each key
// position is an index into the corresponding child pointer position,
// so the child pointers are also dense.
template <class ArtPolicy>
class basic_inode_4 : public basic_inode_4_parent<ArtPolicy> {
  using parent_class = basic_inode_4_parent<ArtPolicy>;

  using typename parent_class::inode16_type;
  using typename parent_class::inode4_type;
  using typename parent_class::inode_type;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_inode4_unique_ptr;
  using typename parent_class::db_leaf_unique_ptr;
  using typename parent_class::find_result;
  using typename parent_class::larger_derived_type;
  using typename parent_class::leaf_type;
  using typename parent_class::node_ptr;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  constexpr basic_inode_4(db &, art_key k1, art_key shifted_k2,
                          tree_depth depth) noexcept
      : parent_class{k1, shifted_k2, depth} {}

  constexpr basic_inode_4(db &, art_key k1, art_key shifted_k2,
                          tree_depth depth, leaf_type *child1,
                          db_leaf_unique_ptr &&child2) noexcept
      : parent_class{k1, shifted_k2, depth} {
    init(k1, shifted_k2, depth, child1, std::move(child2));
  }

  constexpr basic_inode_4(db &, node_ptr source_node, unsigned len) noexcept
      : parent_class{len, *source_node.template ptr<inode_type *>()} {}

  constexpr basic_inode_4(db &, node_ptr source_node, unsigned len,
                          tree_depth depth,
                          db_leaf_unique_ptr &&child1) noexcept
      : parent_class{len, *source_node.template ptr<inode_type *>()} {
    init(source_node, len, depth, std::move(child1));
  }

  constexpr basic_inode_4(db &, const inode16_type &source_node) noexcept
      : parent_class{source_node} {}

  constexpr basic_inode_4(db &db_instance, inode16_type &source_node,
                          std::uint8_t child_to_delete)
      : parent_class{source_node} {
    init(db_instance, source_node, child_to_delete);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr void init(node_ptr source_node, unsigned len, tree_depth depth,
                      db_leaf_unique_ptr &&child1) {
    auto *const source_inode{source_node.template ptr<inode_type *>()};
    auto &source_key_prefix = source_inode->get_key_prefix();
    UNODB_DETAIL_ASSERT(len < source_key_prefix.length());

    const auto diff_key_byte_i =
        static_cast<std::remove_cv_t<decltype(art_key::size)>>(depth) + len;
    UNODB_DETAIL_ASSERT(diff_key_byte_i < art_key::size);

    const auto source_node_key_byte = source_key_prefix[len];
    source_key_prefix.cut(len + 1);
    const auto new_key_byte = child1->get_key()[diff_key_byte_i];
    add_two_to_empty(source_node_key_byte, source_node, new_key_byte,
                     std::move(child1));
  }

  constexpr void init(db &db_instance, inode16_type &source_node,
                      std::uint8_t child_to_delete) {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr<inode16_type>(
            &source_node, db_instance)};
    auto source_keys_itr = source_node.keys.byte_array.cbegin();
    auto keys_itr = keys.byte_array.begin();
    auto source_children_itr = source_node.children.cbegin();
    auto children_itr = children.begin();

    while (source_keys_itr !=
           source_node.keys.byte_array.cbegin() + child_to_delete) {
      *keys_itr++ = *source_keys_itr++;
      *children_itr++ = *source_children_itr++;
    }

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        source_children_itr->load().template ptr<leaf_type *>(), db_instance)};

    ++source_keys_itr;
    ++source_children_itr;

    while (source_keys_itr !=
           source_node.keys.byte_array.cbegin() + inode16_type::min_size) {
      *keys_itr++ = *source_keys_itr++;
      *children_itr++ = *source_children_itr++;
    }

    UNODB_DETAIL_ASSERT(this->children_count == basic_inode_4::capacity);
    UNODB_DETAIL_ASSERT(
        std::is_sorted(keys.byte_array.cbegin(),
                       keys.byte_array.cbegin() + basic_inode_4::capacity));
  }

  constexpr void init(art_key k1, art_key shifted_k2, tree_depth depth,
                      leaf_type *child1, db_leaf_unique_ptr &&child2) noexcept {
    const auto k2_next_byte_depth = this->get_key_prefix().length();
    const auto k1_next_byte_depth = k2_next_byte_depth + depth;
    add_two_to_empty(k1[k1_next_byte_depth], node_ptr{child1, node_type::LEAF},
                     shifted_k2[k2_next_byte_depth], std::move(child2));
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    UNODB_DETAIL_ASSERT(children_count_ == this->children_count);
    UNODB_DETAIL_ASSERT(children_count_ < parent_class::capacity);
    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));

    const auto key_byte = static_cast<std::uint8_t>(child->get_key()[depth]);

#ifdef UNODB_DETAIL_X86_64
    const auto mask = (1U << children_count_) - 1;
    const auto insert_pos_index = get_insert_pos(key_byte, mask);
#else
    // This is also currently the best ARM implementation.
    const auto first_lt = ((keys.integer & 0xFFU) < key_byte) ? 1 : 0;
    const auto second_lt = (((keys.integer >> 8U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto third_lt = ((keys.integer >> 16U) & 0xFFU) < key_byte ? 1 : 0;
    const auto insert_pos_index =
        static_cast<unsigned>(first_lt + second_lt + third_lt);
#endif

    for (typename decltype(keys.byte_array)::size_type i = children_count_;
         i > insert_pos_index; --i) {
      keys.byte_array[i] = keys.byte_array[i - 1];
      children[i] = children[i - 1];
    }
    keys.byte_array[insert_pos_index] = static_cast<std::byte>(key_byte);
    children[insert_pos_index] = node_ptr{child.release(), node_type::LEAF};

    ++children_count_;
    this->children_count = children_count_;

    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    auto children_count_ = this->children_count.load();

    UNODB_DETAIL_ASSERT(child_index < children_count_);
    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_index].load().template ptr<leaf_type *>(), db_instance)};

    typename decltype(keys.byte_array)::size_type i = child_index;
    for (; i < static_cast<unsigned>(children_count_ - 1); ++i) {
      keys.byte_array[i] = keys.byte_array[i + 1];
      children[i] = children[i + 1];
    }
#ifndef UNODB_DETAIL_X86_64
    keys.byte_array[i] = unused_key_byte;
#endif

    --children_count_;
    this->children_count = children_count_;

    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));
  }

  [[nodiscard]] constexpr auto leave_last_child(std::uint8_t child_to_delete,
                                                db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(this->is_min_size());
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    UNODB_DETAIL_ASSERT(child_to_delete == 0 || child_to_delete == 1);

    auto *const child_to_delete_ptr{
        children[child_to_delete].load().template ptr<leaf_type *>()};
    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(child_to_delete_ptr,
                                                       db_instance)};

    const std::uint8_t child_to_leave = (child_to_delete == 0) ? 1U : 0U;
    const auto child_to_leave_ptr = children[child_to_leave].load();
    if (child_to_leave_ptr.type() != node_type::LEAF) {
      auto *const inode_to_leave_ptr{
          child_to_leave_ptr.template ptr<inode_type *>()};
      inode_to_leave_ptr->get_key_prefix().prepend(
          this->get_key_prefix(), keys.byte_array[child_to_leave]);
    }
    return child_to_leave_ptr;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[nodiscard]] find_result find_child(std::byte key_byte) noexcept {
#ifdef UNODB_DETAIL_X86_64
    const auto replicated_search_key =
        _mm_set1_epi8(static_cast<char>(key_byte));
    const auto keys_in_sse_reg =
        _mm_cvtsi32_si128(static_cast<std::int32_t>(keys.integer.load()));
    const auto matching_key_positions =
        _mm_cmpeq_epi8(replicated_search_key, keys_in_sse_reg);
    const auto mask = (1U << this->children_count.load()) - 1;
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
    if (bit_field != 0) {
      const auto i = detail::ctz(bit_field);
      return std::make_pair(
          i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
    }
    return parent_class::child_not_found;
#elif defined(__aarch64__)
    const auto replicated_search_key =
        vdupq_n_u8(static_cast<std::uint8_t>(key_byte));
    const auto keys_in_neon_reg = vreinterpretq_u8_u32(
        // NOLINTNEXTLINE(misc-const-correctness)
        vsetq_lane_u32(keys.integer.load(), vdupq_n_u32(0), 0));
    const auto mask = (1ULL << (this->children_count.load() << 3U)) - 1;
    const auto matching_key_positions =
        vceqq_u8(replicated_search_key, keys_in_neon_reg);
    const auto u64_pos_in_vec =
        vget_low_u64(vreinterpretq_u64_u8(matching_key_positions));
    // NOLINTNEXTLINE(misc-const-correctness)
    const auto pos_in_scalar = vget_lane_u64(u64_pos_in_vec, 0);
    const auto masked_pos = pos_in_scalar & mask;

    if (masked_pos == 0) return parent_class::child_not_found;

    const auto i = static_cast<unsigned>(detail::ctz(masked_pos) >> 3U);
    return std::make_pair(
        i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
#else   // #ifdef UNODB_DETAIL_X86_64
    // Bit twiddling:
    // contains_byte:     __builtin_ffs:   for key index:
    //    0x80000000               0x20                3
    //      0x800000               0x18                2
    //      0x808000               0x10                1
    //          0x80                0x8                0
    //           0x0                0x0        not found
    const auto result =
        static_cast<typename decltype(keys.byte_array)::size_type>(
            // __builtin_ffs takes signed argument:
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            __builtin_ffs(static_cast<std::int32_t>(
                contains_byte(keys.integer, key_byte))) >>
            3);

    // The second condition could be replaced with masking, but this seems to
    // result in a benchmark regression
    if ((result == 0) || (result > this->children_count.load()))
      return parent_class::child_not_found;

    return std::make_pair(result - 1,
                          static_cast<critical_section_policy<node_ptr> *>(
                              &children[result - 1]));
#endif  // #ifdef UNODB_DETAIL_X86_64
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] constexpr node_ptr get_child(
      std::uint8_t child_index) noexcept {
    return children[child_index].load();
  }

  // N4 - position on the first child (there is always at least two
  // children for N4).
  [[nodiscard]] constexpr typename basic_inode_4::iter_result begin() noexcept {
    const auto key = keys.byte_array[0].load();
    return {node_ptr{this, node_type::I4}, key, static_cast<uint8_t>(0)};
  }

  // N4 - position on the last child (there is always at least two
  // children for N4).
  //
  // TODO(laurynas) The iter_result-returning sequences follow the
  // same pattern once child_index is known. Look into extracting a
  // small helper method. This might apply to other inode types
  [[nodiscard]] constexpr typename basic_inode_4::iter_result last() noexcept {
    const auto child_index{
        static_cast<std::uint8_t>(this->children_count.load() - 1)};
    const auto key = keys.byte_array[child_index].load();
    return {node_ptr{this, node_type::I4}, key, child_index};
  }

  // TODO(laurynas) explore 1) branchless 2) SIMD implementations for
  // begin(), last(), next(), prior(), get_key_byte(), and
  // lte_key_byte().  next() and begin() will be the most frequently
  // invoked methods (assuming forward traversal), so that would be
  // the place to start.  The GTE and LTE methods are only used by
  // seek() so they are relatively rarely invoked. Look at each of the
  // inode types when doing this.
  [[nodiscard]] constexpr typename basic_inode_4::iter_result_opt next(
      std::uint8_t child_index) noexcept {
    const auto nchildren{this->children_count.load()};
    const auto next_index{static_cast<uint8_t>(child_index + 1)};
    if (next_index >= nchildren) return parent_class::end_result;
    const auto key = keys.byte_array[next_index].load();
    return {{node_ptr{this, node_type::I4}, key, next_index}};
  }

  [[nodiscard]] constexpr typename basic_inode_4::iter_result_opt prior(
      std::uint8_t child_index) noexcept {
    if (child_index == 0) return parent_class::end_result;
    const auto next_index{static_cast<std::uint8_t>(child_index - 1)};
    const auto key = keys.byte_array[next_index].load();
    return {{node_ptr{this, node_type::I4}, key, next_index}};
  }

  // N4: The keys[] is ordered for N4, so we scan the keys[] in order,
  // returning the first value GTE the given [key_byte].
  [[nodiscard]] constexpr typename basic_inode_4::iter_result_opt gte_key_byte(
      std::byte key_byte) noexcept {
    const auto nchildren{this->children_count.load()};
    for (std::uint8_t i = 0; i < nchildren; ++i) {
      const auto key = keys.byte_array[i].load();
      if (key >= key_byte) {
        return {{node_ptr{this, node_type::I4}, key, i}};
      }
    }
    // This should only occur if there is no entry in the keys[] which
    // is greater-than the given [key_byte].
    return parent_class::end_result;
  }

  // N4: The keys[] is ordered for N4, so we scan the keys[] in
  // reverse order, returning the first value LTE the given
  // [key_byte].
  [[nodiscard]] constexpr typename basic_inode_4::iter_result_opt lte_key_byte(
      std::byte key_byte) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::int64_t i = children_count_ - 1; i >= 0; i--) {
      const auto child_index = static_cast<std::uint8_t>(i);
      const auto key = keys.byte_array[child_index].load();
      if (key <= key_byte) {
        return {{node_ptr{this, node_type::I4}, key, child_index}};
      }
    }
    // The first key in the node is GT the given key_byte.
    return parent_class::end_result;
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
    const std::uint8_t children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i) {
      ArtPolicy::delete_subtree(children[i], db_instance);
    }
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os,
                                                bool recursive) const {
    parent_class::dump(os, recursive);
    const auto children_count_ = this->children_count.load();
    os << ", key bytes =";
    for (std::uint8_t i = 0; i < children_count_; i++)
      dump_byte(os, keys.byte_array[i]);
    if (recursive) {
      os << ", children:  \n";
      for (std::uint8_t i = 0; i < children_count_; i++)
        ArtPolicy::dump_node(os, children[i].load());
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 private:
  constexpr void add_two_to_empty(std::byte key1, node_ptr child1,
                                  std::byte key2,
                                  db_leaf_unique_ptr child2) noexcept {
    UNODB_DETAIL_ASSERT(key1 != key2);
    UNODB_DETAIL_ASSERT(this->children_count == 2);

    const std::uint8_t key1_i = key1 < key2 ? 0U : 1U;
    const std::uint8_t key2_i = 1U - key1_i;
    keys.byte_array[key1_i] = key1;
    children[key1_i] = child1;
    keys.byte_array[key2_i] = key2;
    children[key2_i] = node_ptr{child2.release(), node_type::LEAF};
#ifndef UNODB_DETAIL_X86_64
    keys.byte_array[2] = unused_key_byte;
    keys.byte_array[3] = unused_key_byte;
#endif

    UNODB_DETAIL_ASSERT(
        std::is_sorted(keys.byte_array.cbegin(),
                       keys.byte_array.cbegin() + this->children_count));
  }

  union key_union {
    std::array<critical_section_policy<std::byte>, basic_inode_4::capacity>
        byte_array;
    critical_section_policy<std::uint32_t> integer;

    UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
    key_union() noexcept {}
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  } keys;

  static_assert(std::alignment_of_v<decltype(keys)> == 4);
  static_assert(sizeof(keys) == 4);

 protected:
  std::array<critical_section_policy<node_ptr>, basic_inode_4::capacity>
      children;

  static_assert(sizeof(children) == 32);

 private:
#ifdef UNODB_DETAIL_X86_64
  [[nodiscard]] auto get_insert_pos(std::uint8_t insert_key_byte,
                                    unsigned node_key_mask) const noexcept {
    UNODB_DETAIL_ASSERT(node_key_mask ==
                        (1U << this->children_count.load()) - 1);

    const auto replicated_insert_key_byte =
        _mm_set1_epi8(static_cast<char>(insert_key_byte));
    const auto node_keys_in_sse_reg =
        _mm_cvtsi32_si128(static_cast<std::int32_t>(keys.integer.load()));
    // Since the existing and insert key values cannot be equal, it's OK to use
    // "<=" comparison as "<".
    const auto le_node_key_positions =
        _mm_cmple_epu8(node_keys_in_sse_reg, replicated_insert_key_byte);
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(le_node_key_positions)) &
        node_key_mask;
    return detail::popcount(bit_field);
  }
#else
  // The baseline implementation compares key bytes with less-than past the
  // current node size
  static constexpr std::byte unused_key_byte{0xFF};
#endif

  template <class>
  friend class basic_inode_16;
};  // class basic_inode_4

template <class ArtPolicy>
using basic_inode_16_parent = basic_inode<
    ArtPolicy, 5, 16, node_type::I16, typename ArtPolicy::inode4_type,
    typename ArtPolicy::inode48_type, typename ArtPolicy::inode16_type>;

// An internal node used to store data for nodes having 5-16 child
// pointers (if there are fewer child pointers, a basic_inode_4 is
// used instead).  Like the basic_inode_4, the keys are maintained in
// lexicographic order and the child pointers are 1:1 with the key
// positions, hence dense.
template <class ArtPolicy>
class basic_inode_16 : public basic_inode_16_parent<ArtPolicy> {
  using parent_class = basic_inode_16_parent<ArtPolicy>;

  using typename parent_class::inode16_type;
  using typename parent_class::inode48_type;
  using typename parent_class::inode4_type;
  using typename parent_class::leaf_type;
  using typename parent_class::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_leaf_unique_ptr;
  using typename parent_class::find_result;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  constexpr basic_inode_16(db &, const inode4_type &source_node) noexcept
      : parent_class{source_node} {}

  constexpr basic_inode_16(db &, const inode48_type &source_node) noexcept
      : parent_class{source_node} {}

  constexpr basic_inode_16(db &db_instance, inode4_type &source_node,
                           db_leaf_unique_ptr &&child,
                           tree_depth depth) noexcept
      : parent_class{source_node} {
    init(db_instance, source_node, std::move(child), depth);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
  constexpr basic_inode_16(db &db_instance, inode48_type &source_node,
                           std::uint8_t child_to_delete) noexcept
      : parent_class{source_node} {
    init(db_instance, source_node, child_to_delete);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr void init(db &db_instance, inode4_type &source_node,
                      db_leaf_unique_ptr child, tree_depth depth) noexcept {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr<inode4_type>(
            &source_node, db_instance)};
    const auto key_byte = static_cast<std::uint8_t>(child->get_key()[depth]);

#ifdef UNODB_DETAIL_X86_64
    const auto insert_pos_index = source_node.get_insert_pos(key_byte, 0xFU);
#else
    const auto keys_integer = source_node.keys.integer.load();
    const auto first_lt = ((keys_integer & 0xFFU) < key_byte) ? 1 : 0;
    const auto second_lt = (((keys_integer >> 8U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto third_lt = (((keys_integer >> 16U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto fourth_lt = (((keys_integer >> 24U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto insert_pos_index =
        static_cast<unsigned>(first_lt + second_lt + third_lt + fourth_lt);
#endif

    unsigned i = 0;
    for (; i < insert_pos_index; ++i) {
      keys.byte_array[i] = source_node.keys.byte_array[i];
      children[i] = source_node.children[i];
    }

    UNODB_DETAIL_ASSUME(i < parent_class::capacity);

    keys.byte_array[i] = static_cast<std::byte>(key_byte);
    children[i] = node_ptr{child.release(), node_type::LEAF};
    ++i;

    for (; i <= inode4_type::capacity; ++i) {
      keys.byte_array[i] = source_node.keys.byte_array[i - 1];
      children[i] = source_node.children[i - 1];
    }
  }

  constexpr void init(db &db_instance, inode48_type &source_node,
                      std::uint8_t child_to_delete) noexcept {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr<inode48_type>(
            &source_node, db_instance)};
    source_node.remove_child_pointer(child_to_delete, db_instance);
    source_node.child_indexes[child_to_delete] = inode48_type::empty_child;

    // TODO(laurynas): consider AVX2 gather?
    unsigned next_child = 0;
    unsigned i = 0;
    while (true) {
      const auto source_child_i = source_node.child_indexes[i].load();
      if (source_child_i != inode48_type::empty_child) {
        keys.byte_array[next_child] = static_cast<std::byte>(i);
        const auto source_child_ptr =
            source_node.children.pointer_array[source_child_i].load();
        UNODB_DETAIL_ASSERT(source_child_ptr != nullptr);
        children[next_child] = source_child_ptr;
        ++next_child;
        if (next_child == basic_inode_16::capacity) break;
      }
      UNODB_DETAIL_ASSERT(i < 255);
      ++i;
    }

    UNODB_DETAIL_ASSERT(this->children_count == basic_inode_16::capacity);
    UNODB_DETAIL_ASSERT(
        std::is_sorted(keys.byte_array.cbegin(),
                       keys.byte_array.cbegin() + basic_inode_16::capacity));
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    UNODB_DETAIL_ASSERT(children_count_ == this->children_count);
    UNODB_DETAIL_ASSERT(children_count_ < parent_class::capacity);
    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));

    const auto key_byte = child->get_key()[depth];

    const auto insert_pos_index =
        get_sorted_key_array_insert_position(key_byte);

    if (insert_pos_index != children_count_) {
      UNODB_DETAIL_ASSERT(insert_pos_index < children_count_);
      UNODB_DETAIL_ASSERT(keys.byte_array[insert_pos_index] != key_byte);

      std::copy_backward(keys.byte_array.cbegin() + insert_pos_index,
                         keys.byte_array.cbegin() + children_count_,
                         keys.byte_array.begin() + children_count_ + 1);
      std::copy_backward(children.begin() + insert_pos_index,
                         children.begin() + children_count_,
                         children.begin() + children_count_ + 1);
    }

    keys.byte_array[insert_pos_index] = key_byte;
    children[insert_pos_index] = node_ptr{child.release(), node_type::LEAF};
    ++children_count_;
    this->children_count = children_count_;

    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    auto children_count_ = this->children_count.load();
    UNODB_DETAIL_ASSERT(child_index < children_count_);
    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_index].load().template ptr<leaf_type *>(), db_instance)};

    for (unsigned i = child_index + 1U; i < children_count_; ++i) {
      keys.byte_array[i - 1] = keys.byte_array[i];
      children[i - 1] = children[i];
    }

    --children_count_;
    this->children_count = children_count_;

    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[nodiscard]] constexpr find_result find_child(std::byte key_byte) noexcept {
#ifdef UNODB_DETAIL_X86_64
    const auto replicated_search_key =
        _mm_set1_epi8(static_cast<char>(key_byte));
    const auto matching_key_positions =
        _mm_cmpeq_epi8(replicated_search_key, keys.byte_vector);
    const auto mask = (1U << this->children_count) - 1;
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
    if (bit_field != 0) {
      const auto i = detail::ctz(bit_field);
      return std::make_pair(
          i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
    }
    return parent_class::child_not_found;
#elif defined(__aarch64__)
    const auto replicated_search_key =
        vdupq_n_u8(static_cast<std::uint8_t>(key_byte));
    const auto matching_key_positions =
        vceqq_u8(replicated_search_key, keys.byte_vector);
    const auto narrowed_positions =
        vshrn_n_u16(vreinterpretq_u16_u8(matching_key_positions), 4);
    const auto scalar_pos =
        // NOLINTNEXTLINE(misc-const-correctness)
        vget_lane_u64(vreinterpret_u64_u8(narrowed_positions), 0);
    const auto child_count = this->children_count.load();
    const auto mask = (child_count == 16) ? 0xFFFFFFFF'FFFFFFFFULL
                                          : (1ULL << (child_count << 2U)) - 1;
    const auto masked_pos = scalar_pos & mask;

    if (masked_pos == 0) return parent_class::child_not_found;

    const auto i = static_cast<unsigned>(detail::ctz(masked_pos) >> 2U);
    return std::make_pair(
        i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
#else
    for (size_t i = 0; i < this->children_count.load(); ++i)
      if (key_byte == keys.byte_array[i])
        return std::make_pair(
            i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
    return parent_class::child_not_found;
#endif
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] constexpr node_ptr get_child(
      std::uint8_t child_index) noexcept {
    return children[child_index].load();
  }

  // N16 - position on the first child.
  [[nodiscard]] constexpr typename basic_inode_16::iter_result
  begin() noexcept {
    const auto key = keys.byte_array[0].load();
    return {node_ptr{this, node_type::I16}, key, 0};
  }

  // N16 - position on the last child.
  [[nodiscard]] constexpr typename basic_inode_16::iter_result last() noexcept {
    const auto child_index{
        static_cast<std::uint8_t>(this->children_count.load() - 1)};
    const auto key = keys.byte_array[child_index].load();
    return {node_ptr{this, node_type::I16}, key, child_index};
  }

  [[nodiscard]] constexpr typename basic_inode_16::iter_result_opt next(
      std::uint8_t child_index) noexcept {
    const auto nchildren{this->children_count.load()};
    const auto next_index{static_cast<std::uint8_t>(child_index + 1)};
    if (next_index >= nchildren) return parent_class::end_result;
    const auto key = keys.byte_array[next_index].load();
    return {{node_ptr{this, node_type::I16}, key, next_index}};
  }

  [[nodiscard]] constexpr typename basic_inode_16::iter_result_opt prior(
      std::uint8_t child_index) noexcept {
    if (child_index == 0) return parent_class::end_result;
    const auto next_index{static_cast<std::uint8_t>(child_index - 1)};
    const auto key = keys.byte_array[next_index].load();
    return {{node_ptr{this, node_type::I16}, key, next_index}};
  }

  // N16: The keys[] is ordered, so we scan the keys[] in reverse
  // order, returning the first value LTE the given [key_byte].
  [[nodiscard]] constexpr typename basic_inode_16::iter_result_opt lte_key_byte(
      std::byte key_byte) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::int64_t i = children_count_ - 1; i >= 0; i--) {
      const auto child_index = static_cast<std::uint8_t>(i);
      const auto key = keys.byte_array[child_index].load();
      if (key <= key_byte) {
        return {{node_ptr{this, node_type::I16}, key, child_index}};
      }
    }
    // The first key in the node is GT the given key_byte.
    return parent_class::end_result;
  }

  // N16: The keys[] is ordered for N16, so we scan the keys[] in order,
  // returning the first value GTE the given [key_byte].
  [[nodiscard]] constexpr typename basic_inode_16::iter_result_opt gte_key_byte(
      std::byte key_byte) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i) {
      const auto key = keys.byte_array[i].load();
      if (key >= key_byte) {
        return {{node_ptr{this, node_type::I16}, key, i}};
      }
    }
    // This should only occur if there is no entry in the keys[] which
    // is greater-than the given [key_byte].
    return parent_class::end_result;
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
    const uint8_t children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i)
      ArtPolicy::delete_subtree(children[i], db_instance);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os,
                                                bool recursive) const {
    parent_class::dump(os, recursive);
    const auto children_count_ = this->children_count.load();
    os << ", key bytes =";
    for (std::uint8_t i = 0; i < children_count_; ++i)
      dump_byte(os, keys.byte_array[i]);
    if (recursive) {
      os << ", children:  \n";
      for (std::uint8_t i = 0; i < children_count_; ++i)
        ArtPolicy::dump_node(os, children[i].load());
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 private:
  [[nodiscard, gnu::pure]] constexpr auto get_sorted_key_array_insert_position(
      std::byte key_byte) noexcept {
    const auto children_count_ = this->children_count.load();

    UNODB_DETAIL_ASSERT(children_count_ < basic_inode_16::capacity);
    UNODB_DETAIL_ASSERT(std::is_sorted(
        keys.byte_array.cbegin(), keys.byte_array.cbegin() + children_count_));
    UNODB_DETAIL_ASSERT(
        std::adjacent_find(keys.byte_array.cbegin(),
                           keys.byte_array.cbegin() + children_count_) >=
        keys.byte_array.cbegin() + children_count_);

#ifdef UNODB_DETAIL_X86_64
    const auto replicated_insert_key =
        _mm_set1_epi8(static_cast<char>(key_byte));
    const auto lesser_key_positions =
        _mm_cmple_epu8(replicated_insert_key, keys.byte_vector);
    const auto mask = (1U << children_count_) - 1;
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(lesser_key_positions)) & mask;
    const auto result = (bit_field != 0)
                            ? detail::ctz(bit_field)
                            : static_cast<std::uint8_t>(children_count_);
#else
    // This is also the best current ARM implementation
    const auto result = static_cast<std::uint8_t>(
        std::lower_bound(keys.byte_array.cbegin(),
                         keys.byte_array.cbegin() + children_count_, key_byte) -
        keys.byte_array.cbegin());
#endif

    UNODB_DETAIL_ASSERT(
        result == children_count_ ||
        (result < children_count_ && keys.byte_array[result] != key_byte));
    return result;
  }

 protected:
  union key_union {
    std::array<critical_section_policy<std::byte>, basic_inode_16::capacity>
        byte_array;
#ifdef UNODB_DETAIL_X86_64
    __m128i byte_vector;
#elif defined(__aarch64__)
    uint8x16_t byte_vector;
#endif
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
    key_union() noexcept {}
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  } keys;
  std::array<critical_section_policy<node_ptr>, basic_inode_16::capacity>
      children;

 private:
  static constexpr std::uint8_t empty_child = 0xFF;

  template <class>
  friend class basic_inode_4;
  template <class>
  friend class basic_inode_48;
};  // class basic_inode_16

template <class ArtPolicy>
using basic_inode_48_parent = basic_inode<
    ArtPolicy, 17, 48, node_type::I48, typename ArtPolicy::inode16_type,
    typename ArtPolicy::inode256_type, typename ArtPolicy::inode48_type>;

// An internal node that is used for nodes having between 17 and 48
// children.  The keys[] is 256 bytes and is directly indexed by the
// byte value of the current byte of the search key.  The values
// stored in the keys[] are index positions in the child pointer
// array.  Thus, neither keys[] nor the child pointer[] are dense.
template <class ArtPolicy>
class basic_inode_48 : public basic_inode_48_parent<ArtPolicy> {
  using parent_class = basic_inode_48_parent<ArtPolicy>;

  using typename parent_class::inode16_type;
  using typename parent_class::inode256_type;
  using typename parent_class::inode48_type;
  using typename parent_class::leaf_type;
  using typename parent_class::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_leaf_unique_ptr;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  constexpr basic_inode_48(db &, const inode16_type &source_node) noexcept
      : parent_class{source_node} {}

  constexpr basic_inode_48(db &, const inode256_type &source_node) noexcept
      : parent_class{source_node} {}

  constexpr basic_inode_48(db &db_instance,
                           inode16_type &__restrict source_node,
                           db_leaf_unique_ptr &&child,
                           tree_depth depth) noexcept
      : parent_class{source_node} {
    init(db_instance, source_node, std::move(child), depth);
  }

  constexpr basic_inode_48(db &db_instance,
                           inode256_type &__restrict source_node,
                           std::uint8_t child_to_delete) noexcept
      : parent_class{source_node} {
    init(db_instance, source_node, child_to_delete);
  }

  constexpr void init(db &db_instance, inode16_type &__restrict source_node,
                      db_leaf_unique_ptr child, tree_depth depth) noexcept {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr<inode16_type>(
            &source_node, db_instance)};
    auto *const __restrict child_ptr = child.release();

    // TODO(laurynas): consider AVX512 scatter?
    std::uint8_t i = 0;
    for (; i < inode16_type::capacity; ++i) {
      const auto existing_key_byte = source_node.keys.byte_array[i].load();
      child_indexes[static_cast<std::uint8_t>(existing_key_byte)] = i;
    }
    for (i = 0; i < inode16_type::capacity; ++i) {
      children.pointer_array[i] = source_node.children[i];
    }

    const auto key_byte =
        static_cast<std::uint8_t>(child_ptr->get_key()[depth]);

    UNODB_DETAIL_ASSERT(child_indexes[key_byte] == empty_child);
    UNODB_DETAIL_ASSUME(i == inode16_type::capacity);

    child_indexes[key_byte] = i;
    children.pointer_array[i] = node_ptr{child_ptr, node_type::LEAF};
    for (i = this->children_count; i < basic_inode_48::capacity; i++) {
      children.pointer_array[i] = node_ptr{nullptr};
    }
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr void init(db &db_instance, inode256_type &__restrict source_node,
                      std::uint8_t child_to_delete) noexcept {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr<inode256_type>(
            &source_node, db_instance)};
    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        source_node.children[child_to_delete]
            .load()
            .template ptr<leaf_type *>(),
        db_instance)};

    source_node.children[child_to_delete] = node_ptr{nullptr};

    std::uint8_t next_child = 0;
    for (unsigned child_i = 0; child_i < 256; child_i++) {
      const auto child_ptr = source_node.children[child_i].load();
      if (child_ptr == nullptr) continue;

      child_indexes[child_i] = next_child;
      children.pointer_array[next_child] = source_node.children[child_i].load();
      ++next_child;

      if (next_child == basic_inode_48::capacity) return;
    }
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    UNODB_DETAIL_ASSERT(this->children_count == children_count_);
    UNODB_DETAIL_ASSERT(children_count_ >= parent_class::min_size);
    UNODB_DETAIL_ASSERT(children_count_ < parent_class::capacity);

    const auto key_byte = static_cast<uint8_t>(child->get_key()[depth]);
    UNODB_DETAIL_ASSERT(child_indexes[key_byte] == empty_child);
    unsigned i{0};
#ifdef UNODB_DETAIL_SSE4_2
    const auto nullptr_vector = _mm_setzero_si128();
    while (true) {
      const auto ptr_vec0 = _mm_load_si128(&children.pointer_vector[i]);
      const auto ptr_vec1 = _mm_load_si128(&children.pointer_vector[i + 1]);
      const auto ptr_vec2 = _mm_load_si128(&children.pointer_vector[i + 2]);
      const auto ptr_vec3 = _mm_load_si128(&children.pointer_vector[i + 3]);
      const auto vec0_cmp = _mm_cmpeq_epi64(ptr_vec0, nullptr_vector);
      const auto vec1_cmp = _mm_cmpeq_epi64(ptr_vec1, nullptr_vector);
      const auto vec2_cmp = _mm_cmpeq_epi64(ptr_vec2, nullptr_vector);
      const auto vec3_cmp = _mm_cmpeq_epi64(ptr_vec3, nullptr_vector);
      // OK to treat 64-bit comparison result as 32-bit vector: we need to find
      // the first 0xFF only.
      const auto vec01_cmp = _mm_packs_epi32(vec0_cmp, vec1_cmp);
      const auto vec23_cmp = _mm_packs_epi32(vec2_cmp, vec3_cmp);
      const auto vec_cmp = _mm_packs_epi32(vec01_cmp, vec23_cmp);
      const auto cmp_mask =
          static_cast<std::uint64_t>(_mm_movemask_epi8(vec_cmp));
      if (cmp_mask != 0) {
        i = (i << 1U) + (((detail::ctz(cmp_mask)) + 1U) >> 1U);
        break;
      }
      i += 4;
    }
#elif defined(UNODB_DETAIL_AVX2)
    const auto nullptr_vector = _mm256_setzero_si256();
    while (true) {
      const auto ptr_vec0 = _mm256_load_si256(&children.pointer_vector[i]);
      const auto ptr_vec1 = _mm256_load_si256(&children.pointer_vector[i + 1]);
      const auto ptr_vec2 = _mm256_load_si256(&children.pointer_vector[i + 2]);
      const auto ptr_vec3 = _mm256_load_si256(&children.pointer_vector[i + 3]);
      const auto vec0_cmp = _mm256_cmpeq_epi64(ptr_vec0, nullptr_vector);
      const auto vec1_cmp = _mm256_cmpeq_epi64(ptr_vec1, nullptr_vector);
      const auto vec2_cmp = _mm256_cmpeq_epi64(ptr_vec2, nullptr_vector);
      const auto vec3_cmp = _mm256_cmpeq_epi64(ptr_vec3, nullptr_vector);
      const auto interleaved_vec01_cmp = _mm256_packs_epi32(vec0_cmp, vec1_cmp);
      const auto interleaved_vec23_cmp = _mm256_packs_epi32(vec2_cmp, vec3_cmp);
      const auto doubly_interleaved_vec_cmp =
          _mm256_packs_epi32(interleaved_vec01_cmp, interleaved_vec23_cmp);
      if (!_mm256_testz_si256(doubly_interleaved_vec_cmp,
                              doubly_interleaved_vec_cmp)) {
        const auto vec01_cmp =
            _mm256_permute4x64_epi64(interleaved_vec01_cmp, 0b11'01'10'00);
        const auto vec23_cmp =
            _mm256_permute4x64_epi64(interleaved_vec23_cmp, 0b11'01'10'00);
        const auto interleaved_vec_cmp =
            _mm256_packs_epi32(vec01_cmp, vec23_cmp);
        const auto vec_cmp =
            _mm256_permute4x64_epi64(interleaved_vec_cmp, 0b11'01'10'00);
        const auto cmp_mask =
            static_cast<std::uint64_t>(_mm256_movemask_epi8(vec_cmp));
        i = (i << 2U) + (detail::ctz(cmp_mask) >> 1U);
        break;
      }
      i += 4;
    }
#elif defined(__aarch64__)
    const auto nullptr_vector = vdupq_n_u64(0);
    while (true) {
      const auto ptr_vec0 = children.pointer_vector[i];
      const auto ptr_vec1 = children.pointer_vector[i + 1];
      const auto ptr_vec2 = children.pointer_vector[i + 2];
      const auto ptr_vec3 = children.pointer_vector[i + 3];
      const auto vec0_cmp = vceqq_u64(nullptr_vector, ptr_vec0);
      const auto vec1_cmp = vceqq_u64(nullptr_vector, ptr_vec1);
      const auto vec2_cmp = vceqq_u64(nullptr_vector, ptr_vec2);
      const auto vec3_cmp = vceqq_u64(nullptr_vector, ptr_vec3);
      const auto narrowed_cmp0 = vshrn_n_u64(vec0_cmp, 4);
      const auto narrowed_cmp1 = vshrn_n_u64(vec1_cmp, 4);
      const auto narrowed_cmp2 = vshrn_n_u64(vec2_cmp, 4);
      const auto narrowed_cmp3 = vshrn_n_u64(vec3_cmp, 4);
      const auto cmp01 = vcombine_u32(narrowed_cmp0, narrowed_cmp1);
      const auto cmp23 = vcombine_u32(narrowed_cmp2, narrowed_cmp3);
      // NOLINTNEXTLINE(misc-const-correctness)
      const auto narrowed_cmp01 = vshrn_n_u32(cmp01, 4);
      // NOLINTNEXTLINE(misc-const-correctness)
      const auto narrowed_cmp23 = vshrn_n_u32(cmp23, 4);
      const auto cmp = vcombine_u16(narrowed_cmp01, narrowed_cmp23);
      // NOLINTNEXTLINE(misc-const-correctness)
      const auto narrowed_cmp = vshrn_n_u16(cmp, 4);
      const auto scalar_pos =
          // NOLINTNEXTLINE(misc-const-correctness)
          vget_lane_u64(vreinterpret_u64_u8(narrowed_cmp), 0);
      if (scalar_pos != 0) {
        i = (i << 1U) + static_cast<unsigned>(detail::ctz(scalar_pos) >> 3U);
        break;
      }
      i += 4;
    }
#else   // #ifdef UNODB_DETAIL_X86_64
    node_ptr child_ptr;
    while (true) {
      child_ptr = children.pointer_array[i];
      if (child_ptr == nullptr) break;
      UNODB_DETAIL_ASSERT(i < 255);
      ++i;
    }
#endif  // #ifdef UNODB_DETAIL_X86_64

#ifndef NDEBUG
    UNODB_DETAIL_ASSERT(i < parent_class::capacity);
    UNODB_DETAIL_ASSERT(children.pointer_array[i] == nullptr);
    for (unsigned j = 0; j < i; ++j)
      UNODB_DETAIL_ASSERT(children.pointer_array[j] != nullptr);
#endif

    child_indexes[key_byte] = static_cast<std::uint8_t>(i);
    children.pointer_array[i] = node_ptr{child.release(), node_type::LEAF};
    this->children_count = children_count_ + 1U;
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    remove_child_pointer(child_index, db_instance);
    children.pointer_array[child_indexes[child_index]] = node_ptr{nullptr};
    child_indexes[child_index] = empty_child;
    --this->children_count;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[nodiscard]] constexpr typename basic_inode_48::find_result find_child(
      std::byte key_byte) noexcept {
    const auto child_i =
        child_indexes[static_cast<std::uint8_t>(key_byte)].load();
    if (child_i != empty_child) {
      return std::make_pair(static_cast<std::uint8_t>(key_byte),
                            &children.pointer_array[child_i]);
    }
    return parent_class::child_not_found;
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // N48: This is the case where we need to indirect through child_indices.
  [[nodiscard]] constexpr node_ptr get_child(
      std::uint8_t child_index) noexcept {
    const auto child_i = child_indexes[child_index].load();
    // In a data race, the child_indices[] can be concurrently
    // modified, which will cause the OLC version tag to get
    // bumped. However, we are in the middle of reading and acting on
    // the data while that happens.  This can cause the value stored
    // in child_indices[] at our desired child_index to be empty_child
    // (0xFF).  In this circumstance, the caller will correctly detect
    // a problem when they do read_critical_section::check(), but we
    // will have still indirected beyond the end of the allocation and
    // ASAN can fail us.  To prevent that and read only the data that
    // is legally allocated to the node, we return nullptr in this
    // case and rely on the caller to detect a problem when they call
    // read_critical_section::check().
    return UNODB_DETAIL_UNLIKELY(child_i == empty_child)
               ? node_ptr()  // aka nullptr
               : children.pointer_array[child_i].load();
  }

  // N48: Return the child pointer for the first key in the
  // lexicographic ordering that is mapped to some child.  We scan
  // child_indexes[256], which contains an index into each of the 48
  // possible children iff a given key is mapped to a child, looking
  // for the first mapped entry. That will be the smallest key mapped
  // by the N48 node.
  [[nodiscard]] constexpr typename basic_inode_48::iter_result
  begin() noexcept {
    for (std::uint64_t i = 0; i < 256; i++) {
      if (child_indexes[i] != empty_child) {
        const auto key = static_cast<std::byte>(i);
        const auto child_index = static_cast<std::uint8_t>(i);
        return {node_ptr{this, node_type::I48}, key, child_index};
      }
    }
    UNODB_DETAIL_CANNOT_HAPPEN();  // because we always have at least 17 keys.
  }

  // N48: Return the child pointer for the last key in the
  // lexicographic ordering that is mapped to some child.  We scan
  // child_indexes[256] in reverse, which contains an index into each
  // of the 48 possible children iff a given key is mapped to a child,
  // looking for the last mapped entry. That will be the greatest key
  // mapped by the N48 node.
  [[nodiscard]] constexpr typename basic_inode_48::iter_result last() noexcept {
    for (std::int64_t i = 255; i >= 0; i--) {
      if (child_indexes[static_cast<std::uint8_t>(i)] != empty_child) {
        const auto key = static_cast<std::byte>(i);
        const auto child_index = static_cast<std::uint8_t>(i);
        return {node_ptr{this, node_type::I48}, key, child_index};
      }
    }
    // because we always have at least 17 keys.
    UNODB_DETAIL_CANNOT_HAPPEN();  // LCOV_EXCL_LINE
  }

  [[nodiscard]] constexpr typename basic_inode_48::iter_result_opt next(
      std::uint8_t child_index) noexcept {
    // loop over the remaining byte values in lexical order.
    for (auto i = static_cast<std::uint64_t>(child_index) + 1; i < 256; i++) {
      if (child_indexes[i] != empty_child) {
        const auto key = static_cast<std::byte>(i);
        const auto next_index = static_cast<std::uint8_t>(i);
        return {{node_ptr{this, node_type::I48}, key, next_index}};
      }
    }
    return parent_class::end_result;
  }

  [[nodiscard]] constexpr typename basic_inode_48::iter_result_opt prior(
      std::uint8_t child_index) noexcept {
    // loop over the prior byte values in lexical order.
    for (auto i = static_cast<std::int64_t>(child_index) - 1; i >= 0; i--) {
      if (child_indexes[static_cast<std::uint8_t>(i)] != empty_child) {
        const auto key = static_cast<std::byte>(i);
        const auto next_index = static_cast<std::uint8_t>(i);
        return {{node_ptr{this, node_type::I48}, key, next_index}};
      }
    }
    return parent_class::end_result;
  }

  // N48: This is nearly identical to prior() except that we start the
  // search on the [key_byte] rather than the position before that.
  [[nodiscard]] constexpr typename basic_inode_48::iter_result_opt lte_key_byte(
      std::byte key_byte) noexcept {
    // loop over the prior byte values in lexical order.
    for (auto i = static_cast<std::int64_t>(key_byte); i >= 0; i--) {
      const auto child_index = static_cast<std::uint8_t>(i);
      if (child_indexes[child_index] != empty_child) {
        const auto key = static_cast<std::byte>(i);
        return {{node_ptr{this, node_type::I48}, key, child_index}};
      }
    }
    return parent_class::end_result;
  }

  // N48: This is nearly identical to next() except that we start the
  // search on the [key_byte] rather than the position after that.
  [[nodiscard]] constexpr typename basic_inode_48::iter_result_opt gte_key_byte(
      std::byte key_byte) noexcept {
    // loop over the remaining byte values in lexical order.
    for (auto i = static_cast<std::uint64_t>(key_byte); i < 256; i++) {
      const auto child_index = static_cast<std::uint8_t>(i);
      if (child_indexes[child_index] != empty_child) {
        const auto key = static_cast<std::byte>(i);
        return {{node_ptr{this, node_type::I48}, key, child_index}};
      }
    }
    return parent_class::end_result;
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
#ifndef NDEBUG
    const auto children_count_ = this->children_count.load();
    unsigned actual_children_count = 0;
#endif

    for (unsigned i = 0; i < this->capacity; ++i) {
      const auto child = children.pointer_array[i].load();
      if (child != nullptr) {
        ArtPolicy::delete_subtree(child, db_instance);
#ifndef NDEBUG
        ++actual_children_count;
        UNODB_DETAIL_ASSERT(actual_children_count <= children_count_);
#endif
      }
    }
    UNODB_DETAIL_ASSERT(actual_children_count == children_count_);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os,
                                                bool recursive) const {
    parent_class::dump(os, recursive);
#ifndef NDEBUG
    const auto children_count_ = this->children_count.load();
    unsigned actual_children_count = 0;
#endif

    os << ", key bytes & child indexes\n";
    for (unsigned i = 0; i < 256; i++)
      if (child_indexes[i] != empty_child) {
        os << " ";
        dump_byte(os, static_cast<std::byte>(i));
        os << ", child index = " << static_cast<unsigned>(child_indexes[i])
           << ": ";
        UNODB_DETAIL_ASSERT(children.pointer_array[child_indexes[i]] !=
                            nullptr);
        if (recursive) {
          ArtPolicy::dump_node(os,
                               children.pointer_array[child_indexes[i]].load());
        }
#ifndef NDEBUG
        ++actual_children_count;
        UNODB_DETAIL_ASSERT(actual_children_count <= children_count_);
#endif
      }

    UNODB_DETAIL_ASSERT(actual_children_count == children_count_);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 private:
  constexpr void remove_child_pointer(std::uint8_t child_index,
                                      db &db_instance) noexcept {
    direct_remove_child_pointer(child_indexes[child_index], db_instance);
  }

  constexpr void direct_remove_child_pointer(std::uint8_t children_i,
                                             db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(children_i != empty_child);

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children.pointer_array[children_i].load().template ptr<leaf_type *>(),
        db_instance)};
  }

  static constexpr std::uint8_t empty_child = 0xFF;

  // The only way I found to initialize this array so that everyone is happy and
  // efficient. In the case of OLC, a std::fill compiles to a loop doing a
  // single byte per iteration. memset is likely an UB, and atomic_ref is not
  // available in C++17, and I don't like using it anyway, because this variable
  // *is* atomic.
  std::array<critical_section_policy<std::uint8_t>, 256> child_indexes{
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child, empty_child, empty_child, empty_child, empty_child,
      empty_child};

  union children_union {
    std::array<critical_section_policy<node_ptr>, basic_inode_48::capacity>
        pointer_array;
#ifdef UNODB_DETAIL_SSE4_2
    static_assert(basic_inode_48::capacity % 8 == 0);
    // No std::array below because it would ignore the alignment attribute
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    __m128i
        pointer_vector[basic_inode_48::capacity / 2];  // NOLINT(runtime/arrays)
#elif defined(UNODB_DETAIL_AVX2)
    static_assert(basic_inode_48::capacity % 16 == 0);
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    __m256i
        pointer_vector[basic_inode_48::capacity / 4];  // NOLINT(runtime/arrays)
#elif defined(__aarch64__)
    static_assert(basic_inode_48::capacity % 8 == 0);
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    uint64x2_t
        pointer_vector[basic_inode_48::capacity / 2];  // NOLINT(runtime/arrays)
#endif

    UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
    children_union() noexcept {}
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  } children;

  template <class>
  friend class basic_inode_16;
  template <class>
  friend class basic_inode_256;
};  // class basic_inode_48

template <class ArtPolicy>
using basic_inode_256_parent =
    basic_inode<ArtPolicy, 49, 256, node_type::I256,
                typename ArtPolicy::inode48_type, fake_inode,
                typename ArtPolicy::inode256_type>;

// An internal node used to store data for nodes having between 49 and
// 256 child pointers.  There is no keys[].  Instead, the current byte
// of the search key is used as a direct index into the child
// pointer[].
template <class ArtPolicy>
class basic_inode_256 : public basic_inode_256_parent<ArtPolicy> {
  using parent_class = basic_inode_256_parent<ArtPolicy>;

  using typename parent_class::inode48_type;
  using typename parent_class::leaf_type;
  using typename parent_class::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_leaf_unique_ptr;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  constexpr basic_inode_256(db &, const inode48_type &source_node) noexcept
      : parent_class{source_node} {}

  constexpr basic_inode_256(db &db_instance, inode48_type &source_node,
                            db_leaf_unique_ptr &&child,
                            tree_depth depth) noexcept
      : parent_class{source_node} {
    init(db_instance, source_node, std::move(child), depth);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr void init(db &db_instance, inode48_type &__restrict source_node,
                      db_leaf_unique_ptr child, tree_depth depth) noexcept {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr<inode48_type>(
            &source_node, db_instance)};
    unsigned children_copied = 0;
    unsigned i = 0;
    while (true) {
      const auto children_i = source_node.child_indexes[i].load();
      if (children_i == inode48_type::empty_child) {
        children[i] = node_ptr{nullptr};
      } else {
        children[i] = source_node.children.pointer_array[children_i].load();
        ++children_copied;
        if (children_copied == inode48_type::capacity) break;
      }
      ++i;
    }

    ++i;
    for (; i < basic_inode_256::capacity; ++i) children[i] = node_ptr{nullptr};

    const auto key_byte = static_cast<uint8_t>(child->get_key()[depth]);
    UNODB_DETAIL_ASSERT(children[key_byte] == nullptr);
    children[key_byte] = node_ptr{child.release(), node_type::LEAF};
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    UNODB_DETAIL_ASSERT(this->children_count == children_count_);
    UNODB_DETAIL_ASSERT(children_count_ < parent_class::capacity);

    const auto key_byte = static_cast<std::uint8_t>(child->get_key()[depth]);
    UNODB_DETAIL_ASSERT(children[key_byte] == nullptr);
    children[key_byte] = node_ptr{child.release(), node_type::LEAF};
    this->children_count = static_cast<std::uint8_t>(children_count_ + 1U);
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_index].load().template ptr<leaf_type *>(), db_instance)};

    children[child_index] = node_ptr{nullptr};
    --this->children_count;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[nodiscard]] constexpr typename basic_inode_256::find_result find_child(
      std::byte key_byte) noexcept {
    const auto key_int_byte = static_cast<std::uint8_t>(key_byte);
    if (children[key_int_byte] != nullptr)
      return std::make_pair(key_int_byte, &children[key_int_byte]);
    return parent_class::child_not_found;
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] constexpr node_ptr get_child(
      std::uint8_t child_index) noexcept {
    return children[child_index].load();
  }

  // N256: Return the first mapped child.  The children[] is always in
  // order since it is directly indexed by a byte from the key.
  [[nodiscard]] constexpr typename basic_inode_256::iter_result
  begin() noexcept {
    for (std::uint64_t i = 0; i < basic_inode_256::capacity; i++) {
      if (children[i] != nullptr) {
        const auto key = static_cast<std::byte>(i);  // child_index is key byte
        const auto child_index = static_cast<std::uint8_t>(i);
        return {node_ptr{this, node_type::I256}, key, child_index};
      }
    }
    // because we always have at least 49 keys.
    UNODB_DETAIL_CANNOT_HAPPEN();  // LCOV_EXCL_LINE
  }

  // N256: Return the last mapped child.  The children[] is always in
  // order since it is directly indexed by a byte from the key.
  [[nodiscard]] constexpr typename basic_inode_256::iter_result
  last() noexcept {
    for (std::int64_t i = basic_inode_256::capacity - 1; i >= 0; i--) {
      if (children[static_cast<std::uint8_t>(i)] != nullptr) {
        const auto key = static_cast<std::byte>(i);  // child_index is key byte
        const auto child_index = static_cast<std::uint8_t>(i);
        return {node_ptr{this, node_type::I256}, key, child_index};
      }
    }
    // because we always have at least 49 keys.
    UNODB_DETAIL_CANNOT_HAPPEN();  // LCOV_EXCL_LINE
  }

  [[nodiscard]] constexpr typename basic_inode_256::iter_result_opt next(
      const std::uint8_t child_index) noexcept {
    // loop over the remaining byte values in lexical order.
    for (auto i = static_cast<std::uint64_t>(child_index) + 1;
         i < basic_inode_256::capacity; i++) {
      if (children[i] != nullptr) {
        const auto key = static_cast<std::byte>(i);
        const auto next_index = static_cast<std::uint8_t>(i);
        return {{node_ptr{this, node_type::I256}, key, next_index}};
      }
    }
    return parent_class::end_result;
  }

  [[nodiscard]] constexpr typename basic_inode_256::iter_result_opt prior(
      const std::uint8_t child_index) noexcept {
    // loop over the remaining byte values in lexical order.
    for (auto i = static_cast<std::int64_t>(child_index) - 1; i >= 0; i--) {
      const auto next_index = static_cast<std::uint8_t>(i);
      if (children[next_index] != nullptr) {
        const auto key = static_cast<std::byte>(i);
        return {{node_ptr{this, node_type::I256}, key, next_index}};
      }
    }
    return parent_class::end_result;
  }

  // N256: This is nearly identical to prior() except that we start
  // the search on the [key_byte] rather than the position before
  // that.
  [[nodiscard]] constexpr typename basic_inode_256::iter_result_opt
  lte_key_byte(std::byte key_byte) noexcept {
    // loop over the prior byte values in lexical order.
    for (auto i = static_cast<std::int64_t>(key_byte); i >= 0; i--) {
      const auto child_index = static_cast<std::uint8_t>(i);
      if (children[child_index] != nullptr) {
        const auto key = static_cast<std::byte>(i);
        return {{node_ptr{this, node_type::I256}, key, child_index}};
      }
    }
    return parent_class::end_result;
  }

  // N256: This is nearly identical to next() except that we start the
  // search on the [key_byte] rather than the position after that.
  [[nodiscard]] constexpr typename basic_inode_256::iter_result_opt
  gte_key_byte(std::byte key_byte) noexcept {
    // loop over the remaining byte values in lexical order.
    for (auto i = static_cast<std::uint64_t>(key_byte);
         i < basic_inode_256::capacity; i++) {
      const auto child_index = static_cast<std::uint8_t>(i);
      if (children[child_index] != nullptr) {
        const auto key = static_cast<std::byte>(i);
        return {{node_ptr{this, node_type::I48}, key, child_index}};
      }
    }
    return parent_class::end_result;
  }

  // TODO(laurynas) Lifting this out might help with iterator and
  // lambda patterns.
  template <typename Function>
  constexpr void for_each_child(Function func) const
      noexcept(noexcept(func(0, node_ptr{nullptr}))) {
#ifndef NDEBUG
    const auto children_count_ = this->children_count.load();
    std::uint8_t actual_children_count = 0;
#endif

    for (unsigned i = 0; i < 256; ++i) {
      const auto child_ptr = children[i].load();
      if (child_ptr != nullptr) {
        func(i, child_ptr);
#ifndef NDEBUG
        ++actual_children_count;
        UNODB_DETAIL_ASSERT(actual_children_count <= children_count_ ||
                            children_count_ == 0);
#endif
      }
    }
    UNODB_DETAIL_ASSERT(actual_children_count == children_count_);
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
    for_each_child([&db_instance](unsigned, node_ptr child) noexcept {
      ArtPolicy::delete_subtree(child, db_instance);
    });
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os,
                                                bool recursive) const {
    parent_class::dump(os, recursive);
    os << ", key bytes & children:\n";
    for_each_child([&os, recursive](unsigned i, node_ptr child) {
      os << ' ';
      dump_byte(os, static_cast<std::byte>(i));
      os << ' ';
      if (recursive) {
        ArtPolicy::dump_node(os, child);
      }
    });
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 private:
  std::array<critical_section_policy<node_ptr>, basic_inode_256::capacity>
      children;

  template <class>
  friend class basic_inode_48;
};  // class basic_inode_256

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_ART_INTERNAL_IMPL_HPP
