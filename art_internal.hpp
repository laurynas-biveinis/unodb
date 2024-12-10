// Copyright 2019-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_INTERNAL_HPP
#define UNODB_DETAIL_ART_INTERNAL_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__fwd/ostream.h>
// IWYU pragma: no_include <_string.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>  // IWYU pragma: keep
#include <memory>
#include <type_traits>

#include "art_common.hpp"
#include "assert.hpp"
#include "node_type.hpp"

namespace unodb::detail {

// Forward declarations to use in unodb::db and its siblings
template <class>
class [[nodiscard]] basic_leaf;

template <class, class>
class [[nodiscard]] basic_db_leaf_deleter;

// Internal ART key in binary-comparable format
template <typename KeyType>
struct [[nodiscard]] basic_art_key final {
  // Convert an external key into an internal key supporting
  // lexicographic comparison.  This is only intended for key types
  // for which simple conversions are possible.  For complex keys,
  // including multiple key components or Unicode data, the
  // application should use a gsl::space<std::byte> which already
  // supports lexicographic comparison.
  [[nodiscard, gnu::const]] static UNODB_DETAIL_CONSTEXPR_NOT_MSVC KeyType
  make_binary_comparable(KeyType key) noexcept;

  // Convert an internal key into an external key. This is only
  // intended for key types for which simple conversions are possible.
  // For complex keys, including multiple key components or Unicode
  // data, the application should use a gsl::space<std::byte> which
  // already supports lexicographic comparison.
  [[nodiscard, gnu::const]] static UNODB_DETAIL_CONSTEXPR_NOT_MSVC KeyType
  make_external(KeyType key) noexcept;

  constexpr basic_art_key() noexcept = default;

  UNODB_DETAIL_CONSTEXPR_NOT_MSVC explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  [[nodiscard, gnu::pure]] constexpr bool operator==(
      basic_art_key<KeyType> key2) const noexcept {
    // FIXME This is wrong for variable length keys.  It needs to
    // consider no more bytes than the shorter key and if the two keys
    // have the same prefix, then they are != if one is longer (and
    // for == we can just compare the size as a short cut for this).
    // Also needs unit tests for variable length keys.
    return !std::memcmp(&key, &key2.key, size);
  }

  // @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
  [[nodiscard, gnu::pure]] constexpr int cmp(
      basic_art_key<KeyType> key2) const noexcept {
    // FIXME This is wrong for variable length keys.  It needs to
    // consider no more bytes than the shorter key and if the two keys
    // have the same prefix, then they are != if one is longer.  Also
    // needs unit tests for variable length keys.
    return std::memcmp(&key, &key2.key, size);
  }

  [[nodiscard, gnu::pure]] constexpr auto operator[](
      std::size_t index) const noexcept {
    UNODB_DETAIL_ASSERT(index < size);
    return key_bytes[index];
  }

  [[nodiscard, gnu::pure]] constexpr explicit operator KeyType()
      const noexcept {
    return key;
  }

  // return the decoded form of the key.
  [[nodiscard, gnu::pure]] constexpr KeyType decode() const noexcept {
    return make_external(key);
  }

  constexpr void shift_right(const std::size_t num_bytes) noexcept {
    UNODB_DETAIL_ASSERT(num_bytes <= size);
    key >>= (num_bytes * 8);
  }

  static constexpr auto size = sizeof(KeyType);

  union {
    KeyType key;
    std::array<std::byte, size> key_bytes;
  };

  static void static_asserts() {
    static_assert(std::is_trivially_copyable_v<basic_art_key<KeyType>>);
    static_assert(sizeof(basic_art_key<KeyType>) == sizeof(KeyType));
  }
};  // class basic_art_key

using art_key = basic_art_key<unodb::key>;

[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_byte(std::ostream &os,
                                                   std::byte byte);

[[gnu::cold]] UNODB_DETAIL_NOINLINE std::ostream &operator<<(
    std::ostream &os UNODB_DETAIL_LIFETIMEBOUND, art_key key);

class [[nodiscard]] tree_depth final {
 public:
  using value_type = unsigned;

  explicit constexpr tree_depth(value_type value_ = 0) noexcept
      : value{value_} {
    UNODB_DETAIL_ASSERT(value <= art_key::size);
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  [[nodiscard, gnu::pure]] constexpr operator value_type() const noexcept {
    UNODB_DETAIL_ASSERT(value <= art_key::size);
    return value;
  }

  constexpr tree_depth &operator++() noexcept {
    ++value;
    UNODB_DETAIL_ASSERT(value <= art_key::size);
    return *this;
  }

  constexpr void operator+=(value_type delta) noexcept {
    value += delta;
    UNODB_DETAIL_ASSERT(value <= art_key::size);
  }

 private:
  value_type value;
};

template <class Header, class Db>
class basic_db_leaf_deleter {
 public:
  using leaf_type = basic_leaf<Header>;

  static_assert(std::is_trivially_destructible_v<leaf_type>);

  constexpr explicit basic_db_leaf_deleter(
      Db &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : db{db_} {}

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
  constexpr explicit basic_db_inode_deleter(
      Db &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : db{db_} {}

  void operator()(INode *inode_ptr) noexcept;

  [[nodiscard, gnu::pure]] Db &get_db() noexcept { return db; }

 private:
  Db &db;
};

// basic_node_ptr is a tagged pointer (the tag is the node type).  You
// have to know statically the target type, then call
// node_ptr_var.ptr<target_type *>.ptr() to get target_type.
UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)
template <class Header>
class [[nodiscard]] basic_node_ptr {
 public:
  using header_type = Header;

  // The default constructor does not initialize fields: it is used by
  // std::array and we don't want to initialize to zero or any other value there
  // at construction time.
  // cppcheck-suppress uninitMemberVar
  constexpr basic_node_ptr() noexcept = default;

  explicit basic_node_ptr(std::nullptr_t) noexcept
      : tagged_ptr{reinterpret_cast<std::uintptr_t>(nullptr)} {}

  // construct a node pointer given a raw pointer and a node type.
  //
  // Note: The constructor casts away [const] for use when the
  // node_ptr will be [const].
  basic_node_ptr(const header_type *ptr UNODB_DETAIL_LIFETIMEBOUND,
                 unodb::node_type type) noexcept
      : tagged_ptr{tag_ptr(const_cast<header_type *>(ptr), type)} {}

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

  template <class T>
  [[nodiscard, gnu::pure]] auto *ptr() const noexcept {
    return reinterpret_cast<T>(tagged_ptr & ptr_bit_mask);
  }

  // same raw_val means same type and same ptr.
  [[nodiscard, gnu::pure]] auto operator==(
      const basic_node_ptr &other) const noexcept {
    return tagged_ptr == other.tagged_ptr;
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
      Header *ptr_, unodb::node_type tag) noexcept {
    const auto uintptr = reinterpret_cast<std::uintptr_t>(ptr_);
    const auto result =
        uintptr | static_cast<std::underlying_type_t<decltype(tag)>>(tag);
    UNODB_DETAIL_ASSERT((result & ptr_bit_mask) == uintptr);
    return result;
  }

  [[nodiscard, gnu::const]] static constexpr unsigned mask_bits_needed(
      unsigned count) noexcept {
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
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace unodb::detail

namespace unodb {

// An object visited by the scan API.  The visitor passed to the
// caller's lambda by the scan for each index entry visited by the
// scan.
template <typename Iterator>
class visitor {
  friend class olc_db;
  friend class db;

 protected:
  Iterator &it;
  explicit visitor(Iterator &it_) : it(it_) {}

 public:
  // Visit the (decoded) key.
  //
  // Note: The lambda MUST NOT export a reference to the visited key.
  // If you want to access the visited key outside of the scope of a
  // single lambda invocation, then you must make a copy of the data.
  //
  // Note: Key decoding can be expensive and its utility is limited to
  // simple primitive keys.  In particular, key decoding is not well
  // defined for Unicode data in keys.
  //
  // TODO(thompsonbry) Variable length keys: We need to define a
  // visitor method to visit the internal key buffer without any
  // decoding.
  inline auto get_key() const noexcept { return it.get_key().value(); }

  // Visit the value.
  //
  // Note: The lambda MUST NOT export a reference to the visited
  // value.  If you to access the value outside of the scope of a
  // single lambda invocation, then you must make a copy of the data.
  inline auto get_value() const noexcept { return it.get_val().value(); }
};  // class visitor

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_INTERNAL_HPP
