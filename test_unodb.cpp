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

auto assert_result_eq(unodb::db::get_result result,
                      unodb::value_view expected) noexcept {
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

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
  assert_result_eq(result, test_value_3);
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
  assert_result_eq(result, test_value_2);
  result = test_db.get(1);
  assert_result_eq(result, test_value_3);
  result = test_db.get(2);
  ASSERT_FALSE(result);
}

TEST(UnoDB, duplicate_key) {
  unodb::db test_db;
  ASSERT_TRUE(test_db.insert(0, unodb::value_view{test_value_1}));
  ASSERT_FALSE(test_db.insert(0, unodb::value_view{test_value_4}));
}

}  // namespace
