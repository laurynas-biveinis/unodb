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
//
// Note: This source file contains some functions derived from DuckDB
// for encode and decode of floating point values.
#ifndef UNODB_DUCKDB_ENCODE_DECODE
#define UNODB_DUCKDB_ENCODE_DECODE

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <cmath>
#include <cstddef>
#include <cstdint>

// to avoid conflicts with the DuckDB methods of the same name.
namespace unodb::detail {

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

/// This method is derived from DuckDB.
template <typename U, typename F>
[[nodiscard]] inline U EncodeFloatingPoint(F x) {
  constexpr auto msb = unodb::detail::msb<U>();
  if (std::isnan(x)) {
    return std::numeric_limits<U>::max();  // NaN
  }
  if (std::isinf(x)) {  // (+|-) inf
    if (x > 0)
      return std::numeric_limits<U>::max() - 1;  // +inf
    else
      return 0;  // -inf
  }
  // U buff = reinterpret_cast<U&>(x);
  U buff = unodb::detail::bit_cast<U, F>(x);
  if ((buff & msb) == 0) {  //! +0 and positive numbers
    buff |= msb;
  } else {         //! negative numbers
    buff = ~buff;  //! complement 1
  }
  return buff;
}

/// This method is derived from DuckDB.
template <typename F, typename U>
[[nodiscard]] inline F DecodeFloatingPoint(U input) {
  constexpr auto msb = unodb::detail::msb<U>();
  if (input == std::numeric_limits<U>::max()) {
    return std::numeric_limits<F>::quiet_NaN();  // NaN
  }
  if (input == std::numeric_limits<U>::max() - 1) {
    return std::numeric_limits<F>::infinity();  // +inf
  }
  if (input == 0) {
    return -std::numeric_limits<F>::infinity();  // -inf
  }
  if (input & msb) {
    input = input ^ msb;  // positive numbers - flip sign bit
  } else {
    input = ~input;  // negative numbers - invert
  }
  // return reinterpret_cast<F&>( input );
  return unodb::detail::bit_cast<F, U>(input);
}

}  // namespace unodb::detail

#endif  // UNODB_DUCKDB_ENCODE_DECODE
