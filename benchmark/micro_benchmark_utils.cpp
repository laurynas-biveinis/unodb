// Copyright 2020-2021 Laurynas Biveinis

#include "global.hpp"

#include "micro_benchmark_utils.hpp"

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace unodb::benchmark {

// Teardown

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state) noexcept {
  // Timer must be stopped on entry
  db.clear();
  ::benchmark::ClobberMemory();
  state.ResumeTiming();
}

template void destroy_tree<unodb::db>(unodb::db &,
                                      ::benchmark::State &) noexcept;
template void destroy_tree<unodb::mutex_db>(unodb::mutex_db &,
                                            ::benchmark::State &) noexcept;
template void destroy_tree<unodb::olc_db>(unodb::olc_db &,
                                          ::benchmark::State &) noexcept;

}  // namespace unodb::benchmark
