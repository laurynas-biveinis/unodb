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

#include <gtest/gtest.h>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "gtest_utils.hpp"

namespace {

// TODO(thompsonbry) : variable length keys. Expand first to have
// coverage for the existing basic_art_key class.  Maybe add
// micro_benchmark_art_key also to give us a baseline for performance.
//
// TODO(thompsonbry) : variable length keys.  Add coverage for
// art_key<gsl::span<const std::byte>>
template <class Db>
class ARTKeyTest : public ::testing::Test {
 public:
  using Test::Test;
};

using ARTTypes = ::testing::Types<unodb::detail::basic_art_key<std::uint64_t>
                                  //  , gsl::span<const std::byte>
                                  >;

// decode a uint64_t key.
inline std::uint64_t decode(unodb::key_view akey) {
  unodb::key_decoder dec{akey};
  std::uint64_t k;
  dec.decode(k);
  return k;
}

UNODB_TYPED_TEST_SUITE(ARTKeyTest, ARTTypes)

UNODB_START_TYPED_TESTS()

// Basic encode/decode for a simple key type.  Mostly we cover this in
// the key_encoder tests, but this covers the historical case for
// uint64_t keys and sets us up for testing shift_right(), etc.
TYPED_TEST(ARTKeyTest, BasicArtKeyC0001) {
  const std::uint64_t ekey = 0x0102030405060708;           // external key
  const TypeParam ikey(ekey);                              // encode
  const std::uint64_t akey = decode(ikey.get_key_view());  // decode
  EXPECT_EQ(ekey, akey);
  // operator[] on art_key
  EXPECT_EQ(static_cast<std::byte>(0x01), ikey[0]);
  EXPECT_EQ(static_cast<std::byte>(0x02), ikey[1]);
  EXPECT_EQ(static_cast<std::byte>(0x03), ikey[2]);
  EXPECT_EQ(static_cast<std::byte>(0x04), ikey[3]);
  EXPECT_EQ(static_cast<std::byte>(0x05), ikey[4]);
  EXPECT_EQ(static_cast<std::byte>(0x06), ikey[5]);
  EXPECT_EQ(static_cast<std::byte>(0x07), ikey[6]);
  EXPECT_EQ(static_cast<std::byte>(0x08), ikey[7]);
}

TYPED_TEST(ARTKeyTest, BasicArtKeyC0010) {
  const std::uint64_t ekey2 = 0x0304050607080000;            // external key
  const TypeParam ikey2(ekey2);                              // encode
  const std::uint64_t akey2 = decode(ikey2.get_key_view());  // round trip
  EXPECT_EQ(ekey2, akey2);
}

UNODB_END_TESTS()

}  // namespace
