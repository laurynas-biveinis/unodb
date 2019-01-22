#include "art.hpp"
#include "gtest/gtest.h"

namespace {

TEST(UnoDB, insert) {
    unodb::db test_db;
    test_db.insert(1, {});
}

} // namespace
