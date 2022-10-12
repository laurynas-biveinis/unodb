// Copyright 2020-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_HEAP_HPP
#define UNODB_DETAIL_HEAP_HPP

#include "global.hpp"

#ifndef NDEBUG
#include <atomic>
#endif
#include <cstdint>
#include <new>

namespace unodb::test {

class allocation_failure_injector final {
 public:
  static void reset() noexcept {
#ifndef NDEBUG
    fail_on_nth_allocation_.store(0, std::memory_order_relaxed);
    allocation_counter.store(0, std::memory_order_release);
#endif
  }

  static void fail_on_nth_allocation(
      std::uint64_t n UNODB_DETAIL_USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
    fail_on_nth_allocation_.store(n, std::memory_order_release);
#endif
  }

#ifndef NDEBUG

  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wanalyzer-malloc-leak")

  static void maybe_fail() {
    const auto fail_counter =
        fail_on_nth_allocation_.load(std::memory_order_acquire);
    if (UNODB_DETAIL_UNLIKELY(fail_counter != 0) &&
        (allocation_counter.fetch_add(1, std::memory_order_relaxed) >=
         fail_counter - 1)) {
      throw std::bad_alloc{};
    }
  }

  UNODB_DETAIL_RESTORE_GCC_WARNINGS()

 private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::atomic<std::uint64_t> allocation_counter;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::atomic<std::uint64_t> fail_on_nth_allocation_;

#endif  // #ifndef NDEBUG
};

template <typename TestAction>
void must_not_allocate(TestAction test_action) noexcept(
    noexcept(test_action())) {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  test_action();
  unodb::test::allocation_failure_injector::reset();
}

}  // namespace unodb::test

#endif  // UNODB_DETAIL_HEAP_HPP
