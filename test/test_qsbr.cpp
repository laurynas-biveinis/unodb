// Copyright 2020-2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include <cmath>
#include <cstddef>  // IWYU pragma: keep
#include <limits>
#include <mutex>  // IWYU pragma: keep
#include <thread>
#include <utility>  // IWYU pragma: keep

#include <gtest/gtest.h>  // IWYU pragma: keep

#include "gtest_utils.hpp"  // IWYU pragma: keep
#include "heap.hpp"
#include "qsbr.hpp"
#include "qsbr_ptr.hpp"
#include "qsbr_test_utils.hpp"
#include "thread_sync.hpp"

namespace {

class QSBR : public ::testing::Test {
 protected:
  QSBR() noexcept {
    if (unodb::this_thread().is_qsbr_paused())
      unodb::this_thread().qsbr_resume();
    unodb::test::expect_idle_qsbr();
    unodb::qsbr::instance().reset_stats();
  }

  ~QSBR() noexcept override { unodb::test::expect_idle_qsbr(); }

  // Epochs

  void mark_epoch() noexcept {
    last_epoch_num = unodb::qsbr::instance().get_current_epoch();
  }

  void check_epoch_advanced() {
    const auto current_epoch = unodb::qsbr::instance().get_current_epoch();
    EXPECT_EQ(last_epoch_num + 1, current_epoch);
    last_epoch_num = current_epoch;
  }

  void check_epoch_same() const {
    const auto current_epoch = unodb::qsbr::instance().get_current_epoch();
    EXPECT_EQ(last_epoch_num, current_epoch);
  }

  // Allocation and deallocation

  [[nodiscard]] static void *allocate() {
    return unodb::detail::allocate_aligned(1);
  }

#ifndef NDEBUG
  UNODB_DETAIL_DISABLE_WARNING("-Wunused-variable")
  static void check_ptr_on_qsbr_dealloc(const void *ptr) noexcept {
    // The pointer must be readable
    static volatile char sink = *static_cast<const char *>(ptr);
  }
  UNODB_DETAIL_RESTORE_WARNINGS()
#endif

  static void qsbr_deallocate(void *ptr) {
    unodb::qsbr::instance().on_next_epoch_deallocate(ptr, 1
#ifndef NDEBUG
                                                     ,
                                                     check_ptr_on_qsbr_dealloc
#endif
    );
  }

  static void touch_memory(char *ptr, char opt_val = '\0') {
    if (opt_val != '\0') {
      *ptr = opt_val;
    } else {
      static char value = 'A';
      *ptr = value;
      ++value;
    }
  }

 public:
  QSBR(const QSBR &) = delete;
  QSBR(QSBR &&) = delete;
  QSBR &operator=(const QSBR &) = delete;
  QSBR &operator=(QSBR &&) = delete;

 private:
  std::size_t last_epoch_num{std::numeric_limits<std::size_t>::max()};
};

using QSBRDeathTest = QSBR;

using unodb::detail::thread_sync_1;
using unodb::detail::thread_sync_2;

void active_pointer_ops(void *raw_ptr) noexcept {
  unodb::qsbr_ptr<void> active_ptr{raw_ptr};
  unodb::qsbr_ptr<void> active_ptr2{active_ptr};
  unodb::qsbr_ptr<void> active_ptr3{std::move(active_ptr)};

  active_ptr = active_ptr2;
  active_ptr2 = std::move(active_ptr3);  // -V1001
}

TEST_F(QSBR, SingleThreadQuitPaused) {
  ASSERT_FALSE(unodb::this_thread().is_qsbr_paused());
  unodb::this_thread().qsbr_pause();
  ASSERT_TRUE(unodb::this_thread().is_qsbr_paused());
}

TEST_F(QSBR, SingleThreadPauseResume) {
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreads) {
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  unodb::qsbr_thread second_thread(
      [] { EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2); });
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsSecondQuitPaused) {
  unodb::qsbr_thread second_thread([] { unodb::this_thread().qsbr_pause(); });
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsSecondPaused) {
  unodb::qsbr_thread second_thread([] {
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    ASSERT_FALSE(unodb::this_thread().is_qsbr_paused());
    unodb::this_thread().qsbr_pause();
    ASSERT_TRUE(unodb::this_thread().is_qsbr_paused());
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    unodb::this_thread().qsbr_resume();
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
  });
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsFirstPaused) {
  unodb::qsbr_thread second_thread([] {
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  thread_sync_2.notify();
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsBothPaused) {
  unodb::qsbr_thread second_thread([] {
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    thread_sync_1.notify();
    unodb::this_thread().qsbr_pause();
    thread_sync_2.wait();
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
    unodb::this_thread().qsbr_resume();
  });
  thread_sync_1.wait();
  unodb::this_thread().qsbr_pause();
  thread_sync_2.notify();
  second_thread.join();
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsSequential) {
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::qsbr_thread second_thread(
      [] { ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1); });
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsDefaultCtor) {
  unodb::this_thread().qsbr_pause();
  unodb::qsbr_thread second_thread{};
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  second_thread = unodb::qsbr_thread{
      [] { ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1); }};
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::this_thread().qsbr_resume();
}

#ifndef NDEBUG

TEST_F(QSBR, ThreeThreadsInterleavedCtor) {
  std::thread second_thread_launcher([] {
    unodb::qsbr_thread second_thread(thread_sync_1, thread_sync_2, [] {});
    second_thread.join();
  });

  thread_sync_1.wait();
  unodb::qsbr_thread third_thread([] { thread_sync_2.notify(); });
  second_thread_launcher.join();
  third_thread.join();
}

TEST_F(QSBR, TwoThreadsInterleavedCtorDtor) {
  std::thread second_thread_launcher([] {
    unodb::qsbr_thread second_thread(thread_sync_1, thread_sync_2, [] {
      ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    });
    second_thread.join();
  });
  thread_sync_1.wait();
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  thread_sync_2.notify();
  second_thread_launcher.join();
  unodb::this_thread().qsbr_resume();
}

#endif  // NDEBUG

TEST_F(QSBR, SecondThreadAddedWhileFirstPaused) {
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);

  unodb::qsbr_thread second_thread(
      [] { ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1); });
  second_thread.join();

  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, SecondThreadAddedWhileFirstPausedBothRun) {
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);

  unodb::qsbr_thread second_thread([] {
    ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    thread_sync_1.notify();
    thread_sync_2.wait();
  });
  thread_sync_1.wait();
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
  thread_sync_2.notify();
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, ThreeThreadsInitialPaused) {
  unodb::this_thread().qsbr_pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::qsbr_thread second_thread([] {
    ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    thread_sync_1.notify();
    thread_sync_2.wait();
  });
  thread_sync_1.wait();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  unodb::qsbr_thread third_thread([] {
    ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    thread_sync_2.notify();
  });
  second_thread.join();
  third_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::this_thread().qsbr_resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, SingleThreadOneAllocation) {
  auto *ptr = static_cast<char *>(allocate());
  touch_memory(ptr);
  qsbr_deallocate(ptr);
}

TEST_F(QSBR, SingleThreadAllocationAndEpochChange) {
  auto *ptr = static_cast<char *>(allocate());
  touch_memory(ptr);
  qsbr_deallocate(ptr);

  mark_epoch();

  unodb::this_thread().quiescent();

  check_epoch_advanced();

  ptr = static_cast<char *>(allocate());
  touch_memory(ptr);
  qsbr_deallocate(ptr);
}

TEST_F(QSBR, ActivePointersBeforeQuiescentState) {
  auto *ptr = allocate();
  active_pointer_ops(ptr);
  qsbr_deallocate(ptr);
  unodb::this_thread().quiescent();
}

TEST_F(QSBR, ActivePointersBeforePause) {
  auto *ptr = allocate();
  active_pointer_ops(ptr);
  qsbr_deallocate(ptr);
  unodb::this_thread().qsbr_pause();
}

#ifndef NDEBUG

TEST_F(QSBRDeathTest, ActivePointersDuringQuiescentState) {
  auto *ptr = allocate();
  unodb::qsbr_ptr<void> active_ptr{ptr};
  UNODB_ASSERT_DEATH({ unodb::this_thread().quiescent(); }, "");
  qsbr_deallocate(ptr);
}

#endif

TEST_F(QSBR, TwoThreadEpochChangesSecondStartsQuiescent) {
  mark_epoch();

  unodb::qsbr_thread second_thread([] {
    unodb::this_thread().quiescent();
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();

  unodb::this_thread().quiescent();

  check_epoch_advanced();

  thread_sync_2.notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadEpochChanges) {
  mark_epoch();

  unodb::this_thread().quiescent();

  check_epoch_advanced();

  unodb::qsbr_thread second_thread([] {
    thread_sync_1.notify();
    thread_sync_2.wait();
    unodb::this_thread().quiescent();
    thread_sync_1.notify();
  });

  thread_sync_1.wait();

  check_epoch_same();

  unodb::this_thread().quiescent();

  check_epoch_same();

  thread_sync_2.notify();
  thread_sync_1.wait();

  check_epoch_advanced();

  second_thread.join();
}

TEST_F(QSBR, TwoThreadAllocations) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_sync_1.notify();
    thread_sync_2.wait();

    unodb::this_thread().quiescent();
    thread_sync_1.notify();
    thread_sync_2.wait();

    unodb::this_thread().quiescent();
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  qsbr_deallocate(ptr);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  touch_memory(ptr);

  thread_sync_2.notify();
  thread_sync_1.wait();

  unodb::this_thread().quiescent();

  touch_memory(ptr);

  thread_sync_2.notify();
  thread_sync_1.wait();

  thread_sync_2.notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadAllocationsQuitWithoutQuiescentState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  });

  thread_sync_1.wait();  // 1 <-
  qsbr_deallocate(ptr);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  touch_memory(ptr);

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  touch_memory(ptr);

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, SecondThreadAllocatingWhileFirstPaused) {
  unodb::this_thread().qsbr_pause();

  unodb::qsbr_thread second_thread([] {
    auto *ptr = static_cast<char *>(allocate());
    qsbr_deallocate(ptr);

    ptr = static_cast<char *>(allocate());

    thread_sync_1.notify();
    thread_sync_2.wait();

    qsbr_deallocate(ptr);
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr);

    unodb::this_thread().quiescent();

    touch_memory(ptr);

    thread_sync_1.notify();
    thread_sync_2.wait();

    unodb::this_thread().quiescent();

    touch_memory(ptr);

    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  unodb::this_thread().qsbr_resume();
  thread_sync_2.notify();

  thread_sync_1.wait();
  unodb::this_thread().quiescent();
  thread_sync_2.notify();

  thread_sync_1.wait();
  unodb::this_thread().quiescent();
  thread_sync_2.notify();

  second_thread.join();
}

TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  });

  thread_sync_1.wait();  // 1 <-
  qsbr_deallocate(ptr);

  unodb::this_thread().quiescent();
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  touch_memory(ptr);

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentStateBefore1stThreadQState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  qsbr_deallocate(ptr);

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  thread_sync_2.notify();
  second_thread.join();

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, ToSingleThreadedModeDeallocationsByRemainingThread) {
  unodb::qsbr_thread second_thread{[] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  }};

  thread_sync_1.wait();  // 1 <-

  auto *ptr = allocate();

  qsbr_deallocate(ptr);

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, TwoThreadsConsecutiveEpochAllocations) {
  mark_epoch();
  auto *ptr_1_1 = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    auto *ptr_2_1 = static_cast<char *>(allocate());

    qsbr_deallocate(ptr_2_1);
    unodb::this_thread().quiescent();
    thread_sync_1.notify();
    thread_sync_2.wait();

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_1);
    auto *ptr_2_2 = static_cast<char *>(allocate());
    qsbr_deallocate(ptr_2_2);
    unodb::this_thread().quiescent();

    thread_sync_1.notify();
    thread_sync_2.wait();

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_2);
    unodb::this_thread().quiescent();

    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  qsbr_deallocate(ptr_1_1);
  unodb::this_thread().quiescent();

  check_epoch_advanced();

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_1);
  auto *ptr_1_2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr_1_2);
  unodb::this_thread().quiescent();

  thread_sync_2.notify();
  thread_sync_1.wait();

  check_epoch_advanced();

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_2);
  unodb::this_thread().quiescent();

  thread_sync_2.notify();
  thread_sync_1.wait();

  check_epoch_advanced();

  thread_sync_2.notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsNoImmediateTwoEpochDeallocationOnOneQuitting) {
  mark_epoch();
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    unodb::this_thread().quiescent();

    thread_sync_1.notify();  // 3 ->
    thread_sync_2.wait();    // 4 <-
  }};

  thread_sync_1.wait();  // 1 <-
  qsbr_deallocate(ptr);

  unodb::this_thread().quiescent();

  thread_sync_2.notify();  // 2 ->
  thread_sync_1.wait();    // 3 <-

  check_epoch_advanced();
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  auto *ptr2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr2);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr2);

  thread_sync_2.notify();  // 4 ->
  second_thread.join();

  touch_memory(ptr);
  touch_memory(ptr2);

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, TwoThreadsAllocatingInTwoEpochsAndPausing) {
  mark_epoch();

  auto *ptr_1_1 = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    auto *ptr_2_1 = static_cast<char *>(allocate());
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    qsbr_deallocate(ptr_2_1);
    unodb::this_thread().quiescent();

    thread_sync_1.notify();  // 3 ->
    thread_sync_2.wait();    // 4 <-

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_1);
    auto *ptr_2_2 = static_cast<char *>(allocate());
    qsbr_deallocate(ptr_2_2);
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_2);

    thread_sync_1.notify();  // 5 ->
    thread_sync_2.wait();    // 6 <-

    unodb::this_thread().qsbr_pause();

    thread_sync_1.notify();  // 7 ->

    unodb::this_thread().qsbr_resume();
  }};

  thread_sync_1.wait();  // 1 <-

  qsbr_deallocate(ptr_1_1);
  unodb::this_thread().quiescent();

  thread_sync_2.notify();  // 2 ->
  thread_sync_1.wait();    // 3 <-

  check_epoch_advanced();

  thread_sync_2.notify();  // 4 ->
  thread_sync_1.wait();    // 5 <-

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_1);
  auto *ptr_1_2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr_1_2);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_2);

  unodb::this_thread().qsbr_pause();

  thread_sync_2.notify();  // 6 ->
  thread_sync_1.wait();    // 7 <-

  second_thread.join();

  unodb::this_thread().qsbr_resume();
}

TEST_F(QSBR, TwoThreadsDeallocateBeforeQuittingPointerStaysLive) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[ptr] { qsbr_deallocate(ptr); }};
  second_thread.join();

  touch_memory(ptr);

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, ThreeDeallocationRequestSets) {
  mark_epoch();
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    unodb::this_thread().quiescent();

    thread_sync_1.notify();  // 3 ->
    thread_sync_2.wait();    // 4 <-
  }};

  thread_sync_1.wait();  // 1 <-

  qsbr_deallocate(ptr);
  unodb::this_thread().quiescent();

  thread_sync_2.notify();  // 2 ->
  thread_sync_1.wait();    // 3 <-

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  check_epoch_advanced();
  unodb::this_thread().quiescent();

  thread_sync_2.notify();  // 4 ->

  second_thread.join();
}

TEST_F(QSBR, ReacquireLivePtrAfterQuiescentState) {
  mark_epoch();
  auto *const ptr = static_cast<char *>(allocate());
  touch_memory(ptr, 'A');

  unodb::qsbr_thread second_thread{[ptr] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    qsbr_deallocate(ptr);
  }};

  thread_sync_1.wait();  // 1 <-

  // Wrote ptr to a shared data structure and done with it for now
  unodb::this_thread().quiescent();

  check_epoch_same();

  {
    // Reacquired ptr from a shared data structure
    unodb::qsbr_ptr<char> active_ptr{ptr};

    thread_sync_2.notify();  // 2 ->

    second_thread.join();

    check_epoch_advanced();

    ASSERT_EQ(*active_ptr, 'A');
  }

  unodb::this_thread().quiescent();

  check_epoch_advanced();
}

TEST_F(QSBR, ResetStats) {
  auto *ptr = allocate();
  auto *ptr2 = allocate();

  unodb::qsbr_thread second_thread{[] {
    unodb::this_thread().quiescent();
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  }};

  thread_sync_1.wait();  // 1 <-
  qsbr_deallocate(ptr);
  qsbr_deallocate(ptr2);

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  ASSERT_EQ(unodb::qsbr::instance().get_max_backlog_bytes(), 2);
  ASSERT_EQ(unodb::qsbr::instance().get_mean_backlog_bytes(), 1);
  ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_max(), 2);
  ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_variance(), 1);
  ASSERT_EQ(unodb::qsbr::instance()
                .get_mean_quiescent_states_per_thread_between_epoch_changes(),
            1);

  unodb::qsbr::instance().reset_stats();

  ASSERT_EQ(unodb::qsbr::instance().get_max_backlog_bytes(), 0);
  ASSERT_EQ(unodb::qsbr::instance().get_mean_backlog_bytes(), 0);
  ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_max(), 0);
  ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_variance(), 0);
  ASSERT_TRUE(std::isnan(
      unodb::qsbr::instance()
          .get_mean_quiescent_states_per_thread_between_epoch_changes()));
}

TEST_F(QSBR, GettersConcurrentWithQuiescentState) {
  unodb::qsbr_thread second_thread{[] {
    unodb::this_thread().quiescent();

    thread_sync_1.notify();  // 1 -> & v

    ASSERT_EQ(unodb::qsbr::instance().get_max_backlog_bytes(), 0);
    ASSERT_EQ(unodb::qsbr::instance().get_mean_backlog_bytes(), 0);
    ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_max(), 0);
    ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_variance(), 0);
    volatile auto force_load [[maybe_unused]] =
        unodb::qsbr::instance()
            .get_mean_quiescent_states_per_thread_between_epoch_changes();
    ASSERT_EQ(unodb::qsbr::instance().previous_interval_size(), 0);
    ASSERT_EQ(unodb::qsbr::instance().current_interval_size(), 0);
    ASSERT_EQ(unodb::qsbr::instance().get_reserved_thread_capacity(), 2);
    ASSERT_LE(unodb::qsbr::instance().get_threads_in_previous_epoch(), 2);
    volatile auto force_load2 [[maybe_unused]] =
        unodb::qsbr::instance().get_current_epoch();
  }};

  thread_sync_1.wait();  // 1 <-

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  second_thread.join();
}

// TODO(laurynas): stat tests
// TODO(laurynas): quiescent_state_on_scope_exit tests?
// TODO(laurynas): qsbr::dump test

}  // namespace
