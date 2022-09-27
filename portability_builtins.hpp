// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_PORTABILITY_BUILTINS_HPP
#define UNODB_DETAIL_PORTABILITY_BUILTINS_HPP

#include "global.hpp"

#include <cstdint>

#include <gsl/util>

#ifdef UNODB_DETAIL_MSVC
#include <intrin.h>
#endif

namespace unodb::detail {

[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC std::uint64_t bswap(
    std::uint64_t x) noexcept {
#ifndef UNODB_DETAIL_MSVC
  return __builtin_bswap64(x);
#else
  return _byteswap_uint64(x);
#endif
}

template <typename T>
[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC std::uint8_t ctz(
    T x) noexcept {
  if constexpr (std::is_same_v<unsigned, T>) {
#ifndef UNODB_DETAIL_MSVC
    return gsl::narrow_cast<std::uint8_t>(__builtin_ctz(x));
#else
    unsigned long result;  // NOLINT(runtime/int)
    _BitScanForward(&result, x);
    return gsl::narrow_cast<std::uint8_t>(result);
#endif
  }
  // NOLINTNEXTLINE(google-runtime-int)
  if constexpr (std::is_same_v<unsigned long, T>) {  // NOLINT(runtime/int)
#ifndef UNODB_DETAIL_MSVC
    return gsl::narrow_cast<std::uint8_t>(__builtin_ctzl(x));
#else
    unsigned long result;  // NOLINT(runtime/int)
    _BitScanForward(&result, x);
    return gsl::narrow_cast<std::uint8_t>(result);
#endif
  }
  // NOLINTNEXTLINE(google-runtime-int)
  if constexpr (std::is_same_v<unsigned long long, T>) {  // NOLINT(runtime/int)
#ifndef UNODB_DETAIL_MSVC
    return gsl::narrow_cast<std::uint8_t>(__builtin_ctzll(x));
#else
    unsigned long result;  // NOLINT(runtime/int)
    _BitScanForward64(&result, x);
    return gsl::narrow_cast<std::uint8_t>(result);
#endif
  }
}

[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC unsigned popcount(
    unsigned x) noexcept {
#ifndef UNODB_DETAIL_MSVC
  return static_cast<unsigned>(__builtin_popcount(x));
#else
  return static_cast<unsigned>(__popcnt(x));
#endif
}

}  // namespace unodb::detail

#endif
