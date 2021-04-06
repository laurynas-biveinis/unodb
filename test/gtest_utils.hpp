// Copyright 2021 Laurynas Biveinis
#ifndef UNODB_DB_GTEST_UTILS_HPP_
#define UNODB_DB_GTEST_UTILS_HPP_

#include "global.hpp"

#include <gtest/gtest.h>

// Because Google thinks
// error: must specify at least one argument for '...' parameter of variadic
// macro
//       [-Werror,-Wgnu-zero-variadic-macro-arguments]
// TYPED_TEST_CASE(ARTCorrectnessTest, ARTTypes);
//                                             ^
// is not a bug: https://github.com/google/googletest/issues/2271
#define UNODB_TYPED_TEST_SUITE(Suite, Types)                   \
  DISABLE_CLANG_WARNING("-Wgnu-zero-variadic-macro-arguments") \
  TYPED_TEST_SUITE(Suite, Types);                              \
  RESTORE_CLANG_WARNINGS()

// Because Google thinks
// error: 'void {anonymous}::ARTCorrectnessTest_single_node_tree_empty_value_
// Test<gtest_TypeParam_>::TestBody() [with gtest_TypeParam_ = unodb::db]'
// can be marked override [-Werror=suggest-override] is not a bug:
// https://github.com/google/googletest/issues/1063
#define UNODB_START_TYPED_TESTS() DISABLE_GCC_WARNING("-Wsuggest-override")

#define UNODB_ASSERT_DEATH(statement, regex)        \
  DISABLE_CLANG_WARNING("-Wused-but-marked-unused") \
  DISABLE_CLANG_WARNING("-Wcovered-switch-default") \
  ASSERT_DEATH(statement, regex)                    \
  RESTORE_CLANG_WARNINGS()

#endif  // UNODB_DB_GTEST_UTILS_HPP_
