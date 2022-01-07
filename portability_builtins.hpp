// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_PORTABILITY_BUILTINS_HPP
#define UNODB_DETAIL_PORTABILITY_BUILTINS_HPP

#include "global.hpp"

#include <cstdint>

namespace unodb::detail {

[[nodiscard, gnu::pure]] inline constexpr auto bswap(std::uint64_t x) noexcept {
  return __builtin_bswap64(x);
}

[[nodiscard, gnu::pure]] inline constexpr unsigned ctz(unsigned x) noexcept {
  return static_cast<unsigned>(__builtin_ctz(x));
}

[[nodiscard, gnu::pure]] inline constexpr unsigned ctz64(
    std::uint64_t x) noexcept {
  return static_cast<unsigned>(__builtin_ctzll(x));
}

[[nodiscard, gnu::pure]] inline constexpr unsigned popcount(
    unsigned x) noexcept {
  return static_cast<unsigned>(__builtin_popcount(x));
}

}  // namespace unodb::detail

#endif
