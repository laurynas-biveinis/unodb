// Copyright 2020-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include "micro_benchmark_utils.hpp"

#include <benchmark/benchmark.h>

#include "art.hpp"        // IWYU pragma: keep
#include "mutex_art.hpp"  // IWYU pragma: keep
#include "olc_art.hpp"    // IWYU pragma: keep

namespace unodb::benchmark {

// Teardown

template <class Db>
void destroy_tree(Db &instance, ::benchmark::State &state) {
  // Timer must be stopped on entry
  instance.clear();
  ::benchmark::ClobberMemory();
  state.ResumeTiming();
}

template void destroy_tree<unodb::db<std::uint64_t>>(unodb::db<std::uint64_t> &,
                                                     ::benchmark::State &);
template void destroy_tree<unodb::mutex_db<std::uint64_t>>(
    unodb::mutex_db<std::uint64_t> &, ::benchmark::State &);
template void destroy_tree<unodb::olc_db<std::uint64_t>>(
    unodb::olc_db<std::uint64_t> &, ::benchmark::State &);

}  // namespace unodb::benchmark
