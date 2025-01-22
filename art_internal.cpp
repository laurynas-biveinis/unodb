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

template <typename T>
[[gnu::cold]] UNODB_DETAIL_NOINLINE void dump_key(std::ostream &os, T k) {
  if constexpr (std::is_same_v<T, std::uint64_t>) {
    os << "key: 0x" << std::hex << std::setfill('0') << std::setw(sizeof(k))
       << k << std::dec;
  } else {
    os << "key: 0x";
    for (std::size_t i = 0; i < k.size_bytes(); ++i) dump_byte(os, k[i]);
  }
}
template void dump_key<std::uint64_t>(std::ostream &os, std::uint64_t k);
template void dump_key<key_view>(std::ostream &os, key_view k);

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

}  // namespace unodb::detail
