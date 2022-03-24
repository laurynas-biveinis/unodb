// Copyright 2022 Laurynas Biveinis

#ifndef NDEBUG

#include "global.hpp"

#include <new>

#include <gtest/gtest.h>

#include "gtest_utils.hpp"
#include "heap.hpp"
#include "qsbr.hpp"
#include "qsbr_gtest_utils.hpp"

namespace {

using QSBROOMTest = unodb::test::QSBRTestBase;

}  // namespace

UNODB_START_TESTS()

TEST_F(QSBROOMTest, Resume) {
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unsigned fail_n;
  for (fail_n = 1; fail_n < 3; ++fail_n) {
    unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
    UNODB_ASSERT_THROW(unodb::this_thread().qsbr_resume(), std::bad_alloc);
    unodb::test::allocation_failure_injector::reset();
    UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  }
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
  unodb::this_thread().qsbr_resume();
  unodb::test::allocation_failure_injector::reset();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_END_TESTS()

#endif  // #ifndef NDEBUG
