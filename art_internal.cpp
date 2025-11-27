// Copyright 2021-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include <cstddef>
#include <iomanip>
#include <iostream>  // IWYU pragma: keep

#include "art_common.hpp"
#include "art_internal.hpp"  // IWYU pragma: keep

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
