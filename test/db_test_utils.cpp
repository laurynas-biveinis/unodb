// Copyright 2019-2024 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "db_test_utils.hpp"

#include "art.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace unodb::test {

template class tree_verifier<unodb::db>;
template class tree_verifier<unodb::mutex_db>;
template class tree_verifier<unodb::olc_db>;

}  // namespace unodb::test
