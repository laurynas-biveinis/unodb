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

#include "art_common.hpp"
#include "art_internal.hpp"
#include "heap.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>  // IWYU pragma: keep

namespace unodb::detail {

[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_byte(std::ostream &os,
                                                   std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

template void dump_key<std::uint64_t>(std::ostream &os, std::uint64_t k);
template void dump_key<key_view>(std::ostream &os, key_view k);

[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_val(std::ostream &os,
                                                  unodb::value_view v) {
  const auto sz = v.size_bytes();
  os << "val(" << sz << "): 0x";
  for (std::size_t i = 0; i < sz; ++i) dump_byte(os, v[i]);
}

// Ensure unrolled in .cpp and therefore available to debugger.
template <>
[[gnu::cold]] UNODB_DETAIL_NOINLINE void basic_art_key<std::uint64_t>::dump()
    const {
  dump(std::cerr);
}
template <>
[[gnu::cold]] UNODB_DETAIL_NOINLINE void basic_art_key<unodb::key_view>::dump()
    const {
  dump(std::cerr);
}

void ensure_capacity(std::byte *&buf, size_t &cap, size_t off,
                     size_t min_capacity) {
  // Find the allocation size in bytes which satisfies that minimum
  // capacity.  We first look for the next power of two.  Then we
  // adjust for the case where the [min_capacity] is already a power
  // of two (a common edge case).
  auto nsize = detail::NextPowerOfTwo(min_capacity);
  auto asize = (min_capacity == (nsize >> 1U)) ? min_capacity : nsize;
  auto *tmp = detail::allocate_aligned(asize);  // new allocation.
  std::memcpy(tmp, buf, off);                   // copy over the data.
  if (cap > INITIAL_BUFFER_CAPACITY) {          // free old buffer iff allocated
    detail::free_aligned(buf);
  }
  buf = reinterpret_cast<std::byte *>(tmp);
  cap = asize;
}

}  // namespace unodb::detail
