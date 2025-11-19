// Copyright 2020-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__math/traits.h>
// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <system_error>
#include <utility>

#include "gtest_utils.hpp"
#include "qsbr.hpp"
#include "qsbr_gtest_utils.hpp"
#include "qsbr_ptr.hpp"
#include "thread_sync.hpp"

namespace {

using QSBR = unodb::test::QSBRTestBase;
using QSBRDeathTest = unodb::test::QSBRTestBase;

using unodb::detail::thread_syncs;

void active_pointer_ops(void *raw_ptr) noexcept {
  unodb::qsbr_ptr<void> active_ptr{raw_ptr};
  unodb::qsbr_ptr<void> active_ptr2{active_ptr};
  unodb::qsbr_ptr<void> active_ptr3{std::move(active_ptr)};

  active_ptr = active_ptr2;
  active_ptr2 = std::move(active_ptr3);  // -V1001
}

UNODB_TEST_F(QSBR, SingleThreadQuitPaused) {
  UNODB_ASSERT_FALSE(is_qsbr_paused());
  qsbr_pause();
  UNODB_ASSERT_TRUE(is_qsbr_paused());
}

UNODB_TEST_F(QSBR, SingleThreadPauseResume) {
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, TwoThreads) {
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  unodb::qsbr_thread second_thread(
      []() noexcept { UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2); });
  join(second_thread);
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, TwoThreadsSecondQuitPaused) {
  unodb::qsbr_thread second_thread([]()
#ifndef UNODB_DETAIL_WITH_STATS
                                       noexcept
#endif
                                   { qsbr_pause(); });
  join(second_thread);
}

UNODB_TEST_F(QSBR, TwoThreadsSecondPaused) {
  unodb::qsbr_thread second_thread([] {
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
    UNODB_ASSERT_FALSE(is_qsbr_paused());
    qsbr_pause();
    UNODB_ASSERT_TRUE(is_qsbr_paused());
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 1);
    unodb::this_thread().qsbr_resume();
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
  });
  join(second_thread);
}

UNODB_TEST_F(QSBR, TwoThreadsFirstPaused) {
  unodb::qsbr_thread second_thread([] {
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  thread_syncs[1].notify();
  join(second_thread);
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, TwoThreadsBothPaused) {
  unodb::qsbr_thread second_thread([] {
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
    thread_syncs[0].notify();  // 1 ->
    qsbr_pause();
    thread_syncs[1].wait();  // 2 <-
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 0);
    unodb::this_thread().qsbr_resume();
    thread_syncs[0].notify();  // 3 ->
  });
  thread_syncs[0].wait();  // 1 <-
  qsbr_pause();
  thread_syncs[1].notify();  // 2 ->
  thread_syncs[0].wait();    // 3 <-
  join(second_thread);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, TwoThreadsSequential) {
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::qsbr_thread second_thread(
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); });
  join(second_thread);
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, TwoThreadsDefaultCtor) {
  qsbr_pause();
  unodb::qsbr_thread second_thread{};
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  second_thread = unodb::qsbr_thread{
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); }};
  join(second_thread);
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
}

UNODB_TEST_F(QSBR, SecondThreadAddedWhileFirstPaused) {
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);

  unodb::qsbr_thread second_thread(
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); });
  join(second_thread);

  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, SecondThreadAddedWhileFirstPausedBothRun) {
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);

  unodb::qsbr_thread second_thread([] {
    UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });
  thread_syncs[0].wait();
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 2);
  thread_syncs[1].notify();
  join(second_thread);
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, ThreeThreadsInitialPaused) {
  qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::qsbr_thread second_thread([] {
    UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });
  thread_syncs[0].wait();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  unodb::qsbr_thread third_thread([] {
    UNODB_ASSERT_EQ(get_qsbr_thread_count(), 2);
    thread_syncs[1].notify();
  });
  join(second_thread);
  join(third_thread);
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_TEST_F(QSBR, SingleThreadOneAllocation) {
  auto *ptr = static_cast<char *>(allocate());
  touch_memory(ptr);
  qsbr_deallocate(ptr);
}

UNODB_TEST_F(QSBR, SingleThreadAllocationAndEpochChange) {
  auto *ptr = static_cast<char *>(allocate());
  touch_memory(ptr);
  qsbr_deallocate(ptr);

  mark_epoch();

  quiescent();

  check_epoch_advanced();

  ptr = static_cast<char *>(allocate());
  touch_memory(ptr);
  qsbr_deallocate(ptr);
}

UNODB_TEST_F(QSBR, SingleThreadAllocationAndEpochChangeOnScopeExit) {
  {
    const unodb::quiescent_state_on_scope_exit qsbr_after_deallocate;
    auto *const ptr = static_cast<char *>(allocate());
    touch_memory(ptr);
    qsbr_deallocate(ptr);

    mark_epoch();
  }

  check_epoch_advanced();

  auto *const ptr2 = static_cast<char *>(allocate());
  touch_memory(ptr2);
  qsbr_deallocate(ptr2);
}

UNODB_TEST_F(QSBR, QStateOnScopeExitInException) {
  try {
    const unodb::quiescent_state_on_scope_exit qsbr_on_scope_exit;
    throw std::system_error(std::make_error_code(std::errc::invalid_argument));
  } catch (const std::system_error &) {  // NOLINT(bugprone-empty-catch)
  }
}

UNODB_TEST_F(QSBR, ActivePointersBeforeQuiescentState) {
  auto *ptr = allocate();
  active_pointer_ops(ptr);
  qsbr_deallocate(ptr);
  quiescent();
}

UNODB_TEST_F(QSBR, ActivePointersBeforePause) {
  auto *ptr = allocate();
  active_pointer_ops(ptr);
  qsbr_deallocate(ptr);
  qsbr_pause();
}

#ifndef NDEBUG

UNODB_TEST_F(QSBRDeathTest, ActivePointersDuringQuiescentState) {
  auto *ptr = allocate();
  const unodb::qsbr_ptr<void> active_ptr{ptr};
  UNODB_ASSERT_DEATH({ quiescent(); }, "");
  qsbr_deallocate(ptr);
}

#endif

UNODB_TEST_F(QSBR, TwoThreadEpochChangesSecondStartsQuiescent) {
  mark_epoch();

  unodb::qsbr_thread second_thread([] {
    quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();

  quiescent();

  check_epoch_advanced();

  thread_syncs[1].notify();
  join(second_thread);
}

UNODB_TEST_F(QSBR, TwoThreadEpochChanges) {
  mark_epoch();

  quiescent();

  check_epoch_advanced();

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();
    thread_syncs[1].wait();
    quiescent();
    thread_syncs[0].notify();
  });

  thread_syncs[0].wait();

  check_epoch_same();

  quiescent();

  check_epoch_same();

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  check_epoch_advanced();

  join(second_thread);
}

UNODB_TEST_F(QSBR, QuiescentThreadQuittingDoesNotAdvanceEpoch) {
  unodb::qsbr_thread second_thread{[this] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 8 <-

    check_epoch_same();
    // 9 ->
  }};

  thread_syncs[0].wait();  // 1 <-

  unodb::qsbr_thread third_thread{[] {
    quiescent();

    thread_syncs[2].notify();  // 2 ->
    thread_syncs[3].wait();    // 4 <-
    // 5 ->
  }};

  thread_syncs[2].wait();  // 2 <-

  unodb::qsbr_thread fourth_thread{[this] {
    thread_syncs[4].notify();  // 3 ->
    thread_syncs[5].wait();    // 6 <-

    quiescent();

    mark_epoch();

    quiescent();

    // 7 ->
  }};

  thread_syncs[4].wait();  // 3 <-

  quiescent();

  thread_syncs[3].notify();  // 4 ->
  join(third_thread);        // 5 <-

  thread_syncs[5].notify();  // 6 ->
  join(fourth_thread);       // 7 <-

  qsbr_pause();

  thread_syncs[1].notify();  // 8 ->
  join(second_thread);

  check_epoch_advanced();
}

UNODB_TEST_F(QSBR, TwoThreadAllocations) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  qsbr_deallocate(ptr);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  quiescent();
  quiescent();

  touch_memory(ptr);

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  quiescent();

  touch_memory(ptr);

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  thread_syncs[1].notify();
  join(second_thread);
}

UNODB_TEST_F(QSBR, TwoThreadAllocationsQuitWithoutQuiescentState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  });

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  quiescent();
  quiescent();

  touch_memory(ptr);

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  touch_memory(ptr);

  quiescent();
}

UNODB_TEST_F(QSBR, SecondThreadAllocatingWhileFirstPaused) {
  qsbr_pause();

  unodb::qsbr_thread second_thread([] {
    auto *ptr = static_cast<char *>(allocate());
    qsbr_deallocate(ptr);

    ptr = static_cast<char *>(allocate());

    thread_syncs[0].notify();
    thread_syncs[1].wait();

    qsbr_deallocate(ptr);
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr);

    quiescent();

    touch_memory(ptr);

    thread_syncs[0].notify();
    thread_syncs[1].wait();

    quiescent();

    touch_memory(ptr);

    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  unodb::this_thread().qsbr_resume();
  thread_syncs[1].notify();

  thread_syncs[0].wait();
  quiescent();
  thread_syncs[1].notify();

  thread_syncs[0].wait();
  quiescent();
  thread_syncs[1].notify();

  join(second_thread);
}

UNODB_TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  });

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);

  quiescent();
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  touch_memory(ptr);

  quiescent();
}

UNODB_TEST_F(QSBR, SecondThreadQuittingWithoutQStateBefore1stThreadQState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  qsbr_deallocate(ptr);

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  thread_syncs[1].notify();
  join(second_thread);

  quiescent();
}

UNODB_TEST_F(QSBR, ToSingleThreadedModeBeforeDeallocation) {
  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  auto *ptr = allocate();

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  qsbr_deallocate(ptr);
}

UNODB_TEST_F(QSBR, ToSingleThreadedModeAfterDeallocation) {
  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  auto *ptr = allocate();

  qsbr_deallocate(ptr);

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  quiescent();
}

UNODB_TEST_F(QSBR, ToSingleThreadedModeAndAllocate) {
  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-
  auto *ptr = allocate();
  qsbr_deallocate(ptr);

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  auto *ptr2 = allocate();
  qsbr_deallocate(ptr2);
}

UNODB_TEST_F(QSBR, ToSingleThreadedModeDeallocationSeesIt) {
  auto *ptr = allocate();
  auto *ptr2 = allocate();

  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);

  quiescent();

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  auto *ptr3 = allocate();
  qsbr_deallocate(ptr2);
  qsbr_deallocate(ptr3);
}

UNODB_TEST_F(QSBR, TwoThreadsConsecutiveEpochAllocations) {
  mark_epoch();
  auto *ptr_1_1 = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    auto *ptr_2_1 = static_cast<char *>(allocate());

    qsbr_deallocate(ptr_2_1);
    quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_1);
    auto *ptr_2_2 = static_cast<char *>(allocate());
    qsbr_deallocate(ptr_2_2);
    quiescent();

    thread_syncs[0].notify();
    thread_syncs[1].wait();

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_2);
    quiescent();

    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  qsbr_deallocate(ptr_1_1);
  quiescent();

  check_epoch_advanced();

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_1);
  auto *ptr_1_2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr_1_2);
  quiescent();

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  check_epoch_advanced();

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_2);
  quiescent();

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  check_epoch_advanced();

  thread_syncs[1].notify();
  join(second_thread);
}

UNODB_TEST_F(QSBR, TwoThreadsNoImmediateTwoEpochDeallocationOnOneQuitting) {
  mark_epoch();
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    quiescent();

    thread_syncs[0].notify();  // 3 ->
    thread_syncs[1].wait();    // 4 <-
  }};

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);

  quiescent();

  thread_syncs[1].notify();  // 2 ->
  thread_syncs[0].wait();    // 3 <-

  check_epoch_advanced();
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  auto *ptr2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr2);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr2);

  thread_syncs[1].notify();  // 4 ->
  join(second_thread);

  touch_memory(ptr);
  touch_memory(ptr2);

  quiescent();
}

UNODB_TEST_F(QSBR, TwoThreadsAllocatingInTwoEpochsAndPausing) {
  mark_epoch();

  auto *ptr_1_1 = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    auto *ptr_2_1 = static_cast<char *>(allocate());
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    qsbr_deallocate(ptr_2_1);
    quiescent();

    thread_syncs[0].notify();  // 3 ->
    thread_syncs[1].wait();    // 4 <-

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_1);
    auto *ptr_2_2 = static_cast<char *>(allocate());
    qsbr_deallocate(ptr_2_2);
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_2);

    thread_syncs[0].notify();  // 5 ->
    thread_syncs[1].wait();    // 6 <-

    qsbr_pause();

    unodb::this_thread().qsbr_resume();

    thread_syncs[0].notify();  // 7 ->
  }};

  thread_syncs[0].wait();  // 1 <-

  qsbr_deallocate(ptr_1_1);
  quiescent();

  thread_syncs[1].notify();  // 2 ->
  thread_syncs[0].wait();    // 3 <-

  check_epoch_advanced();

  thread_syncs[1].notify();  // 4 ->
  thread_syncs[0].wait();    // 5 <-

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_1);
  auto *ptr_1_2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr_1_2);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_2);

  qsbr_pause();

  thread_syncs[1].notify();  // 6 ->
  thread_syncs[0].wait();    // 7 <-

  join(second_thread);

  unodb::this_thread().qsbr_resume();
}

UNODB_TEST_F(QSBR, TwoThreadsDeallocateBeforeQuittingPointerStaysLive) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[ptr] {
    qsbr_deallocate(ptr);
    thread_syncs[0].notify();  // 1 ->
  }};

  thread_syncs[0].wait();  // 1 <-
  join(second_thread);

  touch_memory(ptr);

  quiescent();
}

UNODB_TEST_F(QSBR, ThreeDeallocationRequestSets) {
  mark_epoch();
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    quiescent();

    thread_syncs[0].notify();  // 3 ->
    thread_syncs[1].wait();    // 4 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  qsbr_deallocate(ptr);
  quiescent();

  thread_syncs[1].notify();  // 2 ->
  thread_syncs[0].wait();    // 3 <-

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  check_epoch_advanced();
  quiescent();

  thread_syncs[1].notify();  // 4 ->

  join(second_thread);
}

UNODB_TEST_F(QSBR, ReacquireLivePtrAfterQuiescentState) {
  mark_epoch();
  auto *const ptr = static_cast<char *>(allocate());
  touch_memory(ptr, 'A');

  unodb::qsbr_thread second_thread{[ptr] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    qsbr_deallocate(ptr);

    thread_syncs[0].notify();  // 3 ->
  }};

  thread_syncs[0].wait();  // 1 <-

  // Wrote ptr to a shared data structure and done with it for now
  quiescent();

  check_epoch_same();

  {
    // Reacquired ptr from a shared data structure
    const unodb::qsbr_ptr<char> active_ptr{ptr};

    thread_syncs[1].notify();  // 2 ->
    thread_syncs[0].wait();    // 3 <-

    join(second_thread);

    check_epoch_advanced();

    UNODB_ASSERT_EQ(*active_ptr, 'A');
  }

  quiescent();

  check_epoch_advanced();
}

#ifdef UNODB_DETAIL_WITH_STATS

UNODB_TEST_F(QSBR, ResetStats) {
  auto *ptr = allocate();
  auto *ptr2 = allocate();

  unodb::qsbr_thread second_thread{[] {
    quiescent();
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);
  qsbr_deallocate(ptr2);

  quiescent();
  quiescent();

  thread_syncs[1].notify();  // 2 ->
  join(second_thread);

  UNODB_ASSERT_EQ(qsbr_get_max_backlog_bytes(), 2);
  UNODB_ASSERT_NEAR(qsbr_get_mean_backlog_bytes(), 0.666667, 0.00001);
  UNODB_ASSERT_EQ(qsbr_get_epoch_callback_count_max(), 2);
  UNODB_ASSERT_NEAR(qsbr_get_epoch_callback_count_variance(), 0.888889,
                    0.00001);
  UNODB_ASSERT_EQ(
      qsbr_get_mean_quiescent_states_per_thread_between_epoch_changes(), 1.0);

  quiescent();

  qsbr_reset_stats();

  UNODB_ASSERT_EQ(qsbr_get_max_backlog_bytes(), 0);
  UNODB_ASSERT_EQ(qsbr_get_mean_backlog_bytes(), 0);
  UNODB_ASSERT_EQ(qsbr_get_epoch_callback_count_max(), 0);
  UNODB_ASSERT_EQ(qsbr_get_epoch_callback_count_variance(), 0);
  UNODB_ASSERT_TRUE(std::isnan(
      qsbr_get_mean_quiescent_states_per_thread_between_epoch_changes()));
}

UNODB_TEST_F(QSBR, GettersConcurrentWithQuiescentState) {
  unodb::qsbr_thread second_thread{[] {
    quiescent();

    thread_syncs[0].notify();  // 1 -> & v

    UNODB_ASSERT_EQ(qsbr_get_max_backlog_bytes(), 0);
    UNODB_ASSERT_EQ(qsbr_get_mean_backlog_bytes(), 0);
    UNODB_ASSERT_EQ(qsbr_get_epoch_callback_count_max(), 0);
    UNODB_ASSERT_EQ(qsbr_get_epoch_callback_count_variance(), 0);
    const volatile auto force_load [[maybe_unused]] =
        qsbr_get_mean_quiescent_states_per_thread_between_epoch_changes();
    UNODB_ASSERT_TRUE(qsbr_previous_interval_orphaned_requests_empty());
    UNODB_ASSERT_TRUE(qsbr_current_interval_orphaned_requests_empty());
    UNODB_ASSERT_LE(get_qsbr_threads_in_previous_epoch(), 2);
    const volatile auto force_load3 [[maybe_unused]] =
        qsbr_get_epoch_change_count();
  }};

  thread_syncs[0].wait();  // 1 <-

  quiescent();
  quiescent();

  join(second_thread);
}

#endif  // UNODB_DETAIL_WITH_STATS

UNODB_TEST_F(QSBR, DeallocEpochAssert) {
  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->

    thread_syncs[1].wait();  // 5 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  auto *ptr = allocate();

  unodb::qsbr_thread third_thread{[] {
    thread_syncs[2].notify();  // 2 ->

    thread_syncs[3].wait();  // 3 <-
    quiescent();
    thread_syncs[0].notify();  // 4 ->

    thread_syncs[2].wait();  // 6 <-
  }};

  thread_syncs[2].wait();  // 2 <-

  quiescent();

  thread_syncs[3].notify();  // 3 ->
  thread_syncs[0].wait();    // 4 <-

  qsbr_deallocate(ptr);

  thread_syncs[1].notify();  // 5 ->
  join(second_thread);

  quiescent();

  thread_syncs[2].notify();  // 6 ->
  join(third_thread);

  qsbr_pause();
}

UNODB_TEST_F(QSBR, Dump) {
  std::ostringstream buf;
  unodb::qsbr::instance().dump(buf);
}

}  // namespace
