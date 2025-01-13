// Copyright 2021-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_GTEST_UTILS_HPP
#define UNODB_DETAIL_GTEST_UTILS_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <gtest/gtest.h>

// Because Google thinks
// error: must specify at least one argument for '...' parameter of variadic
// macro
//       [-Werror,-Wgnu-zero-variadic-macro-arguments]
// TYPED_TEST_CASE(ARTCorrectnessTest, ARTTypes);
//                                             ^
// is not a bug: https://github.com/google/googletest/issues/2271
#define UNODB_TYPED_TEST_SUITE(Suite, Types)                                \
  UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wgnu-zero-variadic-macro-arguments") \
  UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wpedantic")                          \
  TYPED_TEST_SUITE(Suite, Types);                                           \
  UNODB_DETAIL_RESTORE_CLANG_WARNINGS()                                     \
  UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

#define UNODB_START_TESTS()                \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26409) \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26426) \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26440) \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)

#define UNODB_END_TESTS()              \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS() \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS() \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS() \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

// Because Google thinks
// error: 'void {anonymous}::ARTCorrectnessTest_single_node_tree_empty_value_
// Test<gtest_TypeParam_>::TestBody() [with gtest_TypeParam_ = unodb::db]'
// can be marked override [-Werror=suggest-override] is not a bug:
// https://github.com/google/googletest/issues/1063
#define UNODB_START_TYPED_TESTS() \
  UNODB_START_TESTS()             \
  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-override")

#define UNODB_ASSERT_DEATH(statement, regex)                       \
  do {                                                             \
    UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wused-but-marked-unused") \
    UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wcovered-switch-default") \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818)                       \
    ASSERT_DEATH(statement, regex);                                \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()                           \
    UNODB_DETAIL_RESTORE_CLANG_WARNINGS()                          \
  } while (0)

#define UNODB_TEST(Suite, Test)            \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26409) \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26426) \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26440) \
  TEST((Suite), (Test))                    \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#define UNODB_ASSERT_EQ(x, y)                \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)  \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_EQ((x), (y));                     \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_NEAR(x, y, e)           \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)  \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_NEAR((x), (y), (e));              \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_FALSE(cond)             \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_FALSE(cond);                      \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_GT(val1, val2)          \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_GT((val1), (val2));               \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_LE(val1, val2)          \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_LE((val1), (val2));               \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_LT(val1, val2)          \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_LT((val1), (val2));               \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_THAT(value, matcher)    \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_THAT((value), (matcher));         \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_ASSERT_THROW(statement, expected_exception) \
  do {                                                    \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818)              \
    ASSERT_THROW(statement, expected_exception);          \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()                  \
  } while (0)

#define UNODB_ASSERT_TRUE(cond)              \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)  \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    ASSERT_TRUE(cond);                       \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_EXPECT_EQ(x, y)                \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    EXPECT_EQ((x), (y));                     \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_EXPECT_GT(x, y)                \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    EXPECT_GT((x), (y));                     \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_EXPECT_TRUE(cond)              \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    EXPECT_TRUE(cond);                       \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#define UNODB_EXPECT_FALSE(cond)             \
  do {                                       \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26818) \
    EXPECT_FALSE(cond);                      \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  } while (0)

#endif  // UNODB_DETAIL_GTEST_UTILS_HPP
