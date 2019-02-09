// Copyright 2019 Laurynas Biveinis
#include "art.hpp"

#include <array>
#include <cassert>
#include <limits>
#include <stdexcept>
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
static_assert(sizeof(unodb::internal_node_4_unique_ptr) ==
                  sizeof(unodb::internal_node_4 *),
              "Node4 unique_ptr must have no overhead over raw pointer");

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
    return art_key_type::create(&leaf[offset_key]);
  }

  [[nodiscard]] static auto matches(single_value_leaf_type leaf,
                                    art_key_type k) noexcept {
    return k == leaf + offset_key;
  }

  [[nodiscard]] static auto value(single_value_leaf_type leaf) noexcept {
    return value_view(&leaf[offset_value], value_size(leaf));
  }

  [[nodiscard]] static std::size_t size(single_value_leaf_type leaf) noexcept {
    return value_size(leaf) + offset_value;
  }

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

class internal_node_4 final {
 public:
  static const constexpr key_prefix_size_type key_prefix_capacity = 8;

  internal_node_4() noexcept {}

  [[nodiscard]] static internal_node_4_unique_ptr create();

  void add_two_to_empty(single_value_leaf_unique_ptr &&child1,
                        single_value_leaf_unique_ptr &&child2,
                        db::tree_depth_type depth) noexcept;

  void set_key_prefix(art_key_type k1, art_key_type k2,
                      db::tree_depth_type depth) noexcept {
    db::tree_depth_type i;
    for (i = depth; k1[i] == k2[i]; ++i) {
      assert(i - depth < key_prefix_capacity);
      key_prefix[i - depth] = k1[i];
    }
    key_prefix_len = gsl::narrow_cast<key_prefix_size_type>(i - depth);
  }

  [[nodiscard]] const node_ptr find_child(std::byte key_byte) const noexcept;

  [[nodiscard]] key_prefix_size_type get_key_prefix_len() const noexcept {
    return key_prefix_len;
  }

  [[nodiscard]] std::byte key_prefix_byte(key_prefix_size_type i) const
      noexcept {
    Expects(i < key_prefix_len);
    return key_prefix[i];
  }

 private:
  static const constexpr auto capacity = 4;

  node_header header{node_type::I4};

  uint8_t children_count{0};

 private:
  key_prefix_size_type key_prefix_len{0};
  std::array<std::byte, key_prefix_capacity> key_prefix;

  std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;
};

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
                                       db::tree_depth_type depth) noexcept {
  Expects(children_count == 0);
  const auto key1_byte = single_value_leaf::key(child1.get())[depth];
  keys[0] = key1_byte;
  new (&children[0].leaf) single_value_leaf_unique_ptr{std::move(child1)};
  const auto key2_byte = single_value_leaf::key(child2.get())[depth];
  keys[1] = key2_byte;
  new (&children[1].leaf) single_value_leaf_unique_ptr{std::move(child2)};
  children_count = 2;
}

const node_ptr internal_node_4::find_child(std::byte key_byte) const noexcept {
  for (uint_fast8_t i = 0; i < children_count; i++)
    if (keys[i] == key_byte) return children[i];
  return node_ptr{nullptr};
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

__attribute__((pure)) auto key_prefix_matches(
    unodb::art_key_type k, const unodb::internal_node_4 &node,
    unodb::db::tree_depth_type depth) noexcept {
  unodb::db::tree_depth_type key_i = depth;
  uint_fast8_t prefix_i = 0;
  while (prefix_i < node.get_key_prefix_len()) {
    if (k[key_i] != node.key_prefix_byte(prefix_i)) return false;
    ++key_i;
    ++prefix_i;
  }
  return true;
}

}  // namespace

namespace unodb {

db::get_result db::get(key_type k) noexcept {
  return get_from_subtree(root, art_key{k}, 0);
}

db::get_result db::get_from_subtree(const node_ptr node, art_key_type k,
                                    tree_depth_type depth) const noexcept {
  if (node.header == nullptr) return {};
  if (type(node) == node_type::LEAF) {
    if (single_value_leaf::matches(node.leaf.get(), k)) {
      const auto value = single_value_leaf::value(node.leaf.get());
      return get_result{std::in_place, value.cbegin(), value.cend()};
    }
    return {};
  }
  assert(type(node) == node_type::I4);
  if (!key_prefix_matches(k, *node.i4, depth)) return {};
  depth += node.i4->get_key_prefix_len();
  const auto child = node.i4->find_child(k[depth]);
  return get_from_subtree(child, k, depth + 1);
}

void db::insert(key_type k, value_view v) {
  const auto bin_comparable_key = art_key{k};
  auto leaf_node = single_value_leaf::create(bin_comparable_key, v);
  insert_node(bin_comparable_key, std::move(leaf_node), 0);
}

void db::insert_node(art_key_type k, single_value_leaf_unique_ptr node,
                     tree_depth_type depth) {
  if (root.header == nullptr) {
    root.leaf = std::move(node);
    return;
  }
  if (type(root) == node_type::LEAF) {
    auto new_node = internal_node_4::create();
    const auto existing_key = single_value_leaf::key(root.leaf.get());
    new_node->set_key_prefix(existing_key, k, depth);
    depth += new_node->get_key_prefix_len();
    new_node->add_two_to_empty(std::move(node), std::move(root.leaf), depth);
    root.i4 = std::move(new_node);
    return;
  }
  assert(0);
}

}  // namespace unodb
