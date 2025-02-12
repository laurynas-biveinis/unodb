// Copyright 2022-2025 UnoDB contributors
#ifndef UNODB_DETAIL_PORTABILITY_BUILTINS_HPP
#define UNODB_DETAIL_PORTABILITY_BUILTINS_HPP

/// \file
/// Definitions to abstract differences between different compiler builtins and
/// intrinsics.
/// \ingroup internal

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <cstdint>
#include <type_traits>

#ifdef UNODB_DETAIL_MSVC
#include <intrin.h>
#endif

namespace unodb::detail {

/// Reverse the order of bytes in \a x.
template <typename T>
T bswap(T x) {
#ifdef UNODB_DETAIL_MSVC
  static_assert(sizeof(std::uint32_t) ==
                sizeof(unsigned long));               // NOLINT(runtime/int)
  static_assert(std::is_same_v<unsigned short, T> ||  // NOLINT(runtime/int)
                std::is_same_v<std::uint32_t, T> ||
                std::is_same_v<std::uint64_t, T>);
  if constexpr (std::is_same_v<unsigned short, T>) {  // NOLINT(runtime/int)
    return _byteswap_ushort(x);
  }
  if constexpr (std::is_same_v<std::uint32_t, T>) {
    return _byteswap_ulong(x);
  }
  if constexpr (std::is_same_v<std::uint64_t, T>) {
    return _byteswap_uint64(x);
  }
#else   // UNODB_DETAIL_MSVC
  static_assert(std::is_same_v<std::uint16_t, T> ||
                std::is_same_v<std::uint32_t, T> ||
                std::is_same_v<std::uint64_t, T>);
  if constexpr (std::is_same_v<std::uint16_t, T>) {
    return __builtin_bswap16(x);
  }
  if constexpr (std::is_same_v<std::uint32_t, T>) {
    return __builtin_bswap32(x);
  }
  if constexpr (std::is_same_v<std::uint64_t, T>) {
    return __builtin_bswap64(x);
  }  // cppcheck-suppress missingReturn
#endif  // UNODB_DETAIL_MSVC
}

/// Return the number of trailing zero bits in \a x.
/// \pre Argument may not be zero
template <typename T>
[[nodiscard, gnu::pure]] UNODB_DETAIL_CONSTEXPR_NOT_MSVC std::uint8_t ctz(
    T x) noexcept {
  static_assert(std::is_same_v<unsigned, T> ||
                // NOLINTNEXTLINE(google-runtime-int)
                std::is_same_v<unsigned long, T> ||  // NOLINT(runtime/int)
                // NOLINTNEXTLINE(google-runtime-int)
                std::is_same_v<unsigned long long, T>);  // NOLINT(runtime/int)

  if constexpr (std::is_same_v<unsigned, T>) {
#ifndef UNODB_DETAIL_MSVC
    return static_cast<std::uint8_t>(__builtin_ctz(x));
#else
    unsigned long result;  // NOLINT(runtime/int)
    _BitScanForward(&result, x);
    return static_cast<std::uint8_t>(result);
#endif
  }
  // NOLINTNEXTLINE(google-runtime-int)
  if constexpr (std::is_same_v<unsigned long, T>) {  // NOLINT(runtime/int)
#ifndef UNODB_DETAIL_MSVC
    return static_cast<std::uint8_t>(__builtin_ctzl(x));
#else
    unsigned long result;  // NOLINT(runtime/int)
    _BitScanForward(&result, x);
    return static_cast<std::uint8_t>(result);
#endif
  }
  // NOLINTNEXTLINE(google-runtime-int)
  if constexpr (std::is_same_v<unsigned long long, T>) {  // NOLINT(runtime/int)
#ifndef UNODB_DETAIL_MSVC
    return static_cast<std::uint8_t>(__builtin_ctzll(x));
#else
    unsigned long result;  // NOLINT(runtime/int)
    _BitScanForward64(&result, x);
    return static_cast<std::uint8_t>(result);
#endif
  }  // cppcheck-suppress missingReturn
}

/// Return the number of one bits in \a x.
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
