// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cassert>
#include <cstddef>  // for uint8_t
#include <cstdint>  // IWYU pragma: keep
#include <cstring>
#include <memory>
#include <vector>

#include "art_common.hpp"

namespace unodb {

namespace detail {

struct leaf;
class leaf_creator_with_scope_cleanup;

// Internal ART key in binary-comparable format
template <typename KeyType>
struct basic_art_key final {
  [[nodiscard]] static KeyType make_binary_comparable(KeyType key) noexcept;

  basic_art_key() noexcept = default;

  explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  [[nodiscard]] static auto create(const std::byte from[]) noexcept {
    struct basic_art_key result;
    std::memcpy(&result, from, sizeof(result));
    return result;
  }

  void copy_to(std::byte to[]) const noexcept {
    std::memcpy(to, &key, sizeof(*this));
  }

  [[nodiscard]] bool operator==(const std::byte key2[]) const noexcept {
    return !std::memcmp(&key, key2, sizeof(*this));
  }

  [[nodiscard]] bool operator==(basic_art_key<KeyType> key2) const noexcept {
    return !std::memcmp(&key, &key2.key, sizeof(*this));
  }

  [[nodiscard]] bool operator!=(basic_art_key<KeyType> key2) const noexcept {
    return std::memcmp(&key, &key2.key, sizeof(*this));
  }

  [[nodiscard]] __attribute__((pure)) auto operator[](std::size_t index) const
      noexcept {
    assert(index < sizeof(*this));
    return (reinterpret_cast<const std::byte *>(&key))[index];
  }

  KeyType key;
};

using art_key = basic_art_key<key>;

struct node_header;

// This corresponds to the "single value leaf" type in the ART paper. Since we
// have only one kind of leaf nodes, we call them simply "leaf" nodes. Should we
// ever implement other kinds, rename this and related types to
// single_value_leaf.
using raw_leaf = std::byte;
using raw_leaf_ptr = raw_leaf *;

class internal_node;
class internal_node_4;
class internal_node_16;
class internal_node_48;
class internal_node_256;

enum class node_type : std::uint8_t;

// A pointer to some kind of node. It can be accessed either as a node header,
// to query the right node type, a leaf, or as one of the internal nodes. This
// depends on all types being of standard layout and node_header being at the
// same location in node_header and all node types. This is checked by static
// asserts in the implementation file.
union node_ptr {
  node_header *header;
  raw_leaf_ptr leaf;
  internal_node *internal;
  internal_node_4 *node_4;
  internal_node_16 *node_16;
  internal_node_48 *node_48;
  internal_node_256 *node_256;

  node_ptr() noexcept {}
  node_ptr(std::nullptr_t) noexcept : header{nullptr} {}
  node_ptr(raw_leaf_ptr leaf_) noexcept : leaf{leaf_} {}
  node_ptr(internal_node_4 *node_4_) noexcept : node_4{node_4_} {}
  node_ptr(internal_node_16 *node_16_) noexcept : node_16{node_16_} {}
  node_ptr(internal_node_48 *node_48_) noexcept : node_48{node_48_} {}
  node_ptr(internal_node_256 *node_256_) noexcept : node_256{node_256_} {}

  [[nodiscard]] auto operator==(std::nullptr_t) const noexcept {
    return header == nullptr;
  }

  [[nodiscard]] auto operator!=(std::nullptr_t) const noexcept {
    return header != nullptr;
  }

  [[nodiscard]] __attribute__((pure)) node_type type() const noexcept;
};

}  // namespace detail

class db final {
 public:
  using tree_depth_type = unsigned;

  explicit db(std::size_t memory_limit_ = 0) noexcept
      : memory_limit{memory_limit_} {}

  ~db() noexcept;

  [[nodiscard]] get_result get(key k) const noexcept;

  [[nodiscard]] bool insert(key k, value_view v);

  [[nodiscard]] bool remove(key k);

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

  [[nodiscard]] auto empty() const noexcept { return root == nullptr; }

  [[nodiscard]] auto get_current_memory_use() const noexcept {
    return current_memory_use;
  }

 private:
  [[nodiscard]] static get_result get_from_subtree(
      detail::node_ptr node, detail::art_key k, tree_depth_type depth) noexcept;

  [[nodiscard]] bool insert_to_subtree(detail::art_key k,
                                       detail::node_ptr *node, value_view v,
                                       tree_depth_type depth);

  [[nodiscard]] bool remove_from_subtree(detail::art_key k,
                                         tree_depth_type depth,
                                         detail::node_ptr *node);

  void increase_memory_use(std::size_t delta);
  void decrease_memory_use(std::size_t delta) noexcept;

  detail::node_ptr root{nullptr};

  std::size_t current_memory_use{0};
  const std::size_t memory_limit;

  friend struct detail::leaf;
  friend class detail::leaf_creator_with_scope_cleanup;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
