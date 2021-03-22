// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_ART_INTERNAL_HPP_
#define UNODB_ART_INTERNAL_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <memory>
#include <type_traits>  // IWYU pragma: keep

#include "art_common.hpp"

namespace unodb::detail {

// Forward declarations to use in unodb::db and its siblings
template <class>
struct basic_leaf;  // IWYU pragma: keep

template <class, class>
class basic_db_leaf_deleter;  // IWYU pragma: keep

// Internal ART key in binary-comparable format
template <typename KeyType>
struct basic_art_key final {
  [[nodiscard]] static constexpr KeyType make_binary_comparable(
      KeyType key) noexcept;

  constexpr basic_art_key() noexcept = default;

  explicit constexpr basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  [[nodiscard]] static constexpr auto create(const std::byte from[]) noexcept {
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

  [[nodiscard, gnu::pure]] constexpr auto operator[](
      std::size_t index) const noexcept {
    assert(index < size);
    // cppcheck-suppress objectIndex
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

  static void static_asserts() {
    static_assert(std::is_trivially_copyable_v<basic_art_key<KeyType>>);
    static_assert(sizeof(basic_art_key<KeyType>) == sizeof(KeyType));
  }
};

using art_key = basic_art_key<unodb::key>;

[[gnu::cold, gnu::noinline]] void dump_byte(std::ostream &os, std::byte byte);

[[gnu::cold, gnu::noinline]] std::ostream &operator<<(std::ostream &os,
                                                      art_key key);

// This corresponds to the "single value leaf" type in the ART paper. Since we
// have only one kind of leaf nodes, we call them simply "leaf" nodes. Should we
// ever implement other kinds, rename this and related types to
// single_value_leaf.
using raw_leaf = std::byte;
// This pointer should be aligned as node header, but we can only attach alignas
// to a struct or union, which is inconvenient here.
using raw_leaf_ptr = raw_leaf *;

enum class node_type : std::uint8_t { LEAF, I4, I16, I48, I256 };

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

template <class Header, class Db>
class basic_db_leaf_deleter {
 public:
  // cppcheck-suppress constParameter
  constexpr explicit basic_db_leaf_deleter(Db &db_) noexcept : db{db_} {}

  void operator()(raw_leaf_ptr to_delete) const noexcept;

 private:
  Db &db;
};

template <class Header, class Db>
using basic_db_leaf_unique_ptr =
    std::unique_ptr<raw_leaf, basic_db_leaf_deleter<Header, Db>>;

template <class Node4, class Node16, class Node48, class Node256>
struct basic_inode_def final {
  using n4 = Node4;
  using n16 = Node16;
  using n48 = Node48;
  using n256 = Node256;

  basic_inode_def() = delete;
};

// A pointer to some kind of node. It can be accessed either as a node header,
// to query the right node type, a leaf, or as one of the internal nodes. This
// depends on all types being of standard layout and Header being at the same
// location in Header and all node types. This is checked by static asserts in
// the implementation file.
template <class Header, class INode, class INodeDefs>
union basic_node_ptr {
  using header_type = Header;
  using inode = INode;
  using inode4_type = typename INodeDefs::n4;
  using inode16_type = typename INodeDefs::n16;
  using inode48_type = typename INodeDefs::n48;
  using inode256_type = typename INodeDefs::n256;

  header_type *header;
  raw_leaf_ptr leaf;
  INode *internal;
  inode4_type *node_4;
  inode16_type *node_16;
  inode48_type *node_48;
  inode256_type *node_256;

  basic_node_ptr() noexcept {}
  constexpr basic_node_ptr(std::nullptr_t) noexcept : header{nullptr} {}
  constexpr basic_node_ptr(raw_leaf_ptr leaf_) noexcept : leaf{leaf_} {}
  constexpr basic_node_ptr(inode4_type *node_4_) noexcept : node_4{node_4_} {}
  constexpr basic_node_ptr(inode16_type *node_16_) noexcept
      : node_16{node_16_} {}
  constexpr basic_node_ptr(inode48_type *node_48_) noexcept
      : node_48{node_48_} {}
  constexpr basic_node_ptr(inode256_type *node_256_) noexcept
      : node_256{node_256_} {}

  [[nodiscard, gnu::pure]] constexpr auto type() const noexcept {
    return header->type();
  }

  [[nodiscard]] constexpr auto operator==(std::nullptr_t) const noexcept {
    return header == nullptr;
  }

  [[nodiscard]] constexpr auto operator!=(std::nullptr_t) const noexcept {
    return header != nullptr;
  }

  static auto static_asserts() {
    static_assert(
        sizeof(basic_node_ptr<Header, INode, INodeDefs>) == sizeof(void *),
        "node_ptr union must be of equal size to a raw pointer");
  }
};

}  // namespace unodb::detail

#endif  // UNODB_ART_INTERNAL_HPP_
