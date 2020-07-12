// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include "art.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#ifdef __x86_64
#include <emmintrin.h>
#endif
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <gsl/gsl_util>

#include "heap.hpp"

// ART implementation properties that we can enforce at compile time
static_assert(std::is_trivially_copyable_v<unodb::detail::art_key>);
static_assert(sizeof(unodb::detail::art_key) == sizeof(unodb::key));

static_assert(sizeof(unodb::detail::raw_leaf_ptr) ==
              // NOLINTNEXTLINE(bugprone-sizeof-expression)
              sizeof(unodb::detail::node_ptr::header));
static_assert(sizeof(unodb::detail::node_ptr) == sizeof(void *));

namespace {

template <class InternalNode>
[[nodiscard]] inline unodb::detail::pmr_pool_options
get_inode_pool_options() noexcept;

[[nodiscard]] inline auto &get_leaf_node_pool() noexcept {
  return *unodb::detail::pmr_new_delete_resource();
}

// For internal node pools, approximate requesting ~2MB blocks from backing
// storage (when ported to Linux, ask for 2MB huge pages directly)
template <class InternalNode>
[[nodiscard]] inline unodb::detail::pmr_pool_options
get_inode_pool_options() noexcept {
  unodb::detail::pmr_pool_options inode_pool_options;
  inode_pool_options.max_blocks_per_chunk =
      2 * 1024 * 1024 / sizeof(InternalNode);
  inode_pool_options.largest_required_pool_block = sizeof(InternalNode);
  return inode_pool_options;
}

template <class InternalNode>
[[nodiscard]] inline auto &get_inode_pool() {
  static unodb::detail::pmr_unsynchronized_pool_resource inode_pool{
      get_inode_pool_options<InternalNode>()};
  return inode_pool;
}

__attribute__((cold, noinline)) void dump_byte(std::ostream &os,
                                               std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

__attribute__((cold, noinline)) void dump_key(std::ostream &os,
                                              unodb::detail::art_key key) {
  for (std::size_t i = 0; i < sizeof(key); i++) dump_byte(os, key[i]);
}

__attribute__((cold, noinline)) void dump_node(
    std::ostream &os, const unodb::detail::node_ptr &node);

inline __attribute__((noreturn)) void cannot_happen() {
  assert(0);
  __builtin_unreachable();
}

// On GCC up to and including version 10, __builtin_ffs compiles to BSF/CMOVE
// pair if TZCNT is not available. CMOVE is only required if arg is zero, which
// we know not to be. Only GCC 11 gets the hint by "if (arg == 0)
// __builtin_unreachable()"
__attribute__((const)) unsigned ffs_nonzero(std::uint64_t arg) {
  std::int64_t result;
#if defined(__x86_64)
  __asm__("bsfq %1, %0" : "=r"(result) : "rm"(arg) : "cc");
  return gsl::narrow_cast<unsigned>(result + 1);
#else
  return static_cast<unsigned>(__builtin_ffsl(static_cast<std::int64_t>(arg)));
#endif
}

// A class used as a sentinel for basic_inode template args: the
// larger node type for the largest node type and the smaller node type for
// the smallest node type.
class fake_inode {};

}  // namespace

namespace unodb::detail {

template <>
__attribute__((const)) std::uint64_t
basic_art_key<std::uint64_t>::make_binary_comparable(std::uint64_t k) noexcept {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(k);
#else
#error Needs implementing
#endif
}

enum class node_type : std::uint8_t { LEAF, I4, I16, I48, I256 };

// A common prefix shared by all node types
struct node_header final {
  explicit node_header(node_type type_) : m_type{type_} {}

  [[nodiscard]] auto type() const noexcept { return m_type; }

 private:
  const node_type m_type;
};

static_assert(std::is_standard_layout_v<node_header>);

node_type node_ptr::type() const noexcept { return header->type(); }

// leaf_deleter and leaf_unique_ptr could be in anonymous namespace but then
// cyclical dependency with struct leaf happens
struct leaf_deleter {
  void operator()(unodb::detail::raw_leaf_ptr to_delete) const noexcept;
};

using leaf_unique_ptr = std::unique_ptr<unodb::detail::raw_leaf, leaf_deleter>;

static_assert(sizeof(leaf_unique_ptr) == sizeof(void *),
              "Single leaf unique_ptr must have no overhead over raw pointer");

// Helper struct for leaf node-related data and (static) code. We
// don't use a regular class because leaf nodes are of variable size, C++ does
// not support flexible array members, and we want to save one level of
// (heap) indirection.
struct leaf final {
 private:
  using value_size_type = std::uint32_t;

  static constexpr auto offset_header = 0;
  static constexpr auto offset_key = sizeof(node_header);
  static constexpr auto offset_value_size = offset_key + sizeof(art_key);

  static constexpr auto offset_value =
      offset_value_size + sizeof(value_size_type);

  static constexpr auto minimum_size = offset_value;

  [[nodiscard]] static auto value_size(raw_leaf_ptr leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);

    value_size_type result;
    std::memcpy(&result, &leaf[offset_value_size], sizeof(result));
    return result;
  }

 public:
  [[nodiscard]] static leaf_unique_ptr create(art_key k, value_view v,
                                              db &db_instance);

  [[nodiscard]] static auto key(raw_leaf_ptr leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);

    return art_key::create(&leaf[offset_key]);
  }

  [[nodiscard]] static auto matches(raw_leaf_ptr leaf, art_key k) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);

    return k == leaf + offset_key;
  }

  [[nodiscard]] static std::size_t size(raw_leaf_ptr leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);

    return value_size(leaf) + offset_value;
  }

  [[nodiscard]] static auto value(raw_leaf_ptr leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);

    return value_view{&leaf[offset_value], value_size(leaf)};
  }

  __attribute__((cold, noinline)) static void dump(std::ostream &os,
                                                   raw_leaf_ptr leaf);
};

static_assert(std::is_standard_layout_v<leaf>,
              "leaf must be standard layout type to support aliasing through "
              "node_header");

leaf_unique_ptr leaf::create(art_key k, value_view v, db &db_instance) {
  if (unlikely(v.size() > std::numeric_limits<value_size_type>::max())) {
    throw std::length_error("Value length must fit in uint32_t");
  }
  const auto value_size = static_cast<value_size_type>(v.size());
  const auto leaf_size = static_cast<std::size_t>(offset_value) + value_size;
  db_instance.increase_memory_use(leaf_size);
  ++db_instance.leaf_count;

  auto *const leaf_mem = static_cast<std::byte *>(pmr_allocate(
      get_leaf_node_pool(), leaf_size, alignment_for_new<node_header>()));
  new (leaf_mem) node_header{node_type::LEAF};
  k.copy_to(&leaf_mem[offset_key]);
  std::memcpy(&leaf_mem[offset_value_size], &value_size,
              sizeof(value_size_type));
  if (!v.empty())
    std::memcpy(&leaf_mem[offset_value], &v[0],
                static_cast<std::size_t>(v.size()));
  return leaf_unique_ptr{leaf_mem};
}

void leaf::dump(std::ostream &os, raw_leaf_ptr leaf) {
  os << "LEAF: key:";
  dump_key(os, key(leaf));
  os << ", value size: " << value_size(leaf) << '\n';
}

void leaf_deleter::operator()(
    unodb::detail::raw_leaf_ptr to_delete) const noexcept {
  const auto s = unodb::detail::leaf::size(to_delete);

  pmr_deallocate(get_leaf_node_pool(), to_delete, s,
                 alignment_for_new<node_header>());
}

}  // namespace unodb::detail

namespace {

union delete_node_ptr_at_scope_exit {
  const unodb::detail::node_header *header;
  const unodb::detail::leaf_unique_ptr leaf;
  const std::unique_ptr<unodb::detail::inode> internal;
  const std::unique_ptr<unodb::detail::inode_4> node_4;
  const std::unique_ptr<unodb::detail::inode_16> node_16;
  const std::unique_ptr<unodb::detail::inode_48> node_48;
  const std::unique_ptr<unodb::detail::inode_256> node_256;

  explicit delete_node_ptr_at_scope_exit(
      unodb::detail::node_ptr node_ptr_) noexcept
      : header(node_ptr_.header) {}

  ~delete_node_ptr_at_scope_exit() {
    if (header == nullptr) return;

    // While all the unique_ptr union fields look the same in memory, we must
    // invoke destructor of the right type, because the deleters are different
    switch (header->type()) {
      case unodb::detail::node_type::LEAF:
        using unodb::detail::leaf_unique_ptr;
        leaf.~leaf_unique_ptr();
        return;
      case unodb::detail::node_type::I4:
        node_4.~unique_ptr<unodb::detail::inode_4>();
        return;
      case unodb::detail::node_type::I16:
        node_16.~unique_ptr<unodb::detail::inode_16>();
        return;
      case unodb::detail::node_type::I48:
        node_48.~unique_ptr<unodb::detail::inode_48>();
        return;
      case unodb::detail::node_type::I256:
        node_256.~unique_ptr<unodb::detail::inode_256>();
        return;
    }
    cannot_happen();
  }

  delete_node_ptr_at_scope_exit(const delete_node_ptr_at_scope_exit &) = delete;
  delete_node_ptr_at_scope_exit(delete_node_ptr_at_scope_exit &&) = delete;
  auto &operator=(const delete_node_ptr_at_scope_exit &) = delete;
  auto &operator=(delete_node_ptr_at_scope_exit &&) = delete;
};

static_assert(sizeof(delete_node_ptr_at_scope_exit) == sizeof(void *));

}  // namespace

namespace unodb::detail {

class inode {
  // key_prefix fields and methods
 public:
  using key_prefix_size_type = std::uint8_t;

 private:
  static constexpr key_prefix_size_type key_prefix_capacity = 7;

 public:
  using key_prefix_data_type = std::array<std::byte, key_prefix_capacity>;

  [[nodiscard]] __attribute__((pure)) auto get_shared_key_prefix_length(
      unodb::detail::art_key shifted_key) const noexcept {
    const auto prefix_word = header_as_uint64() >> 8U;
    return shared_len(static_cast<std::uint64_t>(shifted_key), prefix_word,
                      key_prefix_length());
  }

  [[nodiscard]] unsigned key_prefix_length() const noexcept {
    assert(f.f.key_prefix_length <= key_prefix_capacity);
    return f.f.key_prefix_length;
  }

  void cut_key_prefix(unsigned cut_len) noexcept {
    assert(cut_len > 0);
    assert(cut_len <= key_prefix_length());

    const auto type = static_cast<std::uint8_t>(f.f.header.type());
    const auto prefix_word = header_as_uint64();
    const auto cut_prefix_word =
        ((prefix_word >> (cut_len * 8)) & key_bytes_mask) | type;
    set_header(cut_prefix_word);

    f.f.key_prefix_length =
        gsl::narrow_cast<key_prefix_size_type>(key_prefix_length() - cut_len);
  }

  void prepend_key_prefix(const inode &prefix1, std::byte prefix2) noexcept {
    assert(key_prefix_length() + prefix1.key_prefix_length() <
           key_prefix_capacity);

    const auto type = static_cast<std::uint8_t>(f.f.header.type());
    const auto prefix_word = header_as_uint64() & key_bytes_mask;
    const auto trailing_prefix_shift = (prefix1.key_prefix_length() + 1U) * 8U;
    const auto shifted_prefix_word = prefix_word << trailing_prefix_shift;
    const auto shifted_prefix2 = static_cast<std::uint64_t>(prefix2)
                                 << trailing_prefix_shift;
    const auto prefix1_mask = ((1ULL << trailing_prefix_shift) - 1) ^ 0xFFU;
    const auto masked_prefix1 = prefix1.header_as_uint64() & prefix1_mask;
    const auto prefix_result =
        shifted_prefix_word | shifted_prefix2 | masked_prefix1 | type;
    set_header(prefix_result);

    f.f.key_prefix_length = gsl::narrow_cast<key_prefix_size_type>(
        key_prefix_length() + prefix1.key_prefix_length() + 1);
  }

  [[nodiscard]] const auto &key_prefix_data() const noexcept {
    return f.f.key_prefix_data;
  }

  __attribute__((cold, noinline)) void dump_key_prefix(std::ostream &os) const {
    const auto len = key_prefix_length();
    os << ", key prefix len = " << static_cast<unsigned>(len);
    if (len > 0) {
      os << ", key prefix =";
      for (std::size_t i = 0; i < len; ++i)
        dump_byte(os, f.f.key_prefix_data[i]);
    } else {
      os << ' ';
    }
  }

  // The first element is the child index in the node, the 2nd is pointer
  // to the child. If not present, the pointer is nullptr, and the index
  // is undefined
  using find_result_type = std::pair<std::uint8_t, node_ptr *>;

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept;

  void remove(std::uint8_t child_index) noexcept;

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  [[nodiscard]] bool is_full() const noexcept;

  [[nodiscard]] bool is_min_size() const noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

  // inode must not be allocated directly on heap
  [[nodiscard]] __attribute((cold, noinline)) static void *operator new(
      std::size_t) {
    cannot_happen();
  }

  DISABLE_CLANG_WARNING("-Wmissing-noreturn")
  __attribute__((cold, noinline)) static void operator delete(void *) {
    cannot_happen();
  }
  RESTORE_CLANG_WARNINGS()

 protected:
  inode(node_type type, std::uint8_t children_count, art_key k1, art_key k2,
        tree_depth depth) noexcept
      : f{type, k1, k2, depth, children_count} {
    assert(type != node_type::LEAF);
    assert(k1 != k2);
  }

  inode(node_type type, std::uint8_t children_count, unsigned key_prefix_len,
        const inode &key_prefix_source_node) noexcept
      : f{type, children_count, key_prefix_len, key_prefix_source_node} {
    assert(type != node_type::LEAF);
  }

  inode(node_type type, std::uint8_t children_count,
        const inode &other) noexcept
      : f{type, children_count, other} {
    assert(type != node_type::LEAF);
  }

  DISABLE_GCC_WARNING("-Wsuggest-attribute=pure")
  template <typename KeysType>
  static auto get_sorted_key_array_insert_position(
      const KeysType &keys, uint8_t children_count,
      std::byte key_byte) noexcept {
    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
    assert(std::adjacent_find(keys.cbegin(), keys.cbegin() + children_count) >=
           keys.cbegin() + children_count);

    const auto result = static_cast<uint8_t>(
        std::lower_bound(keys.cbegin(), keys.cbegin() + children_count,
                         key_byte) -
        keys.cbegin());

    assert(result == children_count || keys[result] != key_byte);
    return result;
  }
  RESTORE_GCC_WARNINGS()

 private:
  static constexpr auto key_bytes_mask = 0xFFFFFFFF'FFFFFF00ULL;

  [[nodiscard]] __attribute__((const)) std::uint64_t header_as_uint64()
      const noexcept {
    return static_cast<std::uint64_t>(f.words[1]) << 32U | f.words[0];
  }

  [[nodiscard]] static __attribute__((pure)) unsigned shared_len(
      std::uint64_t k1, std::uint64_t k2, unsigned clamp_byte_pos) noexcept {
    assert(clamp_byte_pos < 8);

    const auto diff = k1 ^ k2;
    const auto clamped = diff | (1ULL << (clamp_byte_pos * 8U));
    return (ffs_nonzero(clamped) - 1) >> 3U;
  }

  void set_header(std::uint64_t word) noexcept {
    f.words[0] = gsl::narrow_cast<std::uint32_t>(word & 0xFFFFFFFFULL);
    f.words[1] = gsl::narrow_cast<std::uint32_t>(word >> 32U);
  }

  union inode_union {
    struct inode_fields {
      const node_header header;
      key_prefix_data_type key_prefix_data;
      key_prefix_size_type key_prefix_length;
      std::uint8_t children_count;
    } f;
    static_assert(sizeof(inode_fields) == 10);
    std::array<std::uint32_t, 2> words;

    inode_union(node_type type, art_key k1, art_key shifted_k2,
                tree_depth depth, std::uint8_t children_count) noexcept {
      k1.shift_right(depth);

      const auto k1_word = static_cast<std::uint64_t>(k1);

      words[0] = gsl::narrow_cast<std::uint32_t>(
          static_cast<std::uint32_t>(type) | (k1_word & 0xFFFFFFFULL) << 8U);
      words[1] = gsl::narrow_cast<std::uint32_t>(k1_word >> 24U);

      f.key_prefix_length = gsl::narrow_cast<key_prefix_size_type>(
          shared_len(k1_word, static_cast<std::uint64_t>(shifted_k2),
                     key_prefix_capacity));
      f.children_count = children_count;
    }

    inode_union(node_type type, std::uint8_t children_count,
                unsigned key_prefix_len,
                const inode &key_prefix_source_node) noexcept {
      assert(key_prefix_len <= key_prefix_capacity);

      words[0] = (key_prefix_source_node.f.words[0] & 0xFFFFFF00U) |
                 static_cast<std::uint8_t>(type);
      words[1] = key_prefix_source_node.f.words[1];
      f.key_prefix_length =
          gsl::narrow_cast<key_prefix_size_type>(key_prefix_len);
      f.children_count = children_count;
    }

    inode_union(node_type type, std::uint8_t children_count,
                const inode &other) noexcept
        : inode_union{type, children_count, other.key_prefix_length(), other} {}
  } f;

  static_assert(sizeof(inode_union) == 12);

  template <unsigned, unsigned, node_type, class, class, class>
  friend class basic_inode;
  friend class inode_4;
  friend class inode_16;
  friend class inode_48;
  friend class inode_256;
  friend class unodb::db;
};

static_assert(std::is_standard_layout_v<inode>);
static_assert(sizeof(inode) == 12);

}  // namespace unodb::detail

namespace {

void delete_subtree(unodb::detail::node_ptr node) noexcept {
  if (node.header == nullptr) return;

  delete_node_ptr_at_scope_exit delete_on_scope_exit(node);

  if (node.type() != unodb::detail::node_type::LEAF)
    delete_on_scope_exit.internal->delete_subtree();
}

#if !defined(__x86_64)
// From public domain
// https://graphics.stanford.edu/~seander/bithacks.html
inline constexpr __attribute__((const)) std::uint32_t has_zero_byte(
    std::uint32_t v) noexcept {
  return ((v - 0x01010101UL) & ~v & 0x80808080UL);
}

inline constexpr __attribute__((const)) std::uint32_t contains_byte(
    std::uint32_t v, std::byte b) noexcept {
  return has_zero_byte(v ^ (~0U / 255 * static_cast<std::uint8_t>(b)));
}
#endif

}  // namespace

namespace unodb {

namespace detail {

template <unsigned MinSize, unsigned Capacity, node_type NodeType,
          class SmallerDerived, class LargerDerived, class Derived>
class basic_inode : public inode {
  static_assert(NodeType != node_type::LEAF);
  static_assert(!std::is_same_v<Derived, LargerDerived>);
  static_assert(!std::is_same_v<SmallerDerived, Derived>);
  static_assert(!std::is_same_v<SmallerDerived, LargerDerived>);
  static_assert(MinSize < Capacity);

 public:
  [[nodiscard]] static auto create(std::unique_ptr<LargerDerived> &&source_node,
                                   std::uint8_t child_to_remove) {
    return std::make_unique<Derived>(std::move(source_node), child_to_remove);
  }

  [[nodiscard]] static auto create(
      std::unique_ptr<SmallerDerived> &&source_node, leaf_unique_ptr &&child,
      tree_depth depth) {
    return std::make_unique<Derived>(std::move(source_node), std::move(child),
                                     depth);
  }

  [[nodiscard]] static void *operator new(std::size_t size) {
    assert(size == sizeof(Derived));

    return pmr_allocate(get_inode_pool<Derived>(), size,
                        alignment_for_new<Derived>());
  }

  static void operator delete(void *to_delete) {
    pmr_deallocate(get_inode_pool<Derived>(), to_delete, sizeof(Derived),
                   alignment_for_new<Derived>());
  }

  [[nodiscard]] auto is_full() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() == NodeType);

    return f.f.children_count == capacity;
  }

  [[nodiscard]] auto is_min_size() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() == NodeType);

    return f.f.children_count == min_size;
  }

 protected:
  basic_inode(art_key k1, art_key k2, tree_depth depth) noexcept
      : inode{NodeType, MinSize, k1, k2, depth} {
    assert(is_min_size());
  }

  basic_inode(unsigned key_prefix_len,
              const inode &key_prefix_source_node) noexcept
      : inode{NodeType, MinSize, key_prefix_len, key_prefix_source_node} {
    assert(is_min_size());
  }

  explicit basic_inode(const SmallerDerived &source_node) noexcept
      : inode{NodeType, MinSize, source_node} {
    assert(source_node.is_full());
    assert(is_min_size());
  }

  explicit basic_inode(const LargerDerived &source_node) noexcept
      : inode{NodeType, Capacity, source_node} {
    assert(source_node.is_min_size());
    assert(is_full());
  }

  static constexpr auto min_size = MinSize;
  static constexpr auto capacity = Capacity;
  static constexpr auto static_node_type = NodeType;
};

using basic_inode_4 =
    basic_inode<2, 4, node_type::I4, fake_inode, inode_16, inode_4>;

class inode_4 final : public basic_inode_4 {
 public:
  using basic_inode_4::create;

  // Create a new node with two given child nodes
  [[nodiscard]] static auto create(art_key k1, art_key shifted_k2,
                                   tree_depth depth, node_ptr child1,
                                   leaf_unique_ptr &&child2) {
    return std::make_unique<inode_4>(k1, shifted_k2, depth, child1,
                                     std::move(child2));
  }

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused this
  // key prefix split.
  [[nodiscard]] static auto create(node_ptr source_node, unsigned len,
                                   tree_depth depth, leaf_unique_ptr &&child1) {
    return std::make_unique<inode_4>(source_node, len, depth,
                                     std::move(child1));
  }

  inode_4(art_key k1, art_key shifted_k2, tree_depth depth, node_ptr child1,
          leaf_unique_ptr &&child2) noexcept;

  inode_4(node_ptr source_node, unsigned len, tree_depth depth,
          leaf_unique_ptr &&child1) noexcept;

  inode_4(std::unique_ptr<inode_16> &&source_node,
          std::uint8_t child_to_remove) noexcept;

  void add(leaf_unique_ptr &&__restrict__ child, tree_depth depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));

    const auto key_byte =
        static_cast<std::uint8_t>(leaf::key(child.get())[depth]);

    const auto first_lt = ((keys.integer & 0xFFU) < key_byte) ? 1 : 0;
    const auto second_lt = (((keys.integer >> 8U) & 0xFFU) < key_byte) ? 1 : 0;
    const auto third_lt = ((f.f.children_count == 3) &&
                           ((keys.integer >> 16U) & 0xFFU) < key_byte)
                              ? 1
                              : 0;
    const auto insert_pos_index =
        static_cast<unsigned>(first_lt + second_lt + third_lt);

    for (decltype(keys.byte_array)::size_type i = f.f.children_count;
         i > insert_pos_index; --i) {
      keys.byte_array[i] = keys.byte_array[i - 1];
      // TODO(laurynas): Node4 children fit into a single YMM register on AVX
      // onwards, see if it is possible to do shift/insert with it. Checked
      // plain AVX, it seems that at least AVX2 is required.
      children[i] = children[i - 1];
    }
    keys.byte_array[insert_pos_index] = static_cast<std::byte>(key_byte);
    children[insert_pos_index] = child.release();

    ++f.f.children_count;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    assert(child_index < f.f.children_count);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));

    delete_node_ptr_at_scope_exit delete_on_scope_exit{children[child_index]};

    for (decltype(keys.byte_array)::size_type i = child_index;
         i < static_cast<unsigned>(f.f.children_count - 1); ++i) {
      // TODO(laurynas): see the AVX2 TODO at add method
      keys.byte_array[i] = keys.byte_array[i + 1];
      children[i] = children[i + 1];
    }

    --f.f.children_count;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));
  }

  auto leave_last_child(std::uint8_t child_to_delete) noexcept {
    assert(is_min_size());
    assert(child_to_delete == 0 || child_to_delete == 1);
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    const auto child_to_delete_ptr = children[child_to_delete];
    const std::uint8_t child_to_leave = (child_to_delete == 0) ? 1 : 0;
    const auto child_to_leave_ptr = children[child_to_leave];
    delete_node_ptr_at_scope_exit child_to_delete_deleter{child_to_delete_ptr};
    if (child_to_leave_ptr.type() != node_type::LEAF) {
      child_to_leave_ptr.internal->prepend_key_prefix(
          *this, keys.byte_array[child_to_leave]);
    }
    return child_to_leave_ptr;
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  friend class inode_16;

  void add_two_to_empty(std::byte key1, node_ptr child1, std::byte key2,
                        leaf_unique_ptr &&child2) noexcept;

  union {
    std::array<std::byte, capacity> byte_array;
    std::uint32_t integer;
  } keys;
  std::array<node_ptr, capacity> children;
};

inode_4::inode_4(art_key k1, art_key shifted_k2, tree_depth depth,
                 node_ptr child1, leaf_unique_ptr &&child2) noexcept
    : basic_inode_4{k1, shifted_k2, depth} {
  const auto k2_next_byte_depth = key_prefix_length();
  const auto k1_next_byte_depth = k2_next_byte_depth + depth;
  add_two_to_empty(k1[k1_next_byte_depth], child1,
                   shifted_k2[k2_next_byte_depth], std::move(child2));
}

inode_4::inode_4(node_ptr source_node, unsigned len, tree_depth depth,
                 leaf_unique_ptr &&child1) noexcept
    : basic_inode_4{len, *source_node.internal} {
  assert(source_node.type() != node_type::LEAF);
  assert(len < source_node.internal->key_prefix_length());
  assert(depth + len <= art_key::size);

  const auto source_node_key_byte =
      source_node.internal->key_prefix_data()[len];
  source_node.internal->cut_key_prefix(len + 1);
  const auto new_key_byte = leaf::key(child1.get())[depth + len];
  add_two_to_empty(source_node_key_byte, source_node, new_key_byte,
                   std::move(child1));
}

void inode_4::add_two_to_empty(std::byte key1, node_ptr child1, std::byte key2,
                               leaf_unique_ptr &&child2) noexcept {
  assert(key1 != key2);
  assert(f.f.children_count == 2);

  const std::uint8_t key1_i = key1 < key2 ? 0 : 1;
  const std::uint8_t key2_i = key1_i == 0 ? 1 : 0;
  keys.byte_array[key1_i] = key1;
  children[key1_i] = child1;
  keys.byte_array[key2_i] = key2;
  children[key2_i] = child2.release();
  keys.byte_array[2] = std::byte{0};
  keys.byte_array[3] = std::byte{0};

  assert(std::is_sorted(keys.byte_array.cbegin(),
                        keys.byte_array.cbegin() + f.f.children_count));
}

void inode_4::delete_subtree() noexcept {
  for (unsigned i = 0; i < f.f.children_count; ++i)
    ::delete_subtree(children[i]);
}

void inode_4::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (unsigned i = 0; i < f.f.children_count; ++i)
    dump_byte(os, keys.byte_array[i]);
  os << ", children:\n";
  for (unsigned i = 0; i < f.f.children_count; ++i) dump_node(os, children[i]);
}

__attribute__((pure)) inode::find_result_type inode_4::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

#if defined(__x86_64)
  const auto replicated_search_key = _mm_set1_epi8(static_cast<char>(key_byte));
  const auto keys_in_sse_reg =
      _mm_cvtsi32_si128(static_cast<std::int32_t>(keys.integer));
  const auto matching_key_positions =
      _mm_cmpeq_epi8(replicated_search_key, keys_in_sse_reg);
  const auto mask = (1U << f.f.children_count) - 1;
  const auto bit_field =
      static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
  if (bit_field != 0) {
    const auto i = static_cast<unsigned>(__builtin_ctz(bit_field));
    return std::make_pair(i, &children[i]);
  }
  return std::make_pair(0xFF, nullptr);
#else
  // Bit twiddling:
  // contains_byte:     __builtin_ffs:   for key index:
  //    0x80000000               0x20                3
  //      0x800000               0x18                2
  //      0x808000               0x10                1
  //          0x80                0x8                0
  //           0x0                0x0        not found
  const auto result = static_cast<decltype(keys.byte_array)::size_type>(
      // __builtin_ffs takes signed argument:
      // NOLINTNEXTLINE(hicpp-signed-bitwise)
      __builtin_ffs(
          static_cast<std::int32_t>(contains_byte(keys.integer, key_byte))) >>
      3);

  if ((result == 0) || (result > children_count))
    return std::make_pair(0xFF, nullptr);

  return std::make_pair(result - 1, &children[result - 1]);
#endif
}

using basic_inode_16 =
    basic_inode<5, 16, node_type::I16, inode_4, inode_48, inode_16>;

class inode_16 final : public basic_inode_16 {
 public:
  inode_16(std::unique_ptr<inode_4> &&source_node, leaf_unique_ptr &&child,
           tree_depth depth) noexcept;

  inode_16(std::unique_ptr<inode_48> &&source_node,
           uint8_t child_to_remove) noexcept;

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    const auto key_byte = leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(key_byte, std::move(child));
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    remove_from_sorted_key_children_arrays(child_index);
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  void insert_into_sorted_key_children_arrays(
      std::byte key_byte, leaf_unique_ptr &&__restrict__ child) {
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));

    const auto insert_pos_index = get_sorted_key_array_insert_position(
        keys.byte_array, f.f.children_count, key_byte);
    if (insert_pos_index != f.f.children_count) {
      assert(keys.byte_array[insert_pos_index] != key_byte);
      std::copy_backward(keys.byte_array.cbegin() + insert_pos_index,
                         keys.byte_array.cbegin() + f.f.children_count,
                         keys.byte_array.begin() + f.f.children_count + 1);
      std::copy_backward(children.begin() + insert_pos_index,
                         children.begin() + f.f.children_count,
                         children.begin() + f.f.children_count + 1);
    }
    keys.byte_array[insert_pos_index] = key_byte;
    children[insert_pos_index] = child.release();
    ++f.f.children_count;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));
  }

  void remove_from_sorted_key_children_arrays(
      std::uint8_t child_to_remove) noexcept {
    assert(child_to_remove < f.f.children_count);
    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));

    std::copy(keys.byte_array.cbegin() + child_to_remove + 1,
              keys.byte_array.cbegin() + f.f.children_count,
              keys.byte_array.begin() + child_to_remove);

    delete_node_ptr_at_scope_exit delete_on_scope_exit{
        children[child_to_remove]};

    std::copy(children.begin() + child_to_remove + 1,
              children.begin() + f.f.children_count,
              children.begin() + child_to_remove);
    --f.f.children_count;

    assert(std::is_sorted(keys.byte_array.cbegin(),
                          keys.byte_array.cbegin() + f.f.children_count));
  }

  union {
    std::array<std::byte, capacity> byte_array;
    __m128i sse;
  } keys;
  std::array<node_ptr, capacity> children;

  friend class inode_4;
  friend class inode_48;
};

inode_4::inode_4(std::unique_ptr<inode_16> &&source_node,
                 uint8_t child_to_remove) noexcept
    : basic_inode_4{*source_node} {
  const auto source_node_children_count = source_node->f.f.children_count;

  std::copy(source_node->keys.byte_array.cbegin(),
            source_node->keys.byte_array.cbegin() + child_to_remove,
            keys.byte_array.begin());
  std::copy(source_node->keys.byte_array.cbegin() + child_to_remove + 1,
            source_node->keys.byte_array.cbegin() + source_node_children_count,
            keys.byte_array.begin() + child_to_remove);
  std::copy(source_node->children.begin(),
            source_node->children.begin() + child_to_remove, children.begin());

  delete_node_ptr_at_scope_exit delete_on_scope_exit{
      source_node->children[child_to_remove]};

  std::copy(source_node->children.begin() + child_to_remove + 1,
            source_node->children.begin() + source_node_children_count,
            children.begin() + child_to_remove);

  assert(std::is_sorted(keys.byte_array.cbegin(),
                        keys.byte_array.cbegin() + f.f.children_count));
}

inode_16::inode_16(std::unique_ptr<inode_4> &&source_node,
                   leaf_unique_ptr &&child, tree_depth depth) noexcept
    : basic_inode_16{*source_node} {
  const auto key_byte = leaf::key(child.get())[depth];
  const auto insert_pos_index = get_sorted_key_array_insert_position(
      source_node->keys.byte_array, source_node->f.f.children_count, key_byte);
  std::copy(source_node->keys.byte_array.cbegin(),
            source_node->keys.byte_array.cbegin() + insert_pos_index,
            keys.byte_array.begin());
  keys.byte_array[insert_pos_index] = key_byte;
  std::copy(source_node->keys.byte_array.cbegin() + insert_pos_index,
            source_node->keys.byte_array.cend(),
            keys.byte_array.begin() + insert_pos_index + 1);
  std::copy(source_node->children.begin(),
            source_node->children.begin() + insert_pos_index, children.begin());
  children[insert_pos_index] = child.release();
  std::copy(source_node->children.begin() + insert_pos_index,
            source_node->children.end(),
            children.begin() + insert_pos_index + 1);
}

__attribute__((pure)) inode::find_result_type inode_16::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

#if defined(__x86_64)
  const auto replicated_search_key = _mm_set1_epi8(static_cast<char>(key_byte));
  const auto matching_key_positions =
      _mm_cmpeq_epi8(replicated_search_key, keys.sse);
  const auto mask = (1U << f.f.children_count) - 1;
  const auto bit_field =
      static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
  if (bit_field != 0) {
    const auto i = static_cast<unsigned>(__builtin_ctz(bit_field));
    return std::make_pair(i, &children[i]);
  }
  return std::make_pair(0xFF, nullptr);
#else
#error Needs porting
#endif
}

void inode_16::delete_subtree() noexcept {
  for (std::uint8_t i = 0; i < f.f.children_count; ++i)
    ::delete_subtree(children[i]);
}

void inode_16::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (std::uint8_t i = 0; i < f.f.children_count; ++i)
    dump_byte(os, keys.byte_array[i]);
  os << ", children:\n";
  for (std::uint8_t i = 0; i < f.f.children_count; ++i)
    dump_node(os, children[i]);
}

using basic_inode_48 =
    basic_inode<17, 48, node_type::I48, inode_16, inode_256, inode_48>;

class inode_48 final : public basic_inode_48 {
 public:
  inode_48(std::unique_ptr<inode_16> &&source_node, leaf_unique_ptr &&child,
           tree_depth depth) noexcept;

  inode_48(std::unique_ptr<inode_256> &&source_node,
           uint8_t child_to_remove) noexcept;

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    const auto key_byte = static_cast<uint8_t>(leaf::key(child.get())[depth]);
    assert(child_indexes[key_byte] == empty_child);
    std::uint8_t i;
    node_ptr child_ptr;
    for (i = 0; i < capacity; ++i) {
      child_ptr = children[i];
      if (child_ptr == nullptr) break;
    }
    assert(child_ptr == nullptr);
    child_indexes[key_byte] = i;
    children[i] = child.release();
    ++f.f.children_count;
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    remove_child_pointer(child_index);
    children[child_indexes[child_index]] = nullptr;
    child_indexes[child_index] = empty_child;
    --f.f.children_count;
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  void remove_child_pointer(std::uint8_t child_index) noexcept {
    direct_remove_child_pointer(child_indexes[child_index]);
  }

  void direct_remove_child_pointer(std::uint8_t children_i) noexcept {
    const auto child_ptr = children[children_i];

    assert(children_i != empty_child);
    assert(child_ptr != nullptr);

    delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};
  }

  std::array<std::uint8_t, 256> child_indexes;
  std::array<node_ptr, capacity> children;

  static constexpr std::uint8_t empty_child = 0xFF;

  friend class inode_16;
  friend class inode_256;
};

inode_16::inode_16(std::unique_ptr<inode_48> &&source_node,
                   std::uint8_t child_to_remove) noexcept
    : basic_inode_16{*source_node} {
  std::uint8_t next_child = 0;
  for (unsigned i = 0; i < 256; i++) {
    const auto source_child_i = source_node->child_indexes[i];
    if (i == child_to_remove) {
      source_node->direct_remove_child_pointer(source_child_i);
      continue;
    }
    if (source_child_i != inode_48::empty_child) {
      keys.byte_array[next_child] = gsl::narrow_cast<std::byte>(i);
      const auto source_child_ptr = source_node->children[source_child_i];
      assert(source_child_ptr != nullptr);
      children[next_child] = source_child_ptr;
      ++next_child;
      if (next_child == f.f.children_count) {
        if (i < child_to_remove) {
          source_node->remove_child_pointer(child_to_remove);
        }
        break;
      }
    }
  }

  assert(std::is_sorted(keys.byte_array.cbegin(),
                        keys.byte_array.cbegin() + f.f.children_count));
}

inode_48::inode_48(std::unique_ptr<inode_16> &&source_node,
                   leaf_unique_ptr &&child, tree_depth depth) noexcept
    : basic_inode_48{*source_node} {
  std::memset(&child_indexes[0], empty_child,
              child_indexes.size() * sizeof(child_indexes[0]));
  std::uint8_t i;
  for (i = 0; i < inode_16::capacity; ++i) {
    const auto existing_key_byte = source_node->keys.byte_array[i];
    child_indexes[static_cast<std::uint8_t>(existing_key_byte)] = i;
    children[i] = source_node->children[i];
  }

  const auto key_byte =
      static_cast<std::uint8_t>(leaf::key(child.get())[depth]);
  assert(child_indexes[key_byte] == empty_child);
  child_indexes[key_byte] = i;
  children[i] = child.release();
  for (i = f.f.children_count; i < capacity; i++) {
    children[i] = nullptr;
  }
}

__attribute__((pure)) inode::find_result_type inode_48::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

  if (child_indexes[static_cast<std::uint8_t>(key_byte)] != empty_child) {
    const auto child_i = child_indexes[static_cast<std::uint8_t>(key_byte)];
    assert(children[child_i] != nullptr);
    return std::make_pair(static_cast<std::uint8_t>(key_byte),
                          &children[child_i]);
  }
  return std::make_pair(0xFF, nullptr);
}

void inode_48::delete_subtree() noexcept {
  unsigned actual_children_count = 0;
  for (unsigned i = 0; i < capacity; ++i) {
    const auto child = children[i];
    if (child != nullptr) {
      ++actual_children_count;
      ::delete_subtree(child);
      assert(actual_children_count <= f.f.children_count);
    }
  }
  assert(actual_children_count == f.f.children_count);
}

void inode_48::dump(std::ostream &os) const {
  os << ", key bytes & child indexes\n";
  unsigned actual_children_count = 0;
  for (unsigned i = 0; i < 256; i++)
    if (child_indexes[i] != empty_child) {
      ++actual_children_count;
      os << " ";
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ", child index = " << static_cast<unsigned>(child_indexes[i])
         << ": ";
      assert(children[child_indexes[i]] != nullptr);
      dump_node(os, children[child_indexes[i]]);
      assert(actual_children_count <= f.f.children_count);
    }

  assert(actual_children_count == f.f.children_count);
}

using basic_inode_256 =
    basic_inode<49, 256, node_type::I256, inode_48, fake_inode, inode_256>;

class inode_256 final : public basic_inode_256 {
 public:
  inode_256(std::unique_ptr<inode_48> &&source_node, leaf_unique_ptr &&child,
            tree_depth depth) noexcept;

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    assert(!is_full());

    const auto key_byte =
        static_cast<std::uint8_t>(leaf::key(child.get())[depth]);
    assert(children[key_byte] == nullptr);
    children[key_byte] = child.release();
    ++f.f.children_count;
  }

  void remove(std::uint8_t child_index) noexcept {
    const auto child_ptr = children[child_index];

    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    assert(child_ptr != nullptr);

    delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};

    children[child_index] = nullptr;
    --f.f.children_count;
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  template <typename Function>
  void for_each_child(Function func) noexcept(noexcept(func(0, nullptr)));

  template <typename Function>
  void for_each_child(Function func) const noexcept(noexcept(func(0, nullptr)));

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  std::array<node_ptr, capacity> children;

  friend class inode_48;
};

inode_48::inode_48(std::unique_ptr<inode_256> &&source_node,
                   std::uint8_t child_to_remove) noexcept
    : basic_inode_48{*source_node} {
  std::uint8_t next_child = 0;
  unsigned child_i = 0;
  for (; child_i < 256; child_i++) {
    const auto child_ptr = source_node->children[child_i];
    if (child_i == child_to_remove) {
      assert(child_ptr != nullptr);
      delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};
      child_indexes[child_i] = empty_child;
      continue;
    }
    if (child_ptr == nullptr) {
      child_indexes[child_i] = empty_child;
      continue;
    }
    assert(child_ptr != nullptr);
    child_indexes[child_i] = next_child;
    children[next_child] = source_node->children[child_i];
    ++next_child;
    if (next_child == f.f.children_count) {
      if (child_i < child_to_remove) {
        const auto child_to_remove_ptr = source_node->children[child_to_remove];
        assert(child_to_remove_ptr != nullptr);
        delete_node_ptr_at_scope_exit delete_on_scope_exit{child_to_remove_ptr};
      }
      break;
    }
  }

  ++child_i;
  for (; child_i < 256; child_i++) child_indexes[child_i] = empty_child;
}

inode_256::inode_256(std::unique_ptr<inode_48> &&source_node,
                     leaf_unique_ptr &&child, tree_depth depth) noexcept
    : basic_inode_256{*source_node} {
  for (unsigned i = 0; i < 256; i++) {
    const auto children_i = source_node->child_indexes[i];
    children[i] = children_i == inode_48::empty_child
                      ? nullptr
                      : source_node->children[children_i];
  }

  const auto key_byte = static_cast<uint8_t>(leaf::key(child.get())[depth]);
  assert(children[key_byte] == nullptr);
  children[key_byte] = child.release();
}

__attribute__((pure)) inode::find_result_type inode_256::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

  const auto key_int_byte = static_cast<uint8_t>(key_byte);
  if (children[key_int_byte] != nullptr)
    return std::make_pair(key_int_byte, &children[key_int_byte]);
  return std::make_pair(0xFF, nullptr);
}

template <typename Function>
void inode_256::for_each_child(Function func) noexcept(
    noexcept(func(0, nullptr))) {
  std::uint8_t actual_children_count = 0;
  for (unsigned i = 0; i < 256; ++i) {
    const auto child_ptr = children[i];
    if (child_ptr != nullptr) {
      ++actual_children_count;
      func(i, child_ptr);
      assert(actual_children_count <= f.f.children_count ||
             f.f.children_count == 0);
    }
  }
  assert(actual_children_count == f.f.children_count);
}

template <typename Function>
void inode_256::for_each_child(Function func) const
    noexcept(noexcept(func(0, nullptr))) {
  const_cast<inode_256 *>(this)->for_each_child(func);
}

void inode_256::delete_subtree() noexcept {
  for_each_child(
      [](unsigned, node_ptr child) noexcept { ::delete_subtree(child); });
}

void inode_256::dump(std::ostream &os) const {
  os << ", key bytes & children:\n";
  for_each_child([&](unsigned i, node_ptr child) noexcept {
    os << ' ';
    dump_byte(os, gsl::narrow_cast<std::byte>(i));
    os << ' ';
    dump_node(os, child);
  });
}

inline bool inode::is_full() const noexcept {
  switch (f.f.header.type()) {
    case node_type::I4:
      return static_cast<const inode_4 *>(this)->is_full();
    case node_type::I16:
      return static_cast<const inode_16 *>(this)->is_full();
    case node_type::I48:
      return static_cast<const inode_48 *>(this)->is_full();
    case node_type::I256:
      return static_cast<const inode_256 *>(this)->is_full();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

inline bool inode::is_min_size() const noexcept {
  switch (f.f.header.type()) {
    case node_type::I4:
      return static_cast<const inode_4 *>(this)->is_min_size();
    case node_type::I16:
      return static_cast<const inode_16 *>(this)->is_min_size();
    case node_type::I48:
      return static_cast<const inode_48 *>(this)->is_min_size();
    case node_type::I256:
      return static_cast<const inode_256 *>(this)->is_min_size();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

inline void inode::add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
  assert(!is_full());
  assert(child.get() != nullptr);

  switch (f.f.header.type()) {
    case node_type::I4:
      static_cast<inode_4 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I16:
      static_cast<inode_16 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I48:
      static_cast<inode_48 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I256:
      static_cast<inode_256 *>(this)->add(std::move(child), depth);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

inline void inode::remove(std::uint8_t child_index) noexcept {
  assert(!is_min_size());

  switch (f.f.header.type()) {
    case node_type::I4:
      static_cast<inode_4 *>(this)->remove(child_index);
      break;
    case node_type::I16:
      static_cast<inode_16 *>(this)->remove(child_index);
      break;
    case node_type::I48:
      static_cast<inode_48 *>(this)->remove(child_index);
      break;
    case node_type::I256:
      static_cast<inode_256 *>(this)->remove(child_index);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

inline inode::find_result_type inode::find_child(std::byte key_byte) noexcept {
  switch (f.f.header.type()) {
    case node_type::I4:
      return static_cast<inode_4 *>(this)->find_child(key_byte);
    case node_type::I16:
      return static_cast<inode_16 *>(this)->find_child(key_byte);
    case node_type::I48:
      return static_cast<inode_48 *>(this)->find_child(key_byte);
    case node_type::I256:
      return static_cast<inode_256 *>(this)->find_child(key_byte);
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

void inode::delete_subtree() noexcept {
  switch (f.f.header.type()) {
    case node_type::I4:
      return static_cast<inode_4 *>(this)->delete_subtree();
    case node_type::I16:
      return static_cast<inode_16 *>(this)->delete_subtree();
    case node_type::I48:
      return static_cast<inode_48 *>(this)->delete_subtree();
    case node_type::I256:
      return static_cast<inode_256 *>(this)->delete_subtree();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

void inode::dump(std::ostream &os) const {
  switch (f.f.header.type()) {
    case node_type::I4:
      os << "I4: ";
      break;
    case node_type::I16:
      os << "I16: ";
      break;
    case node_type::I48:
      os << "I48: ";
      break;
    case node_type::I256:
      os << "I256: ";
      break;
    case node_type::LEAF:
      cannot_happen();
  }
  os << "# children = "
     << (f.f.children_count == 0 ? 256
                                 : static_cast<unsigned>(f.f.children_count));
  dump_key_prefix(os);
  switch (f.f.header.type()) {
    case node_type::I4:
      static_cast<const inode_4 *>(this)->dump(os);
      break;
    case node_type::I16:
      static_cast<const inode_16 *>(this)->dump(os);
      break;
    case node_type::I48:
      static_cast<const inode_48 *>(this)->dump(os);
      break;
    case node_type::I256:
      static_cast<const inode_256 *>(this)->dump(os);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

class raii_leaf_creator {
 public:
  raii_leaf_creator(art_key k, unodb::value_view v, unodb::db &db_instance_)
      : leaf{leaf::create(k, v, db_instance_)},
        leaf_size{leaf::size(leaf.get())},
        db_instance{db_instance_} {
    assert(std::uncaught_exceptions() == 0);
  }

  raii_leaf_creator(const raii_leaf_creator &) = delete;
  raii_leaf_creator(raii_leaf_creator &&) = delete;

  auto &operator=(const raii_leaf_creator &) = delete;
  auto &operator=(raii_leaf_creator &&) = delete;

  ~raii_leaf_creator() noexcept {
    assert(get_called);

    if (likely(std::uncaught_exceptions() == 0)) return;
    db_instance.decrease_memory_use(leaf_size);

    assert(db_instance.leaf_count > 0);
    --db_instance.leaf_count;
  }

  auto &&get() noexcept {
#ifndef NDEBUG
    assert(!get_called);
    get_called = true;
#endif

    return std::move(leaf);
  }

 private:
  leaf_unique_ptr leaf;
  const std::size_t leaf_size;
  unodb::db &db_instance;
#ifndef NDEBUG
  bool get_called{false};
#endif
};

}  // namespace detail

db::~db() noexcept { ::delete_subtree(root); }

get_result db::get(key search_key) const noexcept {
  if (unlikely(root.header == nullptr)) return {};

  detail::node_ptr node{root};
  const detail::art_key k{search_key};
  detail::art_key remaining_key{k};

  while (true) {
    if (node.type() == detail::node_type::LEAF) {
      if (detail::leaf::matches(node.leaf, k)) {
        const auto value = detail::leaf::value(node.leaf);
        return value;
      }
      return {};
    }

    assert(node.type() != detail::node_type::LEAF);

    if (node.internal->get_shared_key_prefix_length(remaining_key) <
        node.internal->key_prefix_length())
      return {};
    remaining_key.shift_right(node.internal->key_prefix_length());
    auto *const child = node.internal->find_child(remaining_key[0]).second;
    if (child == nullptr) return {};

    node = *child;
    remaining_key.shift_right(1);
  }
}

bool db::insert(key insert_key, value_view v) {
  const auto k = detail::art_key{insert_key};

  if (unlikely(root.header == nullptr)) {
    auto leaf = detail::leaf::create(k, v, *this);
    root = leaf.release();
    return true;
  }

  detail::node_ptr *node = &root;
  detail::tree_depth depth{};
  detail::art_key remaining_key{k};

  while (true) {
    if (node->type() == detail::node_type::LEAF) {
      const auto existing_key = detail::leaf::key(node->leaf);
      if (unlikely(k == existing_key)) return false;
      detail::raii_leaf_creator leaf_creator{k, v, *this};
      auto leaf = leaf_creator.get();
      increase_memory_use(sizeof(detail::inode_4));
      // TODO(laurynas): try to pass leaf node type instead of generic node
      // below. This way it would be apparent that its key prefix does not need
      // updating as leaves don't have any.
      auto new_node = detail::inode_4::create(existing_key, remaining_key,
                                              depth, *node, std::move(leaf));
      *node = new_node.release();
      ++inode4_count;
      ++created_inode4_count;
      assert(created_inode4_count >= inode4_count);
      return true;
    }

    assert(node->type() != detail::node_type::LEAF);
    assert(depth < detail::art_key::size);

    const auto shared_prefix_len =
        node->internal->get_shared_key_prefix_length(remaining_key);
    if (shared_prefix_len < node->internal->key_prefix_length()) {
      detail::raii_leaf_creator leaf_creator{k, v, *this};
      auto leaf = leaf_creator.get();
      increase_memory_use(sizeof(detail::inode_4));
      auto new_node = detail::inode_4::create(*node, shared_prefix_len, depth,
                                              std::move(leaf));
      *node = new_node.release();
      ++inode4_count;
      ++created_inode4_count;
      ++key_prefix_splits;
      assert(created_inode4_count >= inode4_count);
      assert(created_inode4_count > key_prefix_splits);
      return true;
    }

    assert(shared_prefix_len == node->internal->key_prefix_length());
    depth += node->internal->key_prefix_length();
    remaining_key.shift_right(node->internal->key_prefix_length());

    auto *const child = node->internal->find_child(remaining_key[0]).second;

    if (child == nullptr) {
      detail::raii_leaf_creator leaf_creator{k, v, *this};
      auto leaf = leaf_creator.get();

      const auto node_is_full = node->internal->is_full();

      if (likely(!node_is_full)) {
        node->internal->add(std::move(leaf), depth);
        return true;
      }

      assert(node_is_full);

      if (node->type() == detail::node_type::I4) {
        assert(inode4_count > 0);

        increase_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4));
        std::unique_ptr<detail::inode_4> current_node{node->node_4};
        auto larger_node = detail::inode_16::create(std::move(current_node),
                                                    std::move(leaf), depth);
        *node = larger_node.release();

        --inode4_count;
        ++inode16_count;
        ++inode4_to_inode16_count;
        assert(inode4_to_inode16_count >= inode16_count);

      } else if (node->type() == detail::node_type::I16) {
        assert(inode16_count > 0);

        std::unique_ptr<detail::inode_16> current_node{node->node_16};
        increase_memory_use(sizeof(detail::inode_48) -
                            sizeof(detail::inode_16));
        auto larger_node = detail::inode_48::create(std::move(current_node),
                                                    std::move(leaf), depth);
        *node = larger_node.release();

        --inode16_count;
        ++inode48_count;
        ++inode16_to_inode48_count;
        assert(inode16_to_inode48_count >= inode48_count);

      } else {
        assert(inode48_count > 0);

        assert(node->type() == detail::node_type::I48);
        std::unique_ptr<detail::inode_48> current_node{node->node_48};
        increase_memory_use(sizeof(detail::inode_256) -
                            sizeof(detail::inode_48));
        auto larger_node = detail::inode_256::create(std::move(current_node),
                                                     std::move(leaf), depth);
        *node = larger_node.release();

        --inode48_count;
        ++inode256_count;
        ++inode48_to_inode256_count;
        assert(inode48_to_inode256_count >= inode256_count);
      }
      return true;
    }

    node = child;
    ++depth;
    remaining_key.shift_right(1);
  }
}

bool db::remove(key remove_key) {
  const auto k = detail::art_key{remove_key};

  if (unlikely(root == nullptr)) return false;

  if (root.type() == detail::node_type::LEAF) {
    if (detail::leaf::matches(root.leaf, k)) {
      const auto leaf_size = detail::leaf::size(root.leaf);
      detail::leaf_unique_ptr root_leaf_deleter{root.leaf};
      root = nullptr;
      decrease_memory_use(leaf_size);
      assert(leaf_count > 0);
      --leaf_count;
      return true;
    }
    return false;
  }

  detail::node_ptr *node = &root;
  detail::tree_depth depth{};
  detail::art_key remaining_key{k};

  while (true) {
    assert(node->type() != detail::node_type::LEAF);
    assert(depth < detail::art_key::size);

    const auto shared_prefix_len =
        node->internal->get_shared_key_prefix_length(remaining_key);
    if (shared_prefix_len < node->internal->key_prefix_length()) return false;

    assert(shared_prefix_len == node->internal->key_prefix_length());
    depth += node->internal->key_prefix_length();
    remaining_key.shift_right(node->internal->key_prefix_length());

    const auto [child_i, child_ptr] =
        node->internal->find_child(remaining_key[0]);

    if (child_ptr == nullptr) return false;

    if (child_ptr->type() == detail::node_type::LEAF) {
      if (!detail::leaf::matches(child_ptr->leaf, k)) return false;

      assert(leaf_count > 0);

      const auto is_node_min_size = node->internal->is_min_size();
      const auto child_node_size = detail::leaf::size(child_ptr->leaf);

      if (likely(!is_node_min_size)) {
        node->internal->remove(child_i);
        decrease_memory_use(child_node_size);
        --leaf_count;
        return true;
      }

      assert(is_node_min_size);

      if (node->type() == detail::node_type::I4) {
        std::unique_ptr<detail::inode_4> current_node{node->node_4};
        *node = current_node->leave_last_child(child_i);
        decrease_memory_use(child_node_size + sizeof(detail::inode_4));

        assert(inode4_count > 0);
        --inode4_count;
        ++deleted_inode4_count;
        assert(deleted_inode4_count <= created_inode4_count);

      } else if (node->type() == detail::node_type::I16) {
        std::unique_ptr<detail::inode_16> current_node{node->node_16};
        auto new_node{
            detail::inode_4::create(std::move(current_node), child_i)};
        *node = new_node.release();
        decrease_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4) +
                            child_node_size);

        assert(inode16_count > 0);
        --inode16_count;
        ++inode4_count;
        ++inode16_to_inode4_count;
        assert(inode16_to_inode4_count <= inode4_to_inode16_count);

      } else if (node->type() == detail::node_type::I48) {
        std::unique_ptr<detail::inode_48> current_node{node->node_48};
        auto new_node{
            detail::inode_16::create(std::move(current_node), child_i)};
        *node = new_node.release();
        decrease_memory_use(sizeof(detail::inode_48) -
                            sizeof(detail::inode_16) + child_node_size);

        assert(inode48_count > 0);
        --inode48_count;
        ++inode16_count;
        ++inode48_to_inode16_count;
        assert(inode48_to_inode16_count <= inode16_to_inode48_count);

      } else {
        assert(node->type() == detail::node_type::I256);
        std::unique_ptr<detail::inode_256> current_node{node->node_256};
        auto new_node{
            detail::inode_48::create(std::move(current_node), child_i)};
        *node = new_node.release();
        decrease_memory_use(sizeof(detail::inode_256) -
                            sizeof(detail::inode_48) + child_node_size);

        assert(inode256_count > 0);
        --inode256_count;
        ++inode48_count;
        ++inode256_to_inode48_count;
        assert(inode256_to_inode48_count <= inode48_to_inode256_count);
      }

      --leaf_count;
      return true;
    }

    node = child_ptr;
    ++depth;
    remaining_key.shift_right(1);
  }
}

void db::clear() {
  ::delete_subtree(root);
  root = nullptr;
  current_memory_use = 0;

  leaf_count = 0;
  inode4_count = 0;
  inode16_count = 0;
  inode48_count = 0;
  inode256_count = 0;
}

DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")
void db::increase_memory_use(std::size_t delta) {
  if (memory_limit == 0 || delta == 0) return;
  assert(current_memory_use <= memory_limit);
  if (current_memory_use + delta > memory_limit) throw std::bad_alloc{};
  current_memory_use += delta;
}
RESTORE_GCC_WARNINGS()

void db::decrease_memory_use(std::size_t delta) noexcept {
  if (memory_limit == 0 || delta == 0) return;
  assert(delta <= current_memory_use);
  current_memory_use -= delta;
}

}  // namespace unodb

namespace {

void dump_node(std::ostream &os, const unodb::detail::node_ptr &node) {
  os << "node at: " << &node;
  if (node.header == nullptr) {
    os << ", <null>\n";
    return;
  }
  os << ", type = ";
  switch (node.type()) {
    case unodb::detail::node_type::LEAF:
      unodb::detail::leaf::dump(os, node.leaf);
      break;
    case unodb::detail::node_type::I4:
    case unodb::detail::node_type::I16:
    case unodb::detail::node_type::I48:
    case unodb::detail::node_type::I256:
      node.internal->dump(os);
      break;
  }
}

}  // namespace

namespace unodb {

void db::dump(std::ostream &os) const {
  os << "db dump ";
  if (memory_limit == 0) {
    os << "(no memory limit):\n";
  } else {
    os << "memory limit = " << memory_limit
       << ", currently used = " << get_current_memory_use() << '\n';
  }
  dump_node(os, root);
}

}  // namespace unodb
