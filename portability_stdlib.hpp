// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_PORTABILITY_STDLIB_HPP
#define UNODB_DETAIL_PORTABILITY_STDLIB_HPP

#include "global.hpp"

#include <cstddef>
#include <new>

#if !defined(__cpp_lib_hardware_interference_size) || \
    __cpp_lib_hardware_interference_size < 201703

namespace unodb::detail {

#ifdef __x86_64
inline constexpr std::size_t hardware_constructive_interference_size = 64;
// Two cache lines for destructive interference due to Intel fetching cache
// lines in pairs
inline constexpr std::size_t hardware_destructive_interference_size = 128;
#else
#error Needs porting
#endif

static_assert(hardware_constructive_interference_size >=
              alignof(std::max_align_t));
static_assert(hardware_destructive_interference_size >=
              alignof(std::max_align_t));

}  // namespace unodb::detail

#else

using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;

#endif

#endif
