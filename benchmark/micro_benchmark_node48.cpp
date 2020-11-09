// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

void grow_node16_to_node48_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<unodb::db, 16>(state);
}

void grow_node16_to_node48_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<unodb::db, 16>(state);
}

void node48_sequential_add(benchmark::State &state) {
  unodb::benchmark::sequential_add_benchmark<unodb::db, 48>(state);
}

void node48_random_add(benchmark::State &state) {
  unodb::benchmark::random_add_benchmark<unodb::db, 48>(state);
}

void minimal_node48_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::minimal_tree_full_scan<unodb::db, 48>(state);
}

void minimal_node48_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::minimal_tree_random_gets<unodb::db, 48>(state);
}

void full_node48_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<unodb::db, 48>(state);
}

void full_node48_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<unodb::db, 48>(state);
}

void full_node48_tree_sequential_delete(benchmark::State &state) {
  unodb::benchmark::sequential_delete_benchmark<unodb::db, 48>(state);
}

void full_node48_tree_random_delete(benchmark::State &state) {
  unodb::benchmark::random_delete_benchmark<unodb::db, 48>(state);
}

void shrink_node256_to_node48_sequentially(benchmark::State &state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<unodb::db, 48>(state);
}

void shrink_node256_to_node48_randomly(benchmark::State &state) {
  unodb::benchmark::shrink_node_randomly_benchmark<unodb::db, 48>(state);
}

}  // namespace

BENCHMARK(grow_node16_to_node48_sequentially)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node16_to_node48_randomly)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node48_sequential_add)->Range(2, 4096)->Unit(benchmark::kMicrosecond);
BENCHMARK(node48_random_add)->Range(2, 4096)->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node48_tree_full_scan)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node48_tree_random_gets)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node48_tree_full_scan)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node48_tree_random_gets)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node48_tree_sequential_delete)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node48_tree_random_delete)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node256_to_node48_sequentially)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node256_to_node48_randomly)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
