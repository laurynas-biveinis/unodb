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

inline constexpr auto full_node16_tree_key_zero_bits = 0xF0F0F0F0'F0F0F0F0ULL;

// Minimal leaf-level Node48 tree keys over full Node16 tree keys: "base-17"
// values that vary each byte from 0 to 0x10 with the last byte being a constant
// 0x10.
inline constexpr auto number_to_minimal_leaf_node48_over_node16_key(
    std::uint64_t i) noexcept {
  assert(i / (0x10 * 0x10 * 0x10 * 0x10 * 0x10 * 0x10) < 0x10);
  return 0x10ULL | unodb::benchmark::to_base_n_value<0x10>(i) << 8;
}

void grow_node16_to_node48_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<
      unodb::db, 16, full_node16_tree_key_zero_bits>(
      state, number_to_minimal_leaf_node48_over_node16_key);
}

void grow_node16_to_node48_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<
      unodb::db, 16,
      decltype(unodb::benchmark::make_node16_tree_with_gaps<unodb::db>)>(
      state, unodb::benchmark::make_node16_tree_with_gaps,
      unodb::benchmark::generate_random_minimal_node48_over_full_node16_keys);
}

}  // namespace

BENCHMARK(grow_node16_to_node48_sequentially)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node16_to_node48_randomly)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
