// Copyright 2019-2024 Laurynas Biveinis

// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "test_utils.hpp"

namespace {

// TODO(thompsonbry) : variable length keys.  Add coverage for
// lexicographic ordering of all the interesting key types via the
// key_encoder and their proper decoding (where possible) via the
// decoder.
//
// TODO(thompsonbry) : variable length keys.  Add a microbenchmark for
// the key_encoder.
template <class Db>
class ARTKeyEncodeDecodeTest : public ::testing::Test {
 public:
  using Test::Test;
};

using unodb::detail::compare;

// exposes some protected methods and data to the tests.
class my_key_encoder : public unodb::key_encoder {
 public:
  my_key_encoder() : key_encoder() {}
  static constexpr size_t get_initial_capacity() {
    return unodb::detail::INITIAL_BUFFER_CAPACITY;
  }
  size_t capacity() { return key_encoder::capacity(); }
  size_t size_bytes() { return key_encoder::size_bytes(); }
  void ensure_available(size_t req) { key_encoder::ensure_available(req); }
};

static constexpr auto INITIAL_CAPACITY = my_key_encoder::get_initial_capacity();

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
  T akey1, akey2;
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
                           std::array<const std::byte, sizeof(T)> ikey) {
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

TEST(ARTKeyEncodeDecodeTest, std_int8_C00010) {
  using T = std::int8_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{static_cast<std::byte>(0x81)};
  do_encode_decode_test(static_cast<T>(0x01LL), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint8_t pairs.
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(5), static_cast<T>(7));
  using U = T;
  static_assert(sizeof(U) == sizeof(T));
  do_encode_decode_order_test(
      std::numeric_limits<U>::min(),
      static_cast<U>(std::numeric_limits<U>::min() + static_cast<U>(1)));
  do_encode_decode_order_test(
      static_cast<U>(std::numeric_limits<U>::max() - static_cast<U>(1)),
      std::numeric_limits<U>::max());
}

TEST(ARTKeyEncodeDecodeTest, std_uint8_C00010) {
  using T = std::uint8_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{static_cast<std::byte>(0x01)};
  do_encode_decode_test(static_cast<T>(0x01ull), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint8_t pairs.
  do_encode_decode_order_test(static_cast<T>(0x01ull), static_cast<T>(0x09ull));
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(0x7Full), static_cast<T>(0x80ull));
  do_encode_decode_order_test(static_cast<T>(0xFEull), static_cast<T>(~0));
}

TEST(ARTKeyEncodeDecodeTest, std_uint16_C00010) {
  using T = std::uint16_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{static_cast<std::byte>(0x01),
                                              static_cast<std::byte>(0x02)};
  do_encode_decode_test(static_cast<T>(0x0102ull), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint16_t pairs.
  do_encode_decode_order_test(static_cast<T>(0x0102ull),
                              static_cast<T>(0x090Aull));
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(0x7FFFull),
                              static_cast<T>(0x8000ull));
  do_encode_decode_order_test(static_cast<T>(0xFFFEull), static_cast<T>(~0));
}

TEST(ARTKeyEncodeDecodeTest, std_int16_C00010) {
  using T = std::int16_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{static_cast<std::byte>(0x81),
                                              static_cast<std::byte>(0x02)};
  do_encode_decode_test(static_cast<T>(0x0102LL), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint16_t pairs.
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(5), static_cast<T>(7));
  using U = short;
  do_encode_decode_order_test(
      std::numeric_limits<U>::min(),
      static_cast<U>(std::numeric_limits<U>::min() + static_cast<U>(1)));
  do_encode_decode_order_test(
      static_cast<U>(std::numeric_limits<U>::max() - static_cast<U>(1)),
      std::numeric_limits<U>::max());
}

TEST(ARTKeyEncodeDecodeTest, std_uint32_C00010) {
  using T = std::uint32_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)};
  do_encode_decode_test(static_cast<T>(0x01020304), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint32_t pairs.
  do_encode_decode_order_test(static_cast<T>(0x01020304ull),
                              static_cast<T>(0x090A0B0Cull));
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(0x7FFFFFFFull),
                              static_cast<T>(0x80000000ull));
  do_encode_decode_order_test(static_cast<T>(0xFFFFFFFEull),
                              static_cast<T>(~0));
}

TEST(ARTKeyEncodeDecodeTest, std_int32_C00010) {
  using T = std::int32_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x81), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)};
  do_encode_decode_test(static_cast<T>(0x01020304LL), ikey);
  // round-trip tests.
  //
  // Note: 0, 1, ~0, etc. are already std::int32_t.  If that is not
  // true universally, then we need conditional compilation here to
  // avoid "warning useless cast" errors in the compiler.
  do_encode_decode_test(0);
  do_encode_decode_test(~0);
  do_encode_decode_test(0 + 1);
  do_encode_decode_test(0 - 1);
  do_encode_decode_test(~0 + 1);
  do_encode_decode_test(~0 - 1);
  // check lexicographic ordering for std::uint32_t pairs.
  do_encode_decode_order_test(0, 1);
  do_encode_decode_order_test(5, 7);
  do_encode_decode_order_test(std::numeric_limits<T>::min(),
                              std::numeric_limits<T>::min() + 1);
  do_encode_decode_order_test(std::numeric_limits<T>::max() - 1,
                              std::numeric_limits<T>::max());
}

TEST(ARTKeyEncodeDecodeTest, std_uint64_C00010) {
  using T = std::uint64_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04),
      static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
      static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)};
  do_encode_decode_test(static_cast<T>(0x0102030405060708), ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint64_t pairs.
  do_encode_decode_order_test<T>(0x0102030405060708ull,
                                 0x090A0B0C0D0F1011ull);
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test<T>(0x7FFFFFFFFFFFFFFFull,
                                 0x8000000000000000ull);
  do_encode_decode_order_test<T>(0xFFFFFFFFFFFFFFFEull,
                                 static_cast<T>(~0));
}

TEST(ARTKeyEncodeDecodeTest, std_int64_C00010) {
  using T = std::int64_t;
  // Check the encoder byte order.
  std::array<const std::byte, sizeof(T)> ikey{
      static_cast<std::byte>(0x81), static_cast<std::byte>(0x02),
      static_cast<std::byte>(0x03), static_cast<std::byte>(0x04),
      static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
      static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)};
  do_encode_decode_test<T>(0x0102030405060708LL, ikey);
  // round-trip tests.
  do_encode_decode_test(static_cast<T>(0));
  do_encode_decode_test(static_cast<T>(~0));
  do_encode_decode_test(static_cast<T>(0 + 1));
  do_encode_decode_test(static_cast<T>(0 - 1));
  do_encode_decode_test(static_cast<T>(~0 + 1));
  do_encode_decode_test(static_cast<T>(~0 - 1));
  // check lexicographic ordering for std::uint64_t pairs.
  do_encode_decode_order_test(static_cast<T>(0), static_cast<T>(1));
  do_encode_decode_order_test(static_cast<T>(5), static_cast<T>(7));
  do_encode_decode_order_test(std::numeric_limits<T>::min(),
                              std::numeric_limits<T>::min() + 1);
  do_encode_decode_order_test(std::numeric_limits<T>::max() - 1,
                              std::numeric_limits<T>::max());
}

UNODB_END_TESTS()

}  // namespace
