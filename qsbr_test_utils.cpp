// Copyright 2021 Laurynas Biveinis

#include "global.hpp"

#include "qsbr_test_utils.hpp"

#include <gtest/gtest.h>

#include "qsbr.hpp"

void expect_idle_qsbr() {
  EXPECT_TRUE(unodb::qsbr::instance().single_thread_mode());
  EXPECT_EQ(unodb::qsbr::instance().number_of_threads(), 1);
  EXPECT_EQ(unodb::qsbr::instance().previous_interval_size(), 0);
  EXPECT_EQ(unodb::qsbr::instance().current_interval_size(), 0);
  EXPECT_EQ(unodb::qsbr::instance().get_reserved_thread_capacity(), 1);
  EXPECT_EQ(unodb::qsbr::instance().get_threads_in_previous_epoch(), 1);
}
