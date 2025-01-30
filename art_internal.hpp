// Copyright 2019-2025 UnoDB contributors
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

// IWYU pragma: no_include <_string.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
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

template <typename, class, template <class> class>
class [[nodiscard]] basic_db_leaf_deleter;

/// Lexicographic comparison of bytes.
///
/// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
[[nodiscard, gnu::pure]] inline int compare(const void *a, const size_t alen,
                                            const void *b, const size_t blen) {
  const auto shared_length = std::min(alen, blen);
  auto ret = std::memcmp(a, b, shared_length);
  ret = (ret != 0) ? ret : ((alen == blen) ? 0 : (alen < blen ? -1 : 1));
  return ret;
}

/// Lexicographic comparison of key_views.
///
/// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
[[nodiscard, gnu::pure]] inline int compare(const unodb::key_view a,
                                            const unodb::key_view b) {
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
//
/// TODO(thompsonbry) key templating - This never supported anything
/// except u64 keys due to bswap() being u64 specific.  Explicit tests
/// need to be developed for other templated integeral key types.
template <typename KeyType>
struct [[nodiscard]] basic_art_key final {
 private:
  /// ctor helper converts a simple external key into an internal key
  /// supporting lexicographic comparison.
  [[nodiscard, gnu::const]] static UNODB_DETAIL_CONSTEXPR_NOT_MSVC KeyType
  make_binary_comparable(KeyType k) noexcept {
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    return bswap64(k);
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
      // fast path
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
  [[nodiscard, gnu::pure]] constexpr key_view get_key_view() const noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      return key;
    } else {
      return key_view(reinterpret_cast<const std::byte *>(&key), sizeof(key));
    }
  }

  /// Return the first 64-bits (max) of the encoded key.  This is used
  /// by the prefix compression logic to identify some number of bytes
  /// that are in common between the art_key and an inode having some
  /// key_prefix.
  [[nodiscard, gnu::pure]] constexpr std::uint64_t get_u64() const noexcept {
    if constexpr (std::is_same_v<KeyType, key_view>) {
      return unodb::detail::get_u64(key);
    } else {
      // fast path - assumes KeyType is narrower and unsigned.
      return static_cast<std::uint64_t>(key);
    }
  }

  /// Shift the internal key some number of bytes to the right,
  /// causing the key to be shorter by that may bytes.
  //
  /// Note: For a fixed width type, this causes the key to be
  /// logically zero filled as it becomes shorter.  E.g.
  //
  /// ```0x0011223344556677 shift_right(2) => 0x2233445566770000```
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
    if constexpr (std::is_same_v<KeyType, key_view>) {
      os << "key: 0x";
      const auto sz = size();
      for (std::size_t i = 0; i < sz; ++i) dump_byte(os, key[i]);
    } else {
      os << "key: 0x" << std::hex << std::setfill('0') << std::setw(sizeof(key))
         << key << std::dec;
    }
  }

  /// Helper for debugging, writes on std::cerr.  Unrolled in
  /// art_internal.cpp.
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

template <typename Key, class Header, template <class> class Db>
class basic_db_leaf_deleter {
 public:
  using db_type = Db<Key>;
  using leaf_type = basic_leaf<Key, Header>;

  static_assert(std::is_trivially_destructible_v<leaf_type>);

  constexpr explicit basic_db_leaf_deleter(
      db_type &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : db{db_} {}

  void operator()(leaf_type *to_delete) const noexcept;

  [[nodiscard, gnu::pure]] db_type &get_db() const noexcept { return db; }

 private:
  db_type &db;
};

template <typename Key, class Header, template <class> class Db>
using basic_db_leaf_unique_ptr =
    std::unique_ptr<basic_leaf<Key, Header>,
                    basic_db_leaf_deleter<Key, Header, Db>>;

template <class T>
struct dependent_false : std::false_type {};

template <typename Key, class INode, template <class> class Db>
class basic_db_inode_deleter {
 public:
  using db_type = Db<Key>;

  constexpr explicit basic_db_inode_deleter(
      db_type &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : db{db_} {}

  void operator()(INode *inode_ptr) noexcept;

  [[nodiscard, gnu::pure]] db_type &get_db() noexcept { return db; }

 private:
  db_type &db;
};

/// basic_node_ptr is a tagged pointer (the tag is the node type).
/// You have to know statically the target type, then call
/// node_ptr_var.ptr<target_type *>.ptr() to get target_type.
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
  [[nodiscard, gnu::pure]] constexpr auto operator==(
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

///
/// fast utility methods
///

/// 32bit int shift-or utility function that is used by HighestOneBit
/// and NextPowerofTwo functions.  the only reason we have this
/// function is to reduce code repetition.
template <typename T>
constexpr typename std::enable_if<std::is_integral<T>::value, T>::type
shiftOr32bitInt(T i) {
  i |= (i >> 1);
  i |= (i >> 2);
  i |= (i >> 4);
  i |= (i >> 8);
  i |= (i >> 16);
  return i;
}

// Templatated function to find the next power of 2.  We have 32-bit
// and 64-bit specializations.
//
// Note: it will overflow if the there is no higher power of 2 for a
// given type T.
template <typename T>
constexpr typename std::enable_if<std::is_integral<T>::value && sizeof(T) == 4,
                                  T>::type
NextPowerOfTwo(T i) {
  return shiftOr32bitInt(i) + static_cast<T>(1);
}

template <typename T>
constexpr typename std::enable_if<std::is_integral<T>::value && sizeof(T) == 8,
                                  T>::type
NextPowerOfTwo(T i) {
  i = shiftOr32bitInt(i);
  i |= (i >> 32U);
  return ++i;
}

// A buffer containing an expanable binary comparable key.  This is
// used to track the key by the iterator as things are pushed and
// popped on the stack.
class key_buffer {
 protected:
  size_t capacity() const noexcept { return cap; }

  // the number of bytes of data in the buffer.
  size_t size_bytes() const noexcept { return off; }

  // ensure that the buffer can hold at least [req] additional bytes.
  void ensure_available(size_t req) {
    if (UNODB_DETAIL_UNLIKELY(off + req > cap)) {
      ensure_capacity(off + req);  // resize
    }
  }

 public:
  // Construct a new key_buffer.  It will be backed by an internal
  // buffer of a configured size and extended iff required for longer
  // keys.
  key_buffer() : buf(&ibuf[0]), cap(sizeof(ibuf)), off(0) {}

  ~key_buffer() {
    if (cap > sizeof(ibuf)) {  // free old buffer iff allocated
      detail::free_aligned(buf);
    }
  }

  // reset the buffer.
  void reset() { off = 0; }

  // a read-only view of the buffer showing only those bytes that have
  // valid data.
  [[nodiscard]] key_view get_key_view() const noexcept {
    return key_view(buf, off);
  }

  // Append a byte to the buffer.
  void push(std::byte v) {
    ensure_available(sizeof(v));
    buf[off++] = v;
  }

  // Append some bytes to the buffer.
  void push(key_view v) {
    const auto n = v.size_bytes();
    ensure_available(n);
    std::memcpy(buf + off, v.data(), n);
    off += n;
  }

  // Pop off some bytes from the buffer.
  void pop(size_t n) {
    UNODB_DETAIL_ASSERT(off >= n);
    off -= n;
  }

 private:
  // ensure that we have at least the specified capacity in the
  // buffer.
  void ensure_capacity(size_t min_capacity) {
    unodb::detail::ensure_capacity(buf, cap, off, min_capacity);
  }

  // Used for the initial buffer.
  std::byte ibuf[detail::INITIAL_BUFFER_CAPACITY];

  // The buffer to accmulate the key.  Originally this is the [ibuf].
  // If that overflows, then something will be allocated.
  std::byte *buf{};
  size_t cap{};  // current buffer capacity
  size_t off{};  // #of bytes in the buffer having valid data.
};               // class key_buffer

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_ART_INTERNAL_HPP
