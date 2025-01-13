// Copyright 2021-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_TEST_UTILS_HPP
#define UNODB_DETAIL_QSBR_TEST_UTILS_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

namespace unodb::test {

void expect_idle_qsbr() noexcept;

}  // namespace unodb::test

#endif  // UNODB_DETAIL_QSBR_TEST_UTILS_HPP
