// Copyright 2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "art_common.hpp"

#include <iomanip>
#include <iostream>

namespace unodb::detail {

__attribute__((cold, noinline)) void dump_key(std::ostream &os, unodb::key k) {
  os << "key: 0x" << std::hex << std::setfill('0') << std::setw(sizeof(k)) << k
     << std::dec;
}

}  // namespace unodb::detail
