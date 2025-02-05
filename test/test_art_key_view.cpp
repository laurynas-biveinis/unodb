// Copyright 2025 UnoDB contributors

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>

#include <gtest/gtest.h>

#include "art_common.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"

namespace {

template <class Db>
class ARTKeyViewCorrectnessTest : public ::testing::Test {
 public:
  using Test::Test;
};

using ARTTypes =
    ::testing::Types<unodb::test::key_view_db, unodb::test::key_view_mutex_db,
                     unodb::test::key_view_olc_db>;

UNODB_TYPED_TEST_SUITE(ARTKeyViewCorrectnessTest, ARTTypes)

/// Unit test of correct rejection of a key which is too large to be
/// stored in the tree.
UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
TYPED_TEST(ARTKeyViewCorrectnessTest, TooLongKey) {
  constexpr std::byte fake_val{0x00};
  const unodb::key_view too_long{
      &fake_val,
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
          1U};

  unodb::test::tree_verifier<TypeParam> verifier;

  UNODB_ASSERT_THROW(std::ignore = verifier.get_db().insert(too_long, {}),
                     std::length_error);

  verifier.assert_empty();

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

/// Unit test inserts several string keys with proper encoding and
/// validates the tree.
TYPED_TEST(ARTKeyViewCorrectnessTest, EncodedTextKeys) {
  unodb::test::tree_verifier<TypeParam> verifier;
  unodb::key_encoder enc;
  const auto& val = unodb::test::test_values[0];
  verifier.insert(enc.reset().encode_text("").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("a").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("abba").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("banana").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("camel").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("yellow").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("ostritch").get_key_view(), val);
  verifier.insert(enc.reset().encode_text("zebra").get_key_view(), val);
  verifier.check_present_values();  // checks keys and key ordering.
}

/// Unit test inserts several string keys WITHOUT proper encoding
/// (they are just copied in by unodb::key_encoder::append_text(const
/// char*)) but which do not violate the contract (no key may be a
/// prefix of some other key) and validates the tree.
///
/// NOTE: For this test, we do not use any keys which are a prefix of
/// another key!!!  E.g., we use "abba" so we do not use "a" since
/// that is a prefix of "abba".  Likewise, we do not use "" since that
/// is a prefix of all other strings.
TYPED_TEST(ARTKeyViewCorrectnessTest, StringKeysWithoutProperEncoding) {
  unodb::test::tree_verifier<TypeParam> verifier;
  unodb::key_encoder enc;
  const auto& val = unodb::test::test_values[0];
  verifier.insert(enc.reset().append("abba").get_key_view(), val);
  verifier.insert(enc.reset().append("banana").get_key_view(), val);
  verifier.insert(enc.reset().append("camel").get_key_view(), val);
  verifier.insert(enc.reset().append("yellow").get_key_view(), val);
  verifier.insert(enc.reset().append("ostritch").get_key_view(), val);
  verifier.insert(enc.reset().append("zebra").get_key_view(), val);
  verifier.check_present_values();  // checks keys and key ordering.
}

UNODB_END_TESTS()

}  // namespace
