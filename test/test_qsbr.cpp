// Copyright 2020-2021 Laurynas Biveinis

#include "global.hpp"

#include <cmath>
#include <cstddef>  // IWYU pragma: keep
#include <cstdint>
#include <limits>
#include <mutex>  // IWYU pragma: keep
#include <unordered_set>
#include <utility>  // IWYU pragma: keep

#include <gtest/gtest.h>  // IWYU pragma: keep

#include "debug_thread_sync.hpp"
#include "gtest_utils.hpp"  // IWYU pragma: keep
#include "heap.hpp"
#include "qsbr.hpp"
#include "qsbr_ptr.hpp"
#include "qsbr_test_utils.hpp"

namespace {

class mock_pool : public unodb::detail::pmr_pool {
 public:
  using unodb::detail::pmr_pool::pmr_pool;

  ~mock_pool() override { EXPECT_TRUE(empty()); }

  [[nodiscard]] bool empty() const noexcept { return allocations.empty(); }

  DISABLE_GCC_WARNING("-Wsuggest-attribute=pure")
  [[nodiscard]] auto is_allocated(void *ptr) const {
    return allocations.find(reinterpret_cast<std::uintptr_t>(ptr)) !=
           allocations.cend();
  }
  RESTORE_GCC_WARNINGS()

  [[nodiscard]] void *do_allocate(std::size_t bytes,
                                  std::size_t alignment) override {
    auto *const result = unodb::detail::pmr_allocate(
        *unodb::detail::pmr_new_delete_resource(), bytes, alignment);
    allocations.emplace(reinterpret_cast<std::uintptr_t>(result));
    return result;
  }

  void do_deallocate(void *ptr, std::size_t bytes,
                     std::size_t alignment) override {
    const auto elems_removed =
        allocations.erase(reinterpret_cast<std::uintptr_t>(ptr));
    EXPECT_EQ(elems_removed, 1);
    unodb::detail::pmr_deallocate(*unodb::detail::pmr_new_delete_resource(),
                                  ptr, bytes, alignment);
  }

  [[nodiscard]] bool do_is_equal(
      const unodb::detail::pmr_pool &other) const noexcept override {
    return other.is_equal(*unodb::detail::pmr_new_delete_resource());
  }

 private:
  std::unordered_set<std::uintptr_t> allocations{};
};

class QSBR : public ::testing::Test {
 protected:
  QSBR() noexcept {
    if (unodb::current_thread_reclamator().is_paused())
      unodb::current_thread_reclamator().resume();
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

  std::size_t last_epoch_num{std::numeric_limits<std::size_t>::max()};

  // Allocation and deallocation

  [[nodiscard]] void *mock_allocate() {
    std::lock_guard guard{mock_allocator_mutex};
    return allocator.allocate(1);
  }

  void mock_qsbr_deallocate(void *ptr) {
    std::lock_guard guard{mock_allocator_mutex};
    unodb::qsbr::instance().on_next_epoch_pool_deallocate(allocator, ptr, 1);
  }

  [[nodiscard]] bool mock_is_allocated(void *ptr) {
    std::lock_guard guard{mock_allocator_mutex};
    return allocator.is_allocated(ptr);
  }

  unodb::debug::thread_wait thread_sync_1;
  unodb::debug::thread_wait thread_sync_2;

 private:
  mock_pool allocator;
  std::mutex mock_allocator_mutex;
};

using QSBRDeathTest = QSBR;

void active_pointer_ops(void *raw_ptr) noexcept {
  unodb::qsbr_ptr<void> active_ptr{raw_ptr};
  unodb::qsbr_ptr<void> active_ptr2{active_ptr};
  unodb::qsbr_ptr<void> active_ptr3{std::move(active_ptr)};

  active_ptr = active_ptr2;
  active_ptr2 = std::move(active_ptr3);
}

TEST_F(QSBR, SingleThreadQuitPaused) {
  ASSERT_FALSE(unodb::current_thread_reclamator().is_paused());
  unodb::current_thread_reclamator().pause();
  ASSERT_TRUE(unodb::current_thread_reclamator().is_paused());
}

TEST_F(QSBR, SingleThreadPauseResume) {
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::current_thread_reclamator().resume();
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
  unodb::qsbr_thread second_thread(
      [] { unodb::current_thread_reclamator().pause(); });
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsSecondPaused) {
  unodb::qsbr_thread second_thread([] {
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    ASSERT_FALSE(unodb::current_thread_reclamator().is_paused());
    unodb::current_thread_reclamator().pause();
    ASSERT_TRUE(unodb::current_thread_reclamator().is_paused());
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    unodb::current_thread_reclamator().resume();
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
  });
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsFirstPaused) {
  unodb::qsbr_thread second_thread([this] {
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  thread_sync_2.notify();
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::current_thread_reclamator().resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsBothPaused) {
  unodb::qsbr_thread second_thread([this] {
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    thread_sync_1.notify();
    unodb::current_thread_reclamator().pause();
    thread_sync_2.wait();
    EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
    unodb::current_thread_reclamator().resume();
  });
  thread_sync_1.wait();
  unodb::current_thread_reclamator().pause();
  thread_sync_2.notify();
  second_thread.join();
  unodb::current_thread_reclamator().resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsSequential) {
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::qsbr_thread second_thread(
      [] { ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1); });
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::current_thread_reclamator().resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, TwoThreadsDefaultCtor) {
  unodb::current_thread_reclamator().pause();
  unodb::qsbr_thread second_thread{};
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  second_thread = unodb::qsbr_thread{
      [] { ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1); }};
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::current_thread_reclamator().resume();
}

#ifndef NDEBUG

TEST_F(QSBR, ThreeThreadsInterleavedCtor) {
  std::thread second_thread_launcher([this] {
    unodb::qsbr_thread second_thread(thread_sync_1, thread_sync_2, [] {});
    second_thread.join();
  });

  thread_sync_1.wait();
  unodb::qsbr_thread third_thread([this] { thread_sync_2.notify(); });
  second_thread_launcher.join();
  third_thread.join();
}

TEST_F(QSBR, TwoThreadsInterleavedCtorDtor) {
  std::thread second_thread_launcher([this] {
    unodb::qsbr_thread second_thread(thread_sync_1, thread_sync_2, [] {
      ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    });
    second_thread.join();
  });
  thread_sync_1.wait();
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  thread_sync_2.notify();
  second_thread_launcher.join();
  unodb::current_thread_reclamator().resume();
}

#endif  // NDEBUG

TEST_F(QSBR, SecondThreadAddedWhileFirstPaused) {
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);

  unodb::qsbr_thread second_thread(
      [] { ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1); });
  second_thread.join();

  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::current_thread_reclamator().resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, SecondThreadAddedWhileFirstPausedBothRun) {
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);

  unodb::qsbr_thread second_thread([this] {
    ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    thread_sync_1.notify();
    thread_sync_2.wait();
  });
  thread_sync_1.wait();
  unodb::current_thread_reclamator().resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
  thread_sync_2.notify();
  second_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, ThreeThreadsInitialPaused) {
  unodb::current_thread_reclamator().pause();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::qsbr_thread second_thread([this] {
    ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
    thread_sync_1.notify();
    thread_sync_2.wait();
  });
  thread_sync_1.wait();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  unodb::qsbr_thread third_thread([this] {
    ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 2);
    thread_sync_2.notify();
  });
  second_thread.join();
  third_thread.join();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 0);
  unodb::current_thread_reclamator().resume();
  ASSERT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
}

TEST_F(QSBR, SingleThreadOneAllocation) {
  auto *ptr = mock_allocate();
  ASSERT_TRUE(mock_is_allocated(ptr));
  mock_qsbr_deallocate(ptr);
  ASSERT_FALSE(mock_is_allocated(ptr));
}

TEST_F(QSBR, SingleThreadAllocationAndEpochChange) {
  auto *ptr = mock_allocate();
  ASSERT_TRUE(mock_is_allocated(ptr));
  mock_qsbr_deallocate(ptr);
  ASSERT_FALSE(mock_is_allocated(ptr));

  mark_epoch();

  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_advanced();

  ASSERT_FALSE(mock_is_allocated(ptr));
  ptr = mock_allocate();
  ASSERT_TRUE(mock_is_allocated(ptr));
  mock_qsbr_deallocate(ptr);
  ASSERT_FALSE(mock_is_allocated(ptr));
}

TEST_F(QSBR, ActivePointersBeforeQuiescentState) {
  auto *ptr = mock_allocate();
  active_pointer_ops(ptr);
  mock_qsbr_deallocate(ptr);
  unodb::current_thread_reclamator().quiescent_state();
}

TEST_F(QSBR, ActivePointersBeforePause) {
  auto *ptr = mock_allocate();
  active_pointer_ops(ptr);
  mock_qsbr_deallocate(ptr);
  unodb::current_thread_reclamator().pause();
}

#ifndef NDEBUG

TEST_F(QSBRDeathTest, ActivePointersDuringQuiescentState) {
  auto *ptr = mock_allocate();
  unodb::qsbr_ptr<void> active_ptr{ptr};
  UNODB_ASSERT_DEATH({ unodb::current_thread_reclamator().quiescent_state(); },
                     "");
  mock_qsbr_deallocate(ptr);
}

#endif

TEST_F(QSBR, TwoThreadEpochChangesSecondStartsQuiescent) {
  mark_epoch();

  unodb::qsbr_thread second_thread([this] {
    unodb::current_thread_reclamator().quiescent_state();
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();

  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_advanced();

  thread_sync_2.notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadEpochChanges) {
  mark_epoch();

  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_advanced();

  unodb::qsbr_thread second_thread([this] {
    thread_sync_1.notify();
    thread_sync_2.wait();
    unodb::current_thread_reclamator().quiescent_state();
    thread_sync_1.notify();
  });

  thread_sync_1.wait();

  check_epoch_same();

  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_same();

  thread_sync_2.notify();
  thread_sync_1.wait();

  check_epoch_advanced();

  second_thread.join();
}

TEST_F(QSBR, TwoThreadAllocations) {
  auto *ptr = mock_allocate();

  unodb::qsbr_thread second_thread([this] {
    thread_sync_1.notify();
    thread_sync_2.wait();

    unodb::current_thread_reclamator().quiescent_state();
    thread_sync_1.notify();
    thread_sync_2.wait();

    unodb::current_thread_reclamator().quiescent_state();
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  mock_qsbr_deallocate(ptr);
  ASSERT_TRUE(mock_is_allocated(ptr));

  unodb::current_thread_reclamator().quiescent_state();
  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_TRUE(mock_is_allocated(ptr));

  thread_sync_2.notify();
  thread_sync_1.wait();

  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_TRUE(mock_is_allocated(ptr));

  thread_sync_2.notify();
  thread_sync_1.wait();

  ASSERT_FALSE(mock_is_allocated(ptr));

  thread_sync_2.notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadAllocationsQuitWithoutQuiescentState) {
  auto *ptr = mock_allocate();

  unodb::qsbr_thread second_thread([this] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  });

  thread_sync_1.wait();  // 1 <-
  mock_qsbr_deallocate(ptr);
  ASSERT_TRUE(mock_is_allocated(ptr));

  unodb::current_thread_reclamator().quiescent_state();
  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_TRUE(mock_is_allocated(ptr));

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  ASSERT_TRUE(mock_is_allocated(ptr));

  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_FALSE(mock_is_allocated(ptr));
}

TEST_F(QSBR, SecondThreadAllocatingWhileFirstPaused) {
  unodb::current_thread_reclamator().pause();

  unodb::qsbr_thread second_thread([this] {
    auto *ptr = mock_allocate();
    mock_qsbr_deallocate(ptr);
    ASSERT_FALSE(mock_is_allocated(ptr));

    ptr = mock_allocate();

    thread_sync_1.notify();
    thread_sync_2.wait();

    mock_qsbr_deallocate(ptr);
    ASSERT_TRUE(mock_is_allocated(ptr));

    unodb::current_thread_reclamator().quiescent_state();

    ASSERT_TRUE(mock_is_allocated(ptr));

    thread_sync_1.notify();
    thread_sync_2.wait();

    unodb::current_thread_reclamator().quiescent_state();

    ASSERT_TRUE(mock_is_allocated(ptr));

    thread_sync_1.notify();
    thread_sync_2.wait();

    ASSERT_FALSE(mock_is_allocated(ptr));
  });

  thread_sync_1.wait();
  unodb::current_thread_reclamator().resume();
  thread_sync_2.notify();

  thread_sync_1.wait();
  unodb::current_thread_reclamator().quiescent_state();
  thread_sync_2.notify();

  thread_sync_1.wait();
  unodb::current_thread_reclamator().quiescent_state();
  thread_sync_2.notify();

  second_thread.join();
}

TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentState) {
  auto *ptr = mock_allocate();

  unodb::qsbr_thread second_thread([this] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  });

  thread_sync_1.wait();  // 1 <-
  mock_qsbr_deallocate(ptr);

  unodb::current_thread_reclamator().quiescent_state();
  ASSERT_TRUE(mock_is_allocated(ptr));

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  ASSERT_TRUE(mock_is_allocated(ptr));

  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_FALSE(mock_is_allocated(ptr));
}

TEST_F(QSBR, SecondThreadQuittingWithoutQuiescentStateBefore1stThreadQState) {
  auto *ptr = mock_allocate();

  unodb::qsbr_thread second_thread([this] {
    thread_sync_1.notify();
    thread_sync_2.wait();
  });

  thread_sync_1.wait();
  mock_qsbr_deallocate(ptr);

  ASSERT_TRUE(mock_is_allocated(ptr));

  thread_sync_2.notify();
  second_thread.join();

  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_FALSE(mock_is_allocated(ptr));
}

TEST_F(QSBR, ToSingleThreadedModeDeallocationsByRemainingThread) {
  unodb::qsbr_thread second_thread{[this] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  }};

  thread_sync_1.wait();  // 1 <-

  auto *ptr = mock_allocate();

  mock_qsbr_deallocate(ptr);

  thread_sync_2.notify();  // 2 ->
  second_thread.join();

  unodb::current_thread_reclamator().quiescent_state();
}

TEST_F(QSBR, TwoThreadsConsecutiveEpochAllocations) {
  mark_epoch();
  auto *ptr_1_1 = mock_allocate();

  unodb::qsbr_thread second_thread([this] {
    auto *ptr_2_1 = mock_allocate();

    mock_qsbr_deallocate(ptr_2_1);
    unodb::current_thread_reclamator().quiescent_state();
    thread_sync_1.notify();
    thread_sync_2.wait();

    ASSERT_TRUE(mock_is_allocated(ptr_2_1));
    auto *ptr_2_2 = mock_allocate();
    mock_qsbr_deallocate(ptr_2_2);
    unodb::current_thread_reclamator().quiescent_state();

    thread_sync_1.notify();
    thread_sync_2.wait();

    ASSERT_FALSE(mock_is_allocated(ptr_2_1));
    ASSERT_TRUE(mock_is_allocated(ptr_2_2));
    unodb::current_thread_reclamator().quiescent_state();

    thread_sync_1.notify();
    thread_sync_2.wait();

    ASSERT_FALSE(mock_is_allocated(ptr_2_2));
  });

  thread_sync_1.wait();
  mock_qsbr_deallocate(ptr_1_1);
  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_advanced();

  ASSERT_TRUE(mock_is_allocated(ptr_1_1));
  auto *ptr_1_2 = mock_allocate();
  mock_qsbr_deallocate(ptr_1_2);
  unodb::current_thread_reclamator().quiescent_state();

  thread_sync_2.notify();
  thread_sync_1.wait();

  check_epoch_advanced();

  ASSERT_FALSE(mock_is_allocated(ptr_1_1));
  ASSERT_TRUE(mock_is_allocated(ptr_1_2));
  unodb::current_thread_reclamator().quiescent_state();

  thread_sync_2.notify();
  thread_sync_1.wait();

  check_epoch_advanced();

  ASSERT_FALSE(mock_is_allocated(ptr_1_2));

  thread_sync_2.notify();
  second_thread.join();
}

TEST_F(QSBR, TwoThreadsNoImmediateTwoEpochDeallocationOnOneQuitting) {
  mark_epoch();
  auto *ptr = mock_allocate();

  unodb::qsbr_thread second_thread{[this] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    unodb::current_thread_reclamator().quiescent_state();

    thread_sync_1.notify();  // 3 ->
    thread_sync_2.wait();    // 4 <-
  }};

  thread_sync_1.wait();  // 1 <-
  mock_qsbr_deallocate(ptr);

  unodb::current_thread_reclamator().quiescent_state();

  thread_sync_2.notify();  // 2 ->
  thread_sync_1.wait();    // 3 <-

  check_epoch_advanced();
  ASSERT_TRUE(mock_is_allocated(ptr));

  auto *ptr2 = mock_allocate();
  mock_qsbr_deallocate(ptr2);
  ASSERT_TRUE(mock_is_allocated(ptr2));

  thread_sync_2.notify();  // 4 ->
  second_thread.join();

  ASSERT_TRUE(mock_is_allocated(ptr));
  ASSERT_TRUE(mock_is_allocated(ptr2));

  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_FALSE(mock_is_allocated(ptr));
  ASSERT_FALSE(mock_is_allocated(ptr2));
}

TEST_F(QSBR, TwoThreadsAllocatingInTwoEpochsAndPausing) {
  mark_epoch();

  auto *ptr_1_1 = mock_allocate();

  unodb::qsbr_thread second_thread{[this] {
    auto *ptr_2_1 = mock_allocate();
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    mock_qsbr_deallocate(ptr_2_1);
    unodb::current_thread_reclamator().quiescent_state();

    thread_sync_1.notify();  // 3 ->
    thread_sync_2.wait();    // 4 <-

    ASSERT_TRUE(mock_is_allocated(ptr_2_1));
    auto *ptr_2_2 = mock_allocate();
    mock_qsbr_deallocate(ptr_2_2);
    ASSERT_TRUE(mock_is_allocated(ptr_2_2));

    thread_sync_1.notify();  // 5 ->
    thread_sync_2.wait();    // 6 <-

    unodb::current_thread_reclamator().pause();

    thread_sync_1.notify();  // 7 ->

    ASSERT_FALSE(mock_is_allocated(ptr_2_1));
    ASSERT_FALSE(mock_is_allocated(ptr_2_2));

    unodb::current_thread_reclamator().resume();
  }};

  thread_sync_1.wait();  // 1 <-

  mock_qsbr_deallocate(ptr_1_1);
  unodb::current_thread_reclamator().quiescent_state();

  thread_sync_2.notify();  // 2 ->
  thread_sync_1.wait();    // 3 <-

  check_epoch_advanced();

  thread_sync_2.notify();  // 4 ->
  thread_sync_1.wait();    // 5 <-

  ASSERT_TRUE(mock_is_allocated(ptr_1_1));
  auto *ptr_1_2 = mock_allocate();
  mock_qsbr_deallocate(ptr_1_2);
  ASSERT_TRUE(mock_is_allocated(ptr_1_2));

  unodb::current_thread_reclamator().pause();

  thread_sync_2.notify();  // 6 ->
  thread_sync_1.wait();    // 7 <-

  ASSERT_FALSE(mock_is_allocated(ptr_1_1));
  ASSERT_FALSE(mock_is_allocated(ptr_1_2));
  second_thread.join();

  unodb::current_thread_reclamator().resume();
}

TEST_F(QSBR, TwoThreadsDeallocateBeforeQuittingPointerStaysLive) {
  void *test_ptr = mock_allocate();

  unodb::qsbr_thread second_thread{
      [this, test_ptr] { mock_qsbr_deallocate(test_ptr); }};
  second_thread.join();

  ASSERT_TRUE(mock_is_allocated(test_ptr));

  unodb::current_thread_reclamator().quiescent_state();

  ASSERT_FALSE(mock_is_allocated(test_ptr));
}

TEST_F(QSBR, ThreeDeallocationRequestSets) {
  mark_epoch();
  auto *ptr = mock_allocate();

  unodb::qsbr_thread second_thread{[this] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    unodb::current_thread_reclamator().quiescent_state();

    thread_sync_1.notify();  // 3 ->
    thread_sync_2.wait();    // 4 <-
  }};

  thread_sync_1.wait();  // 1 <-

  mock_qsbr_deallocate(ptr);
  unodb::current_thread_reclamator().quiescent_state();

  thread_sync_2.notify();  // 2 ->
  thread_sync_1.wait();    // 3 <-

  ASSERT_TRUE(mock_is_allocated(ptr));

  check_epoch_advanced();
  unodb::current_thread_reclamator().quiescent_state();

  thread_sync_2.notify();  // 4 ->

  second_thread.join();

  ASSERT_FALSE(mock_is_allocated(ptr));
}

TEST_F(QSBR, ReacquireLivePtrAfterQuiescentState) {
  mark_epoch();
  char *ptr = static_cast<char *>(mock_allocate());
  *ptr = 'A';

  unodb::qsbr_thread second_thread{[this, ptr] {
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-

    mock_qsbr_deallocate(ptr);
  }};

  thread_sync_1.wait();  // 1 <-

  // Wrote ptr to a shared data structure and done with it for now
  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_same();

  {
    // Reacquired ptr from a shared data structure
    unodb::qsbr_ptr<char> active_ptr{ptr};

    thread_sync_2.notify();  // 2 ->

    second_thread.join();

    check_epoch_advanced();

    ASSERT_EQ(*active_ptr, 'A');
  }

  unodb::current_thread_reclamator().quiescent_state();

  check_epoch_advanced();
}

TEST_F(QSBR, ResetStats) {
  auto *ptr = mock_allocate();
  auto *ptr2 = mock_allocate();

  unodb::qsbr_thread second_thread{[this] {
    unodb::current_thread_reclamator().quiescent_state();
    thread_sync_1.notify();  // 1 ->
    thread_sync_2.wait();    // 2 <-
  }};

  thread_sync_1.wait();  // 1 <-
  mock_qsbr_deallocate(ptr);
  mock_qsbr_deallocate(ptr2);

  unodb::current_thread_reclamator().quiescent_state();
  unodb::current_thread_reclamator().quiescent_state();

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

// TODO(laurynas): stat tests
// TODO(laurynas): quiescent_state_on_scope_exit tests?
// TODO(laurynas): qsbr::dump test

}  // namespace
