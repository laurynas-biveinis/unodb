// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_ART_COMMON_HPP
#define UNODB_DETAIL_ART_COMMON_HPP

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__fwd/ostream.h>
// IWYU pragma: no_include <ostream>
// IWYU pragma: no_include <ostream.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iosfwd>  // IWYU pragma: keep
#include <iostream>
#include <span>
#include <string_view>

#include "duckdb_encode_decode.hpp"
#include "heap.hpp"
#include "portability_builtins.hpp"

namespace unodb {

template <typename Key, typename Value>
class db;

template <typename Key, typename Value>
class olc_db;

/// Values are passed as non-owning pointers to memory with associated length
/// using a `std::span`. The memory is copied upon insertion.
using value_view = std::span<const std::byte>;

/// Keys are passed as non-owning pointers to memory with associated length
/// using a `std::span`.  The memory is copied upon insertion.
using key_view = std::span<const std::byte>;

/// A type alias determining the maximum size of a key that may be stored in the
/// index.
using key_size_type = std::uint32_t;

/// A type alias determining the maximum size of a value that may be stored in
/// the index.
using value_size_type = std::uint32_t;

/// An object visited by the scan API.  The visitor passed to the caller's
/// lambda by the scan for each index entry visited by the scan.
template <typename Iterator>
class visitor {
 protected:
  Iterator &it;
  explicit visitor(Iterator &it_) : it(it_) {}

 public:
  using key_type = typename Iterator::key_type;
  using value_type = typename Iterator::value_type;
  /// Visit the encoded key.
  ///
  /// \note The lambda MUST NOT export a reference to the visited key.  If you
  /// want to access the visited key outside of the scope of a single lambda
  /// invocation, then you MUST make a copy of the data.
  ///
  /// \note The application MAY use the unodb::key_decoder to decode any key
  /// corresponding to a sequence of one or more primitive data types.  However,
  /// key decoding is not well defined for Unicode sort keys and all floating
  /// point \c NaN values are mapped to a canonical \c NaN by the
  /// unodb::key_encoder.  The recommended pattern when the key contains Unicode
  /// data is to convert it to a sort key using some collation order.  The
  /// Unicode data may then be recovered by associating the key with a record
  /// identifier, looking up the record and reading off the Unicode value there.
  /// This is a common secondary index scenario.
  [[nodiscard]] inline auto get_key() const noexcept {
    // TODO(laurynas) The auto return appears required for get_key().
    // E.g., example_art.cpp fails with return type == key_type.
    return it.get_key();
  }

  /// Visit the value.
  ///
  /// \note The lambda MUST NOT export a reference to the visited value.  If you
  /// to access the value outside of the scope of a single lambda invocation,
  /// then you must make a copy of the data.
  [[nodiscard]] inline auto get_value() const noexcept {
    // Note: auto return required for olc qsbr wrapper.
    return it.get_val();
  }

 private:
  friend class olc_db<key_type, value_type>;
  friend class db<key_type, value_type>;
};  // class visitor

namespace detail {

/// A constant determining the initial capacity for the unodb::key_encoder and
/// other similar internal buffers.  This should be set high enough that such
/// objects DO NOT allocate for commonly used key lengths.  These objects use an
/// internal buffer of this capacity and then switch over to an explicitly
/// allocated buffer if the capacity would be exceeded.
///
/// \note: If you are only using fixed width keys, then this can be sizeof(T).
/// In typical scenarios these objects are on the stack and there is little if
/// any penalty to having a larger initial capacity for these buffers.
static constexpr size_t INITIAL_BUFFER_CAPACITY = 256;

/// Dump the a byte as a hexadecimal number.
[[gnu::cold]] void dump_byte(std::ostream &os, std::byte byte);

/// Dump the value as a sequence of bytes.
[[gnu::cold]] void dump_val(std::ostream &os, unodb::value_view v);

/// Dump a `std::span` byte-wise (works on any unodb::key_view).
template <typename T>
[[gnu::cold]] void dump_key(std::ostream &os, key_view key) {
  const auto sz = key.size_bytes();
  os << "key(" << sz << "): 0x";
  for (std::size_t i = 0; i < sz; ++i) unodb::detail::dump_byte(os, key[i]);
}

/// Dump the key in lexicographic byte-wise order.
template <typename T>
[[gnu::cold]] void dump_key(std::ostream &os, T key) {
  if constexpr (std::is_same_v<T, key_view>) {
    const auto sz = key.size_bytes();
    os << "key(" << sz << "): 0x";
    for (std::size_t i = 0; i < sz; ++i) unodb::detail::dump_byte(os, key[i]);
  } else {
    os << "key: 0x" << std::hex << std::setfill('0') << std::setw(sizeof(key))
       << key << std::dec;
  }
}

//
// fast utility methods
//

/// 32bit int shift-or utility function that is used by
/// unodb::detail::next_power_of_two.
template <typename T>
constexpr typename std::enable_if<std::is_integral<T>::value, T>::type
shift_or_32bit_int(T i) {
  i |= (i >> 1);
  i |= (i >> 2);
  i |= (i >> 4);
  i |= (i >> 8);
  i |= (i >> 16);
  return i;
}

/// Find the next power of \c 2 for a 32-bit or 64-bit value.
///
/// \note it will overflow if the there is no higher power of \c 2 for a given
/// type \c T.
template <typename T>
[[nodiscard, gnu::pure]] constexpr
    typename std::enable_if<std::is_integral<T>::value && sizeof(T) == 4,
                            T>::type
    next_power_of_two(T i) noexcept {
  return shift_or_32bit_int(i) + static_cast<T>(1);
}

template <typename T>
[[nodiscard, gnu::pure]] constexpr
    typename std::enable_if<std::is_integral<T>::value && sizeof(T) == 8,
                            T>::type
    next_power_of_two(T i) noexcept {
  i = shift_or_32bit_int(i);
  i |= (i >> 32U);
  return ++i;
}

/// Compute the lexicographically next bit permutation.  This method gets used
/// when you want to form an exclusive upper bound for some key range.  You take
/// the upper bound and form the bitwise successor of that value to turn it into
/// an exclusive upper bound. This has to be done for each component of the
/// composite key, working backwards from the end of the key, until a component
/// is found which does not overflow (is not already \c ~0).
///
/// Suppose we have a pattern of \c N bits set to \c 1 in an unsigned integer
/// and we want the next permutation of \c N \c 1 bits in a lexicographical
/// sense. For example, if \c N is \c 3 and the bit pattern is \c 00010011, the
/// next patterns would be \c 00010101, \c 00010110, \c 00011001, \c 00011010,
/// \c 00011100, \c 00100011, and so forth. The following is a fast way to
/// compute the next permutation.
///
/// \param v Some unsigned value.
///
/// \sa https://graphics.stanford.edu/~seander/bithacks.html#NextBitPermutation
template <typename T>
[[nodiscard, gnu::pure]] constexpr T lexicographic_successor(T v) noexcept {
  const T t = v | (v - 1u);  // t gets v's least significant 0 bits set to 1
  // Next set to 1 the most significant bit to change, set to 0 the
  // least significant ones, and add the necessary 1 bits.
  const T w = (t + 1) | (((~t & -~t) - 1) >> (ctz(v) + 1));
  return w;
}

/// Utility method for power of two expansion of buffers (internal API, forward
/// declaration).
///
/// \param buf The buffer to resize.
///
/// \param cap The current buffer capacity.
///
/// \param off The current number of used bytes.
///
/// \param min_capacity The desired new minimum capacity.
inline void ensure_capacity(std::byte *&buf, size_t &cap, size_t off,
                            size_t min_capacity) {
  // Find the allocation size in bytes which satisfies that minimum capacity.
  // We first look for the next power of two.  Then we adjust for the case where
  // the [min_capacity] is already a power of two (a common edge case).
  auto nsize = detail::next_power_of_two(min_capacity);
  auto asize = (min_capacity == (nsize >> 1U)) ? min_capacity : nsize;
  auto *tmp = detail::allocate_aligned(asize);  // new allocation.
  std::memcpy(tmp, buf, off);                   // copy over the data.
  if (cap > INITIAL_BUFFER_CAPACITY) {          // free old buffer iff allocated
    detail::free_aligned(buf);
  }
  buf = static_cast<std::byte *>(tmp);
  cap = asize;
}

}  // namespace detail

//
// Key encodes and key decoder
//

/// A utility class to generate binary comparable keys from a sequence of key
/// components.  This class supports the various kinds of primitive data types
/// and provides support for the caller to pass through Unicode sort keys.
///
/// \note This class is NOT final so people can extend or override the
/// unodb::key_encoder (and unodb::key_decoder) for language specific handling
/// of order within floating point values, handling of database \c NULLs, etc.
class key_encoder {
 public:
  /// This indirectly determines the unodb::key_encoder::maxlen and is used as
  /// the byte width for the run-length encoding of the padding.
  ///
  /// \note The choice of `std::uint16` here has implications for both the
  /// maximum allowed key length and the overhead for each encoded text field
  /// (since we must use the same stride to encode the pad run length).  If this
  /// is change to `std::uint32`, then you can encode longer text fields, but
  /// the padding overhead will be \c 5 bytes (vs \c 3 bytes today).
  using size_type = std::uint16_t;

  /// The pad byte used when encoding variable length text into a key to
  /// logically extend the text field to unodb::key_encoder::maxlen bytes.  The
  /// pad byte (which is added to the buffer as an unsigned value) is followed
  /// by a run length count such that the key is logically padded out to the
  /// maximum length of a text field, which is unodb::key_encoder::maxlen.  The
  /// run length count is expressed in the unodb::key_encoder::size_type.
  static constexpr auto pad{static_cast<std::byte>(0x00)};

  /// The maximum length of a text component of the key.  Keys are truncated to
  /// at most this many bytes and then logically extended using the \c pad byte
  /// and a trailing run length until the field is logically \c maxlen bytes
  /// wide.  This field is computed such that the total byte width of the
  /// encoded text can be indexed by `sizeof(size_type)`.
  static constexpr auto maxlen{static_cast<size_type>(
      std::numeric_limits<size_type>::max() - sizeof(pad) - sizeof(size_type))};
  static_assert(sizeof(maxlen) == sizeof(size_type));

 protected:
  // highest bit for various data types.
  static constexpr std::uint8_t msb8 = 1U << 7;
  static constexpr std::uint16_t msb16 = 1U << 15;
  static constexpr std::uint32_t msb32 = 1U << 31;
  static constexpr std::uint64_t msb64 = 1ULL << 63;

 public:
  /// setup a new key encoder.
  key_encoder() noexcept = default;

  ~key_encoder() {
    if (cap > sizeof(ibuf)) {  // free old buffer iff allocated
      detail::free_aligned(buf);
    }
  }

  /// The number of bytes of data in the internal buffer.
  [[nodiscard]] size_t size_bytes() const noexcept { return off; }

  /// The current capacity of the buffer.
  [[nodiscard]] size_t capacity() const noexcept { return cap; }

  /// Ensure that the buffer can hold at least \a req additional bytes.
  void ensure_available(size_t req) {
    if (UNODB_DETAIL_UNLIKELY(off + req > cap)) {
      ensure_capacity(off + req);  // resize
    }
  }

  /// Reset the encoder to encode another key.
  key_encoder &reset() noexcept {
    off = 0;
    return *this;
  }

  /// Read-only view of the internal buffer showing only those bytes that were
  /// encoded since the last unodb::key_encoder::reset call.
  [[nodiscard]] key_view get_key_view() const noexcept {
    return key_view(buf, off);
  }

  /// Append a sequence of bytes to the key.  The caller is responsible for not
  /// violating the ART contract (no key may be a prefix of another key).
  key_encoder &append_bytes(std::span<const std::byte> data) {
    const auto sz = data.size_bytes();
    ensure_available(sz);
    std::memcpy(buf + off, data.data(), sz);
    off += sz;
    return *this;
  }

  //
  // signed integers
  //

  key_encoder &encode(std::int8_t v) {
    // TODO(laurynas): look into inling the 1 constants.
    constexpr auto i_one = static_cast<int8_t>(1);
    constexpr auto u_one = static_cast<uint8_t>(1);
    const auto u = static_cast<uint8_t>(
        (v >= 0) ? msb8 + static_cast<uint8_t>(v)
                 : msb8 - static_cast<uint8_t>(-(v + i_one)) - u_one);
    return encode(u);
  }

  key_encoder &encode(std::int16_t v) {
    constexpr auto i_one = static_cast<int16_t>(1);
    constexpr auto u_one = static_cast<uint16_t>(1);
    const auto u = static_cast<uint16_t>(
        (v >= 0) ? msb16 + static_cast<uint16_t>(v)
                 : msb16 - static_cast<uint16_t>(-(v + i_one)) - u_one);
    return encode(u);
  }

  key_encoder &encode(std::int32_t v) {
    constexpr int32_t i_one = 1;
    constexpr uint32_t u_one = static_cast<uint32_t>(1);
    const uint32_t u =
        (v >= 0) ? msb32 + static_cast<uint32_t>(v)
                 : msb32 - static_cast<uint32_t>(-(v + i_one)) - u_one;
    return encode(u);
  }

  key_encoder &encode(std::int64_t v) {
    constexpr int64_t i_one = static_cast<int64_t>(1);
    constexpr uint64_t u_one = static_cast<uint64_t>(1);
    const uint64_t u =
        (v >= 0) ? msb64 + static_cast<uint64_t>(v)
                 : msb64 - static_cast<uint64_t>(-(v + i_one)) - u_one;
    return encode(u);
  }

  //
  // unsigned integers
  //

  key_encoder &encode(std::uint8_t v) {
    ensure_available(sizeof(v));
    buf[off++] = reinterpret_cast<const std::byte &>(v);
    return *this;
  }

  key_encoder &encode(std::uint16_t v) {
    ensure_available(sizeof(v));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    const auto u = unodb::detail::bswap(v);
#else
    const auto u = v;
#endif
    UNODB_DETAIL_ASSERT(sizeof(u) == sizeof(v));
    std::memcpy(buf + off, &u, sizeof(v));
    off += sizeof(v);
    return *this;
  }

  key_encoder &encode(std::uint32_t v) {
    ensure_available(sizeof(v));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    const auto u = unodb::detail::bswap(v);
#else
    const auto u = v;
#endif
    UNODB_DETAIL_ASSERT(sizeof(u) == sizeof(v));
    std::memcpy(buf + off, &u, sizeof(v));
    off += sizeof(v);
    return *this;
  }

  key_encoder &encode(std::uint64_t v) {
    ensure_available(sizeof(v));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    const auto u = unodb::detail::bswap(v);
#else
    const auto u = v;
#endif
    UNODB_DETAIL_ASSERT(sizeof(u) == sizeof(v));
    std::memcpy(buf + off, &u, sizeof(v));
    off += sizeof(v);
    return *this;
  }

  //
  // floating point
  //

  /// Encode the floating-point value.
  ///
  /// \note Encoding maps all \c NaN values to a single canonical \c NaN.  This
  /// means that decoding is not perfect and various kinds of \c NaN all decode
  /// as a single canonical \c NaN.
  key_encoder &encode(float v) {
    return encode(unodb::detail::encode_floating_point<std::uint32_t>(v));
  }

  /// Encode the double precision value.
  ///
  /// \note Encoding maps all \c NaN values to a single canonical \c NaN.  This
  /// means that decoding is not perfect and various kinds of \c NaN all decode
  /// as a single canonical \c NaN.
  key_encoder &encode(double v) {
    return encode(unodb::detail::encode_floating_point<std::uint64_t>(v));
  }

  /// This encodes ASCII text or Unicode sort keys.  Keys are logically padded
  /// out to unodb::key_encoder::maxlen bytes and will be truncated if they
  /// would exceed unodb::ley_encoder::maxlen bytesq.
  ///
  /// \note The ART index disallows keys which are prefixes of other keys.  The
  /// logical padding addresses this and other issues while preserving
  /// lexicographic ordering.
  ///
  /// When handling Unicode, the caller is responsible for using a quality
  /// library (e.g., ICU) to (a) normalize their Unicode data; and (b) generate
  /// a Unicode sort key from their Unicode data.  The sort key will impose
  /// specific collation ordering semantics as configured by the application
  /// (locale, collation strength, decomposition mode).
  ///
  /// \param text A view onto some sequence of bytes.  The view will be
  /// truncated to at most unodb::key_encoder::maxlen bytes.  A
  /// unodb::key_encoder::pad byte and a run count are added to make all text
  /// fields logically unodb::key_encoder::maxlen bytes long. The truncation and
  /// padding (a) ensures that no key is a prefix of another key; and (b) keeps
  /// multi-field keys with embedded variable length text fields aligned such
  /// that the field following a variable length text field does not bleed into
  /// the lexiographic ordering of the variable length text field.
  key_encoder &encode_text(std::span<const std::byte> text) {
    // truncate view to at most maxlen bytes.
    text = (text.size_bytes() > maxlen) ? text.subspan(0, maxlen) : text;
    // normalize padding by stripping off any trailing [pad] bytes.
    auto sz = text.size_bytes();
    for (; sz > 0; sz--) {
      if (text[sz - 1] != pad) break;
    }
    text = text.subspan(0, sz);  // adjust span in case truncated.
    // Ensure enough room for the text, the pad byte, and the
    // run-length encoding of the pad byte.
    ensure_available(sz + 1 + sizeof(size_type));
    const auto padlen{static_cast<size_type>(maxlen - sz)};
    append_bytes(text);                      // append bytes to the buffer.
    encode(static_cast<std::uint8_t>(pad));  // encode as unsigned byte
    encode(padlen);  // logical run-length of the pad byte.
    return *this;
  }

  /// convenience alias.
  key_encoder &encode_text(std::string_view sv) {
    return encode_text(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(sv.data()), sv.size()));
  }

 private:
  /// Ensure that we have at least the specified capacity in the buffer.
  void ensure_capacity(size_t min_capacity) {
    unodb::detail::ensure_capacity(buf, cap, off, min_capacity);
  }

  /// Used for the initial buffer.
  std::byte ibuf[detail::INITIAL_BUFFER_CAPACITY];

  /// The buffer to accmulate the encoded key.  Originally this is the internal
  /// buffer.  If that overflows, then something will be allocated.
  std::byte *buf{&ibuf[0]};

  /// The current buffer capacity
  size_t cap{sizeof(ibuf)};

  /// The number of bytes in the buffer having encoded data.
  size_t off{0};
};  // class key_encoder

/// A utility class that can decode binary comparable keys as long as those keys
/// (except for Unicode sort keys).  To use this class, you need to know how a
/// given key was encoded as a sequence of key components.
///
/// \note This class is NOT final so people can extend or override the
/// unodb::key_decoder (and unodb::key_encoder) for language specific handling
/// of order within floating point values, handling of \c NULL values in
/// database query languages, etc.
class key_decoder {
 private:
  const std::byte *buf;  /// the data to be decoded
  const size_t cap;      /// #of bytes in that buffer.
  size_t off{0};         /// the byte offset into that data.

  // highest bit for various data types.
  static constexpr uint64_t msb8 = 1ULL << 7;
  static constexpr uint64_t msb16 = 1ULL << 15;
  static constexpr uint64_t msb32 = 1ULL << 31;
  static constexpr uint64_t msb64 = 1ULL << 63;

 public:
  /// Build a decoder for the unodb::key_view.
  explicit key_decoder(const key_view kv)
      : buf(kv.data()), cap(kv.size_bytes()) {}

  //
  // signed integers
  //

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::int8_t &v) {
    const auto one = static_cast<std::uint8_t>(1);
    std::uint8_t u;
    decode(u);
    v = (u >= msb8) ? static_cast<int8_t>(u - msb8)
                    : -static_cast<int8_t>(msb8 - one - u) -
                          static_cast<std::int8_t>(1);
    return *this;
  }

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::int16_t &v) {
    const auto one = static_cast<std::uint16_t>(1);
    std::uint16_t u;
    decode(u);
    v = (u >= msb16) ? static_cast<int16_t>(u - msb16)
                     : -static_cast<int16_t>(msb16 - one - u) -
                           static_cast<std::int16_t>(1);
    return *this;
  }

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::int32_t &v) {
    const auto one = static_cast<std::uint32_t>(1);
    std::uint32_t u;
    decode(u);
    v = (u >= msb32) ? static_cast<int32_t>(u - msb32)
                     : -static_cast<int32_t>(msb32 - one - u) - 1;
    return *this;
  }

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::int64_t &v) {
    const auto one = static_cast<std::uint64_t>(1);
    std::uint64_t u;
    decode(u);
    v = (u >= msb64) ? static_cast<int64_t>(u - msb64)
                     : -static_cast<int64_t>(msb64 - one - u) - 1LL;
    return *this;
  }

  //
  // unsigned integers
  //

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint8_t &v) {
    v = reinterpret_cast<const std::uint8_t &>(buf[off++]);
    return *this;
  }

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint16_t &v) {
    std::uint16_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap(u);
#else
    v = u;
#endif
    off += sizeof(u);
    return *this;
  }

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint32_t &v) {
    std::uint32_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap(u);
#else
    v = u;
#endif
    off += sizeof(u);
    return *this;
  }

  /// Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint64_t &v) {
    std::uint64_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap(u);
#else
    v = u;
#endif
    off += sizeof(u);
    return *this;
  }

  //
  // floating point
  //

  /// Decode a single-precision floating point value from the key.
  ///
  /// \note Encoding maps all \c NaN values to a single canonical \c NaN.  This
  /// means that decoding is not perfect and various kinds of \c NaN all decode
  /// as a single canonical \c NaN.
  key_decoder &decode(float &v) {
    std::uint32_t u;
    decode(u);
    v = unodb::detail::decode_floating_point<float>(u);
    return *this;
  }

  /// Decode a double-precision floating point value from the key.
  ///
  /// \note Encoding maps all \c NaN values to a single canonical \c NaN.  This
  /// means that decoding is not perfect and various kinds of \c NaN all decode
  /// as a single canonical \c NaN.
  key_decoder &decode(double &v) {
    std::uint64_t u;
    decode(u);
    v = unodb::detail::decode_floating_point<double>(u);
    return *this;
  }
};  // class key_decoder

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_COMMON_HPP
