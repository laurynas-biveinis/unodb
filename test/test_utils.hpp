// Copyright 2022-2025 UnoDB contributors
#ifndef UNODB_DETAIL_TEST_UTILS_HPP
#define UNODB_DETAIL_TEST_UTILS_HPP

/// \file
/// Test API for verifying heap allocation behavior.
///
/// \ingroup test-internals
///
/// Utilities for tests to verify heap allocation behavior.

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include "test_heap.hpp"

namespace unodb::test {

/// Test that given action does not allocate heap memory.
///
/// This function configures the allocation failure injector to fail on the
/// first allocation, executes the provided test action, and then resets
/// the injector state. If the action tries to allocate memory, it will throw
/// `std::bad_alloc`. If it completes successfully, we know it didn't allocate.
///
/// \warning This function affects global state. No other threads should
/// allocate memory during execution of this function, as the allocation
/// failure injector is global.
///
/// \tparam TestAction Type of the test action callable
/// \param test_action Test function or callable that must not allocate during
/// its execution.
/// \return The result of test_action (if non-void)
template <typename TestAction>
std::invoke_result_t<TestAction> must_not_allocate(
    TestAction test_action) noexcept(noexcept(test_action())) {
  if constexpr (!std::is_void_v<std::invoke_result_t<TestAction>>) {
    UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(1);
    const auto result = test_action();
    UNODB_DETAIL_RESET_ALLOCATION_FAILURE_INJECTOR();
    return result;
  } else {
    UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(1);
    test_action();
    UNODB_DETAIL_RESET_ALLOCATION_FAILURE_INJECTOR();
  }
}

}  // namespace unodb::test

#endif  // UNODB_DETAIL_TEST_UTILS_HPP
