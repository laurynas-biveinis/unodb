// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_TEST_UTILS_HPP
#define UNODB_DETAIL_TEST_UTILS_HPP

#include "global.hpp"  // IWYU pragma: keep

#include "heap.hpp"

namespace unodb::test {

// warning C26496: The variable 'result' does not change after construction,
// mark it as const (con.4) - but that may preclude move on RVO.
UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
template <typename TestAction>
std::invoke_result_t<TestAction> must_not_allocate(
    TestAction test_action) noexcept(noexcept(test_action())) {
  if constexpr (!std::is_void_v<std::invoke_result_t<TestAction>>) {
    unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
    auto result = test_action();
    unodb::test::allocation_failure_injector::reset();
    return result;
  } else {
    unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
    test_action();
    unodb::test::allocation_failure_injector::reset();
  }
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace unodb::test

#endif  // UNODB_DETAIL_TEST_UTILS_HPP
