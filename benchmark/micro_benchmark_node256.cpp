// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

void grow_node48_to_node256_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<unodb::db, 48>(state);
}

void grow_node48_to_node256_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<unodb::db, 48>(state);
}

}  // namespace

BENCHMARK(grow_node48_to_node256_sequentially)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node48_to_node256_randomly)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
