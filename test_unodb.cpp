// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>

#include "gtest/gtest.h"  // IWYU pragma: keep

#include "art.hpp"

namespace {

const constexpr auto test_value_1 = std::array<std::byte, 1>{std::byte{0x00}};
const constexpr auto test_value_2 =
    std::array<std::byte, 2>{std::byte{0x00}, std::byte{0x02}};
const constexpr auto test_value_3 =
    std::array<std::byte, 3>{std::byte{0x03}, std::byte{0x00}, std::byte{0x01}};
const constexpr auto test_value_4 = std::array<std::byte, 4>{
    std::byte{0x04}, std::byte{0x01}, std::byte{0x00}, std::byte{0x02}};

// warning: 'ScopedTrace' was marked unused but was used
// [-Wused-but-marked-unused]
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wused-but-marked-unused"
#endif

auto assert_result_eq(unodb::db::get_result result, unodb::value_view expected,
                      int caller_line) noexcept {
  testing::ScopedTrace trace(__FILE__, caller_line, "");
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

#define ASSERT_RESULT_EQ(result, expected) \
  assert_result_eq(result, expected, __LINE__)

TEST(UnoDB, single_node_tree_empty_value) {
  unodb::db test_db;
  auto result = test_db.get(1);
  ASSERT_FALSE(result);
  ASSERT_TRUE(test_db.insert(1, {}));
  result = test_db.get(0);
  ASSERT_FALSE(result);
  result = test_db.get(1);
  ASSERT_TRUE(result);
}

TEST(UnoDB, single_node_tree_nonempty_value) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(1, unodb::value_view{test_value_3}));
  const auto result = test_db.get(1);
  ASSERT_RESULT_EQ(result, test_value_3);
}

TEST(UnoDB, too_long_value) {
  std::byte fake_val{0x00};
  unodb::value_view too_long{
      &fake_val,
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U};
  unodb::db test_db;
  ASSERT_THROW((void)test_db.insert(1, too_long), std::length_error);
}

TEST(UnoDB, expand_leaf_to_node4) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(0, unodb::value_view{test_value_2}));
  ASSERT_TRUE(test_db.insert(1, unodb::value_view{test_value_3}));
  auto result = test_db.get(0);
  ASSERT_RESULT_EQ(result, test_value_2);
  result = test_db.get(1);
  ASSERT_RESULT_EQ(result, test_value_3);
  result = test_db.get(2);
  ASSERT_FALSE(result);
}

TEST(UnoDB, duplicate_key) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(0, unodb::value_view{test_value_1}));
  ASSERT_FALSE(test_db.insert(0, unodb::value_view{test_value_4}));
}

TEST(UnoDB, insert_to_full_node4) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(2, unodb::value_view{test_value_2}));
  ASSERT_TRUE(test_db.insert(4, unodb::value_view{test_value_4}));
  ASSERT_TRUE(test_db.insert(0, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(3, unodb::value_view{test_value_3}));
  auto result = test_db.get(3);
  ASSERT_RESULT_EQ(result, test_value_3);
  result = test_db.get(0);
  ASSERT_RESULT_EQ(result, test_value_1);
  result = test_db.get(4);
  ASSERT_RESULT_EQ(result, test_value_4);
  result = test_db.get(2);
  ASSERT_RESULT_EQ(result, test_value_2);
  result = test_db.get(1);
  ASSERT_FALSE(result);
  result = test_db.get(5);
  ASSERT_FALSE(result);
}

TEST(UnoDB, two_node4) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(1, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(3, unodb::value_view{test_value_3}));
  // Insert a value that does not share full prefix with the current Node4
  ASSERT_TRUE(test_db.insert(0xFF01, unodb::value_view{test_value_4}));

  ASSERT_RESULT_EQ(test_db.get(1), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(3), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(0xFF01), test_value_4);
  ASSERT_FALSE(test_db.get(0xFF00));
  ASSERT_FALSE(test_db.get(2));
}

}  // namespace
