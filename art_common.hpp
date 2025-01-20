// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_ART_COMMON_HPP
#define UNODB_DETAIL_ART_COMMON_HPP

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
// IWYU pragma: no_include <ostream>
// IWYU pragma: no_include <ostream.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>  // IWYU pragma: keep
#include <span>
#include "heap.hpp"
#include "portability_builtins.hpp"

namespace unodb {

// Key type for public API
//
// TODO(thompsonbry) : variable length keys. should become a template
// parameter for db, olc_db, mutex_db.
using key = std::uint64_t;
// using key = std::span<const std::byte>;

// Values are passed as non-owning pointers to memory with associated length
// (std::span). The memory is copied upon insertion.
using value_view = std::span<const std::byte>;

// Keys are passed as non-owning pointers to memory with associated
// length (std::span).
using key_view = std::span<const std::byte>;

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

namespace detail {

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
//
// TODO(thompsonbry) variable length keys - lift out as template argument.
static constexpr size_t INITIAL_BUFFER_CAPACITY = 256;

// Dump the encoded key as a sequence of bytes.
template <typename T>
[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_key(std::ostream &os, T k);

// Utility class for power of two expansion of buffers (internal API,
// forward declaration).
void ensure_capacity(std::byte *&buf,     // buffer to resize
                     size_t &cap,         // current buffer capacity
                     size_t off,          // current #of used bytes
                     size_t min_capacity  // desired new minimum capacity
);

}  // namespace detail

///
/// Key encodes and key decoder
///

// A utility class to generate binary comparable keys from a sequence
// of key components.  This class supports the various kinds of
// primitive data types and provides support for the caller to pass
// through Unicode sort keys.
//
// TODO(thompsonbry) - variable length keys - handle integer types
// TODO(thompsonbry) - variable length keys - handle floating point types
// TODO(thompsonbry) - variable length keys - handle successors
//
// TODO(thompsonbry) - variable length keys - handle unicode sort keys
// (caller must normalize, generate the sort key, and pad?)
//
// TODO(thompsonbry) - variable length keys - simply - try to use
// templates for msb, bswap, and encode/decode of unsigned values.
class key_encoder {
 protected:
  // highest bit for various data types.
  static constexpr uint64_t msb8 = 1ull << 7;
  static constexpr uint64_t msb16 = 1ull << 15;
  static constexpr uint64_t msb32 = 1ull << 31;
  static constexpr uint64_t msb64 = 1ull << 63;

  // the current capacity of the buffer.
  size_t capacity() const noexcept { return cap; }

  // the number of bytes of data in the internal buffer.
  size_t size_bytes() const noexcept { return off; }

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

  /// signed integers

  key_encoder &encode(std::int8_t v) {
    const uint8_t u = (v >= 0) ? msb8 + static_cast<uint8_t>(v)
                               : msb8 - static_cast<uint8_t>(-v);
    return encode(u);
  }

  key_encoder &encode(std::int16_t v) {
    const uint16_t u = (v >= 0) ? msb16 + static_cast<uint16_t>(v)
                                : msb16 - static_cast<uint16_t>(-v);
    return encode(u);
  }

  key_encoder &encode(std::int32_t v) {
    const uint32_t u = (v >= 0) ? msb32 + static_cast<uint32_t>(v)
                                : msb32 - static_cast<uint32_t>(-v);
    return encode(u);
  }

  key_encoder &encode(std::int64_t v) {
    const uint64_t u = (v >= 0) ? msb64 + static_cast<uint64_t>(v)
                                : msb64 - static_cast<uint64_t>(-v);
    return encode(u);
  }

  /// unsigned integers

  key_encoder &encode(std::uint8_t v) {
    ensure_available(sizeof(v));
    buf[off++] = reinterpret_cast<const std::byte &>(v);
    return *this;
  }

  key_encoder &encode(std::uint16_t v) {
    ensure_available(sizeof(v));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    const auto u = unodb::detail::bswap16(v);
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
    const auto u = unodb::detail::bswap32(v);
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
    const auto u = unodb::detail::bswap64(v);
#else
    const auto u = v;
#endif
    UNODB_DETAIL_ASSERT(sizeof(u) == sizeof(v));
    std::memcpy(buf + off, &u, sizeof(v));
    off += sizeof(v);
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

  // highest bit for various data types.
  static constexpr uint64_t msb8 = 1ull << 7;
  static constexpr uint64_t msb16 = 1ull << 15;
  static constexpr uint64_t msb32 = 1ull << 31;
  static constexpr uint64_t msb64 = 1ull << 63;

 public:
  // Build a decoder for the key_view.
  explicit key_decoder(const key_view kv)
      : buf(kv.data()), cap(kv.size_bytes()), off(0) {}

  /// signed integers

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::int8_t &v) {
    std::uint8_t u;
    decode(u);
    v = (u >= msb8) ? static_cast<int8_t>(u - msb8)
                    : -static_cast<int8_t>(msb8 - u);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::int16_t &v) {
    std::uint16_t u;
    decode(u);
    v = (u >= msb16) ? static_cast<int16_t>(u - msb16)
                     : -static_cast<int16_t>(msb16 - u);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::int32_t &v) {
    std::uint32_t u;
    decode(u);
    v = (u >= msb32) ? static_cast<int32_t>(u - msb32)
                     : -static_cast<int32_t>(msb32 - u);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::int64_t &v) {
    std::uint64_t u;
    decode(u);
    v = (u >= msb64) ? static_cast<int64_t>(u - msb64)
                     : -static_cast<int64_t>(msb64 - u);
    return *this;
  }

  /// unsigned integers

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint8_t &v) {
    v = reinterpret_cast<const std::uint8_t &>(buf[off++]);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint16_t &v) {
    std::uint16_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap16(u);
#else
    v = u;
#endif
    off += sizeof(u);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint32_t &v) {
    std::uint32_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap32(u);
#else
    v = u;
#endif
    off += sizeof(u);
    return *this;
  }

  // Decode a component of the indicated type from the key.
  key_decoder &decode(std::uint64_t &v) {
    std::uint64_t u;
    std::memcpy(&u, buf + off, sizeof(u));
#ifdef UNODB_DETAIL_LITTLE_ENDIAN
    v = unodb::detail::bswap64(u);
#else
    v = u;
#endif
    off += sizeof(u);
    return *this;
  }

};  // class key_decoder

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_COMMON_HPP
