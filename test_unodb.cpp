// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>

#include "gtest/gtest.h"  // IWYU pragma: keep

#include "art.hpp"

namespace {

constexpr auto test_value_1 = std::array<std::byte, 1>{std::byte{0x00}};
constexpr auto test_value_2 =
    std::array<std::byte, 2>{std::byte{0x00}, std::byte{0x02}};
constexpr auto test_value_3 =
    std::array<std::byte, 3>{std::byte{0x03}, std::byte{0x00}, std::byte{0x01}};
constexpr auto test_value_4 = std::array<std::byte, 4>{
    std::byte{0x04}, std::byte{0x01}, std::byte{0x00}, std::byte{0x02}};
constexpr auto test_value_5 =
    std::array<std::byte, 5>{std::byte{0x05}, std::byte{0xF4}, std::byte{0xFF},
                             std::byte{0x00}, std::byte{0x01}};

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

TEST(UnoDB, db_insert_node_recursion) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(1, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(3, unodb::value_view{test_value_3}));
  // Insert a value that does not share full prefix with the current Node4
  ASSERT_TRUE(test_db.insert(0xFF0001, unodb::value_view{test_value_4}));
  // Then insert a value that shares full prefix with the above node and will
  // ask for a recursive insertion there
  ASSERT_TRUE(test_db.insert(0xFF0101, unodb::value_view{test_value_2}));

  ASSERT_RESULT_EQ(test_db.get(1), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(3), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(0xFF0001), test_value_4);
  ASSERT_RESULT_EQ(test_db.get(0xFF0101), test_value_2);
  ASSERT_FALSE(test_db.get(0xFF0100));
  ASSERT_FALSE(test_db.get(0xFF0000));
  ASSERT_FALSE(test_db.get(2));
}

TEST(UnoDB, node16) {
  unodb::db test_db;

  ASSERT_TRUE(test_db.insert(5, unodb::value_view{test_value_5}));
  ASSERT_TRUE(test_db.insert(3, unodb::value_view{test_value_3}));
  ASSERT_TRUE(test_db.insert(4, unodb::value_view{test_value_4}));
  ASSERT_TRUE(test_db.insert(1, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(2, unodb::value_view{test_value_2}));

  ASSERT_RESULT_EQ(test_db.get(5), test_value_5);
  ASSERT_RESULT_EQ(test_db.get(3), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(4), test_value_4);
  ASSERT_RESULT_EQ(test_db.get(1), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(2), test_value_2);

  ASSERT_FALSE(test_db.get(6));
  ASSERT_FALSE(test_db.get(0x0100));
  ASSERT_FALSE(test_db.get(0xFFFFFFFFFFFFFFFFULL));
}

TEST(UnoDB, full_node16) {
  unodb::db test_db;

  ASSERT_TRUE(test_db.insert(7, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(6, unodb::value_view{test_value_2}));
  ASSERT_TRUE(test_db.insert(5, unodb::value_view{test_value_3}));
  ASSERT_TRUE(test_db.insert(4, unodb::value_view{test_value_4}));
  ASSERT_TRUE(test_db.insert(3, unodb::value_view{test_value_5}));
  ASSERT_TRUE(test_db.insert(2, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(1, unodb::value_view{test_value_2}));
  ASSERT_TRUE(test_db.insert(0, unodb::value_view{test_value_3}));
  ASSERT_TRUE(test_db.insert(8, unodb::value_view{test_value_4}));
  ASSERT_TRUE(test_db.insert(9, unodb::value_view{test_value_5}));
  ASSERT_TRUE(test_db.insert(10, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(11, unodb::value_view{test_value_2}));
  ASSERT_TRUE(test_db.insert(12, unodb::value_view{test_value_3}));
  ASSERT_TRUE(test_db.insert(13, unodb::value_view{test_value_4}));
  ASSERT_TRUE(test_db.insert(14, unodb::value_view{test_value_5}));
  ASSERT_TRUE(test_db.insert(15, unodb::value_view{test_value_1}));

  ASSERT_FALSE(test_db.get(16));

  ASSERT_RESULT_EQ(test_db.get(7), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(6), test_value_2);
  ASSERT_RESULT_EQ(test_db.get(5), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(4), test_value_4);
  ASSERT_RESULT_EQ(test_db.get(3), test_value_5);
  ASSERT_RESULT_EQ(test_db.get(2), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(1), test_value_2);
  ASSERT_RESULT_EQ(test_db.get(0), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(8), test_value_4);
  ASSERT_RESULT_EQ(test_db.get(9), test_value_5);
  ASSERT_RESULT_EQ(test_db.get(10), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(11), test_value_2);
  ASSERT_RESULT_EQ(test_db.get(12), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(13), test_value_4);
  ASSERT_RESULT_EQ(test_db.get(14), test_value_5);
  ASSERT_RESULT_EQ(test_db.get(15), test_value_1);
}

TEST(UnoDB, node16_key_prefix_split) {
  unodb::db test_db;

  ASSERT_TRUE(test_db.insert(20, unodb::value_view{test_value_2}));
  ASSERT_TRUE(test_db.insert(10, unodb::value_view{test_value_1}));
  ASSERT_TRUE(test_db.insert(30, unodb::value_view{test_value_3}));
  ASSERT_TRUE(test_db.insert(40, unodb::value_view{test_value_4}));
  ASSERT_TRUE(test_db.insert(50, unodb::value_view{test_value_5}));

  // Insert a value that does share full prefix with the current Node16
  ASSERT_TRUE(test_db.insert(0x1020, unodb::value_view{test_value_1}));

  ASSERT_RESULT_EQ(test_db.get(20), test_value_2);
  ASSERT_RESULT_EQ(test_db.get(10), test_value_1);
  ASSERT_RESULT_EQ(test_db.get(30), test_value_3);
  ASSERT_RESULT_EQ(test_db.get(40), test_value_4);
  ASSERT_RESULT_EQ(test_db.get(50), test_value_5);
  ASSERT_RESULT_EQ(test_db.get(0x1020), test_value_1);

  ASSERT_FALSE(test_db.get(9));
  ASSERT_FALSE(test_db.get(0x10FF));
}

}  // namespace
