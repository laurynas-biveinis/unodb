// Copyright (C) 2020-2024 Laurynas Biveinis

#include "global.hpp"

#include "micro_benchmark_utils.hpp"

#include <benchmark/benchmark.h>

#include "art.hpp"        // IWYU pragma: keep
#include "mutex_art.hpp"  // IWYU pragma: keep
#include "olc_art.hpp"    // IWYU pragma: keep

namespace unodb::benchmark {

// Teardown

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state) {
  // Timer must be stopped on entry
  db.clear();
  ::benchmark::ClobberMemory();
  state.ResumeTiming();
}

template void destroy_tree<unodb::db>(unodb::db &, ::benchmark::State &);
template void destroy_tree<unodb::mutex_db>(unodb::mutex_db &,
                                            ::benchmark::State &);
template void destroy_tree<unodb::olc_db>(unodb::olc_db &,
                                          ::benchmark::State &);

}  // namespace unodb::benchmark
