// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include "test_utils.hpp"

#include <sstream>

#include "gtest/gtest.h"

#include "art.hpp"

// warning: 'ScopedTrace' was marked unused but was used
// [-Wused-but-marked-unused]
DISABLE_CLANG_WARNING("-Wused-but-marked-unused")

void assert_result_eq(unodb::key_type key, unodb::db::get_result result,
                      unodb::value_view expected, int caller_line) noexcept {
  std::ostringstream msg;
  msg << "key = " << static_cast<unsigned>(key);
  testing::ScopedTrace trace(__FILE__, caller_line, msg.str());
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

RESTORE_CLANG_WARNINGS()

template class tree_verifier<unodb::db>;
template class tree_verifier<unodb::mutex_db>;
