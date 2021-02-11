// Copyright 2019-2020 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cassert>
#include <cstddef>  // for std::uint8_t
#include <cstdint>  // IWYU pragma: keep
#include <cstring>
#include <memory>
#include <vector>

#include "art_common.hpp"

namespace unodb {

namespace detail {

struct leaf;
class db_leaf_deleter;

// Internal ART key in binary-comparable format
template <typename KeyType>
struct basic_art_key final {
  [[nodiscard]] constexpr static KeyType make_binary_comparable(
      KeyType key) noexcept;

  constexpr basic_art_key() noexcept = default;

  constexpr explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  [[nodiscard]] constexpr static auto create(const std::byte from[]) noexcept {
    struct basic_art_key result;
    std::memcpy(&result, from, size);
    return result;
  }

  constexpr void copy_to(std::byte to[]) const noexcept {
    std::memcpy(to, &key, size);
  }

  [[nodiscard]] constexpr bool operator==(
      const std::byte key2[]) const noexcept {
    return !std::memcmp(&key, key2, size);
  }

  [[nodiscard]] constexpr bool operator==(
      basic_art_key<KeyType> key2) const noexcept {
    return !std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard]] constexpr bool operator!=(
      basic_art_key<KeyType> key2) const noexcept {
    return std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard]] __attribute__((pure)) constexpr auto operator[](
      std::size_t index) const noexcept {
    assert(index < size);
    return (reinterpret_cast<const std::byte *>(&key))[index];
  }

  [[nodiscard]] constexpr explicit operator KeyType() const noexcept {
    return key;
  }

  constexpr void shift_right(const std::size_t num_bytes) noexcept {
    key >>= (num_bytes * 8);
  }

  KeyType key;

  static constexpr auto size = sizeof(KeyType);
};

using art_key = basic_art_key<key>;

struct node_header;

// This corresponds to the "single value leaf" type in the ART paper. Since we
// have only one kind of leaf nodes, we call them simply "leaf" nodes. Should we
// ever implement other kinds, rename this and related types to
// single_value_leaf.
using raw_leaf = std::byte;
using raw_leaf_ptr = raw_leaf *;

class inode;

class inode_4;
class inode_16;
class inode_48;
class inode_256;

enum class node_type : std::uint8_t;

class tree_depth final {
 public:
  using value_type = unsigned;

  explicit constexpr tree_depth(value_type value_ = 0) noexcept
      : value{value_} {
    assert(value <= art_key::size);
  }

  [[nodiscard]] constexpr operator value_type() const noexcept {
    assert(value <= art_key::size);
    return value;
  }

  constexpr tree_depth &operator++() noexcept {
    ++value;
    assert(value <= art_key::size);
    return *this;
  }

  constexpr void operator+=(value_type delta) noexcept {
    value += delta;
    assert(value <= art_key::size);
  }

 private:
  value_type value;
};

// A pointer to some kind of node. It can be accessed either as a node header,
// to query the right node type, a leaf, or as one of the internal nodes. This
// depends on all types being of standard layout and node_header being at the
// same location in node_header and all node types. This is checked by static
// asserts in the implementation file.
union node_ptr {
  node_header *header;
  raw_leaf_ptr leaf;
  inode *internal;
  inode_4 *node_4;
  inode_16 *node_16;
  inode_48 *node_48;
  inode_256 *node_256;

  node_ptr() noexcept {}
  constexpr node_ptr(std::nullptr_t) noexcept : header{nullptr} {}
  constexpr node_ptr(raw_leaf_ptr leaf_) noexcept : leaf{leaf_} {}
  constexpr node_ptr(inode_4 *node_4_) noexcept : node_4{node_4_} {}
  constexpr node_ptr(inode_16 *node_16_) noexcept : node_16{node_16_} {}
  constexpr node_ptr(inode_48 *node_48_) noexcept : node_48{node_48_} {}
  constexpr node_ptr(inode_256 *node_256_) noexcept : node_256{node_256_} {}

  [[nodiscard]] constexpr auto operator==(std::nullptr_t) const noexcept {
    return header == nullptr;
  }

  [[nodiscard]] constexpr auto operator!=(std::nullptr_t) const noexcept {
    return header != nullptr;
  }

  [[nodiscard]] __attribute__((pure)) constexpr node_type type() const noexcept;
};

}  // namespace detail

class db final {
 public:
  // Creation and destruction
  constexpr db() noexcept {}

  ~db() noexcept;

  // Querying
  [[nodiscard]] get_result get(key search_key) const noexcept;

  [[nodiscard]] constexpr auto empty() const noexcept {
    return root == nullptr;
  }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] bool insert(key insert_key, value_view v);

  [[nodiscard]] bool remove(key remove_key);

  void clear();

  // Stats

  // Return current memory use by tree nodes in bytes.
  [[nodiscard]] constexpr auto get_current_memory_use() const noexcept {
    return current_memory_use;
  }

  [[nodiscard]] constexpr auto get_leaf_count() const noexcept {
    return leaf_count;
  }

  [[nodiscard]] constexpr auto get_inode4_count() const noexcept {
    return inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode16_count() const noexcept {
    return inode16_count;
  }

  [[nodiscard]] constexpr auto get_inode48_count() const noexcept {
    return inode48_count;
  }

  [[nodiscard]] constexpr auto get_inode256_count() const noexcept {
    return inode256_count;
  }

  [[nodiscard]] constexpr auto get_created_inode4_count() const noexcept {
    return created_inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode4_to_inode16_count() const noexcept {
    return inode4_to_inode16_count;
  }

  [[nodiscard]] constexpr auto get_inode16_to_inode48_count() const noexcept {
    return inode16_to_inode48_count;
  }

  [[nodiscard]] constexpr auto get_inode48_to_inode256_count() const noexcept {
    return inode48_to_inode256_count;
  }

  [[nodiscard]] constexpr auto get_deleted_inode4_count() const noexcept {
    return deleted_inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode16_to_inode4_count() const noexcept {
    return inode16_to_inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode48_to_inode16_count() const noexcept {
    return inode48_to_inode16_count;
  }

  [[nodiscard]] constexpr auto get_inode256_to_inode48_count() const noexcept {
    return inode256_to_inode48_count;
  }

  [[nodiscard]] constexpr auto get_key_prefix_splits() const noexcept {
    return key_prefix_splits;
  }

  // Debugging
  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  void delete_subtree(detail::node_ptr) noexcept;

  constexpr void increase_memory_use(std::size_t delta) noexcept {
    current_memory_use += delta;
  }

  constexpr void decrease_memory_use(std::size_t delta) noexcept {
    assert(delta <= current_memory_use);
    current_memory_use -= delta;
  }

  detail::node_ptr root{nullptr};

  std::size_t current_memory_use{0};

  std::uint64_t leaf_count{0};
  std::uint64_t inode4_count{0};
  std::uint64_t inode16_count{0};
  std::uint64_t inode48_count{0};
  std::uint64_t inode256_count{0};

  std::uint64_t created_inode4_count{0};
  std::uint64_t inode4_to_inode16_count{0};
  std::uint64_t inode16_to_inode48_count{0};
  std::uint64_t inode48_to_inode256_count{0};

  std::uint64_t deleted_inode4_count{0};
  std::uint64_t inode16_to_inode4_count{0};
  std::uint64_t inode48_to_inode16_count{0};
  std::uint64_t inode256_to_inode48_count{0};

  std::uint64_t key_prefix_splits{0};

  friend struct detail::leaf;
  friend class detail::db_leaf_deleter;
  friend class detail::inode_4;
  friend class detail::inode_16;
  friend class detail::inode_48;
  friend class detail::inode_256;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
