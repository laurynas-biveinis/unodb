// Copyright 2019-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_INTERNAL_IMPL_HPP
#define UNODB_DETAIL_ART_INTERNAL_IMPL_HPP

#include "global.hpp"

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
#include <smmintrin.h>
#endif
#ifdef UNODB_DETAIL_AVX2
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

#include <gsl/util>

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

template <>
[[nodiscard, gnu::const]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC std::uint64_t
basic_art_key<std::uint64_t>::make_binary_comparable(std::uint64_t k) noexcept {
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
  return bswap(k);
#else
#error Needs implementing
#endif
}

#ifdef UNODB_DETAIL_X86_64

// Idea from https://stackoverflow.com/a/32945715/80458
[[nodiscard, gnu::const]] inline auto _mm_cmple_epu8(__m128i x,
                                                     __m128i y) noexcept {
  return _mm_cmpeq_epi8(_mm_max_epu8(y, x), y);
}

#else  // #ifdef UNODB_DETAIL_X86_64

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
      : key{k}, value_size{gsl::narrow_cast<value_size_type>(v.size())} {
    UNODB_DETAIL_ASSERT(v.size() <= max_value_size);

    if (!v.empty()) std::memcpy(&value_start[0], v.data(), value_size);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard, gnu::pure]] constexpr auto get_key() const noexcept {
    return key;
  }

  [[nodiscard, gnu::pure]] constexpr auto matches(art_key k) const noexcept {
    return k == get_key();
  }

  [[nodiscard, gnu::pure]] constexpr auto get_value_view() const noexcept {
    return value_view{&value_start[0], value_size};
  }

  [[nodiscard, gnu::pure]] constexpr auto get_size() const noexcept {
    return compute_size(value_size);
  }

  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    os << ", " << get_key() << ", value size: " << value_size << '\n';
  }

  [[nodiscard, gnu::const]] static constexpr auto compute_size(
      value_size_type val_size) noexcept {
    return sizeof(basic_leaf<Header>) + val_size - 1;
  }

  static constexpr std::size_t max_value_size =
      std::numeric_limits<value_size_type>::max();

 private:
  const art_key key;
  const value_size_type value_size;
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  std::byte value_start[1];
};

template <class Header, class Db>
[[nodiscard]] auto make_db_leaf_ptr(art_key k, value_view v, Db &db) {
  using leaf_type = basic_leaf<Header>;

  if (UNODB_DETAIL_UNLIKELY(v.size() > leaf_type::max_value_size)) {
    throw std::length_error("Value length must fit in std::uint32_t");
  }

  const auto size = leaf_type::compute_size(
      gsl::narrow_cast<typename leaf_type::value_size_type>(v.size()));

  auto *const leaf_mem = static_cast<std::byte *>(
      allocate_aligned(size, alignment_for_new<leaf_type>()));

  db.increment_leaf_count(size);

  return basic_db_leaf_unique_ptr<Header, Db>{
      new (leaf_mem) leaf_type{k, v}, basic_db_leaf_deleter<Header, Db>{db}};
}

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
  const auto leaf_size = to_delete->get_size();

  free_aligned(to_delete);

  db.decrement_leaf_count(leaf_size);
}

template <class INode, class Db>
inline void basic_db_inode_deleter<INode, Db>::operator()(
    INode *inode_ptr) noexcept {
  static_assert(std::is_trivially_destructible_v<INode>);

  free_aligned(inode_ptr);

  db.template decrement_inode_count<INode>();
}

template <class Db, template <class> class CriticalSectionPolicy, class NodePtr,
          class INodeDefs, template <class> class INodeReclamator,
          template <class, class> class LeafReclamator>
struct basic_art_policy final {
  using node_ptr = NodePtr;
  using header_type = typename NodePtr::header_type;

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

  [[nodiscard]] static auto make_db_leaf_ptr(art_key k, value_view v,
                                             Db &db_instance) {
    return ::unodb::detail::make_db_leaf_ptr<header_type, Db>(k, v,
                                                              db_instance);
  }

  [[nodiscard]] static auto reclaim_leaf_on_scope_exit(
      leaf_type *leaf, Db &db_instance) noexcept {
    return leaf_reclaimable_ptr{leaf,
                                LeafReclamator<header_type, Db>{db_instance}};
  }

  UNODB_DETAIL_DISABLE_GCC_11_WARNING("-Wmismatched-new-delete")
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26409)
  template <class INode, class... Args>
  [[nodiscard]] static auto make_db_inode_unique_ptr(Db &db_instance,
                                                     Args &&...args) {
    db_inode_unique_ptr<INode> result{
        new INode{db_instance, std::forward<Args>(args)...},
        db_inode_deleter<INode>{db_instance}};

    db_instance.template increment_inode_count<INode>();

    return result;
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  UNODB_DETAIL_RESTORE_GCC_11_WARNINGS()

  template <class INode>
  [[nodiscard]] static auto make_db_inode_unique_ptr(INode *inode_ptr,
                                                     Db &db_instance) noexcept {
    return db_inode_unique_ptr<INode>{inode_ptr,
                                      db_inode_deleter<INode>{db_instance}};
  }

  template <class INode>
  [[nodiscard]] static auto make_db_inode_reclaimable_ptr(
      INode *inode_ptr, Db &db_instance) noexcept {
    return db_inode_reclaimable_ptr<INode>{inode_ptr,
                                           INodeReclamator<INode>{db_instance}};
  }

 private:
  [[nodiscard]] static auto make_db_leaf_ptr(leaf_type *leaf,
                                             Db &db_instance) noexcept {
    return basic_db_leaf_unique_ptr<header_type, Db>{
        leaf, basic_db_leaf_deleter<header_type, Db>{db_instance}};
  }

  struct delete_db_node_ptr_at_scope_exit final {
    constexpr explicit delete_db_node_ptr_at_scope_exit(NodePtr node_ptr_,
                                                        Db &db_) noexcept
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
      std::ostream &os, const NodePtr &node) {
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
        node.template ptr<leaf_type *>()->dump(os);
        break;
      case node_type::I4:
        os << "I4";
        node.template ptr<inode4_type *>()->dump(os);
        break;
      case node_type::I16:
        os << "I16";
        node.template ptr<inode16_type *>()->dump(os);
        break;
      case node_type::I48:
        os << "I48";
        node.template ptr<inode48_type *>()->dump(os);
        break;
      case node_type::I256:
        os << "I256";
        node.template ptr<inode256_type *>()->dump(os);
        break;
    }
  }

  basic_art_policy() = delete;
};

template <template <class> class CriticalSectionPolicy>
union [[nodiscard]] key_prefix {
 private:
  template <typename T>
  using critical_section_policy = CriticalSectionPolicy<T>;

  using key_prefix_size = std::uint8_t;

  static constexpr key_prefix_size key_prefix_capacity = 7;

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

  [[nodiscard]] constexpr auto get_shared_length(
      unodb::detail::art_key shifted_key) const noexcept {
    return shared_len(static_cast<std::uint64_t>(shifted_key), u64, length());
  }

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

  [[nodiscard]] constexpr auto byte_at(std::size_t i) const noexcept {
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
    return detail::ctz64(clamped) >> 3U;
  }

  [[nodiscard, gnu::const]] static constexpr std::uint64_t make_u64(
      art_key k1, art_key shifted_k2, tree_depth depth) noexcept {
    k1.shift_right(depth);

    const auto k1_u64 = static_cast<std::uint64_t>(k1) & key_bytes_mask;

    return k1_u64 | length_to_word(shared_len(
                        k1_u64, static_cast<std::uint64_t>(shifted_k2),
                        key_prefix_capacity));
  }
};

// A class used as a sentinel for basic_inode template args: the
// larger node type for the largest node type and the smaller node type for
// the smallest node type.
class fake_inode final {
 public:
  fake_inode() = delete;
};

template <class ArtPolicy>
class basic_inode_impl : public ArtPolicy::header_type {
 public:
  using node_ptr = typename ArtPolicy::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

  using db_leaf_unique_ptr = typename ArtPolicy::db_leaf_unique_ptr;

  using db = typename ArtPolicy::db;

  // The first element is the child index in the node, the 2nd is pointer
  // to the child. If not present, the pointer is nullptr, and the index
  // is undefined
  using find_result =
      std::pair<std::uint8_t, critical_section_policy<node_ptr> *>;

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
      case node_type::LEAF:
        // LCOV_EXCL_START
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
    UNODB_DETAIL_CANNOT_HAPPEN();
    // LCOV_EXCL_STOP
  }

  [[nodiscard]] constexpr find_result find_child(node_type type,
                                                 std::byte key_byte) noexcept {
    UNODB_DETAIL_ASSERT(type != node_type::LEAF);
    // Because of the parallel updates, the callees below may work on
    // inconsistent nodes and must not assert, just produce results, which are
    // OK to be incorrect/inconsistent as the node state will be checked before
    // acting on them.

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

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // inode must not be allocated directly on heap, concrete subclasses will
  // define their own new and delete operators using node pools
  [[nodiscard, gnu::cold]] UNODB_DETAIL_NOINLINE static void *operator new(
      std::size_t) {
    UNODB_DETAIL_CANNOT_HAPPEN();
  }

  UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-noreturn")
  [[gnu::cold]] UNODB_DETAIL_NOINLINE static void operator delete(void *) {
    UNODB_DETAIL_CANNOT_HAPPEN();
  }
  UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

  constexpr basic_inode_impl(unsigned children_count_, art_key k1,
                             art_key shifted_k2, tree_depth depth) noexcept
      : k_prefix{k1, shifted_k2, depth},
        children_count{gsl::narrow_cast<std::uint8_t>(children_count_)} {}

  constexpr basic_inode_impl(unsigned children_count_, unsigned key_prefix_len,
                             const inode_type &key_prefix_source_node) noexcept
      : k_prefix{key_prefix_len, key_prefix_source_node.get_key_prefix()},
        children_count{gsl::narrow_cast<std::uint8_t>(children_count_)} {}

  constexpr basic_inode_impl(unsigned children_count_,
                             const basic_inode_impl &other) noexcept
      : k_prefix{other.k_prefix},
        children_count{gsl::narrow_cast<std::uint8_t>(children_count_)} {}

 protected:
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
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
  static constexpr find_result child_not_found{child_not_found_i, nullptr};

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
};

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

  [[nodiscard]] static void *operator new(std::size_t size) {
    UNODB_DETAIL_ASSERT(size == sizeof(Derived));

    return allocate_aligned(size, alignment_for_new<Derived>());
  }

  static void operator delete(void *) {
    // Pool-allocated memory should not be released through operator delete.
    UNODB_DETAIL_CANNOT_HAPPEN();  // LCOV_EXCL_LINE
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

    const auto source_node_key_byte = source_key_prefix.byte_at(len);
    source_key_prefix.cut(len + 1);
    const auto new_key_byte = child1->get_key()[diff_key_byte_i];
    add_two_to_empty(source_node_key_byte, source_node, new_key_byte,
                     std::move(child1));
  }

  constexpr void init(db &db_instance, inode16_type &source_node,
                      std::uint8_t child_to_delete) {
    const auto reclaim_source_node{
        ArtPolicy::template make_db_inode_reclaimable_ptr(&source_node,
                                                          db_instance)};
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
        vsetq_lane_u32(keys.integer.load(), vdupq_n_u32(0), 0));
    const auto mask = (1ULL << (this->children_count.load() << 3U)) - 1;
    const auto matching_key_positions =
        vceqq_u8(replicated_search_key, keys_in_neon_reg);
    const auto u64_pos_in_vec =
        vget_low_u64(vreinterpretq_u64_u8(matching_key_positions));
    const auto pos_in_scalar = vget_lane_u64(u64_pos_in_vec, 0);
    const auto masked_pos = pos_in_scalar & mask;

    if (masked_pos == 0) return parent_class::child_not_found;

    const auto i = gsl::narrow_cast<unsigned>(
        detail::ctz(gsl::narrow_cast<unsigned>(masked_pos)) >> 3U);
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

  constexpr void delete_subtree(db &db_instance) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i) {
      ArtPolicy::delete_subtree(children[i], db_instance);
    }
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    parent_class::dump(os);
    const auto children_count_ = this->children_count.load();
    os << ", key bytes =";
    for (std::uint8_t i = 0; i < children_count_; i++)
      dump_byte(os, keys.byte_array[i]);
    os << ", children:\n";
    for (std::uint8_t i = 0; i < children_count_; i++)
      ArtPolicy::dump_node(os, children[i].load());
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
};

template <class ArtPolicy>
using basic_inode_16_parent = basic_inode<
    ArtPolicy, 5, 16, node_type::I16, typename ArtPolicy::inode4_type,
    typename ArtPolicy::inode48_type, typename ArtPolicy::inode16_type>;

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
        ArtPolicy::template make_db_inode_reclaimable_ptr(&source_node,
                                                          db_instance)};
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
        ArtPolicy::template make_db_inode_reclaimable_ptr(&source_node,
                                                          db_instance)};
    source_node.remove_child_pointer(child_to_delete, db_instance);
    source_node.child_indexes[child_to_delete] = inode48_type::empty_child;

    // TODO(laurynas): consider AVX2 gather?
    unsigned next_child = 0;
    unsigned i = 0;
    while (true) {
      const auto source_child_i = source_node.child_indexes[i].load();
      if (source_child_i != inode48_type::empty_child) {
        keys.byte_array[next_child] = gsl::narrow_cast<std::byte>(i);
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
        vget_lane_u64(vreinterpret_u64_u8(narrowed_positions), 0);
    const auto mask = gsl::narrow_cast<std::uint64_t>(
        (static_cast<__uint128_t>(1U) << (this->children_count.load() << 2U)) -
        1);
    const auto masked_pos = scalar_pos & mask;

    if (masked_pos == 0) return parent_class::child_not_found;

    const auto i = detail::ctz64(masked_pos) >> 2U;
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

  constexpr void delete_subtree(db &db_instance) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i)
      ArtPolicy::delete_subtree(children[i], db_instance);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    parent_class::dump(os);
    const auto children_count_ = this->children_count.load();
    os << ", key bytes =";
    for (std::uint8_t i = 0; i < children_count_; ++i)
      dump_byte(os, keys.byte_array[i]);
    os << ", children:\n";
    for (std::uint8_t i = 0; i < children_count_; ++i)
      ArtPolicy::dump_node(os, children[i].load());
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
                            : gsl::narrow_cast<std::uint8_t>(children_count_);
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
};

template <class ArtPolicy>
using basic_inode_48_parent = basic_inode<
    ArtPolicy, 17, 48, node_type::I48, typename ArtPolicy::inode16_type,
    typename ArtPolicy::inode256_type, typename ArtPolicy::inode48_type>;

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
        ArtPolicy::template make_db_inode_reclaimable_ptr(&source_node,
                                                          db_instance)};
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
        ArtPolicy::template make_db_inode_reclaimable_ptr(&source_node,
                                                          db_instance)};
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
        i = (i << 1U) + (((detail::ctz64(cmp_mask)) + 1) >> 1U);
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
        i = (i << 2U) + (detail::ctz64(cmp_mask) >> 1U);
        break;
      }
      i += 4;
    }
#else   // #ifdef UNODB_DETAIL_X86_64
    // This is also the current best ARM implementation
    node_ptr child_ptr;
    while (true) {
      child_ptr = children.pointer_array[i];
      if (child_ptr == nullptr) break;
      UNODB_DETAIL_ASSERT(i < 255);
      ++i;
    }
#endif  // #ifdef UNODB_DETAIL_X86_64

    UNODB_DETAIL_ASSERT(i < parent_class::capacity);
    UNODB_DETAIL_ASSERT(children.pointer_array[i] == nullptr);

    child_indexes[key_byte] = gsl::narrow_cast<std::uint8_t>(i);
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
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    parent_class::dump(os);
#ifndef NDEBUG
    const auto children_count_ = this->children_count.load();
    unsigned actual_children_count = 0;
#endif

    os << ", key bytes & child indexes\n";
    for (unsigned i = 0; i < 256; i++)
      if (child_indexes[i] != empty_child) {
        os << " ";
        dump_byte(os, gsl::narrow_cast<std::byte>(i));
        os << ", child index = " << static_cast<unsigned>(child_indexes[i])
           << ": ";
        UNODB_DETAIL_ASSERT(children.pointer_array[child_indexes[i]] !=
                            nullptr);
        ArtPolicy::dump_node(os,
                             children.pointer_array[child_indexes[i]].load());
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
#endif

    UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
    children_union() noexcept {}
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  } children;

  template <class>
  friend class basic_inode_16;
  template <class>
  friend class basic_inode_256;
};

template <class ArtPolicy>
using basic_inode_256_parent =
    basic_inode<ArtPolicy, 49, 256, node_type::I256,
                typename ArtPolicy::inode48_type, fake_inode,
                typename ArtPolicy::inode256_type>;

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
        ArtPolicy::template make_db_inode_reclaimable_ptr(&source_node,
                                                          db_instance)};
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
    this->children_count = children_count_ + 1U;
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
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    parent_class::dump(os);
    os << ", key bytes & children:\n";
    for_each_child([&os](unsigned i, node_ptr child) {
      os << ' ';
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ' ';
      ArtPolicy::dump_node(os, child);
    });
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 private:
  std::array<critical_section_policy<node_ptr>, basic_inode_256::capacity>
      children;

  template <class>
  friend class basic_inode_48;
};

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_ART_INTERNAL_IMPL_HPP
