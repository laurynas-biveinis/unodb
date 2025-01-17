// Copyright 2021-2025 UnoDB contributors
#if 0
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

#include "art_common.hpp"

#include <iomanip>   // IWYU pragma: keep
#include <iostream>  // IWYU pragma: keep

// TODO(thompsonbry) art_common.cpp is now empty.  Get rid of it?
#endif
