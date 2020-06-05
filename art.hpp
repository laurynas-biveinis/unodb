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
class raii_leaf_creator;

// Internal ART key in binary-comparable format
template <typename KeyType>
struct basic_art_key final {
  [[nodiscard]] static KeyType make_binary_comparable(KeyType key) noexcept;

  basic_art_key() noexcept = default;

  explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  [[nodiscard]] static auto create(const std::byte from[]) noexcept {
    struct basic_art_key result;
    std::memcpy(&result, from, size);
    return result;
  }

  void copy_to(std::byte to[]) const noexcept { std::memcpy(to, &key, size); }

  [[nodiscard]] bool operator==(const std::byte key2[]) const noexcept {
    return !std::memcmp(&key, key2, size);
  }

  [[nodiscard]] bool operator==(basic_art_key<KeyType> key2) const noexcept {
    return !std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard]] bool operator!=(basic_art_key<KeyType> key2) const noexcept {
    return std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard]] __attribute__((pure)) auto operator[](
      std::size_t index) const noexcept {
    assert(index < size);
    return (reinterpret_cast<const std::byte *>(&key))[index];
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

  explicit tree_depth(value_type value_ = 0) noexcept : value{value_} {
    assert(value <= art_key::size);
  }

  [[nodiscard]] operator value_type() const noexcept {
    assert(value <= art_key::size);
    return value;
  }

  tree_depth &operator++() noexcept {
    ++value;
    assert(value <= art_key::size);
    return *this;
  }

  void operator+=(value_type delta) noexcept {
    value += delta;
    assert(value <= art_key::size);
  }

 private:
  value_type value;
};

inline constexpr auto node_type_mask = 0b111;

// A pointer to some kind of node. It can be accessed either as a node header,
// to query the right node type, a leaf, or as one of the internal nodes. This
// depends on all types being of standard layout and node_header being at the
// same location in node_header and all node types. This is checked by static
// asserts in the implementation file.
union node_ptr {
  std::uintptr_t pointer_value;
  node_header *header;
  raw_leaf_ptr leaf;
  inode *internal;
  inode_4 *node_4;
  inode_16 *node_16;
  inode_48 *node_48;
  inode_256 *node_256;

  static constexpr std::uintptr_t sentinel_ptr = node_type_mask;

  node_ptr() noexcept {}
  node_ptr(std::uintptr_t pointer_value_) noexcept
      : pointer_value{pointer_value_} {}
  node_ptr(raw_leaf_ptr leaf_) noexcept : leaf{leaf_} {}
  node_ptr(inode_4 *node_4_) noexcept : node_4{node_4_} {}
  node_ptr(inode_16 *node_16_) noexcept : node_16{node_16_} {}
  node_ptr(inode_48 *node_48_) noexcept : node_48{node_48_} {}
  node_ptr(inode_256 *node_256_) noexcept : node_256{node_256_} {}

  [[nodiscard]] __attribute__((pure)) node_type type() const noexcept;

  [[nodiscard]] bool is_sentinel() const noexcept {
    return pointer_value & node_type_mask;
  }
};

}  // namespace detail

class db final {
 public:
  // Creation and destruction
  explicit db(std::size_t memory_limit_ = 0) noexcept
      : memory_limit{memory_limit_} {}

  ~db() noexcept;

  // Querying
  [[nodiscard]] get_result get(key k) const noexcept;

  [[nodiscard]] auto empty() const noexcept { return root.is_sentinel(); }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] bool insert(key k, value_view v);

  [[nodiscard]] bool remove(key k);

  void clear();

  // Stats

  // Return current memory use by tree nodes in bytes, only accounted if memory
  // limit was specified in the constructor, otherwise always zero.
  [[nodiscard]] auto get_current_memory_use() const noexcept {
    return current_memory_use;
  }

  [[nodiscard]] auto get_leaf_count() const noexcept { return leaf_count; }

  [[nodiscard]] auto get_inode4_count() const noexcept { return inode4_count; }

  [[nodiscard]] auto get_inode16_count() const noexcept {
    return inode16_count;
  }

  [[nodiscard]] auto get_inode48_count() const noexcept {
    return inode48_count;
  }

  [[nodiscard]] auto get_inode256_count() const noexcept {
    return inode256_count;
  }

  [[nodiscard]] auto get_created_inode4_count() const noexcept {
    return created_inode4_count;
  }

  [[nodiscard]] auto get_inode4_to_inode16_count() const noexcept {
    return inode4_to_inode16_count;
  }

  [[nodiscard]] auto get_inode16_to_inode48_count() const noexcept {
    return inode16_to_inode48_count;
  }

  [[nodiscard]] auto get_inode48_to_inode256_count() const noexcept {
    return inode48_to_inode256_count;
  }

  [[nodiscard]] auto get_deleted_inode4_count() const noexcept {
    return deleted_inode4_count;
  }

  [[nodiscard]] auto get_inode16_to_inode4_count() const noexcept {
    return inode16_to_inode4_count;
  }

  [[nodiscard]] auto get_inode48_to_inode16_count() const noexcept {
    return inode48_to_inode16_count;
  }

  [[nodiscard]] auto get_inode256_to_inode48_count() const noexcept {
    return inode256_to_inode48_count;
  }

  [[nodiscard]] auto get_key_prefix_splits() const noexcept {
    return key_prefix_splits;
  }

  // Debugging
  __attribute__((cold, noinline)) void dump(std::ostream &os) const;

 private:
  [[nodiscard]] static get_result get_from_subtree(
      detail::node_ptr node, detail::art_key k,
      detail::tree_depth depth) noexcept;

  [[nodiscard]] bool insert_to_subtree(detail::art_key k,
                                       detail::node_ptr &node, value_view v,
                                       detail::tree_depth depth);

  [[nodiscard]] bool remove_from_subtree(detail::art_key k,
                                         detail::tree_depth depth,
                                         detail::node_ptr &node);

  void increase_memory_use(std::size_t delta);
  void decrease_memory_use(std::size_t delta) noexcept;

  detail::node_ptr root{detail::node_ptr::sentinel_ptr};

  std::size_t current_memory_use{0};
  const std::size_t memory_limit;

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
  friend class detail::raii_leaf_creator;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
