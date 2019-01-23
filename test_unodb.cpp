// Copyright 2019 Laurynas Biveinis
#include "art.hpp"
#include "gtest/gtest.h"  // IWYU pragma: keep

namespace {

TEST(UnoDB, insert) {
    unodb::db test_db;
    test_db.insert(1, {});
}

} // namespace
