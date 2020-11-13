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

void node256_sequential_add(benchmark::State &state) {
  unodb::benchmark::sequential_add_benchmark<unodb::db, 256>(state);
}

void node256_random_add(benchmark::State &state) {
  unodb::benchmark::random_add_benchmark<unodb::db, 256>(state);
}

void minimal_node256_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::minimal_tree_full_scan<unodb::db, 256>(state);
}

void minimal_node256_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::minimal_tree_random_gets<unodb::db, 256>(state);
}

void full_node256_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<unodb::db, 256>(state);
}

void full_node256_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<unodb::db, 256>(state);
}

void full_node256_tree_sequential_delete(benchmark::State &state) {
  unodb::benchmark::sequential_delete_benchmark<unodb::db, 256>(state);
}

void full_node256_tree_random_delete(benchmark::State &state) {
  unodb::benchmark::random_delete_benchmark<unodb::db, 256>(state);
}

}  // namespace

BENCHMARK(grow_node48_to_node256_sequentially)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node48_to_node256_randomly)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node256_sequential_add)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node256_random_add)->Range(1, 1024)->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node256_tree_full_scan)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node256_tree_random_gets)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node256_tree_full_scan)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node256_tree_random_gets)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node256_tree_sequential_delete)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node256_tree_random_delete)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
