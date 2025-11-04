// Copyright 2022-2025 UnoDB contributors

#ifndef NDEBUG

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__new/exceptions.h>
// IWYU pragma: no_include <gtest/gtest.h>
// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>

#include <new>  // IWYU pragma: keep

#include "gtest_utils.hpp"
#include "qsbr.hpp"
#include "qsbr_gtest_utils.hpp"
#include "test_heap.hpp"
#include "thread_sync.hpp"

namespace {

#ifdef __GLIBCXX__
constexpr auto std_thread_thread_alloc_count = 1;
#elif defined(__APPLE__)
constexpr auto std_thread_thread_alloc_count = 3;
#else
#error Needs porting
#endif

template <typename Test, typename AfterOOM>
void oom_test(unsigned fail_limit, Test test, AfterOOM after_oom) {
  unsigned fail_n;
  for (fail_n = 1; fail_n < fail_limit; ++fail_n) {
    unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
    UNODB_ASSERT_THROW(test(), std::bad_alloc);
    unodb::test::allocation_failure_injector::reset();
    after_oom();
  }
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
  test();
  unodb::test::allocation_failure_injector::reset();
}

using QSBROOMTest = unodb::test::QSBRTestBase;

UNODB_TEST_F(QSBROOMTest, Resume) {
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  oom_test(
      3, [] { unodb::this_thread().qsbr_resume(); },
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0); });
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBROOMTest, StartThread) {
  unodb::qsbr_thread second_thread;
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  oom_test(
      4 + std_thread_thread_alloc_count,
      [&second_thread] {
        second_thread = unodb::qsbr_thread{
            []() noexcept { UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2); }};
      },
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); });
  join(second_thread);
}

UNODB_TEST_F(QSBROOMTest, DeferredDeallocation) {
  auto *ptr = static_cast<char *>(allocate());
  unodb::qsbr_thread second_thread{[] {
    unodb::detail::thread_syncs[0].notify();
    unodb::detail::thread_syncs[1].wait();

    quiescent();
  }};
  unodb::detail::thread_syncs[0].wait();
  oom_test(
      2, [ptr] { qsbr_deallocate(ptr); },
      [ptr]() noexcept { touch_memory(ptr); });
  unodb::detail::thread_syncs[1].notify();
  join(second_thread);
}

}  // namespace

#endif  // #ifndef NDEBUG
