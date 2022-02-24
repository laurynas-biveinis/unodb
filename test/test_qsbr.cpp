// Copyright 2020-2022 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include <cmath>
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
 public:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  ~QSBR() override {
    if (unodb::this_thread().is_qsbr_paused())
      unodb::this_thread().qsbr_resume();
    unodb::this_thread().quiescent();
    unodb::test::expect_idle_qsbr();
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 protected:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)
  QSBR() {
    if (unodb::this_thread().is_qsbr_paused())
      unodb::this_thread().qsbr_resume();
    unodb::test::expect_idle_qsbr();
    unodb::qsbr::instance().reset_stats();
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] static auto get_qsbr_thread_count() noexcept {
    return unodb::qsbr_state::get_thread_count(
        unodb::qsbr::instance().get_state());
  }
  // Epochs

  void mark_epoch() noexcept {
    last_epoch =
        unodb::qsbr_state::get_epoch(unodb::qsbr::instance().get_state());
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

  void check_epoch_advanced() noexcept {
    const auto current_epoch =
        unodb::qsbr_state::get_epoch(unodb::qsbr::instance().get_state());
    UNODB_EXPECT_EQ(last_epoch.advance(), current_epoch);
    last_epoch = current_epoch;
  }

  void check_epoch_same() const noexcept {
    const auto current_epoch =
        unodb::qsbr_state::get_epoch(unodb::qsbr::instance().get_state());
    UNODB_EXPECT_EQ(last_epoch, current_epoch);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // Allocation and deallocation

  [[nodiscard]] static void *allocate() {
    return unodb::detail::allocate_aligned(1);
  }

#ifndef NDEBUG
  static void check_ptr_on_qsbr_dealloc(const void *ptr) noexcept {
    // The pointer must be readable
    static volatile char sink UNODB_DETAIL_UNUSED =
        *static_cast<const char *>(ptr);
  }
#endif

  static void qsbr_deallocate(void *ptr) {
    unodb::this_thread().on_next_epoch_deallocate(ptr, 1
#ifndef NDEBUG
                                                  ,
                                                  check_ptr_on_qsbr_dealloc
#endif
    );
  }

  static void touch_memory(char *ptr, char opt_val = '\0') noexcept {
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
  unodb::qsbr_epoch last_epoch{};
};

using QSBRDeathTest = QSBR;

using unodb::detail::thread_syncs;

void active_pointer_ops(void *raw_ptr) noexcept {
  unodb::qsbr_ptr<void> active_ptr{raw_ptr};
  unodb::qsbr_ptr<void> active_ptr2{active_ptr};
  unodb::qsbr_ptr<void> active_ptr3{std::move(active_ptr)};

  active_ptr = active_ptr2;
  active_ptr2 = std::move(active_ptr3);  // -V1001
}

UNODB_START_TESTS()

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

TEST_F(QSBR, SingleThreadQuitPaused) {
  UNODB_ASSERT_FALSE(unodb::this_thread().is_qsbr_paused());
  unodb::this_thread().qsbr_pause();
  UNODB_ASSERT_TRUE(unodb::this_thread().is_qsbr_paused());
}

TEST_F(QSBR, SingleThreadPauseResume) {
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  unodb::this_thread().qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

TEST_F(QSBR, TwoThreads) {
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  unodb::qsbr_thread second_thread(
      []() noexcept { UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2); });
  second_thread.join();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

TEST_F(QSBR, TwoThreadsSecondQuitPaused) {
  unodb::qsbr_thread second_thread([]() { unodb::this_thread().qsbr_pause(); });
  second_thread.join();
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

TEST_F(QSBR, TwoThreadsSecondPaused) {
  unodb::qsbr_thread second_thread([] {
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
    UNODB_ASSERT_FALSE(unodb::this_thread().is_qsbr_paused());
    unodb::this_thread().qsbr_pause();
    UNODB_ASSERT_TRUE(unodb::this_thread().is_qsbr_paused());
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 1);
    unodb::this_thread().qsbr_resume();
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
  });
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsFirstPaused) {
  unodb::qsbr_thread second_thread([] {
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  unodb::this_thread().qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
  thread_syncs[1].notify();
  second_thread.join();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

TEST_F(QSBR, TwoThreadsBothPaused) {
  unodb::qsbr_thread second_thread([] {
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 2);
    thread_syncs[0].notify();
    unodb::this_thread().qsbr_pause();
    thread_syncs[1].wait();
    UNODB_EXPECT_EQ(get_qsbr_thread_count(), 0);
    unodb::this_thread().qsbr_resume();
  });
  thread_syncs[0].wait();
  unodb::this_thread().qsbr_pause();
  thread_syncs[1].notify();
  second_thread.join();
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

TEST_F(QSBR, TwoThreadsSequential) {
  unodb::this_thread().qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::qsbr_thread second_thread(
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); });
  second_thread.join();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

TEST_F(QSBR, TwoThreadsDefaultCtor) {
  unodb::this_thread().qsbr_pause();
  unodb::qsbr_thread second_thread{};
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  second_thread = unodb::qsbr_thread{
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); }};
  second_thread.join();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
}

TEST_F(QSBR, SecondThreadAddedWhileFirstPaused) {
  unodb::this_thread().qsbr_pause();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);

  unodb::qsbr_thread second_thread(
      []() noexcept { UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1); });
  second_thread.join();

  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

TEST_F(QSBR, SecondThreadAddedWhileFirstPausedBothRun) {
  unodb::this_thread().qsbr_pause();
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
  second_thread.join();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

TEST_F(QSBR, ThreeThreadsInitialPaused) {
  unodb::this_thread().qsbr_pause();
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
  second_thread.join();
  third_thread.join();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 0);
  unodb::this_thread().qsbr_resume();
  UNODB_ASSERT_EQ(get_qsbr_thread_count(), 1);
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

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

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

TEST_F(QSBRDeathTest, ActivePointersDuringQuiescentState) {
  auto *ptr = allocate();
  unodb::qsbr_ptr<void> active_ptr{ptr};
  UNODB_ASSERT_DEATH({ unodb::this_thread().quiescent(); }, "");
  qsbr_deallocate(ptr);
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#endif

TEST_F(QSBR, TwoThreadEpochChangesSecondStartsQuiescent) {
  mark_epoch();

  unodb::qsbr_thread second_thread([] {
    unodb::this_thread().quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();

  unodb::this_thread().quiescent();

  check_epoch_advanced();

  thread_syncs[1].notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadEpochChanges) {
  mark_epoch();

  unodb::this_thread().quiescent();

  check_epoch_advanced();

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();
    thread_syncs[1].wait();
    unodb::this_thread().quiescent();
    thread_syncs[0].notify();
  });

  thread_syncs[0].wait();

  check_epoch_same();

  unodb::this_thread().quiescent();

  check_epoch_same();

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  check_epoch_advanced();

  second_thread.join();
}

TEST_F(QSBR, QuiescentThreadQuittingDoesNotAdvanceEpoch) {
  unodb::qsbr_thread second_thread{[this] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 8 <-

    check_epoch_same();
    // 9 ->
  }};

  thread_syncs[0].wait();  // 1 <-

  unodb::qsbr_thread third_thread{[] {
    unodb::this_thread().quiescent();

    thread_syncs[2].notify();  // 2 ->
    thread_syncs[3].wait();    // 4 <-
    // 5 ->
  }};

  thread_syncs[2].wait();  // 2 <-

  unodb::qsbr_thread fourth_thread{[this] {
    thread_syncs[4].notify();  // 3 ->
    thread_syncs[5].wait();    // 6 <-

    unodb::this_thread().quiescent();

    mark_epoch();

    unodb::this_thread().quiescent();

    // 7 ->
  }};

  thread_syncs[4].wait();  // 3 <-

  unodb::this_thread().quiescent();

  thread_syncs[3].notify();  // 4 ->
  third_thread.join();       // 5 <-

  thread_syncs[5].notify();  // 6 ->
  fourth_thread.join();      // 7 <-

  unodb::this_thread().qsbr_pause();

  thread_syncs[1].notify();  // 8 ->
  second_thread.join();

  check_epoch_advanced();
}

TEST_F(QSBR, TwoThreadAllocations) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    unodb::this_thread().quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    unodb::this_thread().quiescent();
    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  qsbr_deallocate(ptr);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  touch_memory(ptr);

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  unodb::this_thread().quiescent();

  touch_memory(ptr);

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  thread_syncs[1].notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadAllocationsQuitWithoutQuiescentState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  });

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  touch_memory(ptr);

  thread_syncs[1].notify();  // 2 ->
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

    thread_syncs[0].notify();
    thread_syncs[1].wait();

    qsbr_deallocate(ptr);
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr);

    unodb::this_thread().quiescent();

    touch_memory(ptr);

    thread_syncs[0].notify();
    thread_syncs[1].wait();

    unodb::this_thread().quiescent();

    touch_memory(ptr);

    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  unodb::this_thread().qsbr_resume();
  thread_syncs[1].notify();

  thread_syncs[0].wait();
  unodb::this_thread().quiescent();
  thread_syncs[1].notify();

  thread_syncs[0].wait();
  unodb::this_thread().quiescent();
  thread_syncs[1].notify();

  second_thread.join();
}

TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentState) {
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread([] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  });

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);

  unodb::this_thread().quiescent();
  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  thread_syncs[1].notify();  // 2 ->
  second_thread.join();

  touch_memory(ptr);

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentStateBefore1stThreadQState) {
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
  second_thread.join();

  unodb::this_thread().quiescent();
}

TEST_F(QSBR, ToSingleThreadedModeDeallocationsByRemainingThread) {
  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  auto *ptr = allocate();

  qsbr_deallocate(ptr);

  thread_syncs[1].notify();  // 2 ->
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
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_1);
    auto *ptr_2_2 = static_cast<char *>(allocate());
    qsbr_deallocate(ptr_2_2);
    unodb::this_thread().quiescent();

    thread_syncs[0].notify();
    thread_syncs[1].wait();

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    touch_memory(ptr_2_2);
    unodb::this_thread().quiescent();

    thread_syncs[0].notify();
    thread_syncs[1].wait();
  });

  thread_syncs[0].wait();
  qsbr_deallocate(ptr_1_1);
  unodb::this_thread().quiescent();

  check_epoch_advanced();

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_1);
  auto *ptr_1_2 = static_cast<char *>(allocate());
  qsbr_deallocate(ptr_1_2);
  unodb::this_thread().quiescent();

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  check_epoch_advanced();

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr_1_2);
  unodb::this_thread().quiescent();

  thread_syncs[1].notify();
  thread_syncs[0].wait();

  check_epoch_advanced();

  thread_syncs[1].notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsNoImmediateTwoEpochDeallocationOnOneQuitting) {
  mark_epoch();
  auto *ptr = static_cast<char *>(allocate());

  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    unodb::this_thread().quiescent();

    thread_syncs[0].notify();  // 3 ->
    thread_syncs[1].wait();    // 4 <-
  }};

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);

  unodb::this_thread().quiescent();

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
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    qsbr_deallocate(ptr_2_1);
    unodb::this_thread().quiescent();

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

    unodb::this_thread().qsbr_pause();

    thread_syncs[0].notify();  // 7 ->

    unodb::this_thread().qsbr_resume();
  }};

  thread_syncs[0].wait();  // 1 <-

  qsbr_deallocate(ptr_1_1);
  unodb::this_thread().quiescent();

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

  unodb::this_thread().qsbr_pause();

  thread_syncs[1].notify();  // 6 ->
  thread_syncs[0].wait();    // 7 <-

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
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    unodb::this_thread().quiescent();

    thread_syncs[0].notify();  // 3 ->
    thread_syncs[1].wait();    // 4 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  qsbr_deallocate(ptr);
  unodb::this_thread().quiescent();

  thread_syncs[1].notify();  // 2 ->
  thread_syncs[0].wait();    // 3 <-

  // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
  touch_memory(ptr);

  check_epoch_advanced();
  unodb::this_thread().quiescent();

  thread_syncs[1].notify();  // 4 ->

  second_thread.join();
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

TEST_F(QSBR, ReacquireLivePtrAfterQuiescentState) {
  mark_epoch();
  auto *const ptr = static_cast<char *>(allocate());
  touch_memory(ptr, 'A');

  unodb::qsbr_thread second_thread{[ptr] {
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-

    qsbr_deallocate(ptr);
  }};

  thread_syncs[0].wait();  // 1 <-

  // Wrote ptr to a shared data structure and done with it for now
  unodb::this_thread().quiescent();

  check_epoch_same();

  {
    // Reacquired ptr from a shared data structure
    unodb::qsbr_ptr<char> active_ptr{ptr};

    thread_syncs[1].notify();  // 2 ->

    second_thread.join();

    check_epoch_advanced();

    UNODB_ASSERT_EQ(*active_ptr, 'A');
  }

  unodb::this_thread().quiescent();

  check_epoch_advanced();
}

TEST_F(QSBR, ResetStats) {
  auto *ptr = allocate();
  auto *ptr2 = allocate();

  unodb::qsbr_thread second_thread{[] {
    unodb::this_thread().quiescent();
    thread_syncs[0].notify();  // 1 ->
    thread_syncs[1].wait();    // 2 <-
  }};

  thread_syncs[0].wait();  // 1 <-
  qsbr_deallocate(ptr);
  qsbr_deallocate(ptr2);

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  thread_syncs[1].notify();  // 2 ->
  second_thread.join();

  UNODB_ASSERT_EQ(unodb::qsbr::instance().get_max_backlog_bytes(), 2);
  UNODB_ASSERT_NEAR(unodb::qsbr::instance().get_mean_backlog_bytes(), 0.666667,
                    0.00001);
  UNODB_ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_max(), 2);
  UNODB_ASSERT_NEAR(unodb::qsbr::instance().get_epoch_callback_count_variance(),
                    0.888889, 0.00001);
  UNODB_ASSERT_EQ(
      unodb::qsbr::instance()
          .get_mean_quiescent_states_per_thread_between_epoch_changes(),
      1.0);

  unodb::qsbr::instance().reset_stats();

  UNODB_ASSERT_EQ(unodb::qsbr::instance().get_max_backlog_bytes(), 0);
  UNODB_ASSERT_EQ(unodb::qsbr::instance().get_mean_backlog_bytes(), 0);
  UNODB_ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_max(), 0);
  UNODB_ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_variance(),
                  0);
  UNODB_ASSERT_TRUE(std::isnan(
      unodb::qsbr::instance()
          .get_mean_quiescent_states_per_thread_between_epoch_changes()));
}

TEST_F(QSBR, GettersConcurrentWithQuiescentState) {
  unodb::qsbr_thread second_thread{[] {
    unodb::this_thread().quiescent();

    thread_syncs[0].notify();  // 1 -> & v

    UNODB_ASSERT_EQ(unodb::qsbr::instance().get_max_backlog_bytes(), 0);
    UNODB_ASSERT_EQ(unodb::qsbr::instance().get_mean_backlog_bytes(), 0);
    UNODB_ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_max(), 0);
    UNODB_ASSERT_EQ(unodb::qsbr::instance().get_epoch_callback_count_variance(),
                    0);
    const volatile auto force_load [[maybe_unused]] =
        unodb::qsbr::instance()
            .get_mean_quiescent_states_per_thread_between_epoch_changes();
    UNODB_ASSERT_TRUE(
        unodb::qsbr::instance().previous_interval_orphaned_requests_empty());
    UNODB_ASSERT_TRUE(
        unodb::qsbr::instance().current_interval_orphaned_requests_empty());
    const auto current_qsbr_state = unodb::qsbr::instance().get_state();
    UNODB_ASSERT_LE(
        unodb::qsbr_state::get_threads_in_previous_epoch(current_qsbr_state),
        2);
    const volatile auto force_load3 [[maybe_unused]] =
        unodb::qsbr::instance().get_epoch_change_count();
  }};

  thread_syncs[0].wait();  // 1 <-

  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

  second_thread.join();
}

TEST_F(QSBR, DeallocEpochAssert) {
  unodb::qsbr_thread second_thread{[] {
    thread_syncs[0].notify();  // 1 ->

    thread_syncs[1].wait();  // 5 <-
  }};

  thread_syncs[0].wait();  // 1 <-

  auto *ptr = allocate();

  unodb::qsbr_thread third_thread{[] {
    thread_syncs[2].notify();  // 2 ->

    thread_syncs[3].wait();  // 3 <-
    unodb::this_thread().quiescent();
    thread_syncs[0].notify();  // 4 ->

    thread_syncs[2].wait();  // 6 <-
  }};

  thread_syncs[2].wait();  // 2 <-

  unodb::this_thread().quiescent();

  thread_syncs[3].notify();  // 3 ->
  thread_syncs[0].wait();    // 4 <-

  qsbr_deallocate(ptr);

  thread_syncs[1].notify();  // 5 ->
  second_thread.join();

  unodb::this_thread().quiescent();

  thread_syncs[2].notify();  // 6 ->
  third_thread.join();

  unodb::this_thread().qsbr_pause();
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

// TODO(laurynas): stat tests
// TODO(laurynas): quiescent_state_on_scope_exit tests?
// TODO(laurynas): qsbr::dump test

UNODB_END_TESTS()

}  // namespace
