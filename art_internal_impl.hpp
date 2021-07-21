// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_INTERNAL_IMPL_HPP
#define UNODB_DETAIL_ART_INTERNAL_IMPL_HPP

#include "global.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#ifdef __x86_64
#include <emmintrin.h>
#include <smmintrin.h>
#endif

#include <gsl/gsl_util>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "heap.hpp"
#include "node_type.hpp"

namespace unodb {
class db;
class olc_db;
}  // namespace unodb

namespace unodb::detail {

// For internal node pools, approximate requesting ~2MB blocks from backing
// storage (when ported to Linux, ask for 2MB huge pages directly)
template <class INode>
[[nodiscard]] inline auto get_inode_pool_options() noexcept {
  pmr_pool_options inode_pool_options;
  inode_pool_options.max_blocks_per_chunk = 2 * 1024 * 1024 / sizeof(INode);
  inode_pool_options.largest_required_pool_block = sizeof(INode);
  return inode_pool_options;
}

[[nodiscard]] inline auto &get_leaf_node_pool() noexcept {
  return *pmr_new_delete_resource();
}

template <>
[[gnu::const]] constexpr std::uint64_t
basic_art_key<std::uint64_t>::make_binary_comparable(std::uint64_t k) noexcept {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(k);
#else
#error Needs implementing
#endif
}

#ifdef __x86_64

// Idea from https://stackoverflow.com/a/32945715/80458
inline auto _mm_cmple_epu8(__m128i x, __m128i y) noexcept {
  return _mm_cmpeq_epi8(_mm_max_epu8(y, x), y);
}

#else  // #ifdef __x86_64

// From public domain
// https://graphics.stanford.edu/~seander/bithacks.html
[[gnu::const]] constexpr std::uint32_t has_zero_byte(std::uint32_t v) noexcept {
  return ((v - 0x01010101UL) & ~v & 0x80808080UL);
}

[[gnu::const]] constexpr std::uint32_t contains_byte(std::uint32_t v,
                                                     std::byte b) noexcept {
  return has_zero_byte(v ^ (~0U / 255 * static_cast<std::uint8_t>(b)));
}

#endif  // #ifdef __x86_64

// Helper struct for leaf node-related data and (static) code. We
// don't use a regular class because leaf nodes are of variable size, C++ does
// not support flexible array members, and we want to save one level of
// (heap) indirection.
template <class Header>
struct basic_leaf final {
  using value_size_type = std::uint32_t;

  static constexpr auto offset_header = 0;
  static constexpr auto offset_key =
      std::is_empty_v<Header> ? offset_header : sizeof(Header);
  static constexpr auto offset_value_size = offset_key + sizeof(art_key);

  static constexpr auto offset_value =
      offset_value_size + sizeof(value_size_type);

  [[nodiscard]] static constexpr auto key(const_raw_leaf_ptr leaf) noexcept {
    assert_invariants(leaf);

    return art_key::create(&leaf[offset_key]);
  }

  [[nodiscard]] static constexpr auto matches(const_raw_leaf_ptr leaf,
                                              art_key k) noexcept {
    assert_invariants(leaf);

    return k == leaf + offset_key;
  }

  [[nodiscard]] static constexpr auto value(const_raw_leaf_ptr leaf) noexcept {
    assert_invariants(leaf);

    return value_view{&leaf[offset_value], value_size(leaf)};
  }

  [[nodiscard]] static constexpr std::size_t size(
      const_raw_leaf_ptr leaf) noexcept {
    assert_invariants(leaf);

    return value_size(leaf) + offset_value;
  }

  [[gnu::cold, gnu::noinline]] static void dump(std::ostream &os,
                                                const_raw_leaf_ptr leaf);

  static constexpr void assert_invariants(
      UNODB_DETAIL_USED_IN_DEBUG const_raw_leaf_ptr leaf) noexcept {
#ifndef NDEBUG
    assert(reinterpret_cast<std::uintptr_t>(leaf) % alignof(Header) == 0);
#endif
  }

 private:
  static constexpr auto minimum_size = offset_value;

  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=pure")
  [[nodiscard]] static auto value_size(const_raw_leaf_ptr leaf) noexcept {
    assert_invariants(leaf);

    value_size_type result;
    std::memcpy(&result, &leaf[offset_value_size], sizeof(result));
    return result;
  }
  UNODB_DETAIL_RESTORE_GCC_WARNINGS()
};

template <class Header, class Db>
auto make_db_leaf_ptr(art_key k, value_view v, Db &db) {
  using leaf_type = basic_leaf<Header>;
  using value_size_type = typename leaf_type::value_size_type;

  if (UNODB_DETAIL_UNLIKELY(v.size() >
                            std::numeric_limits<value_size_type>::max())) {
    throw std::length_error("Value length must fit in std::uint32_t");
  }

  const auto value_size = static_cast<value_size_type>(v.size());
  const auto leaf_size =
      static_cast<std::size_t>(leaf_type::offset_value) + value_size;

  auto *const leaf_mem = static_cast<std::byte *>(pmr_allocate(
      get_leaf_node_pool(), leaf_size, alignment_for_new<Header>()));
  new (leaf_mem) Header{};

  db.increment_leaf_count(leaf_size);

  k.copy_to(&leaf_mem[leaf_type::offset_key]);
  std::memcpy(&leaf_mem[leaf_type::offset_value_size], &value_size,
              sizeof(value_size_type));
  if (!v.empty())
    std::memcpy(&leaf_mem[leaf_type::offset_value], &v[0],
                static_cast<std::size_t>(v.size()));
  leaf_type::assert_invariants(leaf_mem);
  return basic_db_leaf_unique_ptr<Header, Db>{
      leaf_mem, basic_db_leaf_deleter<Header, Db>{db}};
}

template <class Header>
void basic_leaf<Header>::dump(std::ostream &os, const_raw_leaf_ptr leaf) {
  os << ", " << key(leaf) << ", value size: " << value_size(leaf) << '\n';
}

// Implementation of things declared in art_internal.hpp
template <class Header, class Db>
inline void basic_db_leaf_deleter<Header, Db>::operator()(
    raw_leaf_ptr to_delete) const noexcept {
  const auto leaf_size = basic_leaf<Header>::size(to_delete);
  pmr_deallocate(get_leaf_node_pool(), to_delete, leaf_size,
                 alignment_for_new<Header>());

  db.decrement_leaf_count(leaf_size);
}

template <class INode, class Db, class INodeDefs,
          template <class> class INodePoolGetter>
inline void
basic_db_inode_deleter<INode, Db, INodeDefs, INodePoolGetter>::operator()(
    INode *inode_ptr) noexcept {
  static_assert(std::is_trivially_destructible_v<INode>);

  pmr_deallocate(INodePoolGetter<INode>::get(), inode_ptr, sizeof(INode),
                 alignment_for_new<INode>());

  db.template decrement_inode_count<INode>();
}

template <class Db, template <class> class CriticalSectionPolicy, class NodePtr,
          template <class> class INodeReclamator,
          template <class, class> class LeafReclamator,
          template <class> class INodePoolGetter>
struct basic_art_policy final {
  using node_ptr = NodePtr;
  using header_type = typename NodePtr::header_type;

  using inode4_type = typename NodePtr::inode4_type;
  using inode16_type = typename NodePtr::inode16_type;
  using inode48_type = typename NodePtr::inode48_type;
  using inode256_type = typename NodePtr::inode256_type;

  using db = Db;

 private:
  template <class INode>
  using db_inode_deleter =
      basic_db_inode_deleter<INode, Db, typename NodePtr::inode_defs,
                             INodePoolGetter>;

  using leaf_reclaimable_ptr =
      std::unique_ptr<raw_leaf, LeafReclamator<header_type, Db>>;

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

  using db_inode4_reclaimable_ptr = db_inode_reclaimable_ptr<inode4_type>;
  using db_inode16_reclaimable_ptr = db_inode_reclaimable_ptr<inode16_type>;
  using db_inode48_reclaimable_ptr = db_inode_reclaimable_ptr<inode48_type>;
  using db_inode256_reclaimable_ptr = db_inode_reclaimable_ptr<inode256_type>;

  using leaf_type = basic_leaf<header_type>;
  static_assert(std::is_standard_layout_v<leaf_type>,
                "basic_leaf must be standard layout type to support aliasing"
                " through header");

  using db_leaf_unique_ptr = basic_db_leaf_unique_ptr<header_type, Db>;

  template <class INode>
  [[nodiscard]] static auto &get_inode_pool() {
    return INodePoolGetter<INode>::get();
  }

  [[nodiscard]] static auto make_db_leaf_ptr(art_key k, value_view v,
                                             Db &db_instance) {
    return ::unodb::detail::make_db_leaf_ptr<header_type, Db>(k, v,
                                                              db_instance);
  }

  [[nodiscard]] static auto reclaim_leaf_on_scope_exit(
      raw_leaf_ptr leaf_node_ptr, Db &db_instance) {
    return leaf_reclaimable_ptr{leaf_node_ptr,
                                LeafReclamator<header_type, Db>{db_instance}};
  }

  UNODB_DETAIL_DISABLE_GCC_11_WARNING("-Wmismatched-new-delete")
  template <class INode, class... Args>
  [[nodiscard]] static auto make_db_inode_unique_ptr(Db &db_instance,
                                                     Args &&...args) {
    db_inode_unique_ptr<INode> result{new INode{std::forward<Args>(args)...},
                                      db_inode_deleter<INode>{db_instance}};

    db_instance.template increment_inode_count<INode>();

    return result;
  }
  UNODB_DETAIL_RESTORE_GCC_11_WARNINGS()

  template <class INode>
  [[nodiscard]] static auto make_db_inode_unique_ptr(Db &db_instance,
                                                     INode *inode_ptr) {
    return db_inode_unique_ptr<INode>{inode_ptr,
                                      db_inode_deleter<INode>{db_instance}};
  }

 private:
  [[nodiscard]] static auto make_db_leaf_ptr(Db &db_instance,
                                             raw_leaf_ptr leaf) {
    return basic_db_leaf_unique_ptr<header_type, Db>{
        leaf, basic_db_leaf_deleter<header_type, Db>{db_instance}};
  }

  struct delete_db_node_ptr_at_scope_exit final {
    constexpr explicit delete_db_node_ptr_at_scope_exit(NodePtr node_ptr_,
                                                        Db &db_) noexcept
        : node_ptr{node_ptr_}, db{db_} {}

    ~delete_db_node_ptr_at_scope_exit() {
      switch (node_ptr.type()) {
        case node_type::LEAF: {
          const auto r{make_db_leaf_ptr(db, node_ptr.as_leaf())};
          return;
        }
        case node_type::I4: {
          const auto r{make_db_inode_unique_ptr(db, node_ptr.as_inode4())};
          return;
        }
        case node_type::I16: {
          const auto r{make_db_inode_unique_ptr(db, node_ptr.as_inode16())};
          return;
        }
        case node_type::I48: {
          const auto r{make_db_inode_unique_ptr(db, node_ptr.as_inode48())};
          return;
        }
        case node_type::I256: {
          const auto r{make_db_inode_unique_ptr(db, node_ptr.as_inode256())};
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
      case node_type::I4:
        node.as_inode4()->delete_subtree(db_instance);
        return;
      case node_type::I16:
        node.as_inode16()->delete_subtree(db_instance);
        return;
      case node_type::I48:
        node.as_inode48()->delete_subtree(db_instance);
        return;
      case node_type::I256:
        node.as_inode256()->delete_subtree(db_instance);
        return;
    }
  }

  basic_art_policy() = delete;
};

template <class NodePtr>
[[gnu::cold, gnu::noinline]] void dump_node(std::ostream &os,
                                            const NodePtr &node) {
  using local_leaf_type = basic_leaf<typename NodePtr::header_type>;

  os << "node at: " << node.raw_ptr() << ", tagged ptr = 0x" << std::hex
     << node.raw_val() << std::dec;
  if (node == nullptr) {
    os << '\n';
    return;
  }
  os << ", type = ";
  switch (node.type()) {
    case node_type::LEAF:
      os << "LEAF";
      local_leaf_type::dump(os, node.as_leaf());
      break;
    case node_type::I4:
      os << "I4";
      node.as_inode4()->dump(os);
      break;
    case node_type::I16:
      os << "I16";
      node.as_inode16()->dump(os);
      break;
    case node_type::I48:
      os << "I48";
      node.as_inode48()->dump(os);
      break;
    case node_type::I256:
      os << "I256";
      node.as_inode256()->dump(os);
      break;
  }
}

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
  using inode_type = typename node_ptr::inode;
  using db_inode4_reclaimable_ptr =
      typename ArtPolicy::db_inode4_reclaimable_ptr;
  using db_inode4_unique_ptr = typename ArtPolicy::db_inode4_unique_ptr;
  using db_inode16_reclaimable_ptr =
      typename ArtPolicy::db_inode16_reclaimable_ptr;
  using db_inode16_unique_ptr = typename ArtPolicy::db_inode16_unique_ptr;
  using db_inode48_reclaimable_ptr =
      typename ArtPolicy::db_inode48_reclaimable_ptr;
  using db_inode48_unique_ptr = typename ArtPolicy::db_inode48_unique_ptr;
  using db_inode256_reclaimable_ptr =
      typename ArtPolicy::db_inode256_reclaimable_ptr;

 private:
  using header_type = typename ArtPolicy::header_type;
  using inode4_type = typename ArtPolicy::inode4_type;
  using inode16_type = typename ArtPolicy::inode16_type;
  using inode48_type = typename ArtPolicy::inode48_type;
  using inode256_type = typename ArtPolicy::inode256_type;

  // key_prefix fields and methods
 public:
  using key_prefix_size = std::uint8_t;

 private:
  static constexpr key_prefix_size key_prefix_capacity = 7;

 public:
  using key_prefix_data =
      std::array<critical_section_policy<std::byte>, key_prefix_capacity>;

  [[nodiscard, gnu::pure]] constexpr auto get_shared_key_prefix_length(
      unodb::detail::art_key shifted_key) const noexcept {
    return shared_len(static_cast<std::uint64_t>(shifted_key), f.u64,
                      key_prefix_length());
  }

  [[nodiscard]] constexpr unsigned key_prefix_length() const noexcept {
    const auto result = f.f.key_prefix_length.load();
    assert(result <= key_prefix_capacity);
    return result;
  }

  constexpr void cut_key_prefix(unsigned cut_len) noexcept {
    assert(cut_len > 0);
    assert(cut_len <= key_prefix_length());

    f.u64 = ((f.u64 >> (cut_len * 8)) & key_bytes_mask) |
            length_to_word(key_prefix_length() - cut_len);

    assert(f.f.key_prefix_length.load() <= key_prefix_capacity);
  }

  constexpr void prepend_key_prefix(const basic_inode_impl &prefix1,
                                    std::byte prefix2) noexcept {
    assert(key_prefix_length() + prefix1.key_prefix_length() <
           key_prefix_capacity);

    const auto prefix1_bit_length = prefix1.key_prefix_length() * 8U;
    const auto prefix1_mask = (1ULL << prefix1_bit_length) - 1;
    const auto prefix3_bit_length = key_prefix_length() * 8U;
    const auto prefix3_mask = (1ULL << prefix3_bit_length) - 1;
    const auto prefix3 = f.u64 & prefix3_mask;
    const auto shifted_prefix3 = prefix3 << (prefix1_bit_length + 8U);
    const auto shifted_prefix2 = static_cast<std::uint64_t>(prefix2)
                                 << prefix1_bit_length;
    const auto masked_prefix1 = prefix1.f.u64 & prefix1_mask;

    f.u64 =
        shifted_prefix3 | shifted_prefix2 | masked_prefix1 |
        length_to_word(key_prefix_length() + prefix1.key_prefix_length() + 1);

    assert(f.f.key_prefix_length.load() <= key_prefix_capacity);
  }

  [[nodiscard]] constexpr const auto &get_key_prefix() const noexcept {
    return f.f.key_prefix;
  }

  [[gnu::cold, gnu::noinline]] void dump_key_prefix(std::ostream &os) const {
    const auto len = key_prefix_length();
    os << ", key prefix len = " << len;
    if (len > 0) {
      os << ", key prefix =";
      for (std::size_t i = 0; i < len; ++i) dump_byte(os, f.f.key_prefix[i]);
    }
  }

  // Only for unodb::detail use.
  constexpr auto get_children_count() const noexcept {
    return children_count.load();
  }

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
    assert(type != node_type::LEAF);
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

  // inode must not be allocated directly on heap, concrete subclasses will
  // define their own new and delete operators using node pools
  [[nodiscard, gnu::cold, gnu::noinline]] static void *operator new(
      std::size_t) {
    UNODB_DETAIL_CANNOT_HAPPEN();
  }

  UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-noreturn")
  [[gnu::cold, gnu::noinline]] static void operator delete(void *) {
    UNODB_DETAIL_CANNOT_HAPPEN();
  }
  UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

  constexpr basic_inode_impl(unsigned children_count_, art_key k1,
                             art_key shifted_k2, tree_depth depth) noexcept
      : f{k1, shifted_k2, depth},
        children_count{gsl::narrow_cast<std::uint8_t>(children_count_)} {}

  constexpr basic_inode_impl(unsigned children_count_, unsigned key_prefix_len,
                             const inode_type &key_prefix_source_node) noexcept
      : f{key_prefix_len, key_prefix_source_node.f},
        children_count{gsl::narrow_cast<std::uint8_t>(children_count_)} {}

  constexpr basic_inode_impl(unsigned children_count_,
                             const basic_inode_impl &other) noexcept
      : f{other.f},
        children_count{gsl::narrow_cast<std::uint8_t>(children_count_)} {}

 protected:
  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    dump_key_prefix(os);
    const auto children_count_ = this->children_count.load();
    os << ", # children = "
       << (children_count_ == 0 ? 256 : static_cast<unsigned>(children_count_));
  }

 private:
  static constexpr auto key_bytes_mask = 0x00FFFFFF'FFFFFFFFULL;

  [[nodiscard, gnu::pure]] static constexpr unsigned shared_len(
      std::uint64_t k1, std::uint64_t k2, unsigned clamp_byte_pos) noexcept {
    assert(clamp_byte_pos < 8);

    const auto diff = k1 ^ k2;
    const auto clamped = diff | (1ULL << (clamp_byte_pos * 8U));
    return static_cast<unsigned>(__builtin_ctzl(clamped)) >> 3U;
  }

  [[nodiscard, gnu::pure]] static constexpr std::uint64_t length_to_word(
      unsigned length) {
    assert(length <= key_prefix_capacity);
    return static_cast<std::uint64_t>(length) << 56U;
  }

  union key_prefix_union {
    struct inode_fields {
      key_prefix_data key_prefix;
      critical_section_policy<key_prefix_size> key_prefix_length;
    } f;
    critical_section_policy<std::uint64_t> u64;

    key_prefix_union(art_key k1, art_key shifted_k2, tree_depth depth) noexcept
        : u64{make_u64(k1, shifted_k2, depth)} {}

    key_prefix_union(unsigned key_prefix_len,
                     const key_prefix_union &source_key_prefix) noexcept
        : u64{(source_key_prefix.u64 & key_bytes_mask) |
              length_to_word(key_prefix_len)} {
      assert(key_prefix_len <= key_prefix_capacity);
    }

    key_prefix_union(const key_prefix_union &other) noexcept
        : u64{other.u64.load()} {}

    ~key_prefix_union() noexcept = default;

    key_prefix_union(key_prefix_union &&) = delete;
    key_prefix_union &operator=(const key_prefix_union &) = delete;
    key_prefix_union &operator=(key_prefix_union &&) = delete;

   private:
    static void static_asserts() noexcept {
      static_assert(offsetof(inode_fields, key_prefix_data) == 0);
      static_assert(offsetof(inode_fields, key_prefix_length) == 7);
      static_assert(offsetof(inode_fields, children_count) == 8);
      static_assert(sizeof(inode_fields) == 9);
    }

    static std::uint64_t make_u64(art_key k1, art_key shifted_k2,
                                  tree_depth depth) noexcept {
      k1.shift_right(depth);

      const auto k1_u64 = static_cast<std::uint64_t>(k1) & key_bytes_mask;

      return k1_u64 | length_to_word(shared_len(
                          k1_u64, static_cast<std::uint64_t>(shifted_k2),
                          key_prefix_capacity));
    }
  } f;

  critical_section_policy<std::uint8_t> children_count;

 protected:
  using leaf_type = basic_leaf<header_type>;

  friend class inode_4;
  friend class inode_16;
  friend class inode_48;
  friend class inode_256;
  friend class unodb::db;

  friend class olc_inode_4;
  friend class olc_inode_16;
  friend class olc_inode_48;
  friend class olc_inode_256;
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
class basic_inode : public basic_inode_impl<ArtPolicy> {
  static_assert(NodeType != node_type::LEAF);
  static_assert(!std::is_same_v<Derived, LargerDerived>);
  static_assert(!std::is_same_v<SmallerDerived, Derived>);
  static_assert(!std::is_same_v<SmallerDerived, LargerDerived>);
  static_assert(MinSize < Capacity);

 public:
  using typename basic_inode_impl<ArtPolicy>::db_leaf_unique_ptr;
  using typename basic_inode_impl<ArtPolicy>::db;
  using typename basic_inode_impl<ArtPolicy>::node_ptr;

  using db_smaller_derived_reclaimable_ptr =
      typename ArtPolicy::template db_inode_reclaimable_ptr<SmallerDerived>;

  using db_larger_derived_reclaimable_ptr =
      typename ArtPolicy::template db_inode_reclaimable_ptr<LargerDerived>;

  [[nodiscard]] static constexpr auto create(
      db_larger_derived_reclaimable_ptr &&source_node,
      std::uint8_t child_to_delete) {
    return ArtPolicy::template make_db_inode_unique_ptr<Derived>(
        source_node.get_deleter().get_db(), std::move(source_node),
        child_to_delete);
  }

  [[nodiscard]] static constexpr auto create(
      db_smaller_derived_reclaimable_ptr &&source_node,
      db_leaf_unique_ptr &&child, tree_depth depth) {
    return ArtPolicy::template make_db_inode_unique_ptr<Derived>(
        source_node.get_deleter().get_db(), std::move(source_node),
        std::move(child), depth);
  }

  [[nodiscard]] static void *operator new(std::size_t size) {
    assert(size == sizeof(Derived));

    return pmr_allocate(ArtPolicy::template get_inode_pool<Derived>(), size,
                        alignment_for_new<Derived>());
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
    assert(is_min_size());
  }

  constexpr basic_inode(unsigned key_prefix_len,
                        const inode_type &key_prefix_source_node) noexcept
      : basic_inode_impl<ArtPolicy>{MinSize, key_prefix_len,
                                    key_prefix_source_node} {
    assert(is_min_size());
  }

  explicit constexpr basic_inode(const SmallerDerived &source_node) noexcept
      : basic_inode_impl<ArtPolicy>{MinSize, source_node} {
    assert(source_node.is_full_for_add());
    assert(is_min_size());
  }

  explicit constexpr basic_inode(const LargerDerived &source_node) noexcept
      : basic_inode_impl<ArtPolicy>{Capacity, source_node} {
    assert(source_node.is_min_size());
    assert(is_full_for_add());
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
  using typename parent_class::leaf_type;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_inode16_reclaimable_ptr;
  using typename parent_class::db_inode4_unique_ptr;
  using typename parent_class::db_leaf_unique_ptr;
  using typename parent_class::find_result;
  using typename parent_class::larger_derived_type;
  using typename parent_class::node_ptr;

  // Create a new node with two given child leaves
  [[nodiscard]] static constexpr auto create(art_key k1, art_key shifted_k2,
                                             tree_depth depth,
                                             raw_leaf_ptr child1,
                                             db_leaf_unique_ptr &&child2) {
    return ArtPolicy::template make_db_inode_unique_ptr<inode4_type>(
        child2.get_deleter().get_db(), k1, shifted_k2, depth, child1,
        std::move(child2));
  }

  using parent_class::create;

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused this
  // key prefix split.
  [[nodiscard]] static constexpr auto create(node_ptr source_node, unsigned len,
                                             tree_depth depth,
                                             db_leaf_unique_ptr &&child1) {
    return ArtPolicy::template make_db_inode_unique_ptr<inode4_type>(
        child1.get_deleter().get_db(), source_node, len, depth,
        std::move(child1));
  }

  constexpr basic_inode_4(art_key k1, art_key shifted_k2, tree_depth depth,
                          raw_leaf_ptr child1,
                          db_leaf_unique_ptr &&child2) noexcept
      : parent_class{k1, shifted_k2, depth} {
    const auto k2_next_byte_depth = this->key_prefix_length();
    const auto k1_next_byte_depth = k2_next_byte_depth + depth;
    add_two_to_empty(k1[k1_next_byte_depth], node_ptr{child1},
                     shifted_k2[k2_next_byte_depth], std::move(child2));
  }

  constexpr basic_inode_4(node_ptr source_node, unsigned len, tree_depth depth,
                          db_leaf_unique_ptr &&child1) noexcept
      : parent_class{len, *source_node.as_inode()} {
    auto *const source_inode = source_node.as_inode();
    assert(len < source_inode->key_prefix_length());
    assert(depth + len < art_key::size);

    const auto source_node_key_byte =
        source_inode->get_key_prefix()[len].load();
    source_inode->cut_key_prefix(len + 1);
    const auto new_key_byte = leaf_type::key(child1.get())[depth + len];
    add_two_to_empty(source_node_key_byte, source_node, new_key_byte,
                     std::move(child1));
  }

  constexpr basic_inode_4(db_inode16_reclaimable_ptr source_node,
                          std::uint8_t child_to_delete)
      : parent_class{*source_node} {
    const auto *source_keys_itr = source_node->keys.byte_array.cbegin();
    auto *keys_itr = keys.byte_array.begin();
    const auto *source_children_itr = source_node->children.cbegin();
    auto *children_itr = children.begin();

    while (source_keys_itr !=
           source_node->keys.byte_array.cbegin() + child_to_delete) {
      *keys_itr++ = *source_keys_itr++;
      *children_itr++ = *source_children_itr++;
    }

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        source_children_itr->load().as_leaf(),
        source_node.get_deleter().get_db())};

    ++source_keys_itr;
    ++source_children_itr;

    while (source_keys_itr !=
           source_node->keys.byte_array.cbegin() + inode16_type::min_size) {
      *keys_itr++ = *source_keys_itr++;
      *children_itr++ = *source_children_itr++;
    }

    assert(this->children_count == basic_inode_4::capacity);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + basic_inode_4::capacity));
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    assert(children_count_ == this->children_count);
    assert(children_count_ < parent_class::capacity);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));

    const auto key_byte =
        static_cast<std::uint8_t>(leaf_type::key(child.get())[depth]);

#if __x86_64
    const auto mask = (1U << children_count_) - 1;
    const auto insert_pos_index = get_insert_pos(key_byte, mask);
#else
    const auto first_lt = ((keys.integer & 0xFFU) < key_byte) ? 1 : 0;
    const auto second_lt = (((keys.integer >> 8U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto third_lt = ((keys.integer >> 16U) & 0xFFU) < key_byte ? 1 : 0;
    const auto insert_pos_index =
        static_cast<unsigned>(first_lt + second_lt + third_lt);
#endif

    for (typename decltype(keys.byte_array)::size_type i = children_count_;
         i > insert_pos_index; --i) {
      keys.byte_array[i] = keys.byte_array[i - 1];
      // TODO(laurynas): Node4 children fit into a single YMM register on AVX
      // onwards, see if it is possible to do shift/insert with it. Checked
      // plain AVX, it seems that at least AVX2 is required.
      children[i] = children[i - 1];
    }
    keys.byte_array[insert_pos_index] = static_cast<std::byte>(key_byte);
    children[insert_pos_index] = node_ptr{child.release()};

    ++children_count_;
    this->children_count = children_count_;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    auto children_count_ = this->children_count.load();

    assert(child_index < children_count_);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_index].load().as_leaf(), db_instance)};

    typename decltype(keys.byte_array)::size_type i = child_index;
    for (; i < static_cast<unsigned>(children_count_ - 1); ++i) {
      // TODO(laurynas): see the AVX2 TODO at add method
      keys.byte_array[i] = keys.byte_array[i + 1];
      children[i] = children[i + 1];
    }
#ifndef __x86_64
    keys.byte_array[i] = empty_child;
#endif

    --children_count_;
    this->children_count = children_count_;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));
  }

  constexpr auto leave_last_child(std::uint8_t child_to_delete,
                                  db &db_instance) noexcept {
    assert(this->is_min_size());
    assert(child_to_delete == 0 || child_to_delete == 1);

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_to_delete].load().as_leaf(), db_instance)};

    const std::uint8_t child_to_leave = (child_to_delete == 0) ? 1 : 0;
    const auto child_to_leave_ptr = children[child_to_leave].load();
    if (child_to_leave_ptr.type() != node_type::LEAF) {
      child_to_leave_ptr.as_inode()->prepend_key_prefix(
          *this, keys.byte_array[child_to_leave]);
    }
    return child_to_leave_ptr;
  }

  [[nodiscard, gnu::pure]] find_result find_child(std::byte key_byte) noexcept {
#ifdef __x86_64
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
      const auto i = static_cast<unsigned>(__builtin_ctz(bit_field));
      return std::make_pair(
          i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
    }
    return std::make_pair(0xFF, nullptr);
#else   // #ifdef __x86_64
    // Bit twiddling:
    // contains_byte:     __builtin_ffs:   for key index:
    //    0x80000000               0x20                3
    //      0x800000               0x18                2
    //      0x808000               0x10                1
    //          0x80                0x8                0
    //           0x0                0x0        not found
    const auto result =
        static_cast<decltype(keys.byte_array.load())::size_type>(
            // __builtin_ffs takes signed argument:
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            __builtin_ffs(static_cast<std::int32_t>(
                contains_byte(keys.integer, key_byte))) >>
            3);

    if ((result == 0) || (result > this->children_count.load()))
      return std::make_pair(0xFF, nullptr);

    return std::make_pair(result - 1,
                          static_cast<critical_section_policy<node_ptr> *>(
                              &children[result - 1]));
#endif  // #ifdef __x86_64
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i) {
      ArtPolicy::delete_subtree(children[i], db_instance);
    }
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    parent_class::dump(os);
    const auto children_count_ = this->children_count.load();
    os << ", key bytes =";
    for (std::uint8_t i = 0; i < children_count_; i++)
      dump_byte(os, keys.byte_array[i]);
    os << ", children:\n";
    for (std::uint8_t i = 0; i < children_count_; i++)
      dump_node(os, children[i].load());
  }

 protected:
  constexpr void add_two_to_empty(std::byte key1, node_ptr child1,
                                  std::byte key2,
                                  db_leaf_unique_ptr child2) noexcept {
    assert(key1 != key2);
    assert(this->children_count == 2);

    const std::uint8_t key1_i = key1 < key2 ? 0 : 1;
    const std::uint8_t key2_i = key1_i == 0 ? 1 : 0;
    keys.byte_array[key1_i] = key1;
    children[key1_i] = child1;
    keys.byte_array[key2_i] = key2;
    children[key2_i] = node_ptr{child2.release()};
#ifndef __x86_64
    keys.byte_array[2] = empty_child;
    keys.byte_array[3] = empty_child;
#endif

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + this->children_count));
  }

  union {
    std::array<critical_section_policy<std::byte>, basic_inode_4::capacity>
        byte_array;
    critical_section_policy<std::uint32_t> integer;
  } keys;

  std::array<critical_section_policy<node_ptr>, basic_inode_4::capacity>
      children;

 private:
#ifdef __x86_64
  auto get_insert_pos(std::uint8_t insert_key_byte,
                      unsigned node_key_mask) const noexcept {
    assert(node_key_mask == (1U << this->children_count.load()) - 1);

    const auto replicated_insert_key_byte =
        _mm_set1_epi8(static_cast<char>(insert_key_byte));
    const auto node_keys_in_sse_reg =
        _mm_cvtsi32_si128(static_cast<std::int32_t>(keys.integer.load()));
    // Since the existing and insert key values cannot be equal, it's OK to use
    // "<=" comparison as "<".
    const auto lt_node_key_positions =
        _mm_cmple_epu8(node_keys_in_sse_reg, replicated_insert_key_byte);
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(lt_node_key_positions)) &
        node_key_mask;
    return static_cast<unsigned>(__builtin_popcount(bit_field));
  }
#else
  // Non-x86_64 implementation reads children bytes past current node size
  static constexpr std::byte empty_child{0xFF};
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
  using typename parent_class::db_inode48_reclaimable_ptr;
  using typename parent_class::db_inode4_reclaimable_ptr;
  using typename parent_class::db_leaf_unique_ptr;
  using typename parent_class::find_result;

  constexpr basic_inode_16(db_inode4_reclaimable_ptr source_node,
                           db_leaf_unique_ptr child, tree_depth depth) noexcept
      : parent_class{*source_node} {
    const auto key_byte =
        static_cast<std::uint8_t>(leaf_type::key(child.get())[depth]);

#if __x86_64
    const auto insert_pos_index = source_node->get_insert_pos(key_byte, 0xFU);
#else
    const auto keys_integer = source_node->keys.integer.load();
    const auto first_lt = ((keys_integer & 0xFFU) < key_byte) ? 1 : 0;
    const auto second_lt = (((keys_integer >> 8U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto third_lt = (((keys_integer >> 16U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto fourth_lt = (((keys_integer >> 24U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto insert_pos_index =
        static_cast<unsigned>(first_lt + second_lt + third_lt + fourth_lt);
#endif

    unsigned i = 0;
    for (; i < insert_pos_index; ++i) {
      keys.byte_array[i] = source_node->keys.byte_array[i];
      children[i] = source_node->children[i];
    }

    keys.byte_array[i] = static_cast<std::byte>(key_byte);
    children[i] = node_ptr{child.release()};
    ++i;

    for (; i <= inode4_type::capacity; ++i) {
      keys.byte_array[i] = source_node->keys.byte_array[i - 1];
      children[i] = source_node->children[i - 1];
    }
  }

  constexpr basic_inode_16(db_inode48_reclaimable_ptr source_node,
                           std::uint8_t child_to_delete) noexcept
      : parent_class{*source_node} {
    source_node->remove_child_pointer(child_to_delete,
                                      source_node.get_deleter().get_db());
    source_node->child_indexes[child_to_delete] = inode48_type::empty_child;

    // TODO(laurynas): consider AVX512 gather?
    unsigned next_child = 0;
    unsigned i = 0;
    while (true) {
      const auto source_child_i = source_node->child_indexes[i].load();
      if (source_child_i != inode48_type::empty_child) {
        keys.byte_array[next_child] = gsl::narrow_cast<std::byte>(i);
        const auto source_child_ptr =
            source_node->children.pointer_array[source_child_i].load();
        assert(source_child_ptr != nullptr);
        children[next_child] = source_child_ptr;
        ++next_child;
        if (next_child == basic_inode_16::capacity) break;
      }
      assert(i < 255);
      ++i;
    }

    assert(this->children_count == basic_inode_16::capacity);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + basic_inode_16::capacity));
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    assert(children_count_ == this->children_count);
    assert(children_count_ < parent_class::capacity);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));

    const auto key_byte = leaf_type::key(child.get())[depth];

    const auto insert_pos_index =
        get_sorted_key_array_insert_position(key_byte);
    if (insert_pos_index != children_count_) {
      assert(keys.byte_array[insert_pos_index] != key_byte);
      std::copy_backward(keys.byte_array.cbegin() + insert_pos_index,
                         keys.byte_array.cbegin() + children_count_,
                         keys.byte_array.begin() + children_count_ + 1);
      std::copy_backward(children.begin() + insert_pos_index,
                         children.begin() + children_count_,
                         children.begin() + children_count_ + 1);
    }
    keys.byte_array[insert_pos_index] = key_byte;
    children[insert_pos_index] = node_ptr{child.release()};
    ++children_count_;
    this->children_count = children_count_;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    auto children_count_ = this->children_count.load();
    assert(child_index < children_count_);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_index].load().as_leaf(), db_instance)};

    for (unsigned i = child_index + 1; i < children_count_; ++i) {
      keys.byte_array[i - 1] = keys.byte_array[i];
      children[i - 1] = children[i];
    }

    --children_count_;
    this->children_count = children_count_;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));
  }

  [[nodiscard, gnu::pure]] constexpr find_result find_child(
      std::byte key_byte) noexcept {
#ifdef __x86_64
    const auto replicated_search_key =
        _mm_set1_epi8(static_cast<char>(key_byte));
    const auto matching_key_positions =
        _mm_cmpeq_epi8(replicated_search_key, keys.sse);
    const auto mask = (1U << this->children_count) - 1;
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
    if (bit_field != 0) {
      const auto i = static_cast<unsigned>(__builtin_ctz(bit_field));
      return std::make_pair(
          i, static_cast<critical_section_policy<node_ptr> *>(&children[i]));
    }
    return std::make_pair(0xFF, nullptr);
#else
#error Needs porting
#endif
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
    const auto children_count_ = this->children_count.load();
    for (std::uint8_t i = 0; i < children_count_; ++i)
      ArtPolicy::delete_subtree(children[i], db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    parent_class::dump(os);
    const auto children_count_ = this->children_count.load();
    os << ", key bytes =";
    for (std::uint8_t i = 0; i < children_count_; ++i)
      dump_byte(os, keys.byte_array[i]);
    os << ", children:\n";
    for (std::uint8_t i = 0; i < children_count_; ++i)
      dump_node(os, children[i].load());
  }

 private:
  [[nodiscard, gnu::pure]] constexpr auto get_sorted_key_array_insert_position(
      std::byte key_byte) noexcept {
    const auto children_count_ = this->children_count.load();

    assert(children_count_ < basic_inode_16::capacity);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + children_count_));
    assert(std::adjacent_find(keys.byte_array.cbegin(),
                              keys.byte_array.cbegin() + children_count_) >=
           keys.byte_array.cbegin() + children_count_);

#ifdef __x86_64
    const auto replicated_insert_key =
        _mm_set1_epi8(static_cast<char>(key_byte));
    const auto lesser_key_positions =
        _mm_cmple_epu8(replicated_insert_key, keys.sse);
    const auto mask = (1U << children_count_) - 1;
    const auto bit_field =
        static_cast<unsigned>(_mm_movemask_epi8(lesser_key_positions)) & mask;
    const auto result = static_cast<std::uint8_t>(
        (bit_field != 0) ? __builtin_ctz(bit_field) : children_count_);
#else
    const auto result = static_cast<std::uint8_t>(
        std::lower_bound(keys.byte_array.cbegin(),
                         keys.byte_array.cbegin() + children_count_, key_byte) -
        keys.byte_array.cbegin());
#endif

    assert(result == children_count_ || keys.byte_array[result] != key_byte);
    return result;
  }

 protected:
  union {
    std::array<critical_section_policy<std::byte>, basic_inode_16::capacity>
        byte_array;
    __m128i sse;
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
  using typename parent_class::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_inode16_reclaimable_ptr;
  using typename parent_class::db_inode256_reclaimable_ptr;
  using typename parent_class::db_leaf_unique_ptr;

  constexpr basic_inode_48(db_inode16_reclaimable_ptr source_node,
                           db_leaf_unique_ptr child, tree_depth depth) noexcept
      : parent_class{*source_node} {
    auto *const __restrict__ source_node_ptr = source_node.get();
    auto *const __restrict__ child_ptr = child.release();

    // TODO(laurynas): consider AVX512 scatter?
    std::uint8_t i = 0;
    for (; i < inode16_type::capacity; ++i) {
      const auto existing_key_byte = source_node_ptr->keys.byte_array[i].load();
      child_indexes[static_cast<std::uint8_t>(existing_key_byte)] = i;
    }
    for (i = 0; i < inode16_type::capacity; ++i) {
      children.pointer_array[i] = source_node_ptr->children[i];
    }

    const auto key_byte = static_cast<std::uint8_t>(
        basic_inode_48::leaf_type::key(child_ptr)[depth]);
    assert(child_indexes[key_byte] == empty_child);
    child_indexes[key_byte] = i;
    children.pointer_array[i] = node_ptr{child_ptr};
    for (i = this->children_count; i < basic_inode_48::capacity; i++) {
      children.pointer_array[i] = node_ptr{nullptr};
    }
  }

  constexpr basic_inode_48(db_inode256_reclaimable_ptr source_node,
                           std::uint8_t child_to_delete) noexcept
      : parent_class{*source_node} {
    auto *const __restrict__ source_node_ptr = source_node.get();

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        source_node_ptr->children[child_to_delete].load().as_leaf(),
        source_node.get_deleter().get_db())};

    source_node_ptr->children[child_to_delete] = node_ptr{nullptr};

    std::uint8_t next_child = 0;
    for (unsigned child_i = 0; child_i < 256; child_i++) {
      const auto child_ptr = source_node_ptr->children[child_i].load();
      if (child_ptr == nullptr) continue;

      child_indexes[child_i] = next_child;
      children.pointer_array[next_child] =
          source_node_ptr->children[child_i].load();
      ++next_child;

      if (next_child == basic_inode_48::capacity) break;
    }

    assert(this->children_count == basic_inode_48::capacity);
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    assert(this->children_count == children_count_);
    assert(children_count_ < parent_class::capacity);

    const auto key_byte = static_cast<uint8_t>(
        basic_inode_48::leaf_type::key(child.get())[depth]);
    assert(child_indexes[key_byte] == empty_child);
    unsigned i{0};
#ifdef __x86_64
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
        i = (i << 1U) +
            ((static_cast<unsigned>(__builtin_ctzl(cmp_mask)) + 1) >> 1U);
        break;
      }
      i += 4;
    }
#else   // #ifdef __x86_64
    node_ptr child_ptr;
    while (true) {
      child_ptr = children.pointer_array[i];
      if (child_ptr == nullptr) break;
      assert(i < 255);
      ++i;
    }
#endif  // #ifdef __x86_64
    assert(children.pointer_array[i] == nullptr);
    child_indexes[key_byte] = gsl::narrow_cast<std::uint8_t>(i);
    children.pointer_array[i] = node_ptr{child.release()};
    this->children_count = children_count_ + 1;
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    remove_child_pointer(child_index, db_instance);
    children.pointer_array[child_indexes[child_index]] = node_ptr{nullptr};
    child_indexes[child_index] = empty_child;
    --this->children_count;
  }

  [[nodiscard]] constexpr typename basic_inode_48::find_result find_child(
      std::byte key_byte) noexcept {
    if (child_indexes[static_cast<std::uint8_t>(key_byte)] != empty_child) {
      const auto child_i =
          child_indexes[static_cast<std::uint8_t>(key_byte)].load();
      return std::make_pair(static_cast<std::uint8_t>(key_byte),
                            &children.pointer_array[child_i]);
    }
    return std::make_pair(0xFF, nullptr);
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
        assert(actual_children_count <= children_count_);
#endif
      }
    }
    assert(actual_children_count == children_count_);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
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
        assert(children.pointer_array[child_indexes[i]] != nullptr);
        dump_node(os, children.pointer_array[child_indexes[i]].load());
#ifndef NDEBUG
        ++actual_children_count;
        assert(actual_children_count <= children_count_);
#endif
      }

    assert(actual_children_count == children_count_);
  }

 private:
  constexpr void remove_child_pointer(std::uint8_t child_index,
                                      db &db_instance) noexcept {
    direct_remove_child_pointer(child_indexes[child_index], db_instance);
  }

  constexpr void direct_remove_child_pointer(std::uint8_t children_i,
                                             db &db_instance) noexcept {
    assert(children_i != empty_child);

    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children.pointer_array[children_i].load().as_leaf(), db_instance)};
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
#ifdef __x86_64
    static_assert(basic_inode_48::capacity % 2 == 0);
    static_assert((basic_inode_48::capacity / 2) % 4 == 0,
                  "Node48 capacity must support unrolling without remainder");
    // No std::array below because it would ignore the alignment attribute
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    __m128i
        pointer_vector[basic_inode_48::capacity / 2];  // NOLINT(runtime/arrays)
#endif
    children_union() {}
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
  using typename parent_class::node_ptr;

  template <typename T>
  using critical_section_policy =
      typename ArtPolicy::template critical_section_policy<T>;

 public:
  using typename parent_class::db;
  using typename parent_class::db_inode48_reclaimable_ptr;
  using typename parent_class::db_leaf_unique_ptr;

  constexpr basic_inode_256(db_inode48_reclaimable_ptr source_node,
                            db_leaf_unique_ptr child, tree_depth depth) noexcept
      : parent_class{*source_node} {
    unsigned children_copied = 0;
    unsigned i = 0;
    while (true) {
      const auto children_i = source_node->child_indexes[i].load();
      if (children_i == inode48_type::empty_child) {
        children[i] = node_ptr{nullptr};
      } else {
        children[i] = source_node->children.pointer_array[children_i].load();
        ++children_copied;
        if (children_copied == inode48_type::capacity) break;
      }
      ++i;
    }

    ++i;
    for (; i < basic_inode_256::capacity; ++i) children[i] = node_ptr{nullptr};

    const auto key_byte = static_cast<uint8_t>(
        basic_inode_256::leaf_type::key(child.get())[depth]);
    assert(children[key_byte] == nullptr);
    children[key_byte] = node_ptr{child.release()};
  }

  constexpr void add_to_nonfull(db_leaf_unique_ptr &&child, tree_depth depth,
                                std::uint8_t children_count_) noexcept {
    assert(this->children_count == children_count_);
    assert(children_count_ < parent_class::capacity);

    const auto key_byte = static_cast<std::uint8_t>(
        basic_inode_256::leaf_type::key(child.get())[depth]);
    assert(children[key_byte] == nullptr);
    children[key_byte] = node_ptr{child.release()};
    this->children_count = children_count_ + 1;
  }

  constexpr void remove(std::uint8_t child_index, db &db_instance) noexcept {
    const auto r{ArtPolicy::reclaim_leaf_on_scope_exit(
        children[child_index].load().as_leaf(), db_instance)};

    children[child_index] = node_ptr{nullptr};
    --this->children_count;
  }

  [[nodiscard]] constexpr typename basic_inode_256::find_result find_child(
      std::byte key_byte) noexcept {
    const auto key_int_byte = static_cast<std::uint8_t>(key_byte);
    if (children[key_int_byte] != nullptr)
      return std::make_pair(key_int_byte, &children[key_int_byte]);
    return std::make_pair(0xFF, nullptr);
  }

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
        assert(actual_children_count <= children_count_ ||
               children_count_ == 0);
#endif
      }
    }
    assert(actual_children_count == children_count_);
  }

  constexpr void delete_subtree(db &db_instance) noexcept {
    for_each_child([&db_instance](unsigned, node_ptr child) noexcept {
      ArtPolicy::delete_subtree(child, db_instance);
    });
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    parent_class::dump(os);
    os << ", key bytes & children:\n";
    for_each_child([&](unsigned i, node_ptr child) noexcept {
      os << ' ';
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ' ';
      dump_node(os, child);
    });
  }

 private:
  std::array<critical_section_policy<node_ptr>, basic_inode_256::capacity>
      children;

  template <class>
  friend class basic_inode_48;
};

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_ART_INTERNAL_IMPL_HPP
