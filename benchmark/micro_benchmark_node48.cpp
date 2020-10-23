// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <cstddef>
#include <cstdint>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

// Benchmark growing Node16 to Node48: insert to full Node16 tree first:
// 0x0000000000000000 to ...0000F
// 0x0000000000000100 to ...0010F
// ...
// 0x0000000000000F00 to ...00F0F
// 0x0000000000010000 to ...1000F
// ...
// 0x00000000000F0000 to ...F000F
// 0x0000000000010100 to ...1010F
// 0x0000000000010200 to ...1020F
// ...
// The insert in the gaps a "base-17" value with the last byte being a constant
// 10 to get a minimal Node48 tree:
// 0x0000000000000010
// 0x0000000000000110
// ...
// 0x0000000000000F10
// 0x0000000000010010
// ...

void grow_node16_to_node48_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<
      unodb::db, 16, unodb::benchmark::full_node16_tree_key_zero_bits>(
      state, unodb::benchmark::number_to_minimal_leaf_node48_over_node16_key);
}

void grow_node16_to_node48_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<unodb::db, 16>(
      state, unodb::benchmark::number_to_full_node16_with_gaps_key,
      unodb::benchmark::generate_random_keys_over_full_smaller_tree<16>);
}

}  // namespace

BENCHMARK(grow_node16_to_node48_sequentially)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node16_to_node48_randomly)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
