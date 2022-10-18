// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_TEST_UTILS_HPP
#define UNODB_DETAIL_TEST_UTILS_HPP

#include "global.hpp"  // IWYU pragma: keep

#include "heap.hpp"

namespace unodb::test {

template <typename TestAction>
void must_not_allocate(TestAction test_action) noexcept(
    noexcept(test_action())) {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  test_action();
  unodb::test::allocation_failure_injector::reset();
}

}  // namespace unodb::test

#endif  // UNODB_DETAIL_TEST_UTILS_HPP
