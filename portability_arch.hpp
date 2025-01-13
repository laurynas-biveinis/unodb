// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_PORTABILITY_ARCH_HPP
#define UNODB_DETAIL_PORTABILITY_ARCH_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>

namespace unodb::detail {

// Do not use std::hardware_constructive_interference_size and
// std::hardware_destructive_interference_size even when they are available,
// because they are used in public headers, and their values are unstable (i.e.
// can be affected by GCC 12 --param or -mtune flags).

#ifdef UNODB_DETAIL_X86_64
inline constexpr std::size_t hardware_constructive_interference_size = 64;
// Two cache lines for destructive interference due to Intel fetching cache
// lines in pairs
inline constexpr std::size_t hardware_destructive_interference_size = 128;
#elif defined(__aarch64__)
inline constexpr std::size_t hardware_constructive_interference_size = 64;
// Value stolen from GCC 12 implementation
inline constexpr std::size_t hardware_destructive_interference_size = 256;
#else
#error Needs porting
#endif

static_assert(hardware_constructive_interference_size >=
              alignof(std::max_align_t));
static_assert(hardware_destructive_interference_size >=
              alignof(std::max_align_t));

}  // namespace unodb::detail

#endif
