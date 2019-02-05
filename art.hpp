// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>  // for uint64_t
#include <cstdint>  // IWYU pragma: keep
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>
#include <gsl/span>

namespace unodb {

template <typename Key_type>
struct art_key {
  art_key() noexcept = default;

  explicit art_key(Key_type key_) noexcept : key(key_) {}

  static art_key create(const std::byte from[]) noexcept {
    struct art_key result;
    memcpy(&result, from, sizeof(result));
    return result;
  }

  void copy_to(std::byte to[]) const noexcept {
    memcpy(to, &key, sizeof(*this));
  }

  [[nodiscard]] bool operator==(const std::byte key2[]) const noexcept {
    return !memcmp(&key, key2, sizeof(*this));
  }

  [[nodiscard]] std::byte operator[](std::size_t index) const noexcept {
    Expects(index < sizeof(*this));
    // TODO(laurynas): absl::bit_cast?
    return (reinterpret_cast<const std::byte *>(&key))[index];
  }

  Key_type key;
};

// TODO(laurynas): this is in interface, hence should be key_type
using raw_key_type = uint64_t;

// TODO(laurynas): this is in implementation
using key_type = art_key<raw_key_type>;

static_assert(std::is_trivial<key_type>::value);
static_assert(sizeof(key_type) == sizeof(raw_key_type));

using value_view = gsl::span<const std::byte>;

enum class node_type : uint8_t { LEAF, I4 };

// A common prefix shared by all node types
struct node_header {
  explicit node_header(node_type type_) : m_type{type_} {}

  [[nodiscard]] auto type() const noexcept { return m_type; }

 private:
  const node_type m_type;
};

using single_value_leaf_type = std::byte[];

struct single_value_leaf_deleter {
  void operator()(single_value_leaf_type to_delete) const noexcept;
};

using single_value_leaf_unique_ptr =
    std::unique_ptr<single_value_leaf_type, single_value_leaf_deleter>;

class internal_node_4;

struct internal_node_4_deleter {
  void operator()(internal_node_4 *to_delete) const noexcept;
};

using internal_node_4_unique_ptr =
    std::unique_ptr<internal_node_4, internal_node_4_deleter>;

union node_ptr {
  node_header *header;
  single_value_leaf_unique_ptr leaf;
  internal_node_4_unique_ptr i4;

  static_assert(sizeof(leaf) == sizeof(header));
  static_assert(sizeof(i4) == sizeof(header));

  node_ptr() : header{nullptr} {};

  node_ptr(const node_ptr &other) noexcept : header{other.header} {}

  ~node_ptr() {}
};

// Helper struct for leaf node-related data and (static) code. We
// don't use a regular class because leaf nodes are of variable size, C++ does
// not support flexible array members, and we want to save one level of
// (heap) indirection.
struct single_value_leaf {
  static_assert(sizeof(single_value_leaf_unique_ptr) == sizeof(void *));

  using view_ptr = const std::byte *;
  // TODO(laurynas): rename to create
  [[nodiscard]] static single_value_leaf_unique_ptr make(key_type k,
                                                         value_view v);

  [[nodiscard]] static auto key(single_value_leaf_type leaf) noexcept {
    return key_type::create(&leaf[offset_key]);
  }

  [[nodiscard]] static bool matches(single_value_leaf_type leaf,
                                    key_type k) noexcept {
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
  static const constexpr auto offset_value_size = offset_key + sizeof(key_type);

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

inline void single_value_leaf_deleter::operator()(
    single_value_leaf_type to_delete) const noexcept {
  const auto s = single_value_leaf::size(to_delete);
  // TODO(laurynas): hide new_delete_resource() call, here and in creator
  boost::container::pmr::new_delete_resource()->deallocate(to_delete, s);
}

inline boost::container::pmr::memory_resource *get_internal_node_4_pool() {
  // TODO(laurynas) pool options
  static boost::container::pmr::unsynchronized_pool_resource node_4_pool;
  return &node_4_pool;
}

class internal_node_4 {
 public:
  static_assert(sizeof(internal_node_4_unique_ptr) ==
                sizeof(internal_node_4 *));

  static const constexpr auto key_prefix_capacity = 8;

  internal_node_4() : header{node_type::I4} {}

  [[nodiscard]] static internal_node_4_unique_ptr create();

  void add_two_to_empty(single_value_leaf_unique_ptr &&child1,
                        single_value_leaf_unique_ptr &&child2,
                        unsigned depth) noexcept;

  [[nodiscard]] const node_ptr find_child(std::byte key_byte) const noexcept;

 private:
  static const constexpr auto capacity = 4;

  node_header header;

  uint8_t children_count{0};

 public:
  // TODO(laurynas) privatize
  // TODO(laurynas) alias uint8_t
  uint8_t key_prefix_len{0};
  std::array<std::byte, key_prefix_capacity> key_prefix;

 private:
  std::array<std::byte, capacity> keys;
  std::array<node_ptr, capacity> children;
};

inline void internal_node_4_deleter::operator()(
    internal_node_4 *to_delete) const noexcept {
  get_internal_node_4_pool()->deallocate(to_delete, sizeof(*to_delete));
}

class db {
 public:
  using get_result = std::optional<std::vector<std::byte>>;

  [[nodiscard]] get_result get(raw_key_type k) noexcept;

  void insert(raw_key_type k, value_view v);

 private:
  [[nodiscard]] db::get_result get_from_subtree(const node_ptr node, key_type k,
                                                unsigned depth) const noexcept;

  void insert_node(key_type k, single_value_leaf_unique_ptr node,
                   unsigned depth);  // TODO(laurynas) alias "unsigned"

  [[nodiscard]] bool key_prefix_matches(key_type k, const internal_node_4 &node,
                                        unsigned depth) const noexcept;

  node_ptr root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
