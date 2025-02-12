// Copyright 2020-2025 UnoDB contributors

// Should be the first include
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
