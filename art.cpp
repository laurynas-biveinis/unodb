// Copyright 2019 Laurynas Biveinis
#include "art.hpp"

#include <emmintrin.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <stdexcept>
#ifndef NDEBUG
#include <iomanip>
#include <iostream>
#endif
#include <utility>

#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>

#include <gsl/gsl_util>

// ART implementation properties that we can enforce at compile time
static_assert(std::is_trivial<unodb::art_key_type>::value,
              "Internal key type must be POD, i.e. memcpy'able");
static_assert(sizeof(unodb::art_key_type) == sizeof(unodb::key_type),
              "Internal key type must be no larger than API key type");

static_assert(sizeof(unodb::node_ptr::leaf) == sizeof(unodb::node_ptr::header),
              "node_ptr fields must be of equal size to a raw pointer");
static_assert(sizeof(unodb::node_ptr::internal) ==
                  sizeof(unodb::node_ptr::header),
              "node_ptr fields must be of equal size to a raw pointer");
static_assert(sizeof(unodb::node_ptr::node_4) ==
                  sizeof(unodb::node_ptr::header),
              "node_ptr fields must be of equal size to a raw pointer");
static_assert(sizeof(unodb::node_ptr::node_16) ==
                  sizeof(unodb::node_ptr::header),
              "node_ptr fields must be of equal size to a raw pointer");
static_assert(sizeof(unodb::node_ptr::node_48) ==
                  sizeof(unodb::node_ptr::header),
              "node_ptr fields must be of equal size to a raw pointer");
static_assert(sizeof(unodb::node_ptr::node_256) ==
                  sizeof(unodb::node_ptr::header),
              "node_ptr fields must be of equal size to a raw pointer");
static_assert(sizeof(unodb::node_ptr) == sizeof(void *),
              "node_ptr union must be of equal size to a raw pointer");

static_assert(sizeof(unodb::single_value_leaf_unique_ptr) == sizeof(void *),
              "Single leaf unique_ptr must have no overhead over raw pointer");

namespace {

template <typename InternalNode>
[[nodiscard]] inline boost::container::pmr::pool_options
get_internal_node_pool_options();

[[nodiscard]] inline auto *get_leaf_node_pool() {
  return boost::container::pmr::new_delete_resource();
}

template <typename InternalNode>
[[nodiscard]] inline auto *get_internal_node_pool() {
  static boost::container::pmr::unsynchronized_pool_resource internal_node_pool{
      get_internal_node_pool_options<InternalNode>()};
  return &internal_node_pool;
}

#ifndef NDEBUG

void dump_byte(std::ostream &os, std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

void dump_key(std::ostream &os, unodb::art_key_type key) {
  for (size_t i = 0; i < sizeof(key); i++) dump_byte(os, key[i]);
}

void dump_node(std::ostream &os, const unodb::node_ptr &node);

#endif

inline __attribute__((noreturn)) void cannot_happen() {
  assert(0);
  __builtin_unreachable();
}

}  // namespace

namespace unodb {

template <>
__attribute__((const)) uint64_t art_key<uint64_t>::make_binary_comparable(
    uint64_t key) noexcept {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(key);
#else
#error Needs implementing
#endif
}

enum class node_type : uint8_t { LEAF, I4, I16, I48, I256 };

// A common prefix shared by all node types
struct node_header final {
  explicit node_header(node_type type_) : m_type{type_} {}

  [[nodiscard]] auto type() const noexcept { return m_type; }

 private:
  const node_type m_type;
};

static_assert(std::is_standard_layout<unodb::node_header>::value);

node_ptr::node_ptr(std::unique_ptr<internal_node> &&node) noexcept
    : internal{std::move(node)} {}

node_ptr::~node_ptr() {
  if (header == nullptr) return;
  // While all the unique_ptr union fields look the same in memory, we must
  // invoke destructor of the right type, because the deleters are different
  switch (type()) {
    case node_type::LEAF:
      leaf.~single_value_leaf_unique_ptr();
      return;
    case node_type::I4:
      node_4.~unique_ptr<internal_node_4>();
      return;
    case node_type::I16:
      node_16.~unique_ptr<internal_node_16>();
      return;
    case node_type::I48:
      node_48.~unique_ptr<internal_node_48>();
      return;
    case node_type::I256:
      node_256.~unique_ptr<internal_node_256>();
      return;
  }
  cannot_happen();
}

node_ptr &node_ptr::operator=(node_ptr &&other) noexcept {
  // We rely on all the unique_ptr and header pointers being of the same size,
  // thus we can access any field of other, but we must access the right
  // member of this union, so that the right unique_ptr deleter is invoked.
  if (header == nullptr) {
    leaf = std::move(other.leaf);
    return *this;
  }
  switch (type()) {
    case node_type::LEAF:
      leaf = std::move(other.leaf);
      return *this;
    case node_type::I4:
      node_4 = std::move(other.node_4);
      return *this;
    case node_type::I16:
      node_16 = std::move(other.node_16);
      return *this;
    case node_type::I48:
      node_48 = std::move(other.node_48);
      return *this;
    case node_type::I256:
      node_256 = std::move(other.node_256);
      return *this;
  }
  cannot_happen();
}

node_ptr &node_ptr::operator=(std::nullptr_t) noexcept {
  if (header == nullptr) return *this;
  // While all the unique_ptr union fields look the same in memory, we must
  // invoke reset method of the right type, because the deleters are different
  switch (type()) {
    case node_type::LEAF:
      leaf.reset(nullptr);
      return *this;
    case node_type::I4:
      node_4.reset(nullptr);
      return *this;
    case node_type::I16:
      node_16.reset(nullptr);
      return *this;
    case node_type::I48:
      node_48.reset(nullptr);
      return *this;
    case node_type::I256:
      node_256.reset(nullptr);
      return *this;
  }
  cannot_happen();
}

node_type node_ptr::type() const noexcept { return header->type(); }

// Helper struct for leaf node-related data and (static) code. We
// don't use a regular class because leaf nodes are of variable size, C++ does
// not support flexible array members, and we want to save one level of
// (heap) indirection.
struct single_value_leaf final {
  using view_ptr = const std::byte *;

  [[nodiscard]] static single_value_leaf_unique_ptr create(art_key_type k,
                                                           value_view v,
                                                           db &db_instance);

  [[nodiscard]] static auto key(single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return art_key_type::create(&leaf[offset_key]);
  }

  [[nodiscard]] __attribute__((pure)) static auto matches(
      single_value_leaf_type leaf, art_key_type k) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return k == leaf + offset_key;
  }

  [[nodiscard]] static auto value(single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return value_view{&leaf[offset_value], value_size(leaf)};
  }

  [[nodiscard]] __attribute__((pure)) static std::size_t size(
      single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return value_size(leaf) + offset_value;
  }

#ifndef NDEBUG
  static void dump(std::ostream &os, single_value_leaf_type leaf);
#endif

 private:
  using value_size_type = uint32_t;

  static constexpr auto offset_header = 0;
  static constexpr auto offset_key = sizeof(node_header);
  static constexpr auto offset_value_size = offset_key + sizeof(art_key_type);

  static constexpr auto offset_value =
      offset_value_size + sizeof(value_size_type);

  static constexpr auto minimum_size = offset_value;

  [[nodiscard]] __attribute__((pure)) static value_size_type value_size(
      single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    value_size_type result;
    memcpy(&result, &leaf[offset_value_size], sizeof(result));
    return result;
  }
};

static_assert(std::is_standard_layout<unodb::single_value_leaf>::value,
              "single_value_leaf must be standard layout type to support "
              "aliasing through node_header");

void single_value_leaf_deleter::operator()(
    single_value_leaf_type to_delete) const noexcept {
  const auto s = single_value_leaf::size(to_delete);
  get_leaf_node_pool()->deallocate(to_delete, s);
}

single_value_leaf_unique_ptr single_value_leaf::create(art_key_type k,
                                                       value_view v,
                                                       db &db_instance) {
  if (v.size() > std::numeric_limits<value_size_type>::max()) {
    throw std::length_error("Value length must fit in uint32_t");
  }
  const auto value_size = static_cast<value_size_type>(v.size());
  const auto leaf_size = static_cast<std::size_t>(offset_value) + value_size;
  db_instance.increase_memory_use(leaf_size);
  auto *const leaf_mem =
      static_cast<std::byte *>(get_leaf_node_pool()->allocate(leaf_size));
  new (leaf_mem) node_header{node_type::LEAF};
  k.copy_to(&leaf_mem[offset_key]);
  memcpy(&leaf_mem[offset_value_size], &value_size, sizeof(value_size_type));
  if (!v.empty())
    memcpy(&leaf_mem[offset_value], &v[0], static_cast<std::size_t>(v.size()));
  return single_value_leaf_unique_ptr{leaf_mem};
}

#ifndef NDEBUG

void single_value_leaf::dump(std::ostream &os, single_value_leaf_type leaf) {
  os << "LEAF: key:";
  dump_key(os, key(leaf));
  os << ", value size: " << value_size(leaf) << '\n';
}

#endif

class key_prefix_type final {
 public:
  using size_type = uint8_t;

 private:
  static constexpr size_type capacity = 8;

 public:
  using data_type = std::array<std::byte, capacity>;

  key_prefix_type(art_key_type k1, art_key_type k2,
                  db::tree_depth_type depth) noexcept {
    Expects(k1 != k2);
    Expects(depth < sizeof(art_key_type));
    db::tree_depth_type i;
    for (i = depth; k1[i] == k2[i]; ++i) {
      assert(i - depth < capacity);
      data_[i - depth] = k1[i];
    }
    assert(i - depth <= capacity);
    length_ = gsl::narrow_cast<size_type>(i - depth);
  }

  key_prefix_type(const key_prefix_type &other) noexcept
      : length_{other.length_} {
    std::copy(other.data_.cbegin(), other.data_.cbegin() + length_,
              data_.begin());
  }

  key_prefix_type(const data_type &other, size_type other_len) noexcept
      : length_{other_len} {
    Expects(other_len <= capacity);
    std::copy(other.cbegin(), other.cbegin() + other_len, data_.begin());
  }

  key_prefix_type(key_prefix_type &&other) noexcept = delete;

  ~key_prefix_type() = default;

  key_prefix_type &operator=(const key_prefix_type &) = delete;
  key_prefix_type &operator=(key_prefix_type &&) = delete;

  void cut(unsigned cut_len) noexcept {
    Expects(cut_len > 0);
    Expects(cut_len <= length_);
    std::copy(data_.cbegin() + cut_len, data_.cend(), data_.begin());
    length_ = gsl::narrow_cast<size_type>(length_ - cut_len);
  }

  void prepend(const key_prefix_type &prefix1, std::byte prefix2) noexcept {
    Expects(length() + prefix1.length() < capacity);
    std::copy_backward(data_.cbegin(), data_.cbegin() + length(),
                       data_.begin() + length() + prefix1.length() + 1);
    std::copy(prefix1.data_.cbegin(), prefix1.data_.cbegin() + prefix1.length(),
              data_.begin());
    data_[prefix1.length()] = prefix2;
    length_ = gsl::narrow_cast<size_type>(length_ + prefix1.length() + 1);
  }

  [[nodiscard]] size_type length() const noexcept { return length_; }

  [[nodiscard]] const auto &data() const noexcept { return data_; }

  [[nodiscard]] __attribute__((pure)) std::byte operator[](std::size_t i) const
      noexcept {
    Expects(i < length_);
    return data_[i];
  }

  [[nodiscard]] __attribute__((pure)) auto get_shared_length(
      unodb::art_key_type k, unodb::db::tree_depth_type depth) noexcept {
    unodb::db::tree_depth_type key_i = depth;
    unsigned shared_length = 0;
    while (shared_length < length()) {
      if (k[key_i] != data_[(gsl::narrow_cast<size_type>(shared_length))])
        break;
      ++key_i;
      ++shared_length;
    }
    return shared_length;
  }

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  size_type length_;
  data_type data_;
};

static_assert(std::is_standard_layout<key_prefix_type>::value);

#ifndef NDEBUG

void key_prefix_type::dump(std::ostream &os) const {
  os << ", key prefix len = " << static_cast<unsigned>(length_);
  os << ", key prefix =";
  for (size_t i = 0; i < length_; i++) dump_byte(os, data_[i]);
}

#endif

class internal_node {
 public:
  // The first element is the child index in the node, the 2nd is pointer
  // to the child. If not present, the pointer is nullptr, and the index
  // is undefined
  using find_result_type = std::pair<uint8_t, node_ptr *>;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept;

  void remove(uint8_t child_index) noexcept;

  [[nodiscard]] __attribute__((pure)) find_result_type find_child(
      std::byte key_byte) noexcept;

  [[nodiscard]] __attribute__((pure)) bool is_full() const noexcept;

  [[nodiscard]] __attribute__((pure)) bool is_min_size() const noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 protected:
  internal_node(node_type type, uint8_t children_count_, art_key_type k1,
                art_key_type k2, db::tree_depth_type depth) noexcept
      : header{type},
        key_prefix{k1, k2, depth},
        children_count{children_count_} {
    Expects(type != node_type::LEAF);
    Expects(k1 != k2);
  }

  internal_node(node_type type, uint8_t children_count_,
                key_prefix_type::size_type key_prefix_len_,
                const key_prefix_type::data_type &key_prefix_) noexcept
      : header{type},
        key_prefix{key_prefix_, key_prefix_len_},
        children_count{children_count_} {
    Expects(type != node_type::LEAF);
  }

  internal_node(node_type type, uint8_t children_count_,
                const key_prefix_type &key_prefix_) noexcept
      : header{type}, key_prefix{key_prefix_}, children_count{children_count_} {
    Expects(type != node_type::LEAF);
  }

  template <typename KeysType>
  static auto __attribute__((pure))
  get_sorted_key_array_insert_position(const KeysType &keys,
                                       uint8_t children_count,
                                       std::byte key_byte) noexcept {
    Expects(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
    Expects(std::adjacent_find(keys.cbegin(), keys.cbegin() + children_count) >=
            keys.cbegin() + children_count);
    const auto result = static_cast<uint8_t>(
        std::lower_bound(keys.cbegin(), keys.cbegin() + children_count,
                         key_byte) -
        keys.cbegin());
    Ensures(result == children_count || keys[result] != key_byte);
    return result;
  }

  template <typename KeysType, typename ChildrenType>
  static void insert_into_sorted_key_children_arrays(
      KeysType &keys, ChildrenType &children, uint8_t &children_count,
      std::byte key_byte, single_value_leaf_unique_ptr &&child) {
    Expects(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
    const auto insert_pos_index =
        get_sorted_key_array_insert_position(keys, children_count, key_byte);
    if (insert_pos_index != children_count) {
      assert(keys[insert_pos_index] != key_byte);
      std::copy_backward(keys.cbegin() + insert_pos_index,
                         keys.cbegin() + children_count,
                         keys.begin() + children_count + 1);
      std::move_backward(children.begin() + insert_pos_index,
                         children.begin() + children_count,
                         children.begin() + children_count + 1);
    }
    keys[insert_pos_index] = key_byte;
    new (&children[insert_pos_index])
        single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
    Ensures(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
  }

  template <typename KeysType, typename ChildrenType>
  static void remove_from_sorted_key_children_arrays(
      KeysType &keys, ChildrenType &children, uint8_t &children_count,
      uint8_t child_to_remove) noexcept {
    Expects(child_to_remove < children_count);
    Expects(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));

    std::copy(keys.cbegin() + child_to_remove + 1,
              keys.cbegin() + children_count, keys.begin() + child_to_remove);
    std::move(children.begin() + child_to_remove + 1,
              children.begin() + children_count,
              children.begin() + child_to_remove);
    --children_count;

    Ensures(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
  }

  const node_header header;
  key_prefix_type key_prefix;

  uint8_t children_count;

  template <unsigned, unsigned, node_type, typename, typename, typename>
  friend class internal_node_template;
  friend class internal_node_4;
  friend class internal_node_16;
  friend class internal_node_48;
  friend class internal_node_256;
  friend class db;
};

template <unsigned MinSize, unsigned Capacity, node_type NodeType,
          typename SmallerDerived, typename LargerDerived, typename Derived>
class internal_node_template : public internal_node {
  static_assert(NodeType != node_type::LEAF,
                "internal_node_template must be instantiated with a non-leaf "
                "node type");
  static_assert(!std::is_same_v<Derived, LargerDerived>,
                "Node type and next larger node type cannot be identical");
  static_assert(!std::is_same_v<SmallerDerived, Derived>,
                "Node type and next smaller node type cannot be identical");
  static_assert(!std::is_same_v<SmallerDerived, LargerDerived>,
                "Next smaller node type and next larger node type cannot be "
                "identical");
  static_assert(MinSize < Capacity,
                "Node capacity must be larger than minimum size");

 public:
  [[nodiscard]] static std::unique_ptr<Derived> create(
      std::unique_ptr<LargerDerived> &&source_node, uint8_t child_to_remove) {
    return std::make_unique<Derived>(std::move(source_node), child_to_remove);
  }

  [[nodiscard]] static std::unique_ptr<Derived> create(
      std::unique_ptr<SmallerDerived> &&source_node,
      single_value_leaf_unique_ptr &&child, db::tree_depth_type depth) {
    return std::make_unique<Derived>(std::move(source_node), std::move(child),
                                     depth);
  }

  [[nodiscard]] static void *operator new(std::size_t size) {
    assert(size == sizeof(Derived));
    return get_internal_node_pool<Derived>()->allocate(size);
  }

  static void operator delete(void *to_delete) {
    get_internal_node_pool<Derived>()->deallocate(to_delete, sizeof(Derived));
  }

  [[nodiscard]] __attribute__((pure)) bool is_full() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() == NodeType);
    return children_count == capacity;
  }

  [[nodiscard]] __attribute__((pure)) bool is_min_size() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() == NodeType);
    return children_count == min_size;
  }

 protected:
  internal_node_template(art_key_type k1, art_key_type k2,
                         db::tree_depth_type depth) noexcept
      : internal_node{NodeType, MinSize, k1, k2, depth} {
    assert(is_min_size());
  }

  internal_node_template(key_prefix_type::size_type key_prefix_len,
                         const key_prefix_type::data_type &key_prefix_) noexcept
      : internal_node{NodeType, MinSize, key_prefix_len, key_prefix_} {
    assert(is_min_size());
  }

  explicit internal_node_template(const SmallerDerived &source_node) noexcept
      : internal_node{NodeType, MinSize, source_node.key_prefix} {
    Expects(source_node.is_full());
    assert(is_min_size());
  }

  explicit internal_node_template(const LargerDerived &source_node) noexcept
      : internal_node{NodeType, Capacity, source_node.key_prefix} {
    Expects(source_node.is_min_size());
    assert(is_full());
  }

  static constexpr auto min_size = MinSize;
  static constexpr auto capacity = Capacity;
  static constexpr auto static_node_type = NodeType;
};

// A class used as a sentinel for internal_node_template template args: the
// larger node type for the largest node type.
class fake_internal_node {};

using internal_node_4_template =
    internal_node_template<2, 4, node_type::I4, fake_internal_node,
                           internal_node_16, internal_node_4>;

class internal_node_4 final : public internal_node_4_template {
 public:
  using internal_node_4_template::create;

  // Create a new node with two given child nodes
  [[nodiscard]] static std::unique_ptr<internal_node_4> create(
      art_key_type k1, art_key_type k2, db::tree_depth_type depth,
      node_ptr &&child1, single_value_leaf_unique_ptr &&child2) {
    return std::make_unique<internal_node_4>(k1, k2, depth, std::move(child1),
                                             std::move(child2));
  }

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused this
  // key prefix split
  [[nodiscard]] static std::unique_ptr<internal_node_4> create(
      node_ptr &&source_node, unsigned len, db::tree_depth_type depth,
      single_value_leaf_unique_ptr &&child1) {
    return std::make_unique<internal_node_4>(std::move(source_node), len, depth,
                                             std::move(child1));
  }

  internal_node_4(art_key_type k1, art_key_type k2, db::tree_depth_type depth,
                  node_ptr &&child1,
                  single_value_leaf_unique_ptr &&child2) noexcept;

  internal_node_4(node_ptr &&source_node, unsigned len,
                  db::tree_depth_type depth,
                  single_value_leaf_unique_ptr &&child1) noexcept;

  internal_node_4(std::unique_ptr<internal_node_16> &&source_node,
                  uint8_t child_to_remove) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    const auto key_byte = single_value_leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(keys, children, children_count,
                                           key_byte, std::move(child));
  }

  void remove(uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    remove_from_sorted_key_children_arrays(keys, children, children_count,
                                           child_index);
  }

  node_ptr &&leave_last_child(uint8_t child_to_delete) noexcept {
    Expects(is_min_size());
    Expects(child_to_delete == 0 || child_to_delete == 1);
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    const uint8_t child_to_leave = (child_to_delete == 0) ? 1 : 0;
    children[child_to_delete] = nullptr;
    if (children[child_to_leave].type() != node_type::LEAF) {
      children[child_to_leave].internal->key_prefix.prepend(
          key_prefix, keys[child_to_leave]);
    }
    return std::move(children[child_to_leave]);
  }

  [[nodiscard]] __attribute__((pure)) find_result_type find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  friend class internal_node_16;

  void add_two_to_empty(std::byte key1, node_ptr &&child1, std::byte key2,
                        single_value_leaf_unique_ptr &&child2) noexcept;

  std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;
};

internal_node_4::internal_node_4(art_key_type k1, art_key_type k2,
                                 db::tree_depth_type depth, node_ptr &&child1,
                                 single_value_leaf_unique_ptr &&child2) noexcept
    : internal_node_4_template{k1, k2, depth} {
  const auto next_level_depth = depth + key_prefix.length();
  add_two_to_empty(k1[next_level_depth], std::move(child1),
                   k2[next_level_depth], std::move(child2));
}

internal_node_4::internal_node_4(node_ptr &&source_node, unsigned len,
                                 db::tree_depth_type depth,
                                 single_value_leaf_unique_ptr &&child1) noexcept
    : internal_node_4_template{
          gsl::narrow_cast<key_prefix_type::size_type>(len),
          source_node.internal->key_prefix.data()} {
  Expects(source_node.type() != node_type::LEAF);
  Expects(len < source_node.internal->key_prefix.length());
  const auto source_node_key_byte =
      source_node.internal
          ->key_prefix[gsl::narrow_cast<key_prefix_type::size_type>(len)];
  source_node.internal->key_prefix.cut(len + 1);
  const auto new_key_byte = single_value_leaf::key(child1.get())[depth + len];
  add_two_to_empty(source_node_key_byte, std::move(source_node), new_key_byte,
                   std::move(child1));
}

void internal_node_4::add_two_to_empty(
    std::byte key1, node_ptr &&child1, std::byte key2,
    single_value_leaf_unique_ptr &&child2) noexcept {
  Expects(key1 != key2);
  assert(children_count == 2);
  const uint8_t key1_i = key1 < key2 ? 0 : 1;
  const uint8_t key2_i = key1_i == 0 ? 1 : 0;
  keys[key1_i] = key1;
  new (&children[key1_i]) node_ptr{std::move(child1)};
  keys[key2_i] = key2;
  new (&children[key2_i]) single_value_leaf_unique_ptr{std::move(child2)};
  // Initialize elements past children_count for proper std::array destruction.
  new (&children[2]) node_ptr{nullptr};
  new (&children[3]) node_ptr{nullptr};
  assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
}

#ifndef NDEBUG

void internal_node_4::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (uint8_t i = 0; i < children_count; i++) dump_byte(os, keys[i]);
  os << ", children:\n";
  for (uint8_t i = 0; i < children_count; i++) dump_node(os, children[i]);
}

#endif

internal_node::find_result_type internal_node_4::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
  for (unsigned i = 0; i < children_count; i++)
    if (keys[i] == key_byte) return std::make_pair(i, &children[i]);
  return std::make_pair(0xFF, nullptr);
}

using internal_node_16_template =
    internal_node_template<5, 16, node_type::I16, internal_node_4,
                           internal_node_48, internal_node_16>;

class internal_node_16 final : public internal_node_16_template {
 public:
  internal_node_16(std::unique_ptr<internal_node_4> &&node,
                   single_value_leaf_unique_ptr &&child,
                   db::tree_depth_type depth) noexcept;

  internal_node_16(std::unique_ptr<internal_node_48> &&source_node,
                   uint8_t child_to_remove) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    const auto key_byte = single_value_leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(
        keys.byte_array, children, children_count, key_byte, std::move(child));
  }

  void remove(uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    remove_from_sorted_key_children_arrays(keys.byte_array, children,
                                           children_count, child_index);
  }

  [[nodiscard]] __attribute__((pure)) find_result_type find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  union {
    std::array<std::byte, capacity> byte_array;
    __m128i sse;
  } keys;
  std::array<node_ptr, capacity> children;

  friend class internal_node_4;
  friend class internal_node_48;
};

internal_node_4::internal_node_4(
    std::unique_ptr<internal_node_16> &&source_node,
    uint8_t child_to_remove) noexcept
    : internal_node_4_template{*source_node} {
  std::copy(source_node->keys.byte_array.cbegin(),
            source_node->keys.byte_array.cbegin() + child_to_remove,
            keys.begin());
  std::copy(source_node->keys.byte_array.cbegin() + child_to_remove + 1,
            source_node->keys.byte_array.cbegin() + source_node->children_count,
            keys.begin() + child_to_remove);
  std::uninitialized_move(source_node->children.begin(),
                          source_node->children.begin() + child_to_remove,
                          children.begin());
  std::uninitialized_move(
      source_node->children.begin() + child_to_remove + 1,
      source_node->children.begin() + source_node->children_count,
      children.begin() + child_to_remove);
  assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
}

internal_node_16::internal_node_16(std::unique_ptr<internal_node_4> &&node,
                                   single_value_leaf_unique_ptr &&child,
                                   db::tree_depth_type depth) noexcept
    : internal_node_16_template{*node} {
  const auto key_byte = single_value_leaf::key(child.get())[depth];
  const auto insert_pos_index = get_sorted_key_array_insert_position(
      node->keys, node->children_count, key_byte);
  std::copy(node->keys.cbegin(), node->keys.cbegin() + insert_pos_index,
            keys.byte_array.begin());
  keys.byte_array[insert_pos_index] = key_byte;
  std::copy(node->keys.cbegin() + insert_pos_index, node->keys.cend(),
            keys.byte_array.begin() + insert_pos_index + 1);
  std::uninitialized_move(node->children.begin(),
                          node->children.begin() + insert_pos_index,
                          children.begin());
  new (&children[insert_pos_index])
      single_value_leaf_unique_ptr{std::move(child)};
  std::uninitialized_move(node->children.begin() + insert_pos_index,
                          node->children.end(),
                          children.begin() + insert_pos_index + 1);
  // Initialize elements past children_count for proper std::array destruction.
  for (uint8_t i = children_count; i < capacity; i++) {
    new (&children[i]) node_ptr{nullptr};
  }
}

internal_node::find_result_type internal_node_16::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
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
}

#ifndef NDEBUG

void internal_node_16::dump(std::ostream &os) const {
  os << ", key bytes =";
  for (uint8_t i = 0; i < children_count; i++)
    dump_byte(os, keys.byte_array[i]);
  os << ", children:\n";
  for (uint8_t i = 0; i < children_count; i++) dump_node(os, children[i]);
}

#endif

using internal_node_48_template =
    internal_node_template<17, 48, node_type::I48, internal_node_16,
                           internal_node_256, internal_node_48>;

class internal_node_48 final : public internal_node_48_template {
 public:
  internal_node_48(std::unique_ptr<internal_node_16> &&node,
                   single_value_leaf_unique_ptr &&child,
                   db::tree_depth_type depth) noexcept;

  internal_node_48(std::unique_ptr<internal_node_256> &&source_node,
                   uint8_t child_to_remove) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    const auto key_byte =
        static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
    assert(child_indexes[key_byte] == empty_child);
    uint8_t i;
    for (i = 0; i < capacity; i++)
      if (children[i] == nullptr) break;
    assert(children[i] == nullptr);
    child_indexes[key_byte] = i;
    new (&children[i]) single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
  }

  void remove(uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    children[child_indexes[child_index]] = nullptr;
    child_indexes[child_index] = empty_child;
    --children_count;
  }

  [[nodiscard]] __attribute__((pure)) find_result_type find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  std::array<uint8_t, 256> child_indexes;
  std::array<node_ptr, capacity> children;

  static constexpr uint8_t empty_child = 0xFF;

  friend class internal_node_16;
  friend class internal_node_256;
};

internal_node_16::internal_node_16(
    std::unique_ptr<internal_node_48> &&source_node,
    uint8_t child_to_remove) noexcept
    : internal_node_16_template{*source_node} {
  uint8_t next_child = 0;
  for (unsigned i = 0; i < 256; i++) {
    const auto source_child_i = source_node->child_indexes[i];
    if (i == child_to_remove) {
      assert(source_child_i != internal_node_48::empty_child);
      assert(source_node->children[source_child_i] != nullptr);
      continue;
    }
    if (source_child_i != internal_node_48::empty_child) {
      keys.byte_array[next_child] = gsl::narrow_cast<std::byte>(i);
      assert(source_node->children[source_child_i] != nullptr);
      new (&children[next_child])
          node_ptr{std::move(source_node->children[source_child_i])};
      ++next_child;
      if (next_child == children_count) break;
    }
  }
  assert(std::is_sorted(keys.byte_array.cbegin(),
                        keys.byte_array.cbegin() + children_count));
}

internal_node_48::internal_node_48(std::unique_ptr<internal_node_16> &&node,
                                   single_value_leaf_unique_ptr &&child,
                                   db::tree_depth_type depth) noexcept
    : internal_node_48_template{*node} {
  memset(&child_indexes[0], empty_child,
         child_indexes.size() * sizeof(child_indexes[0]));
  uint8_t i;
  for (i = 0; i < node->capacity; i++) {
    const auto existing_key_byte = node->keys.byte_array[i];
    child_indexes[static_cast<uint8_t>(existing_key_byte)] = i;
    new (&children[i]) node_ptr{std::move(node->children[i])};
  }
  const auto key_byte =
      static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
  assert(child_indexes[key_byte] == empty_child);
  child_indexes[key_byte] = i;
  new (&children[i]) node_ptr{std::move(child)};
  // Initialize elements past children_count for proper std::array destruction.
  for (i = children_count; i < capacity; i++) {
    new (&children[i]) node_ptr{nullptr};
  }
}

internal_node::find_result_type internal_node_48::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
  if (child_indexes[static_cast<uint8_t>(key_byte)] != empty_child) {
    const auto child_i = child_indexes[static_cast<uint8_t>(key_byte)];
    assert(children[child_i] != nullptr);
    return std::make_pair(static_cast<uint8_t>(key_byte), &children[child_i]);
  }
  return std::make_pair(0xFF, nullptr);
}

#ifndef NDEBUG

void internal_node_48::dump(std::ostream &os) const {
  os << ", key bytes & child indexes\n";
  for (unsigned i = 0; i < 256; i++)
    if (child_indexes[i] != empty_child) {
      os << " ";
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ", child index = " << static_cast<unsigned>(child_indexes[i])
         << ": ";
      dump_node(os, children[child_indexes[i]]);
    }
}

#endif

using internal_node_256_template =
    internal_node_template<49, 256, node_type::I256, internal_node_48,
                           fake_internal_node, internal_node_256>;

class internal_node_256 final : public internal_node_256_template {
 public:
  internal_node_256(std::unique_ptr<internal_node_48> &&node,
                    single_value_leaf_unique_ptr &&child,
                    db::tree_depth_type depth) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    Expects(!is_full());
    const auto key_byte =
        static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
    assert(children[key_byte] == nullptr);
    new (&children[key_byte]) single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
  }

  void remove(uint8_t child_index) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
    Expects(children[child_index] != nullptr);
    children[child_index] = nullptr;
    --children_count;
  }

  [[nodiscard]] __attribute__((pure)) find_result_type find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  std::array<node_ptr, capacity> children;

  friend class internal_node_48;
};

internal_node_48::internal_node_48(
    std::unique_ptr<internal_node_256> &&source_node,
    uint8_t child_to_remove) noexcept
    : internal_node_48_template{*source_node} {
  uint8_t next_child = 0;
  for (unsigned i = 0; i < 256; i++) {
    if (i == child_to_remove) {
      assert(source_node->children[i] != nullptr);
      child_indexes[i] = empty_child;
      continue;
    }
    if (source_node->children[i] == nullptr) {
      child_indexes[i] = empty_child;
      continue;
    }
    assert(source_node->children[i] != nullptr);
    child_indexes[i] = gsl::narrow_cast<uint8_t>(next_child);
    new (&children[next_child]) node_ptr{std::move(source_node->children[i])};
    ++next_child;
    if (next_child == children_count) break;
  }
}

internal_node_256::internal_node_256(std::unique_ptr<internal_node_48> &&node,
                                     single_value_leaf_unique_ptr &&child,
                                     db::tree_depth_type depth) noexcept
    : internal_node_256_template{*node} {
  for (unsigned i = 0; i < 256; i++) {
    if (node->child_indexes[i] != internal_node_48::empty_child) {
      new (&children[i])
          node_ptr{std::move(node->children[node->child_indexes[i]])};
    } else {
      new (&children[i]) node_ptr{nullptr};
    }
  }
  const uint8_t key_byte =
      static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
  assert(children[key_byte] == nullptr);
  new (&children[key_byte]) single_value_leaf_unique_ptr{std::move(child)};
}

internal_node::find_result_type internal_node_256::find_child(
    std::byte key_byte) noexcept {
  assert(reinterpret_cast<node_header *>(this)->type() == static_node_type);
  const auto key_int_byte = static_cast<uint8_t>(key_byte);
  if (children[key_int_byte] != nullptr)
    return std::make_pair(key_int_byte, &children[key_int_byte]);
  return std::make_pair(0xFF, nullptr);
}

#ifndef NDEBUG

void internal_node_256::dump(std::ostream &os) const {
  os << ", key bytes & children:\n";
  for (size_t i = 0; i < 256; i++) {
    if (children[i] != nullptr) {
      os << ' ';
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ' ';
      dump_node(os, children[i]);
    }
  }
}

#endif

inline bool internal_node::is_full() const noexcept {
  switch (header.type()) {
    case node_type::I4:
      return static_cast<const internal_node_4 *>(this)->is_full();
    case node_type::I16:
      return static_cast<const internal_node_16 *>(this)->is_full();
    case node_type::I48:
      return static_cast<const internal_node_48 *>(this)->is_full();
    case node_type::I256:
      return static_cast<const internal_node_256 *>(this)->is_full();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

inline bool internal_node::is_min_size() const noexcept {
  switch (header.type()) {
    case node_type::I4:
      return static_cast<const internal_node_4 *>(this)->is_min_size();
    case node_type::I16:
      return static_cast<const internal_node_16 *>(this)->is_min_size();
    case node_type::I48:
      return static_cast<const internal_node_48 *>(this)->is_min_size();
    case node_type::I256:
      return static_cast<const internal_node_256 *>(this)->is_min_size();
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

inline void internal_node::add(single_value_leaf_unique_ptr &&child,
                               db::tree_depth_type depth) noexcept {
  Expects(!is_full());
  switch (header.type()) {
    case node_type::I4:
      static_cast<internal_node_4 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I16:
      static_cast<internal_node_16 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I48:
      static_cast<internal_node_48 *>(this)->add(std::move(child), depth);
      break;
    case node_type::I256:
      static_cast<internal_node_256 *>(this)->add(std::move(child), depth);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

inline void internal_node::remove(uint8_t child_index) noexcept {
  Expects(!is_min_size());
  switch (header.type()) {
    case node_type::I4:
      static_cast<internal_node_4 *>(this)->remove(child_index);
      break;
    case node_type::I16:
      static_cast<internal_node_16 *>(this)->remove(child_index);
      break;
    case node_type::I48:
      static_cast<internal_node_48 *>(this)->remove(child_index);
      break;
    case node_type::I256:
      static_cast<internal_node_256 *>(this)->remove(child_index);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

inline internal_node::find_result_type internal_node::find_child(
    std::byte key_byte) noexcept {
  switch (header.type()) {
    case node_type::I4:
      return static_cast<internal_node_4 *>(this)->find_child(key_byte);
    case node_type::I16:
      return static_cast<internal_node_16 *>(this)->find_child(key_byte);
    case node_type::I48:
      return static_cast<internal_node_48 *>(this)->find_child(key_byte);
    case node_type::I256:
      return static_cast<internal_node_256 *>(this)->find_child(key_byte);
    case node_type::LEAF:
      cannot_happen();
  }
  cannot_happen();
}

#ifndef NDEBUG

void internal_node::dump(std::ostream &os) const {
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
  os << "# children = " << static_cast<unsigned>(children_count);
  key_prefix.dump(os);
  switch (header.type()) {
    case node_type::I4:
      static_cast<const internal_node_4 *>(this)->dump(os);
      break;
    case node_type::I16:
      static_cast<const internal_node_16 *>(this)->dump(os);
      break;
    case node_type::I48:
      static_cast<const internal_node_48 *>(this)->dump(os);
      break;
    case node_type::I256:
      static_cast<const internal_node_256 *>(this)->dump(os);
      break;
    case node_type::LEAF:
      cannot_happen();
  }
}

#endif
}  // namespace unodb

namespace {

// For internal node pools, approximate requesting ~2MB blocks from backing
// storage (when ported to Linux, ask for 2MB huge pages directly)
template <typename InternalNode>
[[nodiscard]] inline boost::container::pmr::pool_options
get_internal_node_pool_options() {
  boost::container::pmr::pool_options internal_node_pool_options;
  internal_node_pool_options.max_blocks_per_chunk =
      2 * 1024 * 1024 / sizeof(InternalNode);
  internal_node_pool_options.largest_required_pool_block = sizeof(InternalNode);
  return internal_node_pool_options;
}

}  // namespace

namespace unodb {

db::get_result db::get(key_type k) const noexcept {
  if (BOOST_UNLIKELY(root.header == nullptr)) return {};
  return get_from_subtree(root, art_key{k}, 0);
}

db::get_result db::get_from_subtree(const node_ptr &node, art_key_type k,
                                    tree_depth_type depth) noexcept {
  if (node.type() == node_type::LEAF) {
    if (single_value_leaf::matches(node.leaf.get(), k)) {
      const auto value = single_value_leaf::value(node.leaf.get());
      return value;
    }
    return {};
  }
  assert(node.type() != node_type::LEAF);
  if (node.internal->key_prefix.get_shared_length(k, depth) <
      node.internal->key_prefix.length())
    return {};
  depth += node.internal->key_prefix.length();
  const auto child = node.internal->find_child(k[depth]).second;
  if (child == nullptr) return {};
  return get_from_subtree(*child, k, depth + 1);
}

bool db::insert(key_type k, value_view v) {
  const auto bin_comparable_key = art_key{k};
  if (BOOST_UNLIKELY(root.header == nullptr)) {
    auto leaf = single_value_leaf::create(bin_comparable_key, v, *this);
    root.leaf = std::move(leaf);
    return true;
  }
  return insert_to_subtree(bin_comparable_key, &root, v, 0);
}

bool db::insert_to_subtree(art_key_type k, node_ptr *node,
                           value_view v, tree_depth_type depth) {
  if (node->type() == node_type::LEAF) {
    const auto existing_key = single_value_leaf::key(node->leaf.get());
    if (BOOST_UNLIKELY(k == existing_key)) return false;
    auto leaf = single_value_leaf::create(k, v, *this);
    // TODO(laurynas): if internal_node_4::create throws bad_alloc, mem
    // accounting desyncs
    increase_memory_use(sizeof(internal_node_4));
    auto new_node = internal_node_4::create(existing_key, k, depth,
                                            std::move(*node), std::move(leaf));
    node->internal = std::move(new_node);
    return true;
  }

  assert(node->type() != node_type::LEAF);

  const auto shared_prefix_len =
      node->internal->key_prefix.get_shared_length(k, depth);
  if (shared_prefix_len < node->internal->key_prefix.length()) {
    auto leaf = single_value_leaf::create(k, v, *this);
    increase_memory_use(sizeof(internal_node_4));
    auto new_node = internal_node_4::create(std::move(*node), shared_prefix_len,
                                            depth, std::move(leaf));
    node->internal = std::move(new_node);
    return true;
  }
  depth += node->internal->key_prefix.length();

  auto child = node->internal->find_child(k[depth]).second;

  if (child != nullptr)
    return insert_to_subtree(k, child, v, depth + 1);

  auto leaf = single_value_leaf::create(k, v, *this);

  if (BOOST_LIKELY(!node->internal->is_full())) {
    node->internal->add(std::move(leaf), depth);
    return true;
  }

  assert(node->internal->is_full());
  if (node->type() == node_type::I4) {
    increase_memory_use(sizeof(internal_node_16) - sizeof(internal_node_4));
    auto larger_node = internal_node_16::create(
        std::unique_ptr<internal_node_4>(node->node_4.release()),
        std::move(leaf), depth);
    node->node_16 = std::move(larger_node);
  } else if (node->type() == node_type::I16) {
    increase_memory_use(sizeof(internal_node_48) - sizeof(internal_node_16));
    auto larger_node = internal_node_48::create(
        std::unique_ptr<internal_node_16>(node->node_16.release()),
        std::move(leaf), depth);
    node->node_48 = std::move(larger_node);
  } else {
    assert(node->type() == node_type::I48);
    increase_memory_use(sizeof(internal_node_256) - sizeof(internal_node_48));
    auto larger_node = internal_node_256::create(
        std::unique_ptr<internal_node_48>(node->node_48.release()),
        std::move(leaf), depth);
    node->node_256 = std::move(larger_node);
  }
  return true;
}

bool db::remove(key_type k) {
  const auto bin_comparable_key = art_key{k};
  if (BOOST_UNLIKELY(root.header == nullptr)) return false;
  if (root.type() == node_type::LEAF) {
    if (single_value_leaf::matches(root.leaf.get(), bin_comparable_key)) {
      const auto leaf_size = single_value_leaf::size(root.leaf.get());
      root = nullptr;
      decrease_memory_use(leaf_size);
      return true;
    }
    return false;
  }
  return remove_from_subtree(bin_comparable_key, 0, &root);
}

bool db::remove_from_subtree(art_key_type k, tree_depth_type depth,
                             node_ptr *node) {
  assert(node->type() != node_type::LEAF);

  const auto shared_prefix_len =
      node->internal->key_prefix.get_shared_length(k, depth);
  if (shared_prefix_len < node->internal->key_prefix.length()) return false;

  depth += node->internal->key_prefix.length();

  auto [child_i, child_ptr] = node->internal->find_child(k[depth]);

  if (child_ptr == nullptr) return false;

  if (child_ptr->type() != node_type::LEAF)
    return remove_from_subtree(k, depth + 1, child_ptr);

  if (!single_value_leaf::matches(child_ptr->leaf.get(), k)) return false;

  const auto child_node_size = single_value_leaf::size(child_ptr->leaf.get());

  if (BOOST_LIKELY(!node->internal->is_min_size())) {
    node->internal->remove(child_i);
    decrease_memory_use(child_node_size);
    return true;
  }

  if (node->type() == node_type::I4) {
    *node =
        std::move(std::unique_ptr<internal_node_4>(
                      static_cast<internal_node_4 *>(node->internal.release()))
                      ->leave_last_child(child_i));
    decrease_memory_use(child_node_size + sizeof(internal_node_4));
  } else if (node->type() == node_type::I16) {
    auto smaller_node = internal_node_4::create(
        std::unique_ptr<internal_node_16>(node->node_16.release()), child_i);
    node->node_4 = std::move(smaller_node);
    decrease_memory_use(sizeof(internal_node_16) - sizeof(internal_node_4) +
                        child_node_size);
  } else if (node->type() == node_type::I48) {
    auto smaller_node = internal_node_16::create(
        std::unique_ptr<internal_node_48>(node->node_48.release()), child_i);
    node->node_16 = std::move(smaller_node);
    decrease_memory_use(sizeof(internal_node_48) - sizeof(internal_node_16) +
                        child_node_size);
  } else {
    assert(node->type() == node_type::I256);
    auto smaller_node = internal_node_48::create(
        std::unique_ptr<internal_node_256>(node->node_256.release()), child_i);
    node->node_48 = std::move(smaller_node);
    decrease_memory_use(sizeof(internal_node_256) - sizeof(internal_node_48) +
                        child_node_size);
  }
  return true;
}

void db::increase_memory_use(std::size_t delta) {
  if (memory_limit == 0 || delta == 0) return;
  assert(current_memory_use <= memory_limit);
  if (current_memory_use + delta > memory_limit) throw std::bad_alloc{};
  current_memory_use += delta;
}

void db::decrease_memory_use(std::size_t delta) noexcept {
  if (memory_limit == 0 || delta == 0) return;
  assert(delta <= current_memory_use);
  current_memory_use -= delta;
}

#ifndef NDEBUG

}  // namespace unodb

namespace {

void dump_node(std::ostream &os, const unodb::node_ptr &node) {
  os << "node at: " << &node;
  if (node.header == nullptr) {
    os << ", <null>\n";
    return;
  }
  os << ", type = ";
  switch (node.type()) {
    case unodb::node_type::LEAF:
      unodb::single_value_leaf::dump(os, node.leaf.get());
      break;
    case unodb::node_type::I4:
    case unodb::node_type::I16:
    case unodb::node_type::I48:
    case unodb::node_type::I256:
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
