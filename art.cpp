// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include "art.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#ifdef __x86_64
#include <emmintrin.h>
#endif
#include <limits>
#include <stdexcept>
#include <utility>

#ifndef NDEBUG
#include <iomanip>
#include <iostream>
#endif

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
[[nodiscard]] inline unodb::detail::pmr_pool_options get_inode_pool_options();

[[nodiscard]] inline auto &get_leaf_node_pool() {
  return *unodb::detail::pmr_new_delete_resource();
}

// For internal node pools, approximate requesting ~2MB blocks from backing
// storage (when ported to Linux, ask for 2MB huge pages directly)
template <class InternalNode>
[[nodiscard]] inline unodb::detail::pmr_pool_options get_inode_pool_options() {
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

#ifndef NDEBUG

void dump_byte(std::ostream &os, std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

void dump_key(std::ostream &os, unodb::detail::art_key key) {
  for (std::size_t i = 0; i < sizeof(key); i++) dump_byte(os, key[i]);
}

void dump_node(std::ostream &os, const unodb::detail::node_ptr &node);

#endif

inline __attribute__((noreturn)) void cannot_happen() {
  assert(0);
  __builtin_unreachable();
}

class key_prefix final {
 public:
  using size_type = std::uint8_t;

 private:
  static constexpr size_type capacity = 7;

 public:
  using data_type = std::array<std::byte, capacity>;

 private:
  size_type length_;
  data_type data_;

 public:
  [[nodiscard]] auto length() const noexcept {
    assert(length_ <= capacity);
    return length_;
  }

  key_prefix(unodb::detail::art_key k1, unodb::detail::art_key k2,
             unodb::detail::tree_depth depth) noexcept {
    assert(k1 != k2);
    assert(depth < sizeof(unodb::detail::art_key));

    unodb::detail::tree_depth i;
    for (i = depth; k1[i] == k2[i]; ++i) {
      assert(i - depth < capacity);
      data_[i - depth] = k1[i];
    }
    assert(i - depth <= capacity);
    length_ = gsl::narrow_cast<size_type>(i - depth);
  }

  key_prefix(const key_prefix &other) noexcept : length_{other.length()} {
    std::copy(other.data_.cbegin(), other.data_.cbegin() + length_,
              data_.begin());
  }

  key_prefix(const data_type &other, size_type other_len) noexcept
      : length_{other_len} {
    assert(other_len <= capacity);

    std::copy(other.cbegin(), other.cbegin() + other_len, data_.begin());
  }

  key_prefix(key_prefix &&other) noexcept = delete;

  key_prefix &operator=(const key_prefix &) = delete;
  key_prefix &operator=(key_prefix &&) = delete;

  __attribute__((unused)) ~key_prefix() = default;

  void cut(unsigned cut_len) noexcept {
    assert(cut_len > 0);
    assert(cut_len <= length());

    std::copy(data_.cbegin() + cut_len, data_.cend(), data_.begin());
    length_ = gsl::narrow_cast<size_type>(length() - cut_len);
  }

  void prepend(const key_prefix &prefix1, std::byte prefix2) noexcept {
    assert(length() + prefix1.length() < capacity);

    std::copy_backward(data_.cbegin(), data_.cbegin() + length(),
                       data_.begin() + length() + prefix1.length() + 1);
    std::copy(prefix1.data_.cbegin(), prefix1.data_.cbegin() + prefix1.length(),
              data_.begin());
    data_[prefix1.length()] = prefix2;
    length_ = gsl::narrow_cast<size_type>(length() + prefix1.length() + 1);
  }

  [[nodiscard]] const auto &data() const noexcept { return data_; }

  [[nodiscard]] auto operator[](std::size_t i) const noexcept {
    assert(i < length());

    return data_[i];
  }

  DISABLE_GCC_WARNING("-Wsuggest-attribute=pure")
  [[nodiscard]] auto get_shared_length(unodb::detail::art_key k,
                                       unodb::detail::tree_depth depth) const
      noexcept {
    auto key_i = depth;
    unsigned shared_length = 0;
    while (shared_length < length()) {
      if (k[key_i] != data_[(gsl::narrow_cast<size_type>(shared_length))])
        break;
      ++key_i;
      ++shared_length;
    }
    return shared_length;
  }
  RESTORE_GCC_WARNINGS()

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif
};

static_assert(std::is_standard_layout_v<key_prefix>);

#ifndef NDEBUG

void key_prefix::dump(std::ostream &os) const {
  const auto len = length();
  os << ", key prefix len = " << static_cast<unsigned>(len);
  os << ", key prefix =";
  for (std::size_t i = 0; i < len; ++i) dump_byte(os, data_[i]);
}

#endif

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

#ifndef NDEBUG
  static void dump(std::ostream &os, raw_leaf_ptr leaf);
#endif
};

static_assert(std::is_standard_layout_v<leaf>,
              "leaf must be standard layout type to support aliasing through "
              "node_header");

leaf_unique_ptr leaf::create(art_key k, value_view v, db &db_instance) {
  if (v.size() > std::numeric_limits<value_size_type>::max()) {
    throw std::length_error("Value length must fit in uint32_t");
  }
  const auto value_size = static_cast<value_size_type>(v.size());
  const auto leaf_size = static_cast<std::size_t>(offset_value) + value_size;
  db_instance.increase_memory_use(leaf_size);

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

#ifndef NDEBUG

void leaf::dump(std::ostream &os, raw_leaf_ptr leaf) {
  os << "LEAF: key:";
  dump_key(os, key(leaf));
  os << ", value size: " << value_size(leaf) << '\n';
}

#endif

void leaf_deleter::operator()(unodb::detail::raw_leaf_ptr to_delete) const
    noexcept {
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
 public:
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

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

  // inode must not be allocated directly on heap
  [[nodiscard]] static void *operator new(std::size_t) { cannot_happen(); }

  DISABLE_CLANG_WARNING("-Wmissing-noreturn")
  static void operator delete(void *) { cannot_happen(); }
  RESTORE_CLANG_WARNINGS()

 protected:
  inode(node_type type, std::uint8_t children_count_, art_key k1, art_key k2,
        tree_depth depth) noexcept
      : header{type},
        node_key_prefix{k1, k2, depth},
        children_count{children_count_} {
    assert(type != node_type::LEAF);
    assert(k1 != k2);
  }

  inode(node_type type, std::uint8_t children_count_,
        key_prefix::size_type key_prefix_len_,
        const key_prefix::data_type &key_prefix_) noexcept
      : header{type},
        node_key_prefix{key_prefix_, key_prefix_len_},
        children_count{children_count_} {
    assert(type != node_type::LEAF);
  }

  inode(node_type type, std::uint8_t children_count_,
        const key_prefix &key_prefix_) noexcept
      : header{type},
        node_key_prefix{key_prefix_},
        children_count{children_count_} {
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
    --children_count;

    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
  }

  const node_header header;
  key_prefix node_key_prefix;

  uint8_t children_count;

  template <unsigned, unsigned, node_type, class, class, class>
  friend class basic_inode;
  friend class inode_4;
  friend class inode_16;
  friend class inode_48;
  friend class inode_256;
  friend class unodb::db;
};

}  // namespace unodb::detail

namespace {

void delete_subtree(unodb::detail::node_ptr node) noexcept {
  if (node.header == nullptr) return;

  delete_node_ptr_at_scope_exit delete_on_scope_exit(node);

  if (node.type() != unodb::detail::node_type::LEAF)
    delete_on_scope_exit.internal->delete_subtree();
}

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

    return children_count == capacity;
  }

  [[nodiscard]] auto is_min_size() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() == NodeType);

    return children_count == min_size;
  }

 protected:
  basic_inode(art_key k1, art_key k2, tree_depth depth) noexcept
      : inode{NodeType, MinSize, k1, k2, depth} {
    assert(is_min_size());
  }

  basic_inode(key_prefix::size_type key_prefix_len,
              const key_prefix::data_type &key_prefix_) noexcept
      : inode{NodeType, MinSize, key_prefix_len, key_prefix_} {
    assert(is_min_size());
  }

  explicit basic_inode(const SmallerDerived &source_node) noexcept
      : inode{NodeType, MinSize, source_node.node_key_prefix} {
    assert(source_node.is_full());
    assert(is_min_size());
  }

  explicit basic_inode(const LargerDerived &source_node) noexcept
      : inode{NodeType, Capacity, source_node.node_key_prefix} {
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

  void add(leaf_unique_ptr &&child, tree_depth depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    const auto key_byte = leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(keys, children, children_count,
                                           key_byte, std::move(child));
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    remove_from_sorted_key_children_arrays(keys, children, children_count,
                                           child_index);
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
      child_to_leave_ptr.internal->node_key_prefix.prepend(
          node_key_prefix, keys[child_to_leave]);
    }
    return child_to_leave_ptr;
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  friend class inode_16;

  void add_two_to_empty(std::byte key1, node_ptr child1, std::byte key2,
                        leaf_unique_ptr &&child2) noexcept;

  std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;
};

inode_4::inode_4(art_key k1, art_key k2, tree_depth depth, node_ptr child1,
                 leaf_unique_ptr &&child2) noexcept
    : basic_inode_4{k1, k2, depth} {
  const auto next_level_depth = depth + node_key_prefix.length();
  add_two_to_empty(k1[next_level_depth], child1, k2[next_level_depth],
                   std::move(child2));
}

inode_4::inode_4(node_ptr source_node, unsigned len, tree_depth depth,
                 leaf_unique_ptr &&child1) noexcept
    : basic_inode_4{gsl::narrow_cast<key_prefix::size_type>(len),
                    source_node.internal->node_key_prefix.data()} {
  assert(source_node.type() != node_type::LEAF);
  assert(len < source_node.internal->node_key_prefix.length());

  const auto source_node_key_byte =
      source_node.internal
          ->node_key_prefix[gsl::narrow_cast<key_prefix::size_type>(len)];
  source_node.internal->node_key_prefix.cut(len + 1);
  const auto new_key_byte = leaf::key(child1.get())[depth + len];
  add_two_to_empty(source_node_key_byte, source_node, new_key_byte,
                   std::move(child1));
}

void inode_4::add_two_to_empty(std::byte key1, node_ptr child1, std::byte key2,
                               leaf_unique_ptr &&child2) noexcept {
  assert(key1 != key2);
  assert(children_count == 2);

  const std::uint8_t key1_i = key1 < key2 ? 0 : 1;
  const std::uint8_t key2_i = key1_i == 0 ? 1 : 0;
  keys[key1_i] = key1;
  children[key1_i] = child1;
  keys[key2_i] = key2;
  children[key2_i] = child2.release();

  assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
}

void inode_4::delete_subtree() noexcept {
  for (std::uint8_t i = 0; i < children_count; ++i)
    ::delete_subtree(children[i]);
}

#ifndef NDEBUG

void inode_4::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (std::uint8_t i = 0; i < children_count; ++i) dump_byte(os, keys[i]);
  os << ", children:\n";
  for (std::uint8_t i = 0; i < children_count; ++i) dump_node(os, children[i]);
}

#endif

inode::find_result_type inode_4::find_child(std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

  for (unsigned i = 0; i < children_count; ++i)
    if (keys[i] == key_byte) return std::make_pair(i, &children[i]);
  return std::make_pair(0xFF, nullptr);
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
    insert_into_sorted_key_children_arrays(
        keys.byte_array, children, children_count, key_byte, std::move(child));
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    remove_from_sorted_key_children_arrays(keys.byte_array, children,
                                           children_count, child_index);
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
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
  const auto source_node_children_count = source_node->children_count;

  std::copy(source_node->keys.byte_array.cbegin(),
            source_node->keys.byte_array.cbegin() + child_to_remove,
            keys.begin());
  std::copy(source_node->keys.byte_array.cbegin() + child_to_remove + 1,
            source_node->keys.byte_array.cbegin() + source_node_children_count,
            keys.begin() + child_to_remove);
  std::copy(source_node->children.begin(),
            source_node->children.begin() + child_to_remove, children.begin());

  delete_node_ptr_at_scope_exit delete_on_scope_exit{
      source_node->children[child_to_remove]};

  std::copy(source_node->children.begin() + child_to_remove + 1,
            source_node->children.begin() + source_node_children_count,
            children.begin() + child_to_remove);

  assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
}

inode_16::inode_16(std::unique_ptr<inode_4> &&source_node,
                   leaf_unique_ptr &&child, tree_depth depth) noexcept
    : basic_inode_16{*source_node} {
  const auto key_byte = leaf::key(child.get())[depth];
  const auto insert_pos_index = get_sorted_key_array_insert_position(
      source_node->keys, source_node->children_count, key_byte);
  std::copy(source_node->keys.cbegin(),
            source_node->keys.cbegin() + insert_pos_index,
            keys.byte_array.begin());
  keys.byte_array[insert_pos_index] = key_byte;
  std::copy(source_node->keys.cbegin() + insert_pos_index,
            source_node->keys.cend(),
            keys.byte_array.begin() + insert_pos_index + 1);
  std::copy(source_node->children.begin(),
            source_node->children.begin() + insert_pos_index, children.begin());
  children[insert_pos_index] = child.release();
  std::copy(source_node->children.begin() + insert_pos_index,
            source_node->children.end(),
            children.begin() + insert_pos_index + 1);
}

inode::find_result_type inode_16::find_child(std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

#if defined(__x86_64)
  const auto replicated_search_key = _mm_set1_epi8(static_cast<char>(key_byte));
  const auto matching_key_positions =
      _mm_cmpeq_epi8(replicated_search_key, keys.sse);
  const auto mask = (1U << children_count) - 1;
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
  for (std::uint8_t i = 0; i < children_count; ++i)
    ::delete_subtree(children[i]);
}

#ifndef NDEBUG

void inode_16::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (std::uint8_t i = 0; i < children_count; ++i)
    dump_byte(os, keys.byte_array[i]);
  os << ", children:\n";
  for (std::uint8_t i = 0; i < children_count; ++i) dump_node(os, children[i]);
}

#endif

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
    ++children_count;
  }

  void remove(std::uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);

    remove_child_pointer(child_index);
    children[child_indexes[child_index]] = nullptr;
    child_indexes[child_index] = empty_child;
    --children_count;
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  void delete_subtree() noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

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
  for (i = children_count; i < capacity; i++) {
    children[i] = nullptr;
  }
}

inode::find_result_type inode_48::find_child(std::byte key_byte) noexcept {
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
      assert(actual_children_count <= children_count);
    }
  }
  assert(actual_children_count == children_count);
}

#ifndef NDEBUG

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
      assert(actual_children_count <= children_count);
    }

  assert(actual_children_count == children_count);
}

#endif

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
    ++children_count;
  }

  void remove(std::uint8_t child_index) noexcept {
    const auto child_ptr = children[child_index];

    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    assert(child_ptr != nullptr);

    delete_node_ptr_at_scope_exit delete_on_scope_exit{child_ptr};

    children[child_index] = nullptr;
    --children_count;
  }

  [[nodiscard]] find_result_type find_child(std::byte key_byte) noexcept;

  template <typename Function>
  void for_each_child(Function func) noexcept(noexcept(func(0, nullptr)));

  template <typename Function>
  void for_each_child(Function func) const noexcept(noexcept(func(0, nullptr)));

  void delete_subtree() noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

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
    if (next_child == children_count) {
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

inode::find_result_type inode_256::find_child(std::byte key_byte) noexcept {
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
      assert(actual_children_count <= children_count || children_count == 0);
    }
  }
  assert(actual_children_count == children_count);
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

#ifndef NDEBUG

void inode_256::dump(std::ostream &os) const {
  os << ", key bytes & children:\n";
  for_each_child([&](unsigned i, node_ptr child) noexcept {
    os << ' ';
    dump_byte(os, gsl::narrow_cast<std::byte>(i));
    os << ' ';
    dump_node(os, child);
  });
}

#endif

inline bool inode::is_full() const noexcept {
  switch (header.type()) {
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
  switch (header.type()) {
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

  switch (header.type()) {
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

  switch (header.type()) {
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
  switch (header.type()) {
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
  switch (header.type()) {
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

#ifndef NDEBUG

void inode::dump(std::ostream &os) const {
  switch (header.type()) {
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
     << (children_count == 0 ? 256 : static_cast<unsigned>(children_count));
  node_key_prefix.dump(os);
  switch (header.type()) {
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

#endif

class raii_leaf_creator {
 public:
  raii_leaf_creator(art_key k, unodb::value_view v, unodb::db &db_instance_)
      : leaf{leaf::create(k, v, db_instance_)},
        leaf_size{leaf::size(leaf.get())},
        db_instance{db_instance_},
        exceptions_at_ctor{std::uncaught_exceptions()} {}

  raii_leaf_creator(const raii_leaf_creator &) = delete;
  raii_leaf_creator(raii_leaf_creator &&) = delete;

  auto &operator=(const raii_leaf_creator &) = delete;
  auto &operator=(raii_leaf_creator &&) = delete;

  ~raii_leaf_creator() noexcept {
    assert(get_called);

    if (likely(exceptions_at_ctor == std::uncaught_exceptions())) return;
    db_instance.decrease_memory_use(leaf_size);
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
  const int exceptions_at_ctor;
#ifndef NDEBUG
  bool get_called{false};
#endif
};

}  // namespace detail

db::~db() noexcept { ::delete_subtree(root); }

get_result db::get(key k) const noexcept {
  if (unlikely(root.header == nullptr)) return {};
  return get_from_subtree(root, detail::art_key{k}, 0);
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
  if (node.internal->node_key_prefix.get_shared_length(k, depth) <
      node.internal->node_key_prefix.length())
    return {};
  depth += node.internal->node_key_prefix.length();
  auto *const child = node.internal->find_child(k[depth]).second;
  if (child == nullptr) return {};
  return get_from_subtree(*child, k, depth + 1);
}

bool db::insert(key k, value_view v) {
  const auto bin_comparable_key = detail::art_key{k};
  if (unlikely(root.header == nullptr)) {
    auto leaf = detail::leaf::create(bin_comparable_key, v, *this);
    root = leaf.release();
    return true;
  }
  return insert_to_subtree(bin_comparable_key, &root, v, 0);
}

bool db::insert_to_subtree(detail::art_key k, detail::node_ptr *node,
                           value_view v, detail::tree_depth depth) {
  if (node->type() == detail::node_type::LEAF) {
    const auto existing_key = detail::leaf::key(node->leaf);
    if (unlikely(k == existing_key)) return false;
    detail::raii_leaf_creator leaf_creator{k, v, *this};
    auto leaf = leaf_creator.get();
    increase_memory_use(sizeof(detail::inode_4));
    auto new_node =
        detail::inode_4::create(existing_key, k, depth, *node, std::move(leaf));
    *node = new_node.release();
    return true;
  }

  assert(node->type() != detail::node_type::LEAF);

  const auto shared_prefix_len =
      node->internal->node_key_prefix.get_shared_length(k, depth);
  if (shared_prefix_len < node->internal->node_key_prefix.length()) {
    detail::raii_leaf_creator leaf_creator{k, v, *this};
    auto leaf = leaf_creator.get();
    increase_memory_use(sizeof(detail::inode_4));
    auto new_node = detail::inode_4::create(*node, shared_prefix_len, depth,
                                            std::move(leaf));
    *node = new_node.release();
    return true;
  }
  depth += node->internal->node_key_prefix.length();

  auto *const child = node->internal->find_child(k[depth]).second;

  if (child != nullptr) return insert_to_subtree(k, child, v, depth + 1);

  detail::raii_leaf_creator leaf_creator{k, v, *this};
  auto leaf = leaf_creator.get();

  const auto node_is_full = node->internal->is_full();

  if (likely(!node_is_full)) {
    node->internal->add(std::move(leaf), depth);
    return true;
  }

  assert(node_is_full);

  if (node->type() == detail::node_type::I4) {
    increase_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4));
    std::unique_ptr<detail::inode_4> current_node{node->node_4};
    auto larger_node = detail::inode_16::create(std::move(current_node),
                                                std::move(leaf), depth);
    *node = larger_node.release();

  } else if (node->type() == detail::node_type::I16) {
    std::unique_ptr<detail::inode_16> current_node{node->node_16};
    increase_memory_use(sizeof(detail::inode_48) - sizeof(detail::inode_16));
    auto larger_node = detail::inode_48::create(std::move(current_node),
                                                std::move(leaf), depth);
    *node = larger_node.release();

  } else {
    assert(node->type() == detail::node_type::I48);
    std::unique_ptr<detail::inode_48> current_node{node->node_48};
    increase_memory_use(sizeof(detail::inode_256) - sizeof(detail::inode_48));
    auto larger_node = detail::inode_256::create(std::move(current_node),
                                                 std::move(leaf), depth);
    *node = larger_node.release();
  }
  return true;
}

bool db::remove(key k) {
  const auto bin_comparable_key = detail::art_key{k};
  if (unlikely(root == nullptr)) return false;
  if (root.type() == detail::node_type::LEAF) {
    if (detail::leaf::matches(root.leaf, bin_comparable_key)) {
      const auto leaf_size = detail::leaf::size(root.leaf);
      detail::leaf_unique_ptr root_leaf_deleter{root.leaf};
      root = nullptr;
      decrease_memory_use(leaf_size);
      return true;
    }
    return false;
  }
  return remove_from_subtree(bin_comparable_key, 0, &root);
}

bool db::remove_from_subtree(detail::art_key k, detail::tree_depth depth,
                             detail::node_ptr *node) {
  assert(node->type() != detail::node_type::LEAF);

  const auto shared_prefix_len =
      node->internal->node_key_prefix.get_shared_length(k, depth);
  if (shared_prefix_len < node->internal->node_key_prefix.length())
    return false;

  depth += node->internal->node_key_prefix.length();

  const auto [child_i, child_ptr] = node->internal->find_child(k[depth]);

  if (child_ptr == nullptr) return false;

  if (child_ptr->type() != detail::node_type::LEAF)
    return remove_from_subtree(k, depth + 1, child_ptr);

  if (!detail::leaf::matches(child_ptr->leaf, k)) return false;

  const auto is_node_min_size = node->internal->is_min_size();
  const auto child_node_size = detail::leaf::size(child_ptr->leaf);

  if (likely(!is_node_min_size)) {
    node->internal->remove(child_i);
    decrease_memory_use(child_node_size);
    return true;
  }

  assert(is_node_min_size);

  if (node->type() == detail::node_type::I4) {
    std::unique_ptr<detail::inode_4> current_node{node->node_4};
    *node = current_node->leave_last_child(child_i);
    decrease_memory_use(child_node_size + sizeof(detail::inode_4));
  } else if (node->type() == detail::node_type::I16) {
    std::unique_ptr<detail::inode_16> current_node{node->node_16};
    auto new_node{detail::inode_4::create(std::move(current_node), child_i)};
    *node = new_node.release();
    decrease_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4) +
                        child_node_size);
  } else if (node->type() == detail::node_type::I48) {
    std::unique_ptr<detail::inode_48> current_node{node->node_48};
    auto new_node{detail::inode_16::create(std::move(current_node), child_i)};
    *node = new_node.release();
    decrease_memory_use(sizeof(detail::inode_48) - sizeof(detail::inode_16) +
                        child_node_size);
  } else {
    assert(node->type() == detail::node_type::I256);
    std::unique_ptr<detail::inode_256> current_node{node->node_256};
    auto new_node{detail::inode_48::create(std::move(current_node), child_i)};
    *node = new_node.release();
    decrease_memory_use(sizeof(detail::inode_256) - sizeof(detail::inode_48) +
                        child_node_size);
  }
  return true;
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

#ifndef NDEBUG

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

#endif

}  // namespace unodb
