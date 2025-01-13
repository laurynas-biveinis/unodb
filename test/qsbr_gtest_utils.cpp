// Copyright 2022-2024 Laurynas Biveinis

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include "qsbr_gtest_utils.hpp"

#include "qsbr.hpp"
#include "qsbr_test_utils.hpp"

namespace unodb::test {

UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)
QSBRTestBase::QSBRTestBase() {
  if (is_qsbr_paused()) unodb::this_thread().qsbr_resume();
  unodb::test::expect_idle_qsbr();
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::qsbr::instance().reset_stats();
#endif  // UNODB_DETAIL_WITH_STATS
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
QSBRTestBase::~QSBRTestBase() noexcept {
  if (is_qsbr_paused()) unodb::this_thread().qsbr_resume();
  unodb::this_thread().quiescent();
  unodb::test::expect_idle_qsbr();
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace unodb::test
