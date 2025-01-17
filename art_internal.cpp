// Copyright 2021-2022 Laurynas Biveinis

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

#include "art_internal.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>  // IWYU pragma: keep

namespace unodb::detail {

[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_byte(std::ostream &os,
                                                   std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

[[gnu::cold]] UNODB_DETAIL_NOINLINE std::ostream &operator<<(std::ostream &os,
                                                             art_key key) {
  os << "binary-comparable key:";
  for (std::size_t i = 0; i < sizeof(key); ++i) dump_byte(os, key[i]);
  return os;
}

}  // namespace unodb::detail
