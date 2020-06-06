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

enum class node_type : std::uint8_t { LEAF = 1, I4, I16, I48, I256 };

static_assert((static_cast<std::uint8_t>(node_type::LEAF) & node_type_mask) ==
              static_cast<std::uint8_t>(node_type::LEAF));
static_assert((static_cast<std::uint8_t>(node_type::I4) & node_type_mask) ==
              static_cast<std::uint8_t>(node_type::I4));
static_assert((static_cast<std::uint8_t>(node_type::I16) & node_type_mask) ==
              static_cast<std::uint8_t>(node_type::I16));
static_assert((static_cast<std::uint8_t>(node_type::I48) & node_type_mask) ==
              static_cast<std::uint8_t>(node_type::I48));
static_assert((static_cast<std::uint8_t>(node_type::I256) & node_type_mask) ==
              static_cast<std::uint8_t>(node_type::I256));

// A common prefix shared by all node types
struct node_header final {
  explicit node_header(node_type type_) : m_type{type_} {}

  [[nodiscard]] auto type() const noexcept { return m_type; }

 private:
  const node_type m_type : 3;
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

class inode_header {
 public:
  using key_prefix_size_type = std::uint8_t;

 private:
  // A key prefix can be up to 8 bytes long, or if the key length is 8 bytes or
  // less, key length minus one.
  static constexpr key_prefix_size_type capacity =
      std::min<std::size_t>(8, sizeof(unodb::detail::art_key) - 1);

  const unodb::detail::node_type m_node_type : 3;
  key_prefix_size_type key_prefix_len : 3;
  std::uint8_t node4_children_count_ : 2;

 public:
  using key_prefix_data_type = std::array<std::byte, capacity>;

 private:
  key_prefix_data_type key_prefix_data_;

 public:
  inode_header(unodb::detail::node_type node_type_, unodb::detail::art_key k1,
               unodb::detail::art_key k2, unodb::detail::tree_depth depth,
               std::uint8_t node4_children_count)
      : m_node_type{node_type_},
        node4_children_count_{
            gsl::narrow_cast<std::uint8_t>(node4_children_count - 2)} {
    assert(type() != unodb::detail::node_type::LEAF);
    assert(k1 != k2);

#ifndef NDEBUG
    for (std::size_t j = 0; j < depth; ++j) {
      assert(k1[j] == k2[j]);
    }
#endif

    auto i{depth};
    for (; k1[i] == k2[i]; ++i) {
      assert(i - depth < capacity);
      key_prefix_data_[i - depth] = k1[i];
    }
    assert(i - depth <= capacity);
    key_prefix_len = gsl::narrow_cast<key_prefix_size_type>(i - depth);
  }

  [[nodiscard]] unodb::detail::node_type type() const noexcept {
    return unodb::detail::node_type{m_node_type};
  }

  [[nodiscard]] auto key_prefix_length() const noexcept {
    return key_prefix_len;
  }

  [[nodiscard]] std::uint8_t node4_children_count() const noexcept {
    return node4_children_count_ + 2;
  }

  void set_node4_children_count(std::uint8_t children_count) noexcept {
    assert(children_count >= 2);
    assert(children_count <= 4);

    node4_children_count_ = children_count - 2;
  }

  inode_header(const inode_header &other)
      : m_node_type{other.m_node_type},
        key_prefix_len{other.key_prefix_length()} {
    assert(type() != unodb::detail::node_type::LEAF);

    std::copy(other.key_prefix_data_.cbegin(),
              other.key_prefix_data_.cbegin() + key_prefix_length(),
              key_prefix_data_.begin());
  }

  inode_header(unodb::detail::node_type node_type_,
               key_prefix_size_type other_key_prefix_len,
               const key_prefix_data_type &other,
               std::uint8_t node4_children_count__) noexcept
      : m_node_type{node_type_},
        key_prefix_len{other_key_prefix_len},
        node4_children_count_{
            gsl::narrow_cast<std::uint8_t>(node4_children_count__ - 2)} {
    assert(type() != unodb::detail::node_type::LEAF);
    assert(other_key_prefix_len < capacity);

    std::copy(other.cbegin(), other.cbegin() + other_key_prefix_len,
              key_prefix_data_.begin());
  }

  inode_header(inode_header &&other) noexcept = delete;

  inode_header &operator=(const inode_header &) = delete;
  inode_header &operator=(inode_header &&) = delete;

  void cut_key_prefix(unsigned cut_len) noexcept {
    assert(cut_len > 0);
    assert(cut_len <= key_prefix_length());

    std::copy(key_prefix_data_.cbegin() + cut_len, key_prefix_data_.cend(),
              key_prefix_data_.begin());
    key_prefix_len =
        gsl::narrow_cast<std::uint8_t>(key_prefix_length() - cut_len);
  }

  void prepend_key_prefix(const inode_header &prefix1,
                          std::byte prefix2) noexcept {
    std::copy_backward(key_prefix_data_.cbegin(),
                       key_prefix_data_.cbegin() + key_prefix_length(),
                       key_prefix_data_.begin() + key_prefix_length() +
                           prefix1.key_prefix_length() + 1);
    std::copy(prefix1.key_prefix_data_.cbegin(),
              prefix1.key_prefix_data_.cbegin() + prefix1.key_prefix_length(),
              key_prefix_data_.begin());
    key_prefix_data_[prefix1.key_prefix_length()] = prefix2;
    key_prefix_len = gsl::narrow_cast<std::uint8_t>(
        key_prefix_length() + prefix1.key_prefix_length() + 1);
  }

  [[nodiscard]] const auto &key_prefix_data() const noexcept {
    return key_prefix_data_;
  }

  [[nodiscard]] auto key_prefix_byte(std::size_t i) const noexcept {
    assert(i < key_prefix_length());

    return key_prefix_data_[i];
  }

  [[nodiscard]] auto get_shared_key_prefix_length(
      unodb::detail::art_key k,
      unodb::detail::tree_depth depth) const noexcept {
    auto key_i{depth};
    unsigned shared_length = 0;
    while (shared_length < key_prefix_length()) {
      if (k[key_i] !=
          key_prefix_data_[(gsl::narrow_cast<std::uint8_t>(shared_length))])
        break;
      ++key_i;
      ++shared_length;
    }
    assert(shared_length <= key_prefix_length());
    return shared_length;
  }

  void dump(std::ostream &os) const;
};

static_assert(std::is_standard_layout_v<inode_header>);
static_assert(sizeof(inode_header) == 8);

__attribute__((cold, noinline)) void inode_header::dump(
    std::ostream &os) const {
  const auto len = key_prefix_length();
  switch (type()) {
    case unodb::detail::node_type::I4:
      os << "I4 (children count = "
         << static_cast<unsigned>(node4_children_count()) << "): ";
      break;
    case unodb::detail::node_type::I16:
      os << "I16: ";
      break;
    case unodb::detail::node_type::I48:
      os << "I48: ";
      break;
    case unodb::detail::node_type::I256:
      os << "I256: ";
      break;
    case unodb::detail::node_type::LEAF:
      cannot_happen();
  }
  os << ", key prefix len = " << static_cast<unsigned>(len);
  if (len > 0) {
    os << ", key prefix =";
    for (std::size_t i = 0; i < len; ++i) dump_byte(os, key_prefix_data_[i]);
  } else {
    os << ' ';
  }
}

}  // namespace

namespace unodb::detail {

class inode {
 public:
  // The first element is the child index (not counting the nullptr sentinel) in
  // the node, the 2nd one is reference to the child node_ptr. If the child was
  // not found, the reference points to a sentinel node_ptr whose all fields are
  // nullptr, and the child index is undefined.
  using find_result_type = std::pair<std::uint8_t, node_ptr &>;

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept;

  void remove(std::uint8_t child_index) noexcept;

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  [[nodiscard]] bool is_full() const noexcept;

  [[nodiscard]] bool is_min_size() const noexcept;

  void delete_subtree() noexcept;

  auto &header() noexcept { return header_; }

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
  inode_header header_;
};

}  // namespace unodb::detail

namespace {

void delete_subtree(unodb::detail::node_ptr node) noexcept {
  if (node.is_sentinel()) return;

  delete_node_ptr_at_scope_exit delete_on_scope_exit(node);

  if (node.type() != unodb::detail::node_type::LEAF)
    delete_on_scope_exit.internal->delete_subtree();
}

#if !defined(__x86_64)
// From public domain
// https://graphics.stanford.edu/~seander/bithacks.html
inline std::uint32_t has_zero_byte(std::uint32_t v) noexcept {
  return ((v - 0x01010101UL) & ~v & 0x80808080UL);
}

inline std::uint32_t contains_byte(std::uint32_t v, std::byte b) noexcept {
  return has_zero_byte(v ^ (~0U / 255 * static_cast<std::uint8_t>(b)));
}
#endif

template <unsigned Capacity>
union header_and_children {
  inode_header header;
  std::array<unodb::detail::node_ptr, Capacity + 1> children_with_sentinel;
  struct {
    unodb::detail::node_ptr sentinel;
    std::array<unodb::detail::node_ptr, Capacity> children;
  } c;

  header_and_children(unodb::detail::node_type node_type_,
                      unodb::detail::art_key k1, unodb::detail::art_key k2,
                      unodb::detail::tree_depth depth,
                      std::uint8_t node4_children_count = 0)
      : header{node_type_, k1, k2, depth, node4_children_count} {}

  header_and_children(unodb::detail::node_type node_type_,
                      inode_header::key_prefix_size_type key_prefix_len,
                      const inode_header::key_prefix_data_type &key_prefix_data,
                      std::uint8_t node4_children_count = 0)
      : header{node_type_, key_prefix_len, key_prefix_data,
               node4_children_count} {}
};

}  // namespace

namespace unodb {

namespace detail {

template <unsigned MinSize, unsigned Capacity, node_type NodeType,
          class SmallerDerived, class LargerDerived, class Derived>
class basic_inode {
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

 protected:
  static constexpr auto min_size = MinSize;
  static constexpr auto capacity = Capacity;
  static constexpr auto static_node_type = NodeType;

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

  template <typename KeysType, typename ChildrenType>
  static void insert_into_sorted_key_children_arrays(
      KeysType &keys, ChildrenType &children, std::uint8_t &children_count,
      std::byte key_byte, leaf_unique_ptr &&child) {
    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));

    const auto insert_pos_index =
        get_sorted_key_array_insert_position(keys, children_count, key_byte);
    if (insert_pos_index != children_count) {
      assert(keys[insert_pos_index] != key_byte);
      std::copy_backward(keys.cbegin() + insert_pos_index,
                         keys.cbegin() + children_count,
                         keys.begin() + children_count + 1);
      std::copy_backward(children.begin() + insert_pos_index,
                         children.begin() + children_count,
                         children.begin() + children_count + 1);
    }
    keys[insert_pos_index] = key_byte;
    children[insert_pos_index] = child.release();
    ++children_count;

    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
  }

  template <typename KeysType, typename ChildrenType>
  static void remove_from_sorted_key_children_arrays(
      KeysType &keys, ChildrenType &children, std::uint8_t &children_count,
      std::uint8_t child_to_remove) noexcept {
    assert(child_to_remove < children_count);
    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));

    std::copy(keys.cbegin() + child_to_remove + 1,
              keys.cbegin() + children_count, keys.begin() + child_to_remove);

    delete_node_ptr_at_scope_exit delete_on_scope_exit{
        children[child_to_remove]};

    std::copy(children.begin() + child_to_remove + 1,
              children.begin() + children_count,
              children.begin() + child_to_remove);
    children[static_cast<typename ChildrenType::size_type>(children_count) -
             1] = node_ptr::sentinel_ptr;
    --children_count;

    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
  }
};

using basic_inode_4 =
    basic_inode<2, 4, node_type::I4, fake_inode, inode_16, inode_4>;

class inode_4 final : public basic_inode_4 {
 public:
  using basic_inode_4::create;

  // Create a new node with two given child nodes
  [[nodiscard]] static auto create(art_key k1, art_key k2, tree_depth depth,
                                   node_ptr child1, leaf_unique_ptr &&child2) {
    return std::make_unique<inode_4>(k1, k2, depth, child1, std::move(child2));
  }

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused this
  // key prefix split.
  [[nodiscard]] static auto create(node_ptr source_node, unsigned len,
                                   tree_depth depth, leaf_unique_ptr &&child1) {
    return std::make_unique<inode_4>(source_node, len, depth,
                                     std::move(child1));
  }

  inode_4(art_key k1, art_key k2, tree_depth depth, node_ptr child1,
          leaf_unique_ptr &&child2) noexcept;

  inode_4(node_ptr source_node, unsigned len, tree_depth depth,
          leaf_unique_ptr &&child1) noexcept;

  inode_4(std::unique_ptr<inode_16> &&source_node,
          std::uint8_t child_to_remove) noexcept;

  [[nodiscard]] auto is_full() const noexcept {
    return fields.header.node4_children_count() == 4;
  }

  [[nodiscard]] auto is_min_size() const noexcept {
    return fields.header.node4_children_count() == 2;
  }

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(fields.header.type() == static_node_type);
    assert(fields.header.node4_children_count() >= 2);
    assert(fields.header.node4_children_count() < 4);

    const auto key_byte = leaf::key(child.get())[depth];
    std::uint8_t children_count = fields.header.node4_children_count();
    insert_into_sorted_key_children_arrays(keys.byte_array, fields.c.children,
                                           children_count, key_byte,
                                           std::move(child));
    fields.header.set_node4_children_count(children_count);

    assert(fields.header.node4_children_count() > 2);
    assert(fields.header.node4_children_count() <= 4);
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(fields.header.type() == static_node_type);
    assert(fields.header.node4_children_count() > 2);
    assert(fields.header.node4_children_count() <= 4);

    std::uint8_t children_count = fields.header.node4_children_count();
    remove_from_sorted_key_children_arrays(keys.byte_array, fields.c.children,
                                           children_count, child_index);
    fields.header.set_node4_children_count(children_count);

    assert(fields.header.node4_children_count() >= 2);
    assert(fields.header.node4_children_count() < 4);
  }

  auto leave_last_child(std::uint8_t child_to_delete) noexcept {
    assert(is_min_size());
    assert(child_to_delete == 0 || child_to_delete == 1);
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    const auto child_to_delete_ptr = fields.c.children[child_to_delete];
    const std::uint8_t child_to_leave = (child_to_delete == 0) ? 1 : 0;
    const auto child_to_leave_ptr = fields.c.children[child_to_leave];
    delete_node_ptr_at_scope_exit child_to_delete_deleter{child_to_delete_ptr};
    if (child_to_leave_ptr.type() != node_type::LEAF) {
      child_to_leave_ptr.internal->header().prepend_key_prefix(
          fields.header, keys.byte_array[child_to_leave]);
    }
    return child_to_leave_ptr;
  }

  [[nodiscard]] inode::find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  friend class inode_16;

  void add_two_to_empty(std::byte key1, node_ptr child1, std::byte key2,
                        leaf_unique_ptr &&child2) noexcept;

  header_and_children<capacity> fields;
  union {
    std::array<std::byte, capacity> byte_array;
    std::uint32_t integer;
  } keys;

  static_assert(sizeof(inode_4::fields) == 40);
  static_assert(sizeof(inode_4::keys) == 4);
};

static_assert(sizeof(inode_4) == 48);
static_assert(std::is_standard_layout_v<inode_4>);

inode_4::inode_4(art_key k1, art_key k2, tree_depth depth, node_ptr child1,
                 leaf_unique_ptr &&child2) noexcept
    : fields{node_type::I4, k1, k2, depth, 2} {
  const auto next_level_depth =
      static_cast<tree_depth>(depth + fields.header.key_prefix_length());
  add_two_to_empty(k1[next_level_depth], child1, k2[next_level_depth],
                   std::move(child2));

  assert(fields.header.node4_children_count() == 2);
}

inode_4::inode_4(node_ptr source_node, unsigned len, tree_depth depth,
                 leaf_unique_ptr &&child1) noexcept
    : fields{node_type::I4,
             gsl::narrow_cast<inode_header::key_prefix_size_type>(len),
             source_node.internal->header().key_prefix_data(), 2} {
  assert(source_node.type() != node_type::LEAF);
  //  assert(len < source_node.internal->header().key_prefix_length());
  assert(depth + len <= art_key::size);

  const auto source_node_key_byte =
      source_node.internal->header().key_prefix_byte(
          gsl::narrow_cast<inode_header::key_prefix_size_type>(len));
  source_node.internal->header().cut_key_prefix(len + 1);
  const auto new_key_byte = leaf::key(child1.get())[depth + len];
  add_two_to_empty(source_node_key_byte, source_node, new_key_byte,
                   std::move(child1));

  assert(fields.header.node4_children_count() == 2);
}

void inode_4::add_two_to_empty(std::byte key1, node_ptr child1, std::byte key2,
                               leaf_unique_ptr &&child2) noexcept {
  assert(key1 != key2);
  assert(fields.header.node4_children_count() == 2);

  const std::uint8_t key1_i = key1 < key2 ? 0 : 1;
  const std::uint8_t key2_i = key1_i == 0 ? 1 : 0;

  keys.byte_array[key1_i] = key1;
  keys.byte_array[key2_i] = key2;
  keys.byte_array[2] = std::byte{0};
  keys.byte_array[3] = std::byte{0};

  fields.c.children[key1_i] = child1;
  fields.c.children[key2_i] = child2.release();
  fields.c.children[2] = node_ptr::sentinel_ptr;
  fields.c.children[3] = node_ptr::sentinel_ptr;

  assert(std::is_sorted(
      keys.byte_array.cbegin(),
      keys.byte_array.cbegin() + fields.header.node4_children_count()));
  assert(fields.header.node4_children_count() == 2);
}

void inode_4::delete_subtree() noexcept {
  assert(fields.header.node4_children_count() >= 2);
  assert(fields.header.node4_children_count() <= 4);

  for (std::uint8_t i = 0; i < fields.header.node4_children_count(); ++i)
    ::delete_subtree(fields.c.children[i]);
}

void inode_4::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (std::uint8_t i = 0; i < fields.header.node4_children_count(); ++i)
    dump_byte(os, keys.byte_array[i]);
  os << ", children:\n";
  for (std::uint8_t i = 0; i < fields.header.node4_children_count(); ++i)
    dump_node(os, fields.c.children[i]);
}

inode::find_result_type inode_4::find_child(std::byte key_byte) noexcept {
  assert(fields.header.type() == static_node_type);
  assert(fields.header.node4_children_count() >= 2);
  assert(fields.header.node4_children_count() <= 4);

#if defined(__x86_64)
  const auto replicated_search_key = _mm_set1_epi8(static_cast<char>(key_byte));
  const auto keys_in_sse_reg =
      _mm_cvtsi32_si128(static_cast<std::int32_t>(keys.integer));
  const auto matching_key_positions =
      _mm_cmpeq_epi8(replicated_search_key, keys_in_sse_reg);
  static constexpr auto mask = (1U << capacity) - 1;
  const auto bit_field =
      static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
  const auto i =
      static_cast<std::uint8_t>(__builtin_ffs(static_cast<int>(bit_field)) - 1);
  return std::make_pair(
      i, std::ref(
             fields.children_with_sentinel[static_cast<std::uint8_t>(i + 1)]));
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

  if (result > fields.header.node4_children_count())
    return std::make_pair(0xFF, std::ref(fields.children_with_sentinel[0]));

  return std::make_pair(result - 1,
                        std::ref(fields.children_with_sentinel[result]));
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

  [[nodiscard]] auto is_full() const noexcept { return children_count == 16; }

  [[nodiscard]] auto is_min_size() const noexcept {
    return children_count == 5;
  }

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(fields.header.type() == static_node_type);

    const auto key_byte = leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(keys.byte_array, fields.c.children,
                                           children_count, key_byte,
                                           std::move(child));
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(fields.header.type() == static_node_type);

    remove_from_sorted_key_children_arrays(keys.byte_array, fields.c.children,
                                           children_count, child_index);
  }

  [[nodiscard]] inode::find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  header_and_children<capacity> fields;
  std::uint8_t children_count;
  union {
    std::array<std::byte, capacity> byte_array;
    __m128i sse;
  } keys;

  friend class inode_4;
  friend class inode_48;
};

inode_4::inode_4(std::unique_ptr<inode_16> &&source_node,
                 uint8_t child_to_remove) noexcept
    : fields{node_type::I4, source_node->fields.header.key_prefix_length(),
             source_node->fields.header.key_prefix_data(), 4} {
  const auto source_node_children_count = source_node->children_count;

  std::copy(source_node->keys.byte_array.cbegin(),
            source_node->keys.byte_array.cbegin() + child_to_remove,
            keys.byte_array.begin());
  std::copy(source_node->keys.byte_array.cbegin() + child_to_remove + 1,
            source_node->keys.byte_array.cbegin() + source_node_children_count,
            keys.byte_array.begin() + child_to_remove);
  std::copy(source_node->fields.c.children.begin(),
            source_node->fields.c.children.begin() + child_to_remove,
            fields.c.children.begin());

  delete_node_ptr_at_scope_exit delete_on_scope_exit{
      source_node->fields.c.children[child_to_remove]};

  std::copy(source_node->fields.c.children.begin() + child_to_remove + 1,
            source_node->fields.c.children.begin() + source_node_children_count,
            fields.c.children.begin() + child_to_remove);

  assert(std::is_sorted(
      keys.byte_array.cbegin(),
      keys.byte_array.cbegin() + fields.header.node4_children_count()));
}

inode_16::inode_16(std::unique_ptr<inode_4> &&source_node,
                   leaf_unique_ptr &&child, tree_depth depth) noexcept
    : fields{node_type::I16, source_node->fields.header.key_prefix_length(),
             source_node->fields.header.key_prefix_data()},
      children_count(5) {
  const auto key_byte = leaf::key(child.get())[depth];
  const auto insert_pos_index = get_sorted_key_array_insert_position(
      source_node->keys.byte_array,
      source_node->fields.header.node4_children_count(), key_byte);
  std::copy(source_node->keys.byte_array.cbegin(),
            source_node->keys.byte_array.cbegin() + insert_pos_index,
            keys.byte_array.begin());
  keys.byte_array[insert_pos_index] = key_byte;
  std::copy(source_node->keys.byte_array.cbegin() + insert_pos_index,
            source_node->keys.byte_array.cend(),
            keys.byte_array.begin() + insert_pos_index + 1);
  std::copy(source_node->fields.c.children.begin(),
            source_node->fields.c.children.begin() + insert_pos_index,
            fields.c.children.begin());
  fields.c.children[insert_pos_index] = child.release();
  std::copy(source_node->fields.c.children.begin() + insert_pos_index,
            source_node->fields.c.children.end(),
            fields.c.children.begin() + insert_pos_index + 1);
}

inode::find_result_type inode_16::find_child(std::byte key_byte) noexcept {
  assert(fields.header.type() == static_node_type);

#if defined(__x86_64)
  const auto replicated_search_key = _mm_set1_epi8(static_cast<char>(key_byte));
  const auto matching_key_positions =
      _mm_cmpeq_epi8(replicated_search_key, keys.sse);
  const auto mask = (1U << children_count) - 1;
  const auto bit_field =
      static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
  if (bit_field != 0) {
    const auto i = static_cast<unsigned>(__builtin_ctz(bit_field));
    return std::make_pair(i, std::ref(fields.children_with_sentinel[i + 1]));
  }
  return std::make_pair(0xFF, std::ref(fields.children_with_sentinel[0]));
#else
#error Needs porting
#endif
}

void inode_16::delete_subtree() noexcept {
  for (std::uint8_t i = 0; i < children_count; ++i)
    ::delete_subtree(fields.c.children[i]);
}

void inode_16::dump(std::ostream &os) const {
  os << "# children = " << static_cast<unsigned>(children_count);
  os << ", key bytes =";
  for (std::uint8_t i = 0; i < children_count; ++i)
    dump_byte(os, keys.byte_array[i]);
  os << ", children:\n";
  for (std::uint8_t i = 0; i < children_count; ++i)
    dump_node(os, fields.c.children[i]);
}

using basic_inode_48 =
    basic_inode<17, 48, node_type::I48, inode_16, inode_256, inode_48>;

class inode_48 final : public basic_inode_48 {
 public:
  inode_48(std::unique_ptr<inode_16> &&source_node, leaf_unique_ptr &&child,
           tree_depth depth) noexcept;

  inode_48(std::unique_ptr<inode_256> &&source_node,
           uint8_t child_to_remove) noexcept;

  [[nodiscard]] auto is_full() const noexcept { return children_count == 48; }

  [[nodiscard]] auto is_min_size() const noexcept {
    return children_count == 17;
  }

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(fields.header.type() == static_node_type);

    const auto key_byte = static_cast<uint8_t>(leaf::key(child.get())[depth]);
    assert(child_indexes[key_byte] == empty_child);
    std::uint8_t i;
    node_ptr child_ptr;
    for (i = 0; i < capacity; ++i) {
      child_ptr = fields.c.children[i];
      if (child_ptr.is_sentinel()) break;
    }
    assert(child_ptr.is_sentinel());
    child_indexes[key_byte] = i;
    fields.c.children[i] = child.release();
    ++children_count;
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(fields.header.type() == static_node_type);

    remove_child_pointer(child_index);
    fields.c.children[child_indexes[child_index]] = node_ptr::sentinel_ptr;
    child_indexes[child_index] = empty_child;
    --children_count;
  }

  [[nodiscard]] inode::find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  void remove_child_pointer(std::uint8_t child_index) noexcept {
    direct_remove_child_pointer(child_indexes[child_index]);
  }

  void direct_remove_child_pointer(std::uint8_t children_i) noexcept {
    const auto child_ptr = fields.c.children[children_i];

    assert(children_i != empty_child);
    assert(!child_ptr.is_sentinel());

    delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};
  }

  header_and_children<capacity> fields;
  std::uint8_t children_count;
  std::array<std::uint8_t, 256> child_indexes;

  static constexpr std::uint8_t empty_child = 0xFF;

  friend class inode_16;
  friend class inode_256;
};

inode_16::inode_16(std::unique_ptr<inode_48> &&source_node,
                   std::uint8_t child_to_remove) noexcept
    : fields{node_type::I16, source_node->fields.header.key_prefix_length(),
             source_node->fields.header.key_prefix_data()},
      children_count{16} {
  std::uint8_t next_child = 0;
  for (unsigned i = 0; i < 256; i++) {
    const auto source_child_i = source_node->child_indexes[i];
    if (i == child_to_remove) {
      source_node->direct_remove_child_pointer(source_child_i);
      continue;
    }
    if (source_child_i != inode_48::empty_child) {
      keys.byte_array[next_child] = gsl::narrow_cast<std::byte>(i);
      const auto source_child_ptr =
          source_node->fields.c.children[source_child_i];
      assert(!source_child_ptr.is_sentinel());
      fields.c.children[next_child] = source_child_ptr;
      ++next_child;
      if (next_child == children_count) {
        if (i < child_to_remove) {
          source_node->remove_child_pointer(child_to_remove);
        }
        break;
      }
    }
  }

  assert(std::is_sorted(keys.byte_array.cbegin(),
                        keys.byte_array.cbegin() + children_count));
}

inode_48::inode_48(std::unique_ptr<inode_16> &&source_node,
                   leaf_unique_ptr &&child, tree_depth depth) noexcept
    : fields{node_type::I48, source_node->fields.header.key_prefix_length(),
             source_node->fields.header.key_prefix_data()},
      children_count{17} {
  std::memset(&child_indexes[0], empty_child,
              child_indexes.size() * sizeof(child_indexes[0]));
  std::uint8_t i;
  for (i = 0; i < inode_16::capacity; ++i) {
    const auto existing_key_byte = source_node->keys.byte_array[i];
    child_indexes[static_cast<std::uint8_t>(existing_key_byte)] = i;
    fields.c.children[i] = source_node->fields.c.children[i];
  }

  const auto key_byte =
      static_cast<std::uint8_t>(leaf::key(child.get())[depth]);
  assert(child_indexes[key_byte] == empty_child);
  child_indexes[key_byte] = i;
  fields.c.children[i] = child.release();
  for (i = children_count; i < capacity; i++) {
    fields.c.children[i] = node_ptr::sentinel_ptr;
  }
}

inode::find_result_type inode_48::find_child(std::byte key_byte) noexcept {
  assert(fields.header.type() == static_node_type);

  if (child_indexes[static_cast<std::uint8_t>(key_byte)] != empty_child) {
    const auto child_i =
        static_cast<decltype(fields.children_with_sentinel)::size_type>(
            child_indexes[static_cast<std::uint8_t>(key_byte)]);
    assert(!fields.c.children[child_i].is_sentinel());
    return std::make_pair(static_cast<std::uint8_t>(key_byte),
                          std::ref(fields.children_with_sentinel[child_i + 1]));
  }
  return std::make_pair(0xFF, std::ref(fields.children_with_sentinel[0]));
}

void inode_48::delete_subtree() noexcept {
  unsigned actual_children_count = 0;
  for (unsigned i = 0; i < capacity; ++i) {
    const auto child = fields.c.children[i];
    if (!child.is_sentinel()) {
      ++actual_children_count;
      ::delete_subtree(child);
      assert(actual_children_count <= children_count);
    }
  }
  assert(actual_children_count == children_count);
}

void inode_48::dump(std::ostream &os) const {
  os << "# children = " << static_cast<unsigned>(children_count);
  os << ", key bytes & child indexes\n";
  unsigned actual_children_count = 0;
  for (unsigned i = 0; i < 256; i++)
    if (child_indexes[i] != empty_child) {
      ++actual_children_count;
      os << " ";
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ", child index = " << static_cast<unsigned>(child_indexes[i])
         << ": ";
      assert(!fields.c.children[child_indexes[i]].is_sentinel());
      dump_node(os, fields.c.children[child_indexes[i]]);
      assert(actual_children_count <= children_count);
    }

  assert(actual_children_count == children_count);
}

using basic_inode_256 =
    basic_inode<49, 256, node_type::I256, inode_48, fake_inode, inode_256>;

class inode_256 final : public basic_inode_256 {
 public:
  inode_256(std::unique_ptr<inode_48> &&source_node, leaf_unique_ptr &&child,
            tree_depth depth) noexcept;

  [[nodiscard]] auto is_full() const noexcept {
    // cannot_happen();
    //    return children_count == 256;
    assert(0);
    return false;  // TODO(laurynas)
  }

  [[nodiscard]] auto is_min_size() const noexcept {
    return children_count == 49;
  }

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(fields.header.type() == static_node_type);
    assert(!is_full());

    const auto key_byte =
        static_cast<std::uint8_t>(leaf::key(child.get())[depth]);
    assert(fields.c.children[key_byte].is_sentinel());
    fields.c.children[key_byte] = child.release();
    ++children_count;
  }

  void remove(std::uint8_t child_index) noexcept {
    const auto child_ptr = fields.c.children[child_index];

    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    assert(!child_ptr.is_sentinel());

    delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};

    fields.c.children[child_index] = node_ptr::sentinel_ptr;
    --children_count;
  }

  [[nodiscard]] inode::find_result_type find_child(std::byte key_byte) noexcept;

  template <typename Function>
  void for_each_child(Function func) noexcept(
      noexcept(func(0, node_ptr::sentinel_ptr)));

  template <typename Function>
  void for_each_child(Function func) const
      noexcept(noexcept(func(0, node_ptr::sentinel_ptr)));

  void delete_subtree() noexcept;

  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  header_and_children<capacity> fields;
  std::uint8_t children_count;

  friend class inode_48;
};

inode_48::inode_48(std::unique_ptr<inode_256> &&source_node,
                   std::uint8_t child_to_remove) noexcept
    : fields{node_type::I48, source_node->fields.header.key_prefix_length(),
             source_node->fields.header.key_prefix_data()},
      children_count{48} {
  std::uint8_t next_child = 0;
  unsigned child_i = 0;
  for (; child_i < 256; child_i++) {
    const auto child_ptr = source_node->fields.c.children[child_i];
    if (child_i == child_to_remove) {
      assert(!child_ptr.is_sentinel());
      delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};
      child_indexes[child_i] = empty_child;
      continue;
    }
    if (child_ptr.is_sentinel()) {
      child_indexes[child_i] = empty_child;
      continue;
    }
    child_indexes[child_i] = next_child;
    fields.c.children[next_child] = source_node->fields.c.children[child_i];
    ++next_child;
    if (next_child == children_count) {
      if (child_i < child_to_remove) {
        const auto child_to_remove_ptr =
            source_node->fields.c.children[child_to_remove];
        assert(!child_to_remove_ptr.is_sentinel());
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
    : fields{node_type::I256, source_node->fields.header.key_prefix_length(),
             source_node->fields.header.key_prefix_data()},
      children_count{49} {
  for (unsigned i = 0; i < 256; i++) {
    const auto children_i = source_node->child_indexes[i];
    fields.c.children[i] = children_i == inode_48::empty_child
                               ? node_ptr::sentinel_ptr
                               : source_node->fields.c.children[children_i];
  }

  const auto key_byte = static_cast<uint8_t>(leaf::key(child.get())[depth]);
  assert(fields.c.children[key_byte].is_sentinel());
  fields.c.children[key_byte] = child.release();
}

inode::find_result_type inode_256::find_child(std::byte key_byte) noexcept {
  assert(fields.header.type() == static_node_type);

  const auto key_int_byte = static_cast<std::uint8_t>(key_byte);
  if (!fields.c.children[key_int_byte].is_sentinel())
    return std::make_pair(
        key_int_byte,
        std::ref(
            fields.children_with_sentinel
                [static_cast<decltype(
                     fields.children_with_sentinel)::size_type>(key_int_byte) +
                 1]));
  return std::make_pair(0xFF, std::ref(fields.children_with_sentinel[0]));
}

template <typename Function>
void inode_256::for_each_child(Function func) noexcept(
    noexcept(func(0, node_ptr::sentinel_ptr))) {
  std::uint8_t actual_children_count = 0;
  for (unsigned i = 0; i < 256; ++i) {
    const auto child_ptr = fields.c.children[i];
    if (!child_ptr.is_sentinel()) {
      ++actual_children_count;
      func(i, child_ptr);
      assert(actual_children_count <= children_count || children_count == 0);
    }
  }
  assert(actual_children_count == children_count);
}

template <typename Function>
void inode_256::for_each_child(Function func) const
    noexcept(noexcept(func(0, node_ptr::sentinel_ptr))) {
  const_cast<inode_256 *>(this)->for_each_child(func);
}

void inode_256::delete_subtree() noexcept {
  for_each_child(
      [](unsigned, node_ptr child) noexcept { ::delete_subtree(child); });
}

void inode_256::dump(std::ostream &os) const {
  // TODO(laurynas): 0 == 256
  os << "# children = " << static_cast<unsigned>(children_count);
  os << ", key bytes & children:\n";
  for_each_child([&](unsigned i, node_ptr child) noexcept {
    os << ' ';
    dump_byte(os, gsl::narrow_cast<std::byte>(i));
    os << ' ';
    dump_node(os, child);
  });
}

inline bool inode::is_full() const noexcept {
  switch (header_.type()) {
    case node_type::I4:
      // TODO(laurynas): union?
      return reinterpret_cast<const inode_4 *>(this)->is_full();
    case node_type::I16:
      return reinterpret_cast<const inode_16 *>(this)->is_full();
    case node_type::I48:
      return reinterpret_cast<const inode_48 *>(this)->is_full();
    case node_type::I256:
      return reinterpret_cast<const inode_256 *>(this)->is_full();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

inline bool inode::is_min_size() const noexcept {
  switch (header_.type()) {
    case node_type::I4:
      return reinterpret_cast<const inode_4 *>(this)->is_min_size();
    case node_type::I16:
      return reinterpret_cast<const inode_16 *>(this)->is_min_size();
    case node_type::I48:
      return reinterpret_cast<const inode_48 *>(this)->is_min_size();
    case node_type::I256:
      return reinterpret_cast<const inode_256 *>(this)->is_min_size();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

inline void inode::add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
  assert(!is_full());
  assert(child.get() != nullptr);

  switch (header_.type()) {
    case node_type::I4:
      reinterpret_cast<inode_4 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I16:
      reinterpret_cast<inode_16 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I48:
      reinterpret_cast<inode_48 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I256:
      reinterpret_cast<inode_256 *>(this)->add(std::move(child), depth);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

inline void inode::remove(std::uint8_t child_index) noexcept {
  assert(!is_min_size());

  switch (header_.type()) {
    case node_type::I4:
      reinterpret_cast<inode_4 *>(this)->remove(child_index);
      break;
    case node_type::I16:
      reinterpret_cast<inode_16 *>(this)->remove(child_index);
      break;
    case node_type::I48:
      reinterpret_cast<inode_48 *>(this)->remove(child_index);
      break;
    case node_type::I256:
      reinterpret_cast<inode_256 *>(this)->remove(child_index);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

inline inode::find_result_type inode::find_child(std::byte key_byte) noexcept {
  switch (header_.type()) {
    case node_type::I4:
      return reinterpret_cast<inode_4 *>(this)->find_child(key_byte);
    case node_type::I16:
      return reinterpret_cast<inode_16 *>(this)->find_child(key_byte);
    case node_type::I48:
      return reinterpret_cast<inode_48 *>(this)->find_child(key_byte);
    case node_type::I256:
      return reinterpret_cast<inode_256 *>(this)->find_child(key_byte);
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

void inode::delete_subtree() noexcept {
  switch (header_.type()) {
    case node_type::I4:
      return reinterpret_cast<inode_4 *>(this)->delete_subtree();
    case node_type::I16:
      return reinterpret_cast<inode_16 *>(this)->delete_subtree();
    case node_type::I48:
      return reinterpret_cast<inode_48 *>(this)->delete_subtree();
    case node_type::I256:
      return reinterpret_cast<inode_256 *>(this)->delete_subtree();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

void inode::dump(std::ostream &os) const {
  header_.dump(os);
  switch (header_.type()) {
    case node_type::I4:
      reinterpret_cast<const inode_4 *>(this)->dump(os);
      break;
    case node_type::I16:
      reinterpret_cast<const inode_16 *>(this)->dump(os);
      break;
    case node_type::I48:
      reinterpret_cast<const inode_48 *>(this)->dump(os);
      break;
    case node_type::I256:
      reinterpret_cast<const inode_256 *>(this)->dump(os);
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

get_result db::get(key k) const noexcept {
  if (unlikely(root.is_sentinel())) return {};
  return get_from_subtree(root, detail::art_key{k}, detail::tree_depth{});
}

get_result db::get_from_subtree(detail::node_ptr node, detail::art_key k,
                                detail::tree_depth depth) noexcept {
  if (node.type() == detail::node_type::LEAF) {
    if (detail::leaf::matches(node.leaf, k)) {
      const auto value = detail::leaf::value(node.leaf);
      return value;
    }
    return {};
  }

  assert(node.type() != detail::node_type::LEAF);
  assert(depth < detail::art_key::size);

  if (node.internal->header().get_shared_key_prefix_length(k, depth) <
      node.internal->header().key_prefix_length())
    return {};
  depth += node.internal->header().key_prefix_length();
  const auto child = node.internal->find_child(k[depth]).second;
  if (child.is_sentinel()) return {};

  return get_from_subtree(child, k, ++depth);
}

bool db::insert(key k, value_view v) {
  const auto bin_comparable_key = detail::art_key{k};
  if (unlikely(root.is_sentinel())) {
    auto leaf = detail::leaf::create(bin_comparable_key, v, *this);
    root = leaf.release();
    return true;
  }
  return insert_to_subtree(bin_comparable_key, root, v, detail::tree_depth{});
}

bool db::insert_to_subtree(detail::art_key k, detail::node_ptr &node,
                           value_view v, detail::tree_depth depth) {
  if (node.type() == detail::node_type::LEAF) {
    const auto existing_key = detail::leaf::key(node.leaf);
    if (unlikely(k == existing_key)) return false;
    detail::raii_leaf_creator leaf_creator{k, v, *this};
    auto leaf = leaf_creator.get();
    increase_memory_use(sizeof(detail::inode_4));
    // TODO(laurynas): try to pass leaf node type instead of generic node below.
    // This way it would be apparent that its key prefix does not need updating
    // as leaves don't have any.
    auto new_node =
        detail::inode_4::create(existing_key, k, depth, node, std::move(leaf));
    node = new_node.release();
    ++inode4_count;
    ++created_inode4_count;
    assert(created_inode4_count >= inode4_count);
    return true;
  }

  assert(node.type() != detail::node_type::LEAF);
  assert(depth < detail::art_key::size);

  const auto shared_prefix_len =
      node.internal->header().get_shared_key_prefix_length(k, depth);
  if (shared_prefix_len < node.internal->header().key_prefix_length()) {
    detail::raii_leaf_creator leaf_creator{k, v, *this};
    auto leaf = leaf_creator.get();
    increase_memory_use(sizeof(detail::inode_4));
    auto new_node = detail::inode_4::create(node, shared_prefix_len, depth,
                                            std::move(leaf));
    node = new_node.release();
    ++inode4_count;
    ++created_inode4_count;
    ++key_prefix_splits;
    assert(created_inode4_count >= inode4_count);
    assert(created_inode4_count > key_prefix_splits);
    return true;
  }

  //  assert(shared_prefix_len == node.internal->header().key_prefix_length());
  depth += node.internal->header().key_prefix_length();

  auto &child = node.internal->find_child(k[depth]).second;

  if (!child.is_sentinel()) return insert_to_subtree(k, child, v, ++depth);

  detail::raii_leaf_creator leaf_creator{k, v, *this};
  auto leaf = leaf_creator.get();

  const auto node_is_full = node.internal->is_full();

  if (likely(!node_is_full)) {
    node.internal->add(std::move(leaf), depth);
    return true;
  }

  assert(node_is_full);

  if (node.type() == detail::node_type::I4) {
    assert(inode4_count > 0);

    increase_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4));
    std::unique_ptr<detail::inode_4> current_node{node.node_4};
    auto larger_node = detail::inode_16::create(std::move(current_node),
                                                std::move(leaf), depth);
    node = larger_node.release();

    --inode4_count;
    ++inode16_count;
    ++inode4_to_inode16_count;
    assert(inode4_to_inode16_count >= inode16_count);

  } else if (node.type() == detail::node_type::I16) {
    assert(inode16_count > 0);

    std::unique_ptr<detail::inode_16> current_node{node.node_16};
    increase_memory_use(sizeof(detail::inode_48) - sizeof(detail::inode_16));
    auto larger_node = detail::inode_48::create(std::move(current_node),
                                                std::move(leaf), depth);
    node = larger_node.release();

    --inode16_count;
    ++inode48_count;
    ++inode16_to_inode48_count;
    assert(inode16_to_inode48_count >= inode48_count);

  } else {
    assert(inode48_count > 0);

    assert(node.type() == detail::node_type::I48);
    std::unique_ptr<detail::inode_48> current_node{node.node_48};
    increase_memory_use(sizeof(detail::inode_256) - sizeof(detail::inode_48));
    auto larger_node = detail::inode_256::create(std::move(current_node),
                                                 std::move(leaf), depth);
    node = larger_node.release();

    --inode48_count;
    ++inode256_count;
    ++inode48_to_inode256_count;
    assert(inode48_to_inode256_count >= inode256_count);
  }
  return true;
}

bool db::remove(key k) {
  const auto bin_comparable_key = detail::art_key{k};
  if (unlikely(root.is_sentinel())) return false;
  if (root.type() == detail::node_type::LEAF) {
    if (detail::leaf::matches(root.leaf, bin_comparable_key)) {
      const auto leaf_size = detail::leaf::size(root.leaf);
      detail::leaf_unique_ptr root_leaf_deleter{root.leaf};
      root = detail::node_ptr::sentinel_ptr;
      decrease_memory_use(leaf_size);
      assert(leaf_count > 0);
      --leaf_count;
      return true;
    }
    return false;
  }
  return remove_from_subtree(bin_comparable_key, detail::tree_depth{}, root);
}

bool db::remove_from_subtree(detail::art_key k, detail::tree_depth depth,
                             detail::node_ptr &node) {
  assert(node.type() != detail::node_type::LEAF);
  assert(depth < detail::art_key::size);

  const auto shared_prefix_len =
      node.internal->header().get_shared_key_prefix_length(k, depth);
  if (shared_prefix_len < node.internal->header().key_prefix_length())
    return false;

  // assert(shared_prefix_len == node.internal->header().key_prefix_length());
  depth += node.internal->header().key_prefix_length();

  const auto [child_i, child_ref] = node.internal->find_child(k[depth]);

  if (child_ref.is_sentinel()) return false;

  if (child_ref.type() != detail::node_type::LEAF)
    return remove_from_subtree(k, ++depth, child_ref);

  if (!detail::leaf::matches(child_ref.leaf, k)) return false;

  assert(leaf_count > 0);

  const auto is_node_min_size = node.internal->is_min_size();
  const auto child_node_size = detail::leaf::size(child_ref.leaf);

  if (likely(!is_node_min_size)) {
    node.internal->remove(child_i);
    decrease_memory_use(child_node_size);
    --leaf_count;
    return true;
  }

  assert(is_node_min_size);

  if (node.type() == detail::node_type::I4) {
    std::unique_ptr<detail::inode_4> current_node{node.node_4};
    node = current_node->leave_last_child(child_i);
    decrease_memory_use(child_node_size + sizeof(detail::inode_4));

    assert(inode4_count > 0);
    --inode4_count;
    ++deleted_inode4_count;
    assert(deleted_inode4_count <= created_inode4_count);

  } else if (node.type() == detail::node_type::I16) {
    std::unique_ptr<detail::inode_16> current_node{node.node_16};
    auto new_node{detail::inode_4::create(std::move(current_node), child_i)};
    node = new_node.release();
    decrease_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4) +
                        child_node_size);

    assert(inode16_count > 0);
    --inode16_count;
    ++inode4_count;
    ++inode16_to_inode4_count;
    assert(inode16_to_inode4_count <= inode4_to_inode16_count);

  } else if (node.type() == detail::node_type::I48) {
    std::unique_ptr<detail::inode_48> current_node{node.node_48};
    auto new_node{detail::inode_16::create(std::move(current_node), child_i)};
    node = new_node.release();
    decrease_memory_use(sizeof(detail::inode_48) - sizeof(detail::inode_16) +
                        child_node_size);

    assert(inode48_count > 0);
    --inode48_count;
    ++inode16_count;
    ++inode48_to_inode16_count;
    assert(inode48_to_inode16_count <= inode16_to_inode48_count);

  } else {
    assert(node.type() == detail::node_type::I256);
    std::unique_ptr<detail::inode_256> current_node{node.node_256};
    auto new_node{detail::inode_48::create(std::move(current_node), child_i)};
    node = new_node.release();
    decrease_memory_use(sizeof(detail::inode_256) - sizeof(detail::inode_48) +
                        child_node_size);

    assert(inode256_count > 0);
    --inode256_count;
    ++inode48_count;
    ++inode256_to_inode48_count;
    assert(inode256_to_inode48_count <= inode48_to_inode256_count);
  }

  --leaf_count;
  return true;
}

void db::clear() {
  ::delete_subtree(root);
  root = detail::node_ptr::sentinel_ptr;
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
