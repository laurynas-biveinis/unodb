// Copyright 2021-2023 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_TEST_UTILS_HPP
#define UNODB_DETAIL_QSBR_TEST_UTILS_HPP

// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

#include "global.hpp"  // IWYU pragma: keep

namespace unodb::test {

void expect_idle_qsbr() noexcept;

}  // namespace unodb::test

#endif  // UNODB_DETAIL_QSBR_TEST_UTILS_HPP
