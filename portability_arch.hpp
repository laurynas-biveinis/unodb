// Copyright 2022-2025 Laurynas Biveinis
#ifndef UNODB_DETAIL_PORTABILITY_ARCH_HPP
#define UNODB_DETAIL_PORTABILITY_ARCH_HPP

/// \file
/// Definitions to abstract differences between architectures.
/// \ingroup internal

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>

namespace unodb::detail {

/// \var hardware_constructive_interference_size
/// Maximum size in bytes where multiple variables will be guaranteed to be
/// shared for the purposes of true sharing.
/// \hideinitializer
/// Use this instead of `std::hardware_constructive_interference_size` even if
/// the latter is available, because it is used in public headers and its value
/// may vary, for example, by GCC 12 or later `--param` or `-mtune` flag.

/// \var hardware_destructive_interference_size
/// Minimum size in bytes where multiple variables will be guaranteed to be
/// separated for the purposes of false sharing.
/// \hideinitializer
/// Use this instead of `std::hardware_destructive_interference_size` even if
/// the latter is available, because it is used in public headers and its value
/// may vary, for example, by GCC 12 or later `--param` or `-mtune` flag.

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
