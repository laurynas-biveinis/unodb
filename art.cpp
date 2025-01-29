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

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include "art.hpp"

#include <cstdint>
#include <iostream>  // IWYU pragma: keep

#include "art_common.hpp"
#include "art_internal_impl.hpp"

// Unroll unodb::db templates here.
template class unodb::db<std::uint64_t>;
// template class unodb::db<unodb::key_view>;
