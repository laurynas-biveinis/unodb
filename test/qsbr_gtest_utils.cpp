// Copyright 2022-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"

#include "qsbr_gtest_utils.hpp"  // IWYU pragma: keep

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
