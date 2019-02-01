// Copyright 2019 Laurynas Biveinis
#include "art.hpp"
#include "gtest/gtest.h"  // IWYU pragma: keep

namespace {

TEST(UnoDB, single_node_tree) {
  unodb::db test_db;
  auto result = test_db.get(1);
  ASSERT_TRUE(!result);
  test_db.insert(1, {});
  result = test_db.get(0);
  ASSERT_TRUE(!result);
  result = test_db.get(1);
  ASSERT_TRUE(result);
}

}  // namespace
