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
#include <string>
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
static_assert(sizeof(unodb::node_ptr) == sizeof(void *),
              "node_ptr union must be of equal size to a raw pointer");

static_assert(sizeof(unodb::single_value_leaf_unique_ptr) == sizeof(void *),
              "Single leaf unique_ptr must have no overhead over raw pointer");

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

node_type node_ptr::type() const noexcept { return header->type(); }

}  // namespace unodb

namespace {

using key_prefix_size_type = uint8_t;

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

void dump_node(std::ostream &os, const unodb::node_ptr &node, unsigned indent);

#endif

inline __attribute__((noreturn)) void cannot_happen() {
  assert(0);
  __builtin_unreachable();
}

template <typename BidirInIter, typename BidirOutIter>
BidirOutIter uninitialized_move_backward(
    BidirInIter source_first, BidirInIter source_last,
    BidirOutIter dest_last) noexcept(std::
                                         is_nothrow_move_constructible<
                                             typename std::iterator_traits<
                                                 BidirOutIter>::value_type>()) {
  using value_type = typename std::iterator_traits<BidirOutIter>::value_type;
  if constexpr (std::is_nothrow_move_constructible<value_type>()) {
    while (source_last != source_first) {
      new (std::addressof(*(--dest_last)))
          value_type{std::move(*(--source_last))};
    }
    return dest_last;
  } else {
    static_assert(std::is_nothrow_move_constructible<value_type>());
  }
}

}  // namespace

namespace unodb {

// Helper struct for leaf node-related data and (static) code. We
// don't use a regular class because leaf nodes are of variable size, C++ does
// not support flexible array members, and we want to save one level of
// (heap) indirection.
struct single_value_leaf final {
  using view_ptr = const std::byte *;

  [[nodiscard]] static single_value_leaf_unique_ptr create(art_key_type k,
                                                           value_view v);

  [[nodiscard]] static auto key(single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return art_key_type::create(&leaf[offset_key]);
  }

  [[nodiscard]] static auto matches(single_value_leaf_type leaf,
                                    art_key_type k) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return k == leaf + offset_key;
  }

  [[nodiscard]] static auto value(single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return value_view{&leaf[offset_value], value_size(leaf)};
  }

  [[nodiscard]] static std::size_t size(single_value_leaf_type leaf) noexcept {
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

  [[nodiscard]] static value_size_type value_size(
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
                                                       value_view v) {
  if (v.size() > std::numeric_limits<value_size_type>::max()) {
    throw std::length_error("Value length must fit in uint32_t");
  }
  const auto value_size = static_cast<value_size_type>(v.size());
  const auto leaf_size = static_cast<std::size_t>(offset_value) + value_size;
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

class internal_node {
 public:
  static constexpr key_prefix_size_type key_prefix_capacity = 8;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept;

  [[nodiscard]] __attribute__((pure)) node_ptr *find_child(
      std::byte key_byte) noexcept;

  void cut_prefix(unsigned cut_len) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() != node_type::LEAF);
    Expects(cut_len > 0);
    Expects(cut_len <= key_prefix_len);
    std::copy(key_prefix.cbegin() + cut_len, key_prefix.cend(),
              key_prefix.begin());
    key_prefix_len =
        gsl::narrow_cast<key_prefix_size_type>(key_prefix_len - cut_len);
    assert(key_prefix_len <= key_prefix_capacity);
  }

  [[nodiscard]] key_prefix_size_type get_key_prefix_len() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() !=
           node_type::LEAF);
    return key_prefix_len;
  }

  [[nodiscard]] std::byte key_prefix_byte(key_prefix_size_type i) const
      noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() !=
           node_type::LEAF);
    Expects(i < key_prefix_len);
    return key_prefix[i];
  }

  [[nodiscard]] bool is_full() const noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os, unsigned indent) const;
#endif

 protected:
  using key_prefix_type = std::array<std::byte, key_prefix_capacity>;

  internal_node(node_type type, uint8_t children_count_, art_key_type k1,
                art_key_type k2, db::tree_depth_type depth) noexcept
      : header{type}, children_count{children_count_} {
    db::tree_depth_type i;
    for (i = depth; k1[i] == k2[i]; ++i) {
      assert(i - depth < key_prefix_capacity);
      key_prefix[i - depth] = k1[i];
    }
    assert(i - depth <= key_prefix_capacity);
    key_prefix_len = gsl::narrow_cast<key_prefix_size_type>(i - depth);
  }

  internal_node(node_type type, uint8_t children_count_,
                key_prefix_size_type key_prefix_len_,
                const key_prefix_type &key_prefix_) noexcept
      : header{type},
        children_count{children_count_},
        key_prefix_len{key_prefix_len_} {
    Expects(key_prefix_len_ <= key_prefix_capacity);
    std::copy(key_prefix_.cbegin(), key_prefix_.cbegin() + key_prefix_len,
              key_prefix.begin());
  }

  template <typename Keys_type>
  static auto __attribute__((pure))
  get_sorted_key_array_insert_position(const Keys_type &keys,
                                       uint8_t children_count,
                                       std::byte key_byte) noexcept {
    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
    return static_cast<size_t>(std::lower_bound(keys.begin(),
                                                keys.begin() + children_count,
                                                key_byte) -
                               keys.begin());
  }

  template <typename Keys_type, typename Children_type>
  static void insert_into_sorted_key_children_arrays(
      Keys_type &keys, Children_type &children, uint8_t &children_count,
      std::byte key_byte, single_value_leaf_unique_ptr &&child) {
    // TODO(laurynas): for Node4, sorting networks would be more efficient
    const auto insert_pos_index =
        get_sorted_key_array_insert_position(keys, children_count, key_byte);
    if (insert_pos_index != children_count) {
      assert(keys[insert_pos_index] != key_byte);
      // TODO(laurynas): does it compile to memcpy?
      std::copy_backward(keys.begin() + insert_pos_index,
                         keys.begin() + children_count,
                         keys.begin() + children_count + 1);
      uninitialized_move_backward(children.begin() + insert_pos_index,
                                  children.begin() + children_count,
                                  children.begin() + children_count + 1);
    }
    keys[insert_pos_index] = key_byte;
    new (&children[insert_pos_index].leaf)
        single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
    assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
  }

  const node_header header;

  uint8_t children_count;

  key_prefix_size_type key_prefix_len;
  key_prefix_type key_prefix;

  // TODO(laurynas): a better way?
  friend class internal_node_4;
  friend class internal_node_16;
  friend class internal_node_48;
  friend class internal_node_256;
};

template <unsigned Capacity, typename Derived>
class internal_node_template : public internal_node {
 public:
  [[nodiscard]] static void *operator new(std::size_t size) {
    return get_internal_node_pool<Derived>()->allocate(size);
  }

  static void operator delete(void *to_delete) {
    get_internal_node_pool<Derived>()->deallocate(to_delete, sizeof(Derived));
  }

  [[nodiscard]] bool is_full() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() !=
           node_type::LEAF);
    return children_count == capacity;
  }

 protected:
  using internal_node::internal_node;

  static constexpr auto capacity = Capacity;

  // TODO(laurynas): better way?
  friend class internal_node_48;
};

class internal_node_4 final
    : public internal_node_template<4, internal_node_4> {
 public:
  // Create a new node with two given child nodes
  [[nodiscard]] static std::unique_ptr<internal_node_4> create(
      art_key_type k1, art_key_type k2, db::tree_depth_type depth,
      node_ptr &&child1, node_ptr &&child2) {
    return std::make_unique<internal_node_4>(k1, k2, depth, std::move(child1),
                                             std::move(child2));
  }

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused this
  // key prefix split
  [[nodiscard]] static std::unique_ptr<internal_node_4> create(
      node_ptr &&source_node, unsigned len, db::tree_depth_type depth,
      node_ptr &&new_child) {
    return std::make_unique<internal_node_4>(std::move(source_node), len, depth,
                                             std::move(new_child));
  }

  internal_node_4(art_key_type k1, art_key_type k2, db::tree_depth_type depth,
                  node_ptr &&child1, node_ptr &&child2) noexcept;

  internal_node_4(node_ptr &&source_node, unsigned len,
                  db::tree_depth_type depth, node_ptr &&child1) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == node_type::I4);
    Expects(!is_full());
    const auto key_byte = single_value_leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(keys, children, children_count,
                                           key_byte, std::move(child));
  }

  [[nodiscard]] __attribute__((pure)) node_ptr *find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os, unsigned indent) const;
#endif

 private:
  // TODO(laurynas): a better way?
  friend class internal_node_16;

  void add_two_to_empty(std::byte key1, node_ptr &&child1, std::byte key2,
                        node_ptr &&child2) noexcept;

  std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;
};

internal_node_4::internal_node_4(art_key_type k1, art_key_type k2,
                                 db::tree_depth_type depth, node_ptr &&child1,
                                 node_ptr &&child2) noexcept
    : internal_node_template<4, internal_node_4>{node_type::I4, 2, k1, k2,
                                                 depth} {
  const auto next_level_depth = depth + get_key_prefix_len();
  add_two_to_empty(k1[next_level_depth], std::move(child1),
                   k2[next_level_depth], std::move(child2));
}

internal_node_4::internal_node_4(node_ptr &&source_node, unsigned len,
                                 db::tree_depth_type depth,
                                 node_ptr &&child1) noexcept
    : internal_node_template<4, internal_node_4>{
          node_type::I4, 2, gsl::narrow_cast<key_prefix_size_type>(len),
          source_node.internal->key_prefix} {
  Expects(source_node.type() != node_type::LEAF);
  Expects(child1.type() == node_type::LEAF);
  Expects(len < source_node.internal->get_key_prefix_len());
  const auto source_node_key_byte = source_node.internal->key_prefix_byte(
      gsl::narrow_cast<key_prefix_size_type>(len));
  source_node.internal->cut_prefix(len + 1);
  const auto new_key_byte =
      single_value_leaf::key(child1.leaf.get())[depth + len];
  add_two_to_empty(new_key_byte, std::move(child1), source_node_key_byte,
                   std::move(source_node));
}

void internal_node_4::add_two_to_empty(std::byte key1, node_ptr &&child1,
                                       std::byte key2,
                                       node_ptr &&child2) noexcept {
  const size_t key1_i = key1 < key2 ? 0 : 1;
  const size_t key2_i = key1_i == 0 ? 1 : 0;
  keys[key1_i] = key1;
  new (&children[key1_i].leaf) node_ptr{std::move(child1)};
  keys[key2_i] = key2;
  new (&children[key2_i].leaf) node_ptr{std::move(child2)};
  assert(std::is_sorted(keys.cbegin(), keys.cbegin() + children_count));
}

#ifndef NDEBUG

void internal_node_4::dump(std::ostream &os, unsigned indent) const {
  os << ", key bytes =";
  for (size_t i = 0; i < children_count; i++) dump_byte(os, keys[i]);
  os << ", children:\n";
  for (size_t i = 0; i < children_count; i++)
    dump_node(os, children[i], indent + 2);
}

#endif

node_ptr *internal_node_4::find_child(std::byte key_byte) noexcept {
  assert(reinterpret_cast<const node_header *>(this)->type() == node_type::I4);
  for (unsigned i = 0; i < children_count; i++)
    if (keys[i] == key_byte) return &children[i];
  return nullptr;
}

class internal_node_16 final
    : public internal_node_template<16, internal_node_16> {
 public:
  [[nodiscard]] static std::unique_ptr<internal_node_16> create(
      std::unique_ptr<internal_node> &&node,
      single_value_leaf_unique_ptr &&child, db::tree_depth_type depth) {
    return std::make_unique<internal_node_16>(std::move(node), std::move(child),
                                              depth);
  }

  internal_node_16(std::unique_ptr<internal_node> &&node,
                   single_value_leaf_unique_ptr &&child,
                   db::tree_depth_type depth) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == node_type::I16);
    Expects(!is_full());
    const auto key_byte = single_value_leaf::key(child.get())[depth];
    insert_into_sorted_key_children_arrays(keys, children, children_count,
                                           key_byte, std::move(child));
  }

  [[nodiscard]] __attribute__((pure)) node_ptr *find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os, unsigned indent) const;
#endif

 private:
  alignas(__m128i) std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;

  // TODO(laurynas): better way?
  friend class internal_node_48;
};

internal_node_16::internal_node_16(std::unique_ptr<internal_node> &&node,
                                   single_value_leaf_unique_ptr &&child,
                                   db::tree_depth_type depth) noexcept
    : internal_node_template<16, internal_node_16>{
          node_type::I16, 5, node->get_key_prefix_len(), node->key_prefix} {
  Expects(node->header.type() == node_type::I4);
  Expects(node->is_full());
  const auto node4{std::unique_ptr<internal_node_4>{
      static_cast<internal_node_4 *>(node.release())}};
  const auto key_byte = single_value_leaf::key(child.get())[depth];
  const auto insert_pos_index = get_sorted_key_array_insert_position(
      node4->keys, node4->children_count, key_byte);
  std::copy(node4->keys.cbegin(), node4->keys.cbegin() + insert_pos_index,
            keys.begin());
  keys[insert_pos_index] = key_byte;
  std::copy(node4->keys.cbegin() + insert_pos_index, node4->keys.cend(),
            keys.begin() + insert_pos_index + 1);
  std::uninitialized_move(node4->children.begin(),
                          node4->children.begin() + insert_pos_index,
                          children.begin());
  new (&children[insert_pos_index].leaf)
      single_value_leaf_unique_ptr{std::move(child)};
  std::uninitialized_move(node4->children.begin() + insert_pos_index,
                          node4->children.end(),
                          children.begin() + insert_pos_index + 1);
}

node_ptr *internal_node_16::find_child(std::byte key_byte) noexcept {
  assert(reinterpret_cast<const node_header *>(this)->type() == node_type::I16);
  const auto replicated_search_key = _mm_set1_epi8(static_cast<char>(key_byte));
  // TODO(laurynas): the keys field is alignas(__m128i) but that does not
  // silence GCC -Wcast-align warning. Try a union.
  const auto matching_key_positions = _mm_cmpeq_epi8(
      replicated_search_key, *reinterpret_cast<__m128i *>(&keys));
  const auto mask = (1U << children_count) - 1;
  const auto bit_field =
      static_cast<unsigned>(_mm_movemask_epi8(matching_key_positions)) & mask;
  if (bit_field != 0)
    return &children[static_cast<unsigned>(__builtin_ctz(bit_field))];
  return nullptr;
}

#ifndef NDEBUG

void internal_node_16::dump(std::ostream &os, unsigned indent) const {
  os << ", key bytes =";
  for (size_t i = 0; i < children_count; i++) dump_byte(os, keys[i]);
  os << ", children:\n";
  for (size_t i = 0; i < children_count; i++)
    dump_node(os, children[i], indent + 2);
}

#endif

class internal_node_48 final
    : public internal_node_template<48, internal_node_48> {
 public:
  [[nodiscard]] static std::unique_ptr<internal_node_48> create(
      std::unique_ptr<internal_node> &&node,
      single_value_leaf_unique_ptr &&child, db::tree_depth_type depth) {
    return std::make_unique<internal_node_48>(std::move(node), std::move(child),
                                              depth);
  }

  internal_node_48(std::unique_ptr<internal_node> &&node,
                   single_value_leaf_unique_ptr &&child,
                   db::tree_depth_type depth) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == node_type::I48);
    Expects(!is_full());
    const auto key_byte =
        static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
    assert(child_indexes[key_byte] == empty_child);
    child_indexes[key_byte] = children_count;
    new (&children[children_count].leaf)
        single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
  }

  [[nodiscard]] __attribute__((pure)) node_ptr *find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os, unsigned indent) const;
#endif

 private:
  std::array<uint8_t, 256> child_indexes;
  std::array<node_ptr, capacity> children;

  static constexpr decltype(child_indexes)::size_type empty_child = 0xFF;

  // TODO(laurynas): a better way?
  friend class internal_node_256;
};

internal_node_48::internal_node_48(std::unique_ptr<internal_node> &&node,
                                   single_value_leaf_unique_ptr &&child,
                                   db::tree_depth_type depth) noexcept
    : internal_node_template<48, internal_node_48>{
          node_type::I48, 17, node->get_key_prefix_len(), node->key_prefix} {
  Expects(node->header.type() == node_type::I16);
  Expects(node->is_full());
  const auto node16{std::unique_ptr<internal_node_16>{
      static_cast<internal_node_16 *>(node.release())}};
  memset(&child_indexes[0], empty_child,
         child_indexes.size() * sizeof(child_indexes[0]));
  uint8_t i;
  for (i = 0; i < node16->capacity; i++) {
    const auto existing_key_byte = node16->keys[i];
    child_indexes[static_cast<decltype(child_indexes)::size_type>(
        existing_key_byte)] = i;
    new (&children[i].leaf)
        single_value_leaf_unique_ptr{std::move(node16->children[i].leaf)};
  }
  const auto key_byte = single_value_leaf::key(child.get())[depth];
  // TODO(laurynas) assert it's empty_child
  child_indexes[static_cast<decltype(child_indexes)::size_type>(key_byte)] = i;
  new (&children[i].leaf) single_value_leaf_unique_ptr{std::move(child)};
  // TODO(laurynas): assert keys are sorted
}

node_ptr *internal_node_48::find_child(std::byte key_byte) noexcept {
  assert(reinterpret_cast<const node_header *>(this)->type() == node_type::I48);
  if (child_indexes[static_cast<uint8_t>(key_byte)] != empty_child)
    return &children[child_indexes[static_cast<uint8_t>(key_byte)]];
  return nullptr;
}

#ifndef NDEBUG

void internal_node_48::dump(std::ostream &os, unsigned indent) const {
  os << ", key bytes & child indexes\n";
  for (size_t i = 0; i < children_count; i++) {
    if (child_indexes[i] != empty_child) {
      os << " ";
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      os << ", child index = " << static_cast<unsigned>(child_indexes[i])
         << '\n';
    }
  }
  os << "children:\n";
  for (size_t i = 0; i < children_count; i++) {
    if (child_indexes[i] != 0) {
      os << " " << i << ": ";
      dump_node(os, children[child_indexes[i]], indent + 2);
    }
  }
}

#endif

class internal_node_256 final
    : public internal_node_template<256, internal_node_256> {
 public:
  [[nodiscard]] static std::unique_ptr<internal_node_256> create(
      std::unique_ptr<internal_node> &&node,
      single_value_leaf_unique_ptr &&child, db::tree_depth_type depth) {
    return std::make_unique<internal_node_256>(std::move(node),
                                               std::move(child), depth);
  }

  internal_node_256(std::unique_ptr<internal_node> &&node,
                    single_value_leaf_unique_ptr &&child,
                    db::tree_depth_type depth) noexcept;

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == node_type::I256);
    Expects(!is_full());
    const auto key_byte =
        static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
    assert(children[key_byte] == nullptr);
    new (&children[key_byte].leaf)
        single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
  }

  [[nodiscard]] __attribute__((pure)) node_ptr *find_child(
      std::byte key_byte) noexcept;

#ifndef NDEBUG
  void dump(std::ostream &os, unsigned indent) const;
#endif

 private:
  std::array<node_ptr, capacity> children;
};

internal_node_256::internal_node_256(std::unique_ptr<internal_node> &&node,
                                     single_value_leaf_unique_ptr &&child,
                                     db::tree_depth_type depth) noexcept
    : internal_node_template<256, internal_node_256>{
          node_type::I256, 49, node->get_key_prefix_len(), node->key_prefix} {
  Expects(node->header.type() == node_type::I48);
  Expects(node->is_full());
  const auto node48{std::unique_ptr<internal_node_48>{
      static_cast<internal_node_48 *>(node.release())}};
  for (unsigned i = 0; i < 256; i++) {
    if (node48->child_indexes[i] != internal_node_48::empty_child) {
      new (&children[i].leaf) single_value_leaf_unique_ptr{
          std::move(node48->children[node48->child_indexes[i]].leaf)};
    } else {
      new (&children[i]) node_ptr{nullptr};
    }
  }
  const uint8_t key_byte =
      static_cast<uint8_t>(single_value_leaf::key(child.get())[depth]);
  assert(children[key_byte] == nullptr);
  new (&children[key_byte].leaf) single_value_leaf_unique_ptr{std::move(child)};
}

node_ptr *internal_node_256::find_child(std::byte key_byte) noexcept {
  assert(reinterpret_cast<const node_header *>(this)->type() ==
         node_type::I256);
  if (children[static_cast<uint8_t>(key_byte)] != nullptr)
    return &children[static_cast<uint8_t>(key_byte)];
  return nullptr;
}

#ifndef NDEBUG

void internal_node_256::dump(std::ostream &os, unsigned indent) const {
  os << ", key bytes & children:\n";
  for (size_t i = 0; i < children_count; i++) {
    if (children[i] != nullptr) {
      os << " ";
      dump_byte(os, gsl::narrow_cast<std::byte>(i));
      dump_node(os, children[i], indent + 2);
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

inline void internal_node::add(single_value_leaf_unique_ptr &&child,
                               db::tree_depth_type depth) noexcept {
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

inline node_ptr *internal_node::find_child(std::byte key_byte) noexcept {
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

void internal_node::dump(std::ostream &os, unsigned indent) const {
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
  os << ", key prefix len = " << static_cast<unsigned>(key_prefix_len);
  os << ", key prefix =";
  for (size_t i = 0; i < key_prefix_len; i++) dump_byte(os, key_prefix[i]);
  switch (header.type()) {
    case node_type::I4:
      static_cast<const internal_node_4 *>(this)->dump(os, indent);
      break;
    case node_type::I16:
      static_cast<const internal_node_16 *>(this)->dump(os, indent);
      break;
    case node_type::I48:
      static_cast<const internal_node_48 *>(this)->dump(os, indent);
      break;
    case node_type::I256:
      static_cast<const internal_node_256 *>(this)->dump(os, indent);
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

[[nodiscard]] __attribute__((pure)) auto get_shared_prefix_len(
    unodb::art_key_type k, const unodb::internal_node &node,
    unodb::db::tree_depth_type depth) noexcept {
  unodb::db::tree_depth_type key_i = depth;
  unsigned shared_prefix_len = 0;
  while (shared_prefix_len < node.get_key_prefix_len()) {
    if (k[key_i] != node.key_prefix_byte(gsl::narrow_cast<key_prefix_size_type>(
                        shared_prefix_len)))
      break;
    ++key_i;
    ++shared_prefix_len;
  }
  return shared_prefix_len;
}

}  // namespace

namespace unodb {

node_ptr::node_ptr(std::unique_ptr<internal_node> &&node) noexcept
    : internal{std::move(node)} {}

db::get_result db::get(key_type k) const noexcept {
  if (root.header == nullptr) return {};
  return get_from_subtree(root, art_key{k}, 0);
}

db::get_result db::get_from_subtree(const node_ptr &node, art_key_type k,
                                    tree_depth_type depth) noexcept {
  if (node.type() == node_type::LEAF) {
    if (single_value_leaf::matches(node.leaf.get(), k)) {
      const auto value = single_value_leaf::value(node.leaf.get());
      return get_result{std::in_place, value.cbegin(), value.cend()};
    }
    return {};
  }
  assert(node.type() != node_type::LEAF);
  if (get_shared_prefix_len(k, *node.internal, depth) <
      node.internal->get_key_prefix_len())
    return {};
  depth += node.internal->get_key_prefix_len();
  const auto child = node.internal->find_child(k[depth]);
  if (child == nullptr) return {};
  return get_from_subtree(*child, k, depth + 1);
}

bool db::insert(key_type k, value_view v) {
  const auto bin_comparable_key = art_key{k};
  auto leaf = single_value_leaf::create(bin_comparable_key, v);
  if (BOOST_UNLIKELY(root.header == nullptr)) {
    root.leaf = std::move(leaf);
    return true;
  }
  return insert_leaf(bin_comparable_key, &root, std::move(leaf), 0);
}

bool db::insert_leaf(art_key_type k, node_ptr *node,
                     single_value_leaf_unique_ptr leaf, tree_depth_type depth) {
  if (node->type() == node_type::LEAF) {
    const auto existing_key = single_value_leaf::key(node->leaf.get());
    if (BOOST_UNLIKELY(k == existing_key)) return false;
    auto new_node = internal_node_4::create(
        k, existing_key, depth, node_ptr{std::move(leaf)}, std::move(*node));
    node->internal = std::move(new_node);
    return true;
  }
  assert(node->type() != node_type::LEAF);
  const auto shared_prefix_len =
      get_shared_prefix_len(k, *node->internal, depth);
  if (shared_prefix_len < node->internal->get_key_prefix_len()) {
    auto new_node = internal_node_4::create(std::move(*node), shared_prefix_len,
                                            depth, node_ptr{std::move(leaf)});
    node->internal = std::move(new_node);
    return true;
  }
  depth += node->internal->get_key_prefix_len();
  auto child = node->internal->find_child(k[depth]);
  if (child != nullptr)
    return insert_leaf(k, child, std::move(leaf), depth + 1);
  if (BOOST_UNLIKELY(node->internal->is_full())) {
    if (node->type() == node_type::I4) {
      auto larger_node = internal_node_16::create(std::move(node->internal),
                                                  std::move(leaf), depth);
      node->internal = std::move(larger_node);
    } else if (node->type() == node_type::I16) {
      auto larger_node = internal_node_48::create(std::move(node->internal),
                                                  std::move(leaf), depth);
      node->internal = std::move(larger_node);
    } else {
      assert(node->type() == node_type::I48);
      auto larger_node = internal_node_256::create(std::move(node->internal),
                                                   std::move(leaf), depth);
      node->internal = std::move(larger_node);
    }
  } else {
    node->internal->add(std::move(leaf), depth);
  }
  return true;
}

#ifndef NDEBUG

}  // namespace unodb

namespace {

// TODO(laurynas): indent is not really used
void dump_node(std::ostream &os, const unodb::node_ptr &node, unsigned indent) {
  os << std::string(indent, ' ') << "node at: " << &node;
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
      node.internal->dump(os, indent);
      break;
  }
}

}  // namespace

namespace unodb {

void db::dump(std::ostream &os) const {
  os << "db dump:\n";
  dump_node(os, root, 0);
}

#endif

}  // namespace unodb
