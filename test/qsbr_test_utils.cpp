// Copyright 2021-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <string>
// IWYU pragma: no_include <gtest/gtest.h>
// IWYU pragma: no_include "gtest/gtest.h"

#include "gtest_utils.hpp"
#include "qsbr.hpp"
#include "qsbr_test_utils.hpp"

namespace unodb::test {

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
void expect_idle_qsbr() noexcept {
  const auto state = unodb::qsbr::instance().get_state();
#ifdef UNODB_DETAIL_WITH_STATS
  UNODB_EXPECT_EQ(
      unodb::this_thread().get_current_interval_total_dealloc_size(), 0);
#endif  // UNODB_DETAIL_WITH_STATS
  UNODB_EXPECT_TRUE(qsbr_state::single_thread_mode(state));
  UNODB_EXPECT_TRUE(
      unodb::qsbr::instance().previous_interval_orphaned_requests_empty());
  UNODB_EXPECT_TRUE(
      unodb::qsbr::instance().current_interval_orphaned_requests_empty());
  const auto thread_count = qsbr_state::get_thread_count(state);
  const auto threads_in_previous_epoch =
      qsbr_state::get_threads_in_previous_epoch(state);
  UNODB_EXPECT_EQ(thread_count, 1);
  UNODB_EXPECT_EQ(threads_in_previous_epoch, 1);
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace unodb::test
