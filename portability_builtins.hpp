// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_PORTABILITY_BUILTINS_HPP
#define UNODB_DETAIL_PORTABILITY_BUILTINS_HPP

#include "global.hpp"

#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#include <gsl/gsl_util>
#endif

namespace unodb::detail {

[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC std::uint64_t bswap(
    std::uint64_t x) noexcept {
#ifndef _MSC_VER
  return __builtin_bswap64(x);
#else
  return _byteswap_uint64(x);
#endif
}

[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC unsigned ctz(
    unsigned x) noexcept {
#ifndef _MSC_VER
  return static_cast<unsigned>(__builtin_ctz(x));
#else
  unsigned long result;  // NOLINT(runtime/int)
  _BitScanForward(&result, x);
  return gsl::narrow_cast<unsigned>(result);
#endif
}

[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC unsigned ctz64(
    std::uint64_t x) noexcept {
#ifndef _MSC_VER
  return static_cast<unsigned>(__builtin_ctzll(x));
#else
  unsigned long result;  // NOLINT(runtime/int)
  _BitScanForward64(&result, x);
  return gsl::narrow_cast<unsigned>(result);
#endif
}

[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC unsigned popcount(
    unsigned x) noexcept {
#ifndef _MSC_VER
  return static_cast<unsigned>(__builtin_popcount(x));
#else
  return static_cast<unsigned>(__popcnt(x));
#endif
}

}  // namespace unodb::detail

#endif
