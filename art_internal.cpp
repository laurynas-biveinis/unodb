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
#include "heap.hpp"

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

namespace unodb {
namespace detail {

void ensure_capacity(std::byte *&buf, size_t &cap, size_t off,
                     size_t min_capacity) {
  // Find the allocation size in bytes which satisfies that minimum
  // capacity.  We first look for the next power of two.  Then we
  // adjust for the case where the [min_capacity] is already a power
  // of two (a common edge case).
  auto nsize = detail::NextPowerOfTwo(min_capacity);
  auto asize = (min_capacity == (nsize >> 1)) ? min_capacity : nsize;
  auto tmp = detail::allocate_aligned(asize);  // new allocation.
  std::memcpy(tmp, buf, off);                  // copy over the data.
  if (cap > INITIAL_BUFFER_CAPACITY) {         // free old buffer iff allocated
    detail::free_aligned(buf);
  }
  buf = reinterpret_cast<std::byte *>(tmp);
  cap = asize;
}

}  // namespace detail
}  // namespace unodb
