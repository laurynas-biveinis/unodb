// Copyright 2019-2025 UnoDB contributors
//
// Copyright 2018-2025 Stichting DuckDB Foundation
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/// \file
/// Functions derived from DuckDB for lexicographic encode and decode of
/// floating point values.
#ifndef UNODB_DETAIL_DUCKDB_ENCODE_DECODE
#define UNODB_DETAIL_DUCKDB_ENCODE_DECODE

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "portability_builtins.hpp"

namespace unodb::detail {

/// Return value with most significant bit set for the indicated type \a T.
template <typename T>
[[nodiscard, gnu::const]] consteval T msb() noexcept {
  static_assert(std::is_same_v<std::uint32_t, T> ||
                std::is_same_v<std::uint64_t, T>);
  if (std::is_same_v<std::uint32_t, T>) {
    return 1U << 31U;
  }
  if (std::is_same_v<std::uint64_t, T>) {
    return static_cast<T>(1ULL << 63U);
  }
}

/// Encode floating-point value to lexicographic sort key.
///
/// This encoding preserves the relative order of values - if a < b for
/// floating-point values, then encode(a) < encode(b) for the integer encoded
/// values.
///
/// The returned sort key can be converted back to the original value with
/// decode_floating_point().
///
/// Special values like NaN and infinity are handled specially:
/// - NaN is encoded as the maximum possible integer value
/// - Positive infinity is encoded as maximum possible integer value minus 1
/// - Negative infinity is encoded as 0
///
/// \tparam U The unsigned integer type to encode to
/// \tparam F The floating-point type to encode from
/// \param x The floating-point value to encode
/// \return The lexicographic sort key
template <typename U, typename F>
[[nodiscard]] U encode_floating_point(F x) noexcept {
  constexpr auto msb0 = msb<U>();
  if (std::isnan(x)) {
    return std::numeric_limits<U>::max();  // NaN
  }
  if (std::isinf(x)) {                                  // (+|-) inf
    return (x > 0) ? std::numeric_limits<U>::max() - 1  // +inf
                   : 0;                                 // -inf
  }
  auto buff = bit_cast<U, F>(x);
  if ((buff & msb0) == 0) {  // +0 and positive numbers
    buff |= msb0;
  } else {         // negative numbers
    buff = ~buff;  // complement 1
  }
  return buff;
}

/// Convert lexicographic sort key to original floating-point value.
///
/// Reverses the encoding done by encode_floating_point().
///
/// \tparam F The floating-point type to decode to
/// \tparam U The unsigned integer type to decode from
/// \param input The lexicographic sort key
/// \return The original floating-point value
template <typename F, typename U>
[[nodiscard]] F decode_floating_point(U input) noexcept {
  constexpr auto msb0 = msb<U>();
  if (input == std::numeric_limits<U>::max()) {
    return std::numeric_limits<F>::quiet_NaN();  // NaN
  }
  if (input == std::numeric_limits<U>::max() - 1) {
    return std::numeric_limits<F>::infinity();  // +inf
  }
  if (input == 0) {
    return -std::numeric_limits<F>::infinity();  // -inf
  }
  if (input & msb0) {
    input = input ^ msb0;  // positive numbers - flip sign bit
  } else {
    input = ~input;  // negative numbers - invert
  }
  return bit_cast<F, U>(input);
}

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_DUCKDB_ENCODE_DECODE
