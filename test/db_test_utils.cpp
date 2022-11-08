// Copyright 2019-2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "db_test_utils.hpp"

#include "art.hpp"        // IWYU pragma: keep
#include "mutex_art.hpp"  // IWYU pragma: keep
#include "olc_art.hpp"    // IWYU pragma: keep

namespace unodb::test {

template class tree_verifier<unodb::db>;
template class tree_verifier<unodb::mutex_db>;
template class tree_verifier<unodb::olc_db>;

}  // namespace unodb::test
