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

/// \file Functions derived from DuckDB for encode and decode of
/// floating point values.
#ifndef UNODB_DETAIL_DUCKDB_ENCODE_DECODE
#define UNODB_DETAIL_DUCKDB_ENCODE_DECODE

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "portability_builtins.hpp"

// Namespace avoids conflicts with the DuckDB methods of the same
// name.
namespace unodb::detail {

/// Return value with the most significant bit set for the indicated
/// type.
template <typename T>
[[nodiscard]] constexpr T msb();
template <>
constexpr std::uint64_t msb<std::uint64_t>() {
  return 1ULL << 63;
}
template <>
constexpr std::uint32_t msb<std::uint32_t>() {
  return 1U << 31;
}

// This method is derived from DuckDB.
template <typename U, typename F>
[[nodiscard]] U encode_floating_point(F x) noexcept {
  constexpr auto msb0 = unodb::detail::msb<U>();
  if (std::isnan(x)) {
    return std::numeric_limits<U>::max();  // NaN
  }
  if (std::isinf(x)) {                                  // (+|-) inf
    return (x > 0) ? std::numeric_limits<U>::max() - 1  // +inf
                   : 0;                                 // -inf
  }
  U buff = unodb::detail::bit_cast<U, F>(x);
  if ((buff & msb0) == 0) {  //! +0 and positive numbers
    buff |= msb0;
  } else {         //! negative numbers
    buff = ~buff;  //! complement 1
  }
  return buff;
}

// This method is derived from DuckDB.
template <typename F, typename U>
[[nodiscard]] F decode_floating_point(U input) noexcept {
  constexpr auto msb0 = unodb::detail::msb<U>();
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
  return unodb::detail::bit_cast<F, U>(input);
}

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_DUCKDB_ENCODE_DECODE
