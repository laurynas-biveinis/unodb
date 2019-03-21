// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>  // for uint64_t
#include <cstdint>  // IWYU pragma: keep
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <gsl/gsl_assert>
#include <gsl/span>

namespace unodb {

// Key type for public API
using key_type = uint64_t;

// Internal ART key in binary-comparable format
template <typename Key_type>
struct art_key final {
  [[nodiscard]] static Key_type make_binary_comparable(Key_type key) noexcept;

  art_key() noexcept = default;

  explicit art_key(Key_type key_) noexcept
      : key{make_binary_comparable(key_)} {}

  [[nodiscard]] static art_key create(const std::byte from[]) noexcept {
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

  [[nodiscard]] bool operator==(art_key<Key_type> key2) const noexcept {
    return !memcmp(&key, &key2.key, sizeof(*this));
  }

  [[nodiscard]] bool operator!=(art_key<Key_type> key2) const noexcept {
    return memcmp(&key, &key2.key, sizeof(*this));
  }

  [[nodiscard]] __attribute__((pure)) std::byte operator[](
      std::size_t index) const noexcept {
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

class internal_node;

enum class node_type : uint8_t;

// A pointer to some kind of node. It can be accessed either as a node header,
// to query the right node type, a leaf, or as one of the internal nodes. This
// depends on all types being of standard layout and node_header being at the
// same location in node_header and all node types. This is checked by static
// asserts in the implementation file.
union node_ptr {
  node_header *header;
  single_value_leaf_unique_ptr leaf;
  std::unique_ptr<internal_node> internal;

  node_ptr() noexcept {}
  explicit node_ptr(node_ptr &&other) noexcept : leaf{std::move(other.leaf)} {}
  explicit node_ptr(std::nullptr_t) noexcept : header{nullptr} {}
  explicit node_ptr(single_value_leaf_unique_ptr &&leaf_) noexcept
      : leaf{std::move(leaf_)} {}
  // Cannot be implemented here or "sizeof applied to incomplete type
  // internal_node" with some compilers
  explicit node_ptr(std::unique_ptr<internal_node> &&node) noexcept;

  ~node_ptr() {}

  node_ptr &operator=(node_ptr &&other) noexcept {
    // TODO(laurynas): does this actually destruct leaf/internal?
    header = std::move(other.header);
    return *this;
  }

  node_ptr &operator=(std::nullptr_t) noexcept;

  auto operator==(std::nullptr_t) const noexcept { return header == nullptr; }
  auto operator!=(std::nullptr_t) const noexcept { return header != nullptr; }

  [[nodiscard]] __attribute__((pure)) node_type type() const noexcept;
};

class db final {
 public:
  using get_result = std::optional<std::vector<std::byte>>;

  using tree_depth_type = unsigned;

  [[nodiscard]] get_result get(key_type k) const noexcept;

  [[nodiscard]] bool insert(key_type k, value_view v);

  [[nodiscard]] bool remove(key_type k);

#ifndef NDEBUG
  void dump(std::ostream &os) const;
#endif

 private:
  [[nodiscard]] static db::get_result get_from_subtree(
      const node_ptr &node, art_key_type k, tree_depth_type depth) noexcept;

  [[nodiscard]] static bool insert_leaf(art_key_type k, node_ptr *node,
                                        single_value_leaf_unique_ptr leaf,
                                        tree_depth_type depth);

  node_ptr root{nullptr};
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
