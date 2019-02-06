// Copyright 2019 Laurynas Biveinis
#include "art.hpp"

#include <stdexcept>

// Internal key type must be POD and have no overhead over API key type
static_assert(std::is_trivial<unodb::art_key_type>::value);
static_assert(sizeof(unodb::art_key_type) == sizeof(unodb::key_type));

namespace {

// TODO(laurynas): useless. Always have Raw_key as-is, and
// always have Key in binary-comparable format.
template <typename Raw_key>
[[nodiscard]] Raw_key make_binary_comparable(Raw_key key) noexcept;

template <>
[[nodiscard]] uint64_t make_binary_comparable(uint64_t key) noexcept {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(key);
#else
#error Needs implementing
#endif
}

template <typename Key>
[[nodiscard]] Key make_binary_comparable(Key key) noexcept {
  return Key{make_binary_comparable(key.key)};
}

inline auto type(const unodb::node_ptr node) noexcept {
  return node.header->type();
}

inline boost::container::pmr::memory_resource *get_internal_node_4_pool() {
  // TODO(laurynas) pool options
  static boost::container::pmr::unsynchronized_pool_resource node_4_pool;
  return &node_4_pool;
}

}  // namespace

namespace unodb {

void single_value_leaf_deleter::operator()(
    single_value_leaf_type to_delete) const noexcept {
  const auto s = single_value_leaf::size(to_delete);
  // TODO(laurynas): hide new_delete_resource() call, here and in creator
  boost::container::pmr::new_delete_resource()->deallocate(to_delete, s);
}

single_value_leaf_unique_ptr single_value_leaf::make(art_key_type k,
                                                     value_view v) {
  if (v.size() > std::numeric_limits<value_size_type>::max()) {
    throw std::length_error("Value length must fit in uint32_t");
  }
  const auto value_size = static_cast<value_size_type>(v.size());
  const auto leaf_size = static_cast<std::size_t>(offset_value) + value_size;
  auto *const leaf_mem = static_cast<std::byte *>(
      boost::container::pmr::new_delete_resource()->allocate(leaf_size));
  new (leaf_mem) node_header{node_type::LEAF};
  k.copy_to(&leaf_mem[offset_key]);
  memcpy(&leaf_mem[offset_value_size], &value_size, sizeof(value_size_type));
  if (!v.empty())
    memcpy(&leaf_mem[offset_value], &v[0], static_cast<std::size_t>(v.size()));
  return single_value_leaf_unique_ptr(leaf_mem);
}

void internal_node_4_deleter::operator()(internal_node_4 *to_delete) const
    noexcept {
  get_internal_node_4_pool()->deallocate(to_delete, sizeof(*to_delete));
}

internal_node_4_unique_ptr internal_node_4::create() {
  auto *const node_mem = static_cast<internal_node_4 *>(
      get_internal_node_4_pool()->allocate(sizeof(internal_node_4)));
  return internal_node_4_unique_ptr{new (node_mem) internal_node_4};
}

void internal_node_4::add_two_to_empty(single_value_leaf_unique_ptr &&child1,
                                       single_value_leaf_unique_ptr &&child2,
                                       unsigned depth) noexcept {
  Expects(children_count == 0);
  const auto key1 = single_value_leaf::key(child1.get())[depth];
  keys[0] = key1;
  children[0].leaf = std::move(child1);
  const auto key2 = single_value_leaf::key(child2.get())[depth];
  keys[1] = key2;
  children[1].leaf = std::move(child2);
  children_count = 2;
}

const node_ptr internal_node_4::find_child(std::byte key_byte) const noexcept {
  for (unsigned i = 0; i < children_count; i++)
    if (keys[i] == key_byte) return children[i];
  return node_ptr{};
}

db::get_result db::get(key_type k) noexcept {
  return get_from_subtree(root, art_key{make_binary_comparable(k)}, 0);
}

db::get_result db::get_from_subtree(const node_ptr node, art_key_type k,
                                    unsigned depth) const noexcept {
  if (!node.header) return get_result{};
  if (type(node) == node_type::LEAF) {
    if (single_value_leaf::matches(node.leaf.get(), k)) {
      const auto value_view = single_value_leaf::value(node.leaf.get());
      return get_result{std::in_place, value_view.cbegin(), value_view.cend()};
    } else {
      return get_result{};
    }
  }
  assert(type(node) == node_type::I4);
  if (!key_prefix_matches(k, *node.i4, depth)) return get_result{};
  depth += node.i4->key_prefix_len;
  const auto child = node.i4->find_child(k[depth]);
  return get_from_subtree(child, k, depth + 1);
}

void db::insert(key_type k, value_view v) {
  const auto bin_comparable_key = art_key{make_binary_comparable(k)};
  auto leaf_node = single_value_leaf::make(bin_comparable_key, v);
  insert_node(bin_comparable_key, std::move(leaf_node), 0);
}

void db::insert_node(art_key_type k, single_value_leaf_unique_ptr node,
                     unsigned depth) {
  if (!root.header) {
    root.leaf = std::move(node);
    return;
  }
  if (type(root) == node_type::LEAF) {
    auto new_node = internal_node_4::create();
    const auto existing_key = single_value_leaf::key(root.leaf.get());
    unsigned i;
    for (i = depth; k[i] == existing_key[i]; i++) {
      assert(i - depth < internal_node_4::key_prefix_capacity);
      new_node->key_prefix[i - depth] = k[i];
    }
    new_node->key_prefix_len = gsl::narrow_cast<uint8_t>(i - depth);
    depth += new_node->key_prefix_len;
    new_node->add_two_to_empty(std::move(node), std::move(root.leaf), depth);
    root.i4 = std::move(new_node);
    return;
  }
  assert(0);
}

bool db::key_prefix_matches(art_key_type k, const internal_node_4 &node,
                            unsigned depth) const noexcept {
  unsigned key_i = depth;
  uint8_t prefix_i = 0;
  while (prefix_i < node.key_prefix_len) {
    if (k[key_i] != node.key_prefix[prefix_i]) return false;
    key_i++;
    prefix_i++;
  }
  return true;
}

}  // namespace unodb
