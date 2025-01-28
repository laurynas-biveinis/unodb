// Copyright 2019-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include "mutex_art.hpp"

#include <cstdint>
#include <iostream>  // IWYU pragma: keep

#include "art_common.hpp"

// Unroll unodb::mutex_db templates here.
template class unodb::mutex_db<std::uint64_t>;
template class unodb::mutex_db<unodb::key_view>;
