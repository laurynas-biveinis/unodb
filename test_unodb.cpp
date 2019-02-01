// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>

#include "gtest/gtest.h"  // IWYU pragma: keep

#include "art.hpp"

namespace {

TEST(UnoDB, single_node_tree_empty_value) {
  unodb::db test_db;
  auto result = test_db.get(1);
  ASSERT_TRUE(!result);
  test_db.insert(1, {});
  result = test_db.get(0);
  ASSERT_TRUE(!result);
  result = test_db.get(1);
  ASSERT_TRUE(result);
}

TEST(UnoDB, single_node_tree_nonempty_value) {
  std::array<std::byte, 3> test_value = {std::byte{0x03}, std::byte{0x00},
                                         std::byte{0x01}};
  unodb::db test_db;
  test_db.insert(1, unodb::value_view{test_value});
  const auto result = test_db.get(1);
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), test_value.cbegin(),
                         test_value.cend()));
}

}  // namespace
