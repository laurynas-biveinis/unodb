// Copyright 2019 Laurynas Biveinis
#include "art.hpp"

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
static_assert(sizeof(unodb::node_ptr::i4) == sizeof(unodb::node_ptr::header),
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

enum class node_type : uint8_t { LEAF, I4 };

// A common prefix shared by all node types
struct node_header final {
  explicit node_header(node_type type_) : m_type{type_} {}

  [[nodiscard]] auto type() const noexcept { return m_type; }

 private:
  const node_type m_type;
};

}  // namespace unodb

namespace {

using key_prefix_size_type = uint8_t;

[[nodiscard]] inline auto type(const unodb::node_ptr node) noexcept {
  return node.header->type();
}

[[nodiscard]] inline boost::container::pmr::pool_options
get_node_4_pool_options();

[[nodiscard]] inline auto *get_leaf_node_pool() {
  return boost::container::pmr::new_delete_resource();
}

[[nodiscard]] inline auto *get_internal_node_4_pool() {
  static boost::container::pmr::unsynchronized_pool_resource node_4_pool{
      get_node_4_pool_options()};
  return &node_4_pool;
}

#ifndef NDEBUG

void dump_byte(std::ostream &os, std::byte byte) noexcept {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

void dump_key(std::ostream &os, unodb::art_key_type key) noexcept {
  for (size_t i = 0; i < sizeof(key); i++) dump_byte(os, key[i]);
}

void dump_node(std::ostream &os, const unodb::node_ptr &node,
               unsigned indent) noexcept;

#endif

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
    return value_view(&leaf[offset_value], value_size(leaf));
  }

  [[nodiscard]] static std::size_t size(single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    return value_size(leaf) + offset_value;
  }

#ifndef NDEBUG
  static void dump(std::ostream &os, single_value_leaf_type leaf) noexcept;
#endif

 private:
  using value_size_type = uint32_t;

  static const constexpr auto offset_header = 0;
  static const constexpr auto offset_key = sizeof(node_header);
  static const constexpr auto offset_value_size =
      offset_key + sizeof(art_key_type);

  static const constexpr auto offset_value =
      offset_value_size + sizeof(value_size_type);

  static const constexpr auto minimum_size = offset_value;

  [[nodiscard]] static value_size_type value_size(
      single_value_leaf_type leaf) noexcept {
    assert(reinterpret_cast<node_header *>(leaf)->type() == node_type::LEAF);
    value_size_type result;
    memcpy(&result, &leaf[offset_value_size], sizeof(result));
    return result;
  }
};

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

void single_value_leaf::dump(std::ostream &os,
                             single_value_leaf_type leaf) noexcept {
  os << "key:";
  dump_key(os, key(leaf));
  os << ", value size: " << value_size(leaf) << '\n';
}

#endif

class internal_node_4 final {
 public:
  static const constexpr key_prefix_size_type key_prefix_capacity = 8;

  [[nodiscard]] static void *operator new(std::size_t size) {
    return get_internal_node_4_pool()->allocate(size);
  }

  static void operator delete(void *to_delete) {
    get_internal_node_4_pool()->deallocate(to_delete, sizeof(internal_node_4));
  }

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

  void add(single_value_leaf_unique_ptr &&child,
           db::tree_depth_type depth) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == node_type::I4);
    Expects(!is_full());
    const auto key_byte = single_value_leaf::key(child.get())[depth];
    keys[children_count] = key_byte;
    new (&children[children_count].leaf)
        single_value_leaf_unique_ptr{std::move(child)};
    ++children_count;
  }

  internal_node_4(art_key_type k1, art_key_type k2, db::tree_depth_type depth,
                  node_ptr &&child1, node_ptr &&child2) noexcept;

  internal_node_4(node_ptr &&source_node, unsigned len,
                  db::tree_depth_type depth, node_ptr &&child1) noexcept;

  void cut_prefix(unsigned cut_len) noexcept {
    assert(reinterpret_cast<node_header *>(this)->type() == node_type::I4);
    Expects(cut_len > 0);
    Expects(cut_len <= key_prefix_len);
    std::copy(key_prefix.cbegin() + cut_len, key_prefix.cend(),
              key_prefix.begin());
    key_prefix_len =
        gsl::narrow_cast<key_prefix_size_type>(key_prefix_len - cut_len);
    assert(key_prefix_len <= key_prefix_capacity);
  }

  [[nodiscard]] __attribute__((pure)) node_ptr *find_child(
      std::byte key_byte) noexcept;

  [[nodiscard]] bool is_full() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() ==
           node_type::I4);
    return children_count == capacity;
  }

  [[nodiscard]] key_prefix_size_type get_key_prefix_len() const noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() ==
           node_type::I4);
    return key_prefix_len;
  }

  [[nodiscard]] std::byte key_prefix_byte(key_prefix_size_type i) const
      noexcept {
    assert(reinterpret_cast<const node_header *>(this)->type() ==
           node_type::I4);
    Expects(i < key_prefix_len);
    return key_prefix[i];
  }

#ifndef NDEBUG
  void dump(std::ostream &os, unsigned indent) const noexcept;
#endif

 private:
  static const constexpr auto capacity = 4;

  node_header header{node_type::I4};

  uint8_t children_count{2};

  key_prefix_size_type key_prefix_len;
  std::array<std::byte, key_prefix_capacity> key_prefix;

  std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;

  void add_two_to_empty(std::byte key1, node_ptr &&child1, std::byte key2,
                        node_ptr &&child2) noexcept;
};

internal_node_4::internal_node_4(art_key_type k1, art_key_type k2,
                                 db::tree_depth_type depth, node_ptr &&child1,
                                 node_ptr &&child2) noexcept {
  db::tree_depth_type i;
  for (i = depth; k1[i] == k2[i]; ++i) {
    assert(i - depth < key_prefix_capacity);
    key_prefix[i - depth] = k1[i];
  }
  key_prefix_len = gsl::narrow_cast<key_prefix_size_type>(i - depth);
  assert(key_prefix_len <= key_prefix_capacity);
  const auto next_level_depth = depth + get_key_prefix_len();
  add_two_to_empty(k1[next_level_depth], std::move(child1),
                   k2[next_level_depth], std::move(child2));
}

internal_node_4::internal_node_4(node_ptr &&source_node, unsigned len,
                                 db::tree_depth_type depth,
                                 node_ptr &&child1) noexcept
    : key_prefix_len{gsl::narrow_cast<key_prefix_size_type>(len)} {
  Expects(type(source_node) == node_type::I4);
  Expects(type(child1) == node_type::LEAF);
  Expects(len < source_node.i4->key_prefix_len);
  assert(key_prefix_len <= key_prefix_capacity);
  std::copy(source_node.i4->key_prefix.cbegin(),
            source_node.i4->key_prefix.cbegin() + len, key_prefix.begin());
  const auto source_node_key_byte = source_node.i4->key_prefix_byte(
      gsl::narrow_cast<key_prefix_size_type>(len));
  source_node.i4->cut_prefix(len + 1);
  const auto new_key_byte =
      single_value_leaf::key(child1.leaf.get())[depth + len];
  add_two_to_empty(new_key_byte, std::move(child1), source_node_key_byte,
                   std::move(source_node));
}

void internal_node_4::add_two_to_empty(std::byte key1, node_ptr &&child1,
                                       std::byte key2,
                                       node_ptr &&child2) noexcept {
  keys[0] = key1;
  new (&children[0].leaf) node_ptr{std::move(child1)};
  keys[1] = key2;
  new (&children[1].leaf) node_ptr{std::move(child2)};
}

#ifndef NDEBUG

void internal_node_4::dump(std::ostream &os, unsigned indent) const noexcept {
  os << "# children = " << static_cast<unsigned>(children_count);
  os << ", key prefix len = " << static_cast<unsigned>(key_prefix_len);
  os << ", key prefix =";
  for (size_t i = 0; i < key_prefix_len; i++) dump_byte(os, key_prefix[i]);
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

}  // namespace unodb

namespace {

// For Node4 pool, approximate requesting ~2MB blocks from backing storage
// (when ported to Linux, ask for 2MB huge pages directly)
boost::container::pmr::pool_options get_node_4_pool_options() {
  struct boost::container::pmr::pool_options node_4_pool_options;
  node_4_pool_options.max_blocks_per_chunk =
      2 * 1024 * 1024 / sizeof(unodb::internal_node_4);
  node_4_pool_options.largest_required_pool_block =
      sizeof(unodb::internal_node_4);
  return node_4_pool_options;
}

[[nodiscard]] __attribute__((pure)) auto get_shared_prefix_len(
    unodb::art_key_type k, const unodb::internal_node_4 &node,
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

node_ptr::node_ptr(std::unique_ptr<internal_node_4> &&node) noexcept
    : i4{std::move(node)} {}

db::get_result db::get(key_type k) noexcept {
  if (root.header == nullptr) return {};
  return get_from_subtree(root, art_key{k}, 0);
}

db::get_result db::get_from_subtree(const node_ptr node, art_key_type k,
                                    tree_depth_type depth) const noexcept {
  if (type(node) == node_type::LEAF) {
    if (single_value_leaf::matches(node.leaf.get(), k)) {
      const auto value = single_value_leaf::value(node.leaf.get());
      return get_result{std::in_place, value.cbegin(), value.cend()};
    }
    return {};
  }
  assert(type(node) == node_type::I4);
  if (get_shared_prefix_len(k, *node.i4, depth) < node.i4->get_key_prefix_len())
    return {};
  depth += node.i4->get_key_prefix_len();
  const auto child = node.i4->find_child(k[depth]);
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
  if (type(*node) == node_type::LEAF) {
    const auto existing_key = single_value_leaf::key(node->leaf.get());
    if (BOOST_UNLIKELY(k == existing_key)) return false;
    auto new_node = internal_node_4::create(
        k, existing_key, depth, node_ptr{std::move(leaf)}, std::move(*node));
    node->i4 = std::move(new_node);
    return true;
  }
  assert(type(*node) == node_type::I4);
  const auto shared_prefix_len = get_shared_prefix_len(k, *node->i4, depth);
  if (shared_prefix_len < node->i4->get_key_prefix_len()) {
    auto new_node = internal_node_4::create(std::move(*node), shared_prefix_len,
                                            depth, node_ptr{std::move(leaf)});
    node->i4 = std::move(new_node);
    return true;
  }
  depth += node->i4->get_key_prefix_len();
  auto child = node->i4->find_child(k[depth]);
  if (child != nullptr)
    return insert_leaf(k, child, std::move(leaf), depth + 1);
  if (BOOST_UNLIKELY(node->i4->is_full())) {
    assert(0);
    throw std::logic_error("Not implemented yet");
  }
  node->i4->add(std::move(leaf), depth);
  return true;
}

#ifndef NDEBUG

}  // namespace unodb

namespace {

void dump_node(std::ostream &os, const unodb::node_ptr &node,
               unsigned indent) noexcept {
  os << std::string(indent, ' ') << "node at: " << &node;
  if (node.header == nullptr) {
    os << ", <null>\n";
    return;
  }
  os << ", type = ";
  switch (type(node)) {
    case unodb::node_type::LEAF:
      os << "LEAF, ";
      unodb::single_value_leaf::dump(os, node.leaf.get());
      break;
    case unodb::node_type::I4:
      os << "I4, ";
      node.i4->dump(os, indent);
      break;
  }
}

}  // namespace

namespace unodb {

void db::dump(std::ostream &os) const noexcept {
  os << "db dump:\n";
  dump_node(os, root, 0);
}

#endif

}  // namespace unodb
