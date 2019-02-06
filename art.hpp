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

struct node_header;

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

class db final {
 public:
  using get_result = std::optional<std::vector<std::byte>>;

  using tree_depth_type = unsigned;

  [[nodiscard]] get_result get(key_type k) noexcept;

  void insert(key_type k, value_view v);

 private:
  [[nodiscard]] db::get_result get_from_subtree(const node_ptr node,
                                                art_key_type k,
                                                tree_depth_type depth) const
      noexcept;

  void insert_node(art_key_type k, single_value_leaf_unique_ptr node,
                   tree_depth_type depth);

  [[nodiscard]] bool key_prefix_matches(art_key_type k,
                                        const internal_node_4 &node,
                                        tree_depth_type depth) const noexcept;

  node_ptr root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
