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

// IWYU pragma: no_include <__fwd/ostream.h>
// IWYU pragma: no_include <_string.h>
// IWYU pragma: no_include <ostream>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>  // IWYU pragma: keep
#include <memory>
#include <type_traits>

#include "art_common.hpp"
#include "assert.hpp"
#include "heap.hpp"
#include "node_type.hpp"
#include "portability_builtins.hpp"

namespace unodb::detail {

// A constant determining the initial capacity for the key_encoder and
// other similar internal buffers.  This should be set high enough
// that such objects DO NOT allocate for commonly used key lengths.
// These objects use an internal buffer of this capacity and then
// switch over to an explicitly allocated buffer if the capacity would
// be exceeded.
//
// If you are only using fixed width keys, then this can be sizeof(T).
// But in typical scenarios these objects are on the stack and there
// is little if any penalty to having a larger initial capacity for
// these buffers.
static constexpr size_t INITIAL_BUFFER_CAPACITY = 256;

// Forward declarations to use in unodb::db and its siblings
template <class>
class [[nodiscard]] basic_leaf;

template <class, class>
class [[nodiscard]] basic_db_leaf_deleter;

// Lexicographic comparison of bytes.
//
// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
[[nodiscard, gnu::pure]] constexpr int compare(const void *a, const size_t alen,
                                               const void *b,
                                               const size_t blen) {
  const auto shared_length = std::min(alen, blen);
  auto ret = std::memcmp(a, b, shared_length);
  ret = (ret != 0) ? ret : ((alen == blen) ? 0 : (alen < blen ? -1 : 1));
  return ret;
}

// Lexicographic comparison of key_views.
//
// @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
[[nodiscard, gnu::pure]] constexpr int compare(const unodb::key_view a,
                                               const unodb::key_view b) {
  return compare(a.data(), a.size_bytes(), b.data(), b.size_bytes());
}

// Internal ART key in binary-comparable format.  Application keys may
// be simple fixed width types (such as std::uint64_t) or variable
// length keys.  For the former, there are convenience methods on db,
// olc_db, etc. to convert external keys into the binary compariable
// format.  For the latter, the application is responsible for
// converting the data (e.g., certain columns in some ordering for a
// row of some relation) into the internal binary comparable key
// format.  A convenience class is offered to encode data.  The
// encoding is always well defined and decoding exists for all simple
// fixed width data types.  Unicode encoding is complex and out of
// scope - use a quality library such as ICU to produce appropriate
// Unicode sort keys for your application.  Unicode decoding is NOT
// well defined.  Applications involving database records and Unicode
// data will typically store the record identifier in a secondary
// index (ART) as the value associated with the key.  Using the record
// identifier, the original tuple can be discovered and the original
// Unicode data recovered from that tuple.
template <typename KeyType>
struct [[nodiscard]] basic_art_key final {
 private:
  // Convert a simple external key into an internal key supporting
  // lexicographic comparison.
  [[nodiscard, gnu::const]] static UNODB_DETAIL_CONSTEXPR_NOT_MSVC KeyType
  make_binary_comparable(KeyType k) noexcept {
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    return bswap(k);
#else
#error Needs implementing
#endif
  }

 public:
  constexpr basic_art_key() noexcept = default;

  // Construct converts a fixed width primitive type into a
  // lexicographically ordered key.
  //
  // Note: Use a key_encoder for complex keys, including multiple key
  // components or Unicode data.
  template <typename U = KeyType,
            typename std::enable_if<std::is_integral<U>::value, int>::type = 0>
  UNODB_DETAIL_CONSTEXPR_NOT_MSVC explicit basic_art_key(KeyType key_) noexcept
      : key{make_binary_comparable(key_)} {}

  // TODO(thompsonbry) variable length keys -- THIS NEEDS TO BE REPLACED and WE
  // MUST SUPPORT gsl::span for art_key.
  UNODB_DETAIL_CONSTEXPR_NOT_MSVC explicit basic_art_key(key_view) noexcept
      : key{0ULL} {
    UNODB_DETAIL_CANNOT_HAPPEN();
  }

  // @return -1, 0, or 1 if this key is LT, EQ, or GT the other key.
  [[nodiscard, gnu::pure]] constexpr int cmp(
      basic_art_key<KeyType> key2) const noexcept {
    if constexpr (std::is_same_v<KeyType, std::uint64_t>) {
      // fast path
      return std::memcmp(&key, &key2.key, sizeof(KeyType));
    } else {
      return compare(&key, sizeof(KeyType), &key2.key, sizeof(KeyType));
    }
  }

  // Return the byte at the specified index position in the binary
  // comparable key.
  [[nodiscard, gnu::pure]] constexpr auto operator[](
      std::size_t index) const noexcept {
    UNODB_DETAIL_ASSERT(index < size);
    return key_bytes[index];
  }

  // Return the backing key_view.
  //
  // TODO(thompsonbry) variable length keys.  For uint64_t
  // specialization, we keep this code and the caller needs to know
  // that it is non-owned and will be invalid if this art_key goes out
  // of scope.  For key_view keys, it would just return the key_view
  // backing this art_key.
  [[nodiscard, gnu::pure]] constexpr key_view get_key_view() const noexcept {
    return key_view(reinterpret_cast<const std::byte *>(&key), sizeof(key));
  }

  // TODO(thompsonbry) variable length keys.  This returns the
  // internal key rather than decoding the key.  It is used in THREE
  // (3) places by key_prefix.  Those uses need to be cleaned up and
  // this removed since it completely breaks encapsulation.  Also,
  // this method really can't be written for variable length keys
  // unless we are returning a gsl::span.  I've changed this from a
  // cast operator to something more explicit to make it easier to
  // track and fix this up.
  [[nodiscard, gnu::pure]] constexpr std::uint64_t get_internal_key()
      const noexcept {
    return key;
  }

  // Return the decoded form of the key.
  //
  // TODO(thompsonbry) variable length keys. pull decode() out into a
  // key decoder.  Note that key decoding is best effort only. This is
  // ONLY used by the iterator::visitor::get_key() method.
  [[nodiscard, gnu::pure]] constexpr KeyType decode() const noexcept {
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    return bswap(key);
#else
#error Needs implementing
#endif
  }

  // Shift the internal key some number of bytes to the right, causing
  // the key to be shorter by that may bytes.
  //
  // Note: For a fixed width type, this causes the key to be logically
  // zero filled as it becomes shorter.  E.g.
  //
  // 0x0011223344556677 shift_right(2) => 0x2233445566770000
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

// typed class representing the depth of the tree.
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
/// fast utility methods TODO(thompsonbry) move to a header?
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
  return shiftOr32bitInt(i) + 1;
}

template <typename T>
constexpr typename std::enable_if<std::is_integral<T>::value && sizeof(T) == 8,
                                  T>::type
NextPowerOfTwo(T i) {
  i = shiftOr32bitInt(i);
  i |= (i >> 32);
  return ++i;
}

// Utility class for power of two expansion of buffers.
void ensure_capacity(std::byte *&buf,     // buffer to resize
                     size_t &cap,         // current buffer capacity
                     size_t off,          // current #of used bytes
                     size_t min_capacity  // desired new minimum capacity
);

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
  // Visit the encoded key.
  //
  // Note: The lambda MUST NOT export a reference to the visited key.
  // If you want to access the visited key outside of the scope of a
  // single lambda invocation, then you must make a copy of the data.
  //
  // Note: The application MAY use the [key_decoder] to decode any key
  // corresponding to a sequence of one or more primitive data types.
  // However, key decoding is not well defined for Unicode sort keys.
  // The recommended pattern when the key contains Unicode data is to
  // convert it to a sort key using some collation order.  The Unicode
  // data may then be recovered by associating the key with a record
  // identifier, looking up the record and reading off the Unicode
  // value there.  This is a common secondary index scenario.
  //
  // TODO(thompsonbry) Variable length keys: We need to define a
  // visitor method to visit the internal key buffer without any
  // decoding.
  inline auto get_key() const noexcept { return it.get_key(); }

  // Visit the value.
  //
  // Note: The lambda MUST NOT export a reference to the visited
  // value.  If you to access the value outside of the scope of a
  // single lambda invocation, then you must make a copy of the data.
  inline auto get_value() const noexcept { return it.get_val(); }
};  // class visitor

// A buffer containing an expanable binary comparable key.  This is
// used to track the key by the iterator as things are pushed and
// popped on the stack.
class key_buffer {
 protected:
  size_t capacity() const noexcept { return cap; }

  // the number of bytes of data in the buffer.
  size_t size_bytes() const noexcept { return off; }

  // // a pointer to the start of the internal buffer.
  // template<typename T> T* data() noexcept {
  //   return reinterpret_cast<T*>(buf);
  // }

  // // a pointer to the start of the internal buffer.
  // template<typename T> const T* data() const noexcept {
  //   return reinterpret_cast<const T*>(buf);
  // }

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

};  // class key_buffer

///
/// Key encodes and key decoder
///

// A utility class to generate binary comparable keys from a sequence
// of key components.  This class supports the various kinds of
// primitive data types and provides support for the caller to pass
// through Unicode sort keys.
//
// TODO(thompsonbry) : variable length keys - move to public header
// and private implementation header.  Same for the key_decoder and
// visitor.
class key_encoder {
 protected:
  // The initial capacity of the key encoder.  This much data can fit
  // into the encoder without allocating a buffer on the heap.
  static constexpr uint64_t msb = 1ull << 63;

  size_t capacity() const noexcept { return cap; }

  // the number of bytes of data in the internal buffer.
  size_t size_bytes() const noexcept { return off; }

  // // a pointer to the start of the internal buffer.
  // template<typename T> T* data() noexcept {
  //   return reinterpret_cast<T*>(buf);
  // }

  // // a pointer to the start of the internal buffer.
  // template<typename T> const T* data() const noexcept {
  //   return reinterpret_cast<const T*>(buf);
  // }

  // ensure that the buffer can hold at least [req] additional bytes.
  void ensure_available(size_t req) {
    if (UNODB_DETAIL_UNLIKELY(off + req > cap)) {
      ensure_capacity(off + req);  // resize
    }
  }

 public:
  // setup a new key encoder.
  key_encoder() : buf(&ibuf[0]), cap(sizeof(ibuf)), off(0) {}

  ~key_encoder() {
    if (cap > sizeof(ibuf)) {  // free old buffer iff allocated
      detail::free_aligned(buf);
    }
  }

  // reset the encoder to encode another key.
  key_encoder &reset() {
    off = 0;
    return *this;
  }

  // a read-only view of the internal buffer showing only those bytes
  // that were encoded since the last reset().
  [[nodiscard]] key_view get_key_view() const noexcept {
    return key_view(buf, off);
  }

  key_encoder &encode(std::int64_t v) {
    const uint64_t u = (v >= 0) ? msb + static_cast<uint64_t>(v)
                                : msb - static_cast<uint64_t>(-v);
    return encode(u);
  }

  key_encoder &encode(std::uint64_t v) {
    ensure_available(sizeof(v));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    const auto u = unodb::detail::bswap(v);
#else
    const auto u = v;
#endif
    std::memcpy(buf + off, &u, sizeof(u));
    off += 8;
    return *this;
  }

 private:
  // ensure that we have at least the specified capacity in the
  // buffer.
  void ensure_capacity(size_t min_capacity) {
    unodb::detail::ensure_capacity(buf, cap, off, min_capacity);
  }

  // Used for the initial buffer.
  std::byte ibuf[detail::INITIAL_BUFFER_CAPACITY];

  // The buffer to accmulate the encoded key.  Originally this is the
  // [ibuf].  If that overflows, then something will be allocated.
  std::byte *buf{};
  size_t cap{};  // current buffer capacity
  size_t off{};  // #of bytes in the buffer having encoded data.

};  // class key_encoder

// A utility class that can decode binary comparable keys as long as
// those keys (except for Unicode sort keys).  To use this class, you
// need to know how a given key was encoded as a sequence of key
// components.
class key_decoder {
 private:
  const std::byte *buf;  // the data to be decoded
  const size_t cap;      // #of bytes in that buffer.
  size_t off{};          // the byte offset into that data.

  static constexpr uint64_t msb = 1ull << 63;

 public:
  // Build a decoder for the key_view.
  key_decoder(const key_view kv)
      : buf(kv.data()), cap(kv.size_bytes()), off(0) {}

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::int64_t &v) {
    std::uint64_t u;
    decode(u);
    v = (u >= msb) ? static_cast<int64_t>(u - msb)
                   : -static_cast<int64_t>(msb - u);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint64_t &v) {
    std::uint64_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap(u);
#else
    v = u;
#endif
    off += 8;
    return *this;
  }

};  // class key_decoder

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_INTERNAL_HPP
