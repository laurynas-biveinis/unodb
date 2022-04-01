// Copyright 2022 Laurynas Biveinis

#include "global.hpp"

#include "qsbr_gtest_utils.hpp"

namespace unodb::test {

UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)
QSBRTestBase::QSBRTestBase() {
  if (is_qsbr_paused()) unodb::this_thread().qsbr_resume();
  unodb::test::expect_idle_qsbr();
  unodb::qsbr::instance().reset_stats();
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
