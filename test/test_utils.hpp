// Copyright 2022-2023 Laurynas Biveinis
#ifndef UNODB_DETAIL_TEST_UTILS_HPP
#define UNODB_DETAIL_TEST_UTILS_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include "test_heap.hpp"

namespace unodb::test {

// warning C26496: The variable 'result' does not change after construction,
// mark it as const (con.4) - but that may preclude move on RVO.
UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
// While the purpose of the function is to test that the single given action
// does not allocate heap memory, its implementation is global, and no other
// threads may allocate at the same time. IMHO a simpler global state (and the
// need to debug some racy testcases) is the right trade-off vs. thread local
// allocation-forbidding state.
template <typename TestAction>
std::invoke_result_t<TestAction> must_not_allocate(
    TestAction test_action) noexcept(noexcept(test_action())) {
  if constexpr (!std::is_void_v<std::invoke_result_t<TestAction>>) {
    UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(1);
    auto result = test_action();
    UNODB_DETAIL_RESET_ALLOCATION_FAILURE_INJECTOR();
    return result;
  } else {
    UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(1);
    test_action();
    UNODB_DETAIL_RESET_ALLOCATION_FAILURE_INJECTOR();
  }
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace unodb::test

#endif  // UNODB_DETAIL_TEST_UTILS_HPP
