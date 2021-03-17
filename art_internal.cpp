// Copyright 2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "art_internal.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>

namespace unodb::detail {

__attribute__((cold, noinline)) void dump_byte(std::ostream &os,
                                               std::byte byte) {
  os << ' ' << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<unsigned>(byte) << std::dec;
}

__attribute__((cold, noinline)) std::ostream &operator<<(std::ostream &os,
                                                         art_key key) {
  os << "binary-comparable key:";
  for (std::size_t i = 0; i < sizeof(key); ++i) dump_byte(os, key[i]);
  return os;
}

}  // namespace unodb::detail
