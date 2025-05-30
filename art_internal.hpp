// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_ART_INTERNAL_HPP
#define UNODB_DETAIL_ART_INTERNAL_HPP

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <_string.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>

#include "art_common.hpp"
#include "assert.hpp"
#include "heap.hpp"
#include "node_type.hpp"
#include "portability_builtins.hpp"

namespace unodb::detail {

// Forward declarations to use in unodb::db and its siblings
template <class, class>
class [[nodiscard]] basic_leaf;

template <class>
class [[nodiscard]] basic_db_leaf_deleter;

/// Lexicographic comparison of bytes.
///
/// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
[[nodiscard, gnu::pure]] inline int compare(const void *a, const size_t alen,
                                            const void *b,
                                            const size_t blen) noexcept {
  // TODO(thompsonbry) consider changing this over to std::span
  // arguments and do not let the (ptr,len) pattern progagate
  // outwards.
  const auto shared_length = std::min(alen, blen);
  auto ret = std::memcmp(a, b, shared_length);
  ret = (ret != 0) ? ret : ((alen == blen) ? 0 : (alen < blen ? -1 : 1));
  return ret;
}

/// Lexicographic comparison of key_views.
///
/// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
[[nodiscard, gnu::pure]] inline int compare(const unodb::key_view a,
                                            const unodb::key_view b) noexcept {
  return compare(a.data(), a.size_bytes(), b.data(), b.size_bytes());
}

/// Return the first 64-bits of the encoded key.  This is used by the
/// prefix compression logic to identify some number of bytes that are
/// in common between the art_key and an inode having some key_prefix.
[[nodiscard, gnu::pure]] inline std::uint64_t get_u64(key_view key) noexcept {
  std::uint64_t u{};  // will hold the first 64-bits.
  std::memcpy(&u, key.data(), std::min(key.size_bytes(), sizeof(u)));
  return u;
}

/// Internal ART key in binary-comparable format.  Application keys may
/// be simple fixed width types (such as std::uint64_t) or variable
/// length keys.  For the former, there are convenience methods on db,
/// olc_db, etc. to convert external keys into the binary compariable
/// format.  For the latter, the application is responsible for
/// converting the data (e.g., certain columns in some ordering for a
/// row of some relation) into the internal binary comparable key
/// format.  A convenience class (unodb::key_encoder) is offered to
/// encode data.  The encoding is always well defined and decoding
/// (unodb::key_decoder) exists for all simple fixed width data types.
/// Unicode encoding is complex and out of scope - use a quality
/// library such as ICU to produce appropriate Unicode sort keys for
/// your application.  Unicode decoding is NOT well defined.
/// Applications involving database records and Unicode data will
/// typically store the record identifier in a secondary index (ART) as
/// the value associated with the key.  Using the record identifier,
/// the original tuple can be discovered and the original Unicode data
/// recovered from that tuple.
template <typename KeyType>
struct [[nodiscard]] basic_art_key final {
 private:
  /// ctor helper converts a simple external key into an internal key
  /// supporting lexicographic comparison.
  [[nodiscard, gnu::const]] static UNODB_DETAIL_CONSTEXPR_NOT_MSVC KeyType
  make_binary_comparable(KeyType k) noexcept {
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    return bswap(k);
#else
#error Needs implementing
#endif
  }

 public:
  /// Construct converts a fixed width primitive type into a
  /// lexicographically ordered key.
  ///
  /// Note: Use a key_encoder for complex keys, including multiple key
  /// components or Unicode data.
  template <typename U = KeyType,
            typename std::enable_if<std::is_integral<U>::value, int>::type = 0>
  UNODB_DETAIL_CONSTEXPR_NOT_MSVC explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  /// Construct converts a key_view which must already be
  /// lexicographically ordered.
  UNODB_DETAIL_CONSTEXPR_NOT_MSVC explicit basic_art_key(key_view key_) noexcept
      : key{key_} {}

  /// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
  [[nodiscard, gnu::pure]] constexpr int cmp(
      basic_art_key<KeyType> key2) const noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      return compare(&key, sizeof(KeyType), &key2.key, sizeof(KeyType));
    } else {
      return std::memcmp(&key, &key2.key, sizeof(KeyType));
    }
  }

  /// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
  [[nodiscard, gnu::pure]] constexpr int cmp(key_view key2) const noexcept {
    if constexpr (std::is_same_v<KeyType, unodb::key_view>) {
      // variable length keys
      return compare(key, key2);
    } else {
      // fixed width keys
      return compare(&key, sizeof(KeyType), key2.data(), key2.size_bytes());
    }
  }

  /// Return the byte at the specified index position in the binary
  /// comparable key.
  [[nodiscard, gnu::pure]] constexpr auto operator[](
      std::size_t index) const noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      return key[index];
    } else {
      // The key_bytes[] provides access the different byte positions
      // in the primitive type [Key key].
      UNODB_DETAIL_ASSERT(index < sizeof(KeyType));
      return key_bytes[index];
    }
  }

  /// Return the backing key_view.
  ///
  /// Note: For integral keys, this is a non-owned view of the data in
  /// the basic_art_key and will be invalid if that object goes out of
  /// scope.
  ///
  /// Note: For key_view keys, this is the key_view backing this
  /// art_key and its validity depends on the scope of the backing
  /// byte array.
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)
  [[nodiscard, gnu::pure]] constexpr key_view get_key_view() const noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      return key;
    } else {
      return key_view(reinterpret_cast<const std::byte *>(&key), sizeof(key));
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Return the first 64-bits (max) of the encoded key.  This is used
  /// by the prefix compression logic to identify some number of bytes
  /// that are in common between the art_key and an inode having some
  /// key_prefix.
  [[nodiscard, gnu::pure]] constexpr std::uint64_t get_u64() const noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      return unodb::detail::get_u64(key);
    } else {
      return static_cast<std::uint64_t>(key);
    }
  }

  /// Shift the internal key some number of bytes to the right (the
  /// leading bytes are discarded), causing the key to be shorter by
  /// that many bytes. It is a lexicographic key. The first byte is
  /// the most significant.  The last byte is the least significant.
  ///
  /// When backed by a u64, we get trailing bytes which are zeros.
  /// Thus, for a fixed width type, this causes the key to be
  /// logically zero filled as it becomes shorter.  E.g.
  ///
  /// `0x0011223344556677 shift_right(2) => 0x2233445566770000`
  constexpr void shift_right(const std::size_t num_bytes) noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      UNODB_DETAIL_ASSERT(num_bytes <= key.size_bytes());
      key = key.subspan(num_bytes);
    } else {
      UNODB_DETAIL_ASSERT(num_bytes <= sizeof(KeyType));
      key >>= (num_bytes * 8);
    }
  }

  /// Return the number of bytes required to represent the key.
  constexpr size_t size() const noexcept {
    if constexpr (std::is_same_v<KeyType, unodb::key_view>) {
      return key.size_bytes();
    } else {
      return sizeof(KeyType);
    }
  }

  union {
    /// The lexicographic byte-wise comparable binary key.
    ///
    /// Note: When KeyType == key_view, this is all you need.
    KeyType key;

    /// Used iff the key is not a key_view.  This provides a mechanism
    /// to index into the bytes in the key for operator[].  This is
    /// ignored if KeyType==key_view as the key_view provides a
    /// byte-wise index operator already.
    std::array<std::byte, sizeof(KeyType)> key_bytes;
  };

  static void static_asserts() {
    static_assert(std::is_trivially_copyable_v<basic_art_key<KeyType>>);
    static_assert(sizeof(basic_art_key<KeyType>) == sizeof(KeyType));
  }

  /// dump the key in lexicographic byte-wise order.
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    dump_key(os, key);
  }

  /// Helper for debugging, writes on std::cerr.
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump() const;

  friend std::ostream &operator<<(std ::ostream &os, const basic_art_key &k) {
    k.dump(os);
    return os;
  }
};  // class basic_art_key

/// A typed class representing the number of key bytes consumed along
/// some path in the tree.  In general, the unodb::detail::key_prefix
/// consumes up to some fixed number of bytes and one byte is consumed
/// based on the key_byte for each node as we descend along that byte
/// value to some child index.
template <typename ArtKey>
class [[nodiscard]] tree_depth final {
 public:
  using value_type = std::uint32_t;  // explicitly since also used in leaf.

  explicit constexpr tree_depth(value_type value_ = 0) noexcept
      : value{value_} {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  [[nodiscard, gnu::pure]] constexpr operator value_type() const noexcept {
    return value;
  }

  constexpr tree_depth &operator++() noexcept {
    ++value;
    return *this;
  }

  constexpr void operator+=(value_type delta) noexcept { value += delta; }

 private:
  value_type value;
};

template <class Db>
class basic_db_leaf_deleter {
 public:
  using leaf_type = basic_leaf<typename Db::key_type, typename Db::header_type>;

  static_assert(std::is_trivially_destructible_v<leaf_type>);

  constexpr explicit basic_db_leaf_deleter(
      Db &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : db{db_} {}

  void operator()(leaf_type *to_delete) const noexcept;

  [[nodiscard, gnu::pure]] Db &get_db() const noexcept { return db; }

 private:
  Db &db;
};

// Not taken from Db to break a dependency circle
template <typename Key, typename Value, class Header,
          template <typename, typename> class Db>
using basic_db_leaf_unique_ptr =
    std::unique_ptr<basic_leaf<Key, Header>,
                    basic_db_leaf_deleter<Db<Key, Value>>>;

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

/// basic_node_ptr is a tagged pointer (the tag is the node type).
/// You have to know statically the target type, then call
/// node_ptr_var.ptr<target_type *>.ptr() to get target_type.
template <class Header>
class [[nodiscard]] basic_node_ptr {
 public:
  using header_type = Header;

  // The default constructor does not initialize fields: it is used by
  // std::array and we don't want to initialize to zero or any other value there
  // at construction time.
  // cppcheck-suppress uninitMemberVar
  constexpr basic_node_ptr() noexcept = default;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)

  explicit basic_node_ptr(std::nullptr_t) noexcept
      : tagged_ptr{reinterpret_cast<std::uintptr_t>(nullptr)} {}

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // construct a node pointer given a raw pointer and a node type.
  basic_node_ptr(const header_type *ptr UNODB_DETAIL_LIFETIMEBOUND,
                 unodb::node_type type) noexcept
      : tagged_ptr{tag_ptr(ptr, type)} {}

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)

  basic_node_ptr<Header> &operator=(std::nullptr_t) noexcept {
    tagged_ptr = reinterpret_cast<std::uintptr_t>(nullptr);
    return *this;
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard, gnu::pure]] constexpr auto type() const noexcept {
    return static_cast<unodb::node_type>(tagged_ptr & tag_bit_mask);
  }

  [[nodiscard, gnu::pure]] constexpr auto raw_val() const noexcept {
    return tagged_ptr;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)

  template <class T>
  [[nodiscard, gnu::pure]] auto *ptr() const noexcept {
    return reinterpret_cast<T>(tagged_ptr & ptr_bit_mask);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // same raw_val means same type and same ptr.
  [[nodiscard, gnu::pure]] constexpr bool operator==(
      const basic_node_ptr &other) const noexcept {
    return tagged_ptr == other.tagged_ptr;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)

  [[nodiscard, gnu::pure]] bool operator==(std::nullptr_t) const noexcept {
    return tagged_ptr == reinterpret_cast<std::uintptr_t>(nullptr);
  }

 private:
  std::uintptr_t tagged_ptr;

  [[nodiscard, gnu::const]] static std::uintptr_t tag_ptr(
      const Header *ptr_, unodb::node_type tag) noexcept {
    const auto uintptr = reinterpret_cast<std::uintptr_t>(ptr_);
    const auto result =
        uintptr | static_cast<std::underlying_type_t<decltype(tag)>>(tag);
    UNODB_DETAIL_ASSERT((result & ptr_bit_mask) == uintptr);
    return result;
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

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

// A buffer containing an expandable binary comparable key.  This is
// used to track the key by the iterator as things are pushed and
// popped on the stack.
class key_buffer {
  // TODO(thompsonbry) This data structure (std::vector but with
  // unstable &data[0] pointer and initial stack allocation) is
  // repeated and could be refactored out into a base class.
 protected:
  /// The capacity of the backing buffer.
  [[nodiscard]] size_t capacity() const noexcept { return cap; }

  /// The number of bytes of data in the buffer.
  [[nodiscard]] size_t size_bytes() const noexcept { return off; }

  /// Ensure that the buffer can hold at least [req] additional bytes.
  void ensure_available(size_t req) {
    if (UNODB_DETAIL_UNLIKELY(off + req > cap)) {
      ensure_capacity(off + req);  // resize
    }
  }

 public:
  /// Construct a new key_buffer.  It will be backed by an internal
  /// buffer of a configured size and extended iff required for longer
  /// keys.
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
  key_buffer() noexcept = default;
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  ~key_buffer() {
    if (cap > sizeof(ibuf)) {  // free old buffer iff allocated
      detail::free_aligned(buf);
    }
  }

  /// Reset the buffer.
  void reset() noexcept { off = 0; }

  /// Return a read-only view of the buffer showing only those bytes
  /// that have valid data.
  [[nodiscard]] key_view get_key_view() const noexcept {
    return key_view(buf, off);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)

  /// Append a byte to the buffer.
  void push(std::byte v) {
    ensure_available(sizeof(v));
    buf[off++] = v;
  }

  /// Append some bytes to the buffer.
  void push(key_view v) {
    const auto n = v.size_bytes();
    ensure_available(n);
    std::memcpy(buf + off, v.data(), n);
    off += n;
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Pop off some bytes from the buffer.
  void pop(size_t n) noexcept {
    UNODB_DETAIL_ASSERT(off >= n);
    off -= n;
  }

  key_buffer(const key_buffer &) = delete;
  key_buffer(key_buffer &&) = delete;
  key_buffer &operator=(const key_buffer &) = delete;
  key_buffer &operator=(key_buffer &&) = delete;

 private:
  /// Ensure that we have at least the specified capacity in the
  /// buffer.
  void ensure_capacity(size_t min_capacity) {
    unodb::detail::ensure_capacity(buf, cap, off, min_capacity);
  }

  /// Used for the initial buffer.
  std::byte ibuf[detail::INITIAL_BUFFER_CAPACITY];

  /// The buffer to accmulate the key.  Originally this is the [ibuf].
  /// If that overflows, then something will be allocated.
  std::byte *buf{&ibuf[0]};
  size_t cap{sizeof(ibuf)};  // current buffer capacity
  size_t off{0};             // #of bytes in the buffer having valid data.
};                           // class key_buffer

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_ART_INTERNAL_HPP
