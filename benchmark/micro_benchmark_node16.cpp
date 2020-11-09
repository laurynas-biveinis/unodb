// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

void grow_node4_to_node16_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<unodb::db, 4>(state);
}

void grow_node4_to_node16_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<unodb::db, 4>(state);
}

void node16_sequential_add(benchmark::State &state) {
  unodb::benchmark::sequential_add_benchmark<unodb::db, 16>(state);
}

void node16_random_add(benchmark::State &state) {
  unodb::benchmark::random_add_benchmark<unodb::db, 16>(state);
}

void minimal_node16_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::minimal_tree_full_scan<unodb::db, 16>(state);
}

void minimal_node16_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::minimal_tree_random_gets<unodb::db, 16>(state);
}

void full_node16_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<unodb::db, 16>(state);
}

void full_node16_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<unodb::db, 16>(state);
}

void full_node16_tree_sequential_delete(benchmark::State &state) {
  unodb::benchmark::sequential_delete_benchmark<unodb::db, 16>(state);
}

void full_node16_tree_random_delete(benchmark::State &state) {
  unodb::benchmark::random_delete_benchmark<unodb::db, 16>(state);
}

void shrink_node48_to_node16_sequentially(benchmark::State &state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<unodb::db, 16>(state);
}

void shrink_node48_to_node16_randomly(benchmark::State &state) {
  unodb::benchmark::shrink_node_randomly_benchmark<unodb::db, 16>(state);
}

}  // namespace

BENCHMARK(grow_node4_to_node16_sequentially)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node4_to_node16_randomly)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node16_sequential_add)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node16_random_add)->Range(10, 16383)->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node16_tree_full_scan)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node16_tree_random_gets)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_full_scan)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_random_gets)
    ->Range(64, 24600)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_sequential_delete)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_random_delete)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node48_to_node16_sequentially)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node48_to_node16_randomly)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_MAIN();
