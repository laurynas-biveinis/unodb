// Copyright 2021-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include "art_internal.hpp"  // IWYU pragma: keep

#include "art_common.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>  // IWYU pragma: keep

namespace unodb::detail {

[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_byte(std::ostream &os,
                                                   std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_val(std::ostream &os,
                                                  unodb::value_view v) {
  const auto sz = v.size_bytes();
  os << "val(" << sz << "): 0x";
  for (std::size_t i = 0; i < sz; ++i) dump_byte(os, v[i]);
}

}  // namespace unodb::detail
