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

// Key type for public API
using key_type = uint64_t;

// Internal ART key in binary-comparable format
template <typename Key_type>
struct art_key final {
  art_key() noexcept = default;

  explicit art_key(Key_type key_) noexcept : key{key_} {}

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
    return (reinterpret_cast<const std::byte *>(&key))[index];
  }

  Key_type key;
};

using art_key_type = art_key<key_type>;

// Value type for public API. Values are passed as non-owning pointers to
// memory with associated length (gsl::span). The memory is copied upon
// insertion.
using value_view = gsl::span<const std::byte>;

enum class node_type : uint8_t { LEAF, I4 };

// A common prefix shared by all node types
struct node_header final {
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

  node_ptr() : header{nullptr} {};

  node_ptr(const node_ptr &other) noexcept : header{other.header} {}

  ~node_ptr() {}
};

class internal_node_4 final {
 public:
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

class db final {
 public:
  using get_result = std::optional<std::vector<std::byte>>;

  [[nodiscard]] get_result get(key_type k) noexcept;

  void insert(key_type k, value_view v);

 private:
  [[nodiscard]] db::get_result get_from_subtree(const node_ptr node,
                                                art_key_type k,
                                                unsigned depth) const noexcept;

  void insert_node(art_key_type k, single_value_leaf_unique_ptr node,
                   unsigned depth);  // TODO(laurynas) alias "unsigned"

  [[nodiscard]] bool key_prefix_matches(art_key_type k,
                                        const internal_node_4 &node,
                                        unsigned depth) const noexcept;

  node_ptr root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
