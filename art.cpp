// Copyright 2019-2025 UnoDB contributors

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

#include "art.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>  // IWYU pragma: keep

#include "art_common.hpp"
#include "art_internal_impl.hpp"
#include "node_type.hpp"

namespace unodb {

///
/// unodb::db::iterator
///

// LCOV_EXCL_START
template <class Key>
void db<Key>::iterator::dump(std::ostream &os) const {
  if (empty()) {
    os << "iter::stack:: empty\n";
    return;
  }
  // Create a new stack and copy everything there.  Using the new
  // stack, print out the stack in top-bottom order.  This avoids
  // modifications to the existing stack for the iterator.
  auto tmp = stack_;
  auto level = tmp.size() - 1;
  while (!tmp.empty()) {
    const auto &e = tmp.top();
    const auto &np = e.node;
    os << "iter::stack:: level = " << level << ", key_byte=0x" << std::hex
       << std::setfill('0') << std::setw(2)
       << static_cast<std::uint64_t>(e.key_byte) << std::dec
       << ", child_index=0x" << std::hex << std::setfill('0') << std::setw(2)
       << static_cast<std::uint64_t>(e.child_index) << std::dec << ", ";
    art_policy::dump_node(os, np, false /*recursive*/);
    if (np.type() != node_type::LEAF) os << '\n';
    tmp.pop();
    level--;
  }
}

template <class Key>
void db<Key>::iterator::dump() const {
  dump(std::cerr);
}
// LCOV_EXCL_STOP

}  // namespace unodb

// Unroll unodb::db templates here.
template class unodb::db<std::uint64_t>;
template class unodb::db<unodb::key_view>;
