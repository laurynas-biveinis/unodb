// Copyright 2023-2025 UnoDB contributors
#ifndef UNODB_DETAIL_TEST_HEAP_HPP
#define UNODB_DETAIL_TEST_HEAP_HPP

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#ifndef NDEBUG

#include <atomic>
#include <cstdint>
#include <iostream>
#include <new>
#include <thread>

namespace unodb::test {

/// Test helper class may be used to inject memory allocation faults,
/// throwing std::bad_alloc once some number of allocations have been
/// made.
class allocation_failure_injector final {
  friend class pause_heap_faults;

 public:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(4514)

  /// Reset the counters tracking the number of allocations and the
  /// allocation number at which allocation will throw std::bad_alloc
  /// to zero.
  static void reset() noexcept {
    fail_on_nth_allocation_.store(0, std::memory_order_relaxed);
    allocation_counter.store(0, std::memory_order_release);
  }

  /// Set the allocation number at which allocation will fail.
  static void fail_on_nth_allocation(
      std::uint64_t n UNODB_DETAIL_USED_IN_DEBUG) noexcept {
    fail_on_nth_allocation_.store(n, std::memory_order_release);
  }

  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wanalyzer-malloc-leak")

  /// Invoked from the gtest harness allocator hooks to fail memory
  /// allocation by throwing std::bad_alloc iff (a) memory allocation
  /// tracking is enabled; and (b) the allocation fail counter is
  /// breached.
  static void maybe_fail() {
    // Inspects the fail counter.  If non-zero, then bumps the
    // allocation counter.  If that results in the allocation counter
    // reaching or exceeding the fail counter, then throw
    // std::bad_alloc.
    if (UNODB_DETAIL_UNLIKELY(paused)) return;
    const auto fail_counter =
        fail_on_nth_allocation_.load(std::memory_order_acquire);
    if (UNODB_DETAIL_UNLIKELY(fail_counter != 0) &&
        (allocation_counter.fetch_add(1, std::memory_order_relaxed) >=
         fail_counter - 1)) {
      throw std::bad_alloc{};
    }
  }

  /// Debugging
  static void dump(std::string msg = "") {
    std::cerr << msg << "allocation_failure_injector"
              << "{fail_on_nth_allocation="
              << fail_on_nth_allocation_.load(std::memory_order_acquire)
              << ",allocation_counter="
              << allocation_counter.load(std::memory_order_relaxed)
              << ",paused=" << paused << "}\n";
  }

  UNODB_DETAIL_RESTORE_GCC_WARNINGS()
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  inline static std::atomic<std::uint64_t> allocation_counter{0};
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  inline static std::atomic<std::uint64_t> fail_on_nth_allocation_{0};
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  inline static thread_local bool paused{};  /// used to suspend memory tracking
};                                           // allocation_failure_injector

/// Lexically scoped guard to pause heap allocation tracking and
/// faulting.
class pause_heap_faults {
 public:
  /// Pause heap faults.
  explicit pause_heap_faults() { allocation_failure_injector::paused = true; }
  /// Resumes heap faults.
  ~pause_heap_faults() { allocation_failure_injector::paused = false; }
};  // class pause_heap_faults

}  // namespace unodb::test

#define UNODB_DETAIL_RESET_ALLOCATION_FAILURE_INJECTOR() \
  unodb::test::allocation_failure_injector::reset()
#define UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(n) \
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(n)

#else  // !NDEBUG

#define UNODB_DETAIL_RESET_ALLOCATION_FAILURE_INJECTOR()
#define UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(n)

#endif  // !NDEBUG

#endif  // UNODB_DETAIL_TEST_HEAP_HPP
