// Copyright 2019-2024 Laurynas Biveinis

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "gtest_utils.hpp"

namespace {

// TODO(thompsonbry) : variable length keys.  Add coverage for
// lexicographic ordering of all the interesting key types via the
// key_encoder and their proper decoding (where possible) via the
// decoder.
//
// TODO(thompsonbry) : variable length keys.  Add a microbenchmark for
// the key_encoder & key_decoder.
//
// TODO(thompsonbry) : variable length keys.  To understand the
// overhead associated with variable length keys vs fixed width known
// type keys, compare performance for uint64_t as the Key type vs as
// the sole component of a variable length key.
//
template <class Db>
class ARTKeyEncodeDecodeTest : public ::testing::Test {
 public:
  using Test::Test;
};

using unodb::detail::compare;

// exposes some protected methods and data to the tests.
class my_key_encoder : public unodb::key_encoder {
 public:
  my_key_encoder() = default;
  static constexpr size_t get_initial_capacity() {
    return unodb::detail::INITIAL_BUFFER_CAPACITY;
  }
  size_t capacity() { return key_encoder::capacity(); }
  size_t size_bytes() { return key_encoder::size_bytes(); }
  void ensure_available(size_t req) { key_encoder::ensure_available(req); }
};

constexpr auto INITIAL_CAPACITY = my_key_encoder::get_initial_capacity();

// Test helper verifies that [ekey1] < [ekey2].
template <typename T>
void do_encode_decode_order_test(const T ekey1, const T ekey2) {
  unodb::key_encoder enc1{};
  unodb::key_encoder enc2{};  // separate decoder (backed by different span).
  const auto ikey1 = enc1.encode(ekey1).get_key_view();  // into encoder buf!
  const auto ikey2 = enc2.encode(ekey2).get_key_view();  // into encoder buf!
  EXPECT_EQ(compare(ikey1, ikey1), 0);
  EXPECT_EQ(compare(ikey2, ikey2), 0);
  EXPECT_NE(compare(ikey1, ikey2), 0);
  EXPECT_LT(compare(ikey1, ikey2), 0);
  EXPECT_GT(compare(ikey2, ikey1), 0);
  unodb::key_decoder dec1{ikey1};
  unodb::key_decoder dec2{ikey2};
  T akey1;
  T akey2;
  dec1.decode(akey1);
  dec2.decode(akey2);
  EXPECT_EQ(ekey1, akey1);
  EXPECT_EQ(ekey2, akey2);
}

UNODB_START_TESTS()

// basic memory management - initial buffer case.
TEST(ARTKeyEncodeDecodeTest, C00001) {
  my_key_encoder enc{};
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY);
  EXPECT_EQ(enc.size_bytes(), 0);
  // ensure some space is available w/o change in encoder.
  enc.ensure_available(INITIAL_CAPACITY - 1);  // edge case
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY);
  EXPECT_EQ(enc.size_bytes(), 0);
  // ensure some space is available w/o change in encoder.
  enc.ensure_available(INITIAL_CAPACITY);  // edge case
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY);
  EXPECT_EQ(enc.size_bytes(), 0);
  // reset -- nothing changes.
  enc.reset();
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY);
  EXPECT_EQ(enc.size_bytes(), 0);
  // key_view is empty
  auto kv = enc.get_key_view();
  EXPECT_EQ(kv.size_bytes(), 0);
}

// basic memory management -- buffer extension case.
TEST(ARTKeyEncodeDecodeTest, C00002) {
  my_key_encoder enc{};
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY);
  EXPECT_EQ(enc.size_bytes(), 0);
  // ensure some space is available w/o change in encoder.
  enc.ensure_available(INITIAL_CAPACITY + 1);       // edge case.
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY * 2);  // assumes power of two
  EXPECT_EQ(enc.size_bytes(), 0);
  EXPECT_EQ(enc.get_key_view().size_bytes(), 0);  // key_view is empty
  // reset.
  enc.reset();
  EXPECT_EQ(enc.capacity(), INITIAL_CAPACITY * 2);  // unchanged
  EXPECT_EQ(enc.size_bytes(), 0);                   // reset
  EXPECT_EQ(enc.get_key_view().size_bytes(), 0);    // key_view is empty
}

// Encode/decode round trip test.
template <typename T>
void do_encode_decode_test(const T ekey) {
  my_key_encoder enc{};
  enc.encode(ekey);
  const unodb::key_view kv = enc.get_key_view();  // encode
  EXPECT_EQ(kv.size_bytes(), sizeof(ekey));       // check size
  // decode check
  unodb::key_decoder dec{kv};
  T akey;
  dec.decode(akey);
  EXPECT_EQ(akey, ekey);
}

// Encode/decode round trip test which also verifies the encoded byte sequence.
template <typename T>
void do_encode_decode_test(const T ekey,
                           const std::array<const std::byte, sizeof(T)> ikey) {
  my_key_encoder enc{};
  enc.encode(ekey);
  const unodb::key_view kv = enc.get_key_view();  // encode
  EXPECT_EQ(kv.size_bytes(), sizeof(ekey));       // check size
  // check order.
  size_t i = 0;
  for (auto it = ikey.begin(); it != ikey.end(); it++) {
    EXPECT_EQ(*it, kv[i++]);
  }
  // decode check
  unodb::key_decoder dec{kv};
  T akey;
  dec.decode(akey);
  EXPECT_EQ(akey, ekey);
}

TEST(ARTKeyEncodeDecodeTest, UInt8C00010) {
  using T = std::uint8_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x01)};
  do_encode_decode_test(static_cast<T>(0x01ULL), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint8_t pairs.
  do_encode_decode_order_test(static_cast<T>(0x01ULL), static_cast<T>(0x09ULL));
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(0x7FULL), static_cast<T>(0x80ULL));
  do_encode_decode_order_test(static_cast<T>(0xFEULL), static_cast<T>(~0ULL));
}

TEST(ARTKeyEncodeDecodeTest, Int8C00010) {
  using T = std::int8_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x81)};
  do_encode_decode_test(static_cast<T>(0x01LL), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint8_t pairs.
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(5), static_cast<T>(7));
  do_encode_decode_order_test(
      std::numeric_limits<T>::min(),
      static_cast<T>(std::numeric_limits<T>::min() + one));
  do_encode_decode_order_test(
      static_cast<T>(std::numeric_limits<T>::max() - one),
      std::numeric_limits<T>::max());
}

TEST(ARTKeyEncodeDecodeTest, UInt16C00010) {
  using T = std::uint16_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x01), static_cast<std::byte>(0x02)};
  do_encode_decode_test(static_cast<T>(0x0102ULL), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint16_t pairs.
  do_encode_decode_order_test(static_cast<T>(0x0102ULL),
                              static_cast<T>(0x090AULL));
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(0x7FFFULL),
                              static_cast<T>(0x8000ULL));
  do_encode_decode_order_test(static_cast<T>(0xFFFEULL), static_cast<T>(~0ULL));
}

TEST(ARTKeyEncodeDecodeTest, Int16C00010) {
  using T = std::int16_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x81), static_cast<std::byte>(0x02)};
  do_encode_decode_test(static_cast<T>(0x0102LL), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint16_t pairs.
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(5), static_cast<T>(7));
  do_encode_decode_order_test(
      std::numeric_limits<T>::min(),
      static_cast<T>(std::numeric_limits<T>::min() + one));
  do_encode_decode_order_test(
      static_cast<T>(std::numeric_limits<T>::max() - one),
      std::numeric_limits<T>::max());
}

TEST(ARTKeyEncodeDecodeTest, Uint32C00010) {
  using T = std::uint32_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)};
  do_encode_decode_test(static_cast<T>(0x01020304), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint32_t pairs.
  do_encode_decode_order_test(static_cast<T>(0x01020304ULL),
                              static_cast<T>(0x090A0B0CULL));
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(0x7FFFFFFFULL),
                              static_cast<T>(0x80000000ULL));
  do_encode_decode_order_test(static_cast<T>(0xFFFFFFFEULL),
                              static_cast<T>(~0ULL));
}

TEST(ARTKeyEncodeDecodeTest, Int32C00010) {
  using T = std::int32_t;
  constexpr auto one = 1;  // useless cast:: static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x81), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)};
  do_encode_decode_test(static_cast<T>(0x01020304LL), ikey);
  // round-trip tests.
  //
  // Note: 0, 1, ~0, etc. are already std::int32_t.  If that is not
  // true universally, then we need conditional compilation here to
  // avoid "warning useless cast" errors in the compiler.
  do_encode_decode_test(0);
  do_encode_decode_test(0 + 1);
  do_encode_decode_test(0 - 1);
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint32_t pairs.
  do_encode_decode_order_test(0, 1);
  do_encode_decode_order_test(5, 7);
  do_encode_decode_order_test(std::numeric_limits<T>::min(),
                              std::numeric_limits<T>::min() + one);
  do_encode_decode_order_test(std::numeric_limits<T>::max() - one,
                              std::numeric_limits<T>::max());
}

TEST(ARTKeyEncodeDecodeTest, UInt64C00010) {
  using T = std::uint64_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04),
      static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
      static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)};
  do_encode_decode_test(static_cast<T>(0x0102030405060708), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint64_t pairs.
  do_encode_decode_order_test<T>(0x0102030405060708ULL, 0x090A0B0C0D0F1011ULL);
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test<T>(0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL);
  do_encode_decode_order_test<T>(0xFFFFFFFFFFFFFFFEULL, static_cast<T>(~0ULL));
}

TEST(ARTKeyEncodeDecodeTest, Int64C00010) {
  using T = std::int64_t;
  constexpr auto one = static_cast<T>(1);
  // Check the encoder byte order.
  constexpr std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x81), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04),
      static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
      static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)};
  do_encode_decode_test<T>(0x0102030405060708LL, ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(std::numeric_limits<T>::min());
  do_encode_decode_test(std::numeric_limits<T>::max());
  do_encode_decode_test(std::numeric_limits<T>::min() + one);
  do_encode_decode_test(std::numeric_limits<T>::max() - one);
  // check lexicographic ordering for std::uint64_t pairs.
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(5), static_cast<T>(7));
  do_encode_decode_order_test(std::numeric_limits<T>::min(),
                              std::numeric_limits<T>::min() + one);
  do_encode_decode_order_test(std::numeric_limits<T>::max() - one,
                              std::numeric_limits<T>::max());
}

UNODB_END_TESTS()

}  // namespace
