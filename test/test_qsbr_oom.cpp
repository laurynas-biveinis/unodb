// Copyright 2022 Laurynas Biveinis

#ifndef NDEBUG

#include "global.hpp"

#include <new>

#include <gtest/gtest.h>

#include "gtest_utils.hpp"
#include "heap.hpp"
#include "qsbr.hpp"
#include "qsbr_gtest_utils.hpp"
#include "thread_sync.hpp"

namespace {

#ifdef __GLIBCXX__
constexpr auto std_thread_thread_alloc_count = 1;
#elif defined(__APPLE__)
constexpr auto std_thread_thread_alloc_count = 3;
#else
#error Needs porting
#endif

template <typename Init, typename TestOOM, typename AfterOOM,
          typename TestSuccess, typename AfterSuccess>
void oom_test(unsigned fail_limit, Init init, TestOOM test_oom,
              AfterOOM after_oom, TestSuccess test_success,
              AfterSuccess after_success) {
  init();
  unsigned fail_n;
  for (fail_n = 1; fail_n < fail_limit; ++fail_n) {
    unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
    UNODB_ASSERT_THROW(test_oom(), std::bad_alloc);
    unodb::test::allocation_failure_injector::reset();
    after_oom();
  }
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
  test_success();
  unodb::test::allocation_failure_injector::reset();
  after_success();
}

using QSBROOMTest = unodb::test::QSBRTestBase;

UNODB_START_TESTS()

TEST_F(QSBROOMTest, Resume) {
  oom_test(
      3,
      []() noexcept {
        qsbr_pause();
        UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
      },
      [] { unodb::this_thread().qsbr_resume(); },
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0); },
      [] { unodb::this_thread().qsbr_resume(); },
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); });
}

TEST_F(QSBROOMTest, StartThread) {
  unodb::qsbr_thread second_thread;
  oom_test(
      4 + std_thread_thread_alloc_count,
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); },
      [] { unodb::qsbr_thread oom_thread{[]() noexcept {}}; },
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); },
      [&second_thread] {
        second_thread = unodb::qsbr_thread{
            []() noexcept { UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2); }};
      },
      [&second_thread] { join(second_thread); });
}

TEST_F(QSBROOMTest, DeferredDeallocation) {
  auto *ptr = static_cast<char *>(allocate());
  unodb::qsbr_thread second_thread;
  const auto current_interval_total_dealloc_size_before =
      unodb::this_thread().get_current_interval_total_dealloc_size();
  oom_test(
      2,
      [&second_thread]() noexcept {
        second_thread = unodb::qsbr_thread{[] {
          unodb::detail::thread_syncs[0].notify();
          unodb::detail::thread_syncs[1].wait();

          quiescent();
        }};

        unodb::detail::thread_syncs[0].wait();
      },
      [ptr] { qsbr_deallocate(ptr); },
      [ptr, current_interval_total_dealloc_size_before]() noexcept {
        touch_memory(ptr);
        const auto current_interval_total_dealloc_size_after =
            unodb::this_thread().get_current_interval_total_dealloc_size();
        UNODB_ASSERT_EQ(current_interval_total_dealloc_size_before,
                        current_interval_total_dealloc_size_after);
      },
      [ptr] { qsbr_deallocate(ptr); },
      [&second_thread] {
        unodb::detail::thread_syncs[1].notify();
        join(second_thread);
      });
}

UNODB_END_TESTS()

}  // namespace

#endif  // #ifndef NDEBUG
