// Copyright 2019-2024 Laurynas Biveinis

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
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
