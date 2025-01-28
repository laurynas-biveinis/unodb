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

#include <cstdint>
#include <iostream>  // IWYU pragma: keep

#include "art.hpp"
#include "art_common.hpp"
#include "art_internal.hpp"
#include "db_test_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace unodb {

//
// Instantiate the various classes under test.
//

// TODO(thompsonbry) variable length keys. declare key_view variants
// here.

template class unodb::db<std::uint64_t>;
template class unodb::mutex_db<std::uint64_t>;
template class unodb::olc_db<std::uint64_t>;

}  // namespace unodb

namespace unodb::test {

//
// Instantiate tree_verifier variants for each class under test.
//

template class tree_verifier<u64_db>;
template class tree_verifier<u64_mutex_db>;
template class tree_verifier<u64_olc_db>;

}  // namespace unodb::test

namespace unodb {

// Ensure unrolled in .cpp and therefore available to debugger.
template <>
[[gnu::cold]] UNODB_DETAIL_NOINLINE void
detail::basic_art_key<std::uint64_t>::dump() const {
  dump(std::cerr);
}
template <>
[[gnu::cold]] UNODB_DETAIL_NOINLINE void
detail::basic_art_key<unodb::key_view>::dump() const {
  dump(std::cerr);
}

}  // namespace unodb
