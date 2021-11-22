// Copyright 2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "qsbr_test_utils.hpp"

#include <gtest/gtest.h>  // IWYU pragma: keep

#include "qsbr.hpp"

namespace unodb::test {

void expect_idle_qsbr() {
  // Copy-paste-tweak with qsbr::assert_idle_locked, but not clear how to fix
  // this: here we are using Google Test macros with the public interface, over
  // there we are asserting over internals.
  EXPECT_TRUE(unodb::qsbr::instance().single_thread_mode());
  EXPECT_EQ(unodb::qsbr::instance().previous_interval_size(), 0);
  EXPECT_EQ(unodb::qsbr::instance().current_interval_size(), 0);
  if (unodb::qsbr::instance().number_of_threads() == 0) {
    EXPECT_EQ(unodb::qsbr::instance().get_threads_in_previous_epoch(), 0);
  } else if (unodb::qsbr::instance().number_of_threads() == 1) {
    EXPECT_EQ(unodb::qsbr::instance().get_threads_in_previous_epoch(), 1);
  } else {
    EXPECT_LE(unodb::qsbr::instance().number_of_threads(), 1);
  }
}

}  // namespace unodb::test
