// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_INTERNAL_HPP
#define UNODB_DETAIL_ART_INTERNAL_HPP

#include "global.hpp"  // IWYU pragma: keep

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <memory>
#include <type_traits>  // IWYU pragma: keep

#include "art_common.hpp"
#include "node_type.hpp"

namespace unodb::detail {

// Forward declarations to use in unodb::db and its siblings
template <class>
class [[nodiscard]] basic_leaf;  // IWYU pragma: keep

template <class, class>
class [[nodiscard]] basic_db_leaf_deleter;  // IWYU pragma: keep

// Internal ART key in binary-comparable format
template <typename KeyType>
struct [[nodiscard]] basic_art_key final {
  [[nodiscard, gnu::const]] static constexpr KeyType make_binary_comparable(
      KeyType key) noexcept;

  constexpr basic_art_key() noexcept = default;

  constexpr explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  [[nodiscard]] static constexpr auto create(const std::byte from[]) noexcept {
    struct basic_art_key result;
    std::memcpy(&result, from, size);
    return result;
  }

  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  constexpr void copy_to(std::byte to[]) const noexcept {
    std::memcpy(to, &key, size);
  }

  [[nodiscard, gnu::pure]] constexpr bool operator==(
      // NOLINTNEXTLINE(modernize-avoid-c-arrays)
      const std::byte key2[]) const noexcept {
    return !std::memcmp(&key, key2, size);
  }

  [[nodiscard, gnu::pure]] constexpr bool operator==(
      basic_art_key<KeyType> key2) const noexcept {
    return !std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard, gnu::pure]] constexpr bool operator!=(
      basic_art_key<KeyType> key2) const noexcept {
    return std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard, gnu::pure]] constexpr auto operator[](
      std::size_t index) const noexcept {
    assert(index < size);
    // cppcheck-suppress objectIndex
    return (reinterpret_cast<const std::byte *>(&key))[index];
  }

  [[nodiscard, gnu::pure]] constexpr explicit operator KeyType()
      const noexcept {
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

class [[nodiscard]] tree_depth final {
 public:
  using value_type = unsigned;

  explicit constexpr tree_depth(value_type value_ = 0) noexcept
      : value{value_} {
    assert(value <= art_key::size);
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  [[nodiscard, gnu::pure]] constexpr operator value_type() const noexcept {
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
  using leaf_type = basic_leaf<Header>;

  static_assert(std::is_trivially_destructible_v<leaf_type>);

  constexpr explicit basic_db_leaf_deleter(Db &db_) noexcept : db{db_} {}

  void operator()(leaf_type *to_delete) const noexcept;

  [[nodiscard, gnu::pure]] Db &get_db() const noexcept { return db; }

 private:
  Db &db;
};

template <class Header, class Db>
using basic_db_leaf_unique_ptr =
    std::unique_ptr<basic_leaf<Header>, basic_db_leaf_deleter<Header, Db>>;

template <class T>
struct dependent_false : std::false_type {};

template <class INode, class Db>
class basic_db_inode_deleter {
 public:
  constexpr explicit basic_db_inode_deleter(Db &db_) noexcept : db{db_} {}

  void operator()(INode *inode_ptr) noexcept;

  [[nodiscard, gnu::pure]] Db &get_db() noexcept { return db; }

 private:
  Db &db;
};

template <class Header>
class [[nodiscard]] basic_node_ptr {
 public:
  using header_type = Header;

  constexpr basic_node_ptr() noexcept = default;

  explicit basic_node_ptr(std::nullptr_t) noexcept
      : tagged_ptr{reinterpret_cast<std::uintptr_t>(nullptr)} {}

  basic_node_ptr(header_type *ptr, unodb::node_type type)
      : tagged_ptr{tag_ptr(ptr, type)} {}

  basic_node_ptr<Header> &operator=(std::nullptr_t) noexcept {
    tagged_ptr = reinterpret_cast<std::uintptr_t>(nullptr);
    return *this;
  }

  [[nodiscard, gnu::pure]] constexpr auto type() const noexcept {
    return static_cast<unodb::node_type>(tagged_ptr & tag_bit_mask);
  }

  [[nodiscard, gnu::pure]] constexpr auto raw_val() const noexcept {
    return tagged_ptr;
  }

  [[nodiscard, gnu::pure]] auto *ptr() const noexcept {
    return reinterpret_cast<Header *>(tagged_ptr & ptr_bit_mask);
  }

  [[nodiscard, gnu::pure]] auto operator==(std::nullptr_t) const noexcept {
    return tagged_ptr == reinterpret_cast<std::uintptr_t>(nullptr);
  }

  [[nodiscard, gnu::pure]] auto operator!=(std::nullptr_t) const noexcept {
    return tagged_ptr != reinterpret_cast<std::uintptr_t>(nullptr);
  }

 private:
  std::uintptr_t tagged_ptr;

  [[nodiscard, gnu::const]] static std::uintptr_t tag_ptr(
      Header *ptr_, unodb::node_type tag) {
    const auto uintptr = reinterpret_cast<std::uintptr_t>(ptr_);
    const auto result =
        uintptr | static_cast<std::underlying_type_t<decltype(tag)>>(tag);
    assert((result & ptr_bit_mask) == uintptr);
    return result;
  }

  [[nodiscard, gnu::const]] static constexpr unsigned mask_bits_needed(
      unsigned count) {
    return count < 2 ? 1 : 1 + mask_bits_needed(count >> 1U);
  }

  static constexpr auto lowest_non_tag_bit =
      1ULL << mask_bits_needed(node_type_count);
  static constexpr auto tag_bit_mask = lowest_non_tag_bit - 1;
  static constexpr auto ptr_bit_mask = ~tag_bit_mask;

  static auto static_asserts() {
    static_assert(sizeof(basic_node_ptr<Header>) == sizeof(void *));
    static_assert(alignof(header_type) - 1 > lowest_non_tag_bit);
  }
};

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_ART_INTERNAL_HPP
