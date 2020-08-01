// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <cstdint>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark.hpp"

namespace {

// Benchmark growing Node4 to Node16: insert to dense Node4 tree first:
// 0x0000000000000000 to ...003
// 0x0000000000000100 to ...103
// 0x0000000000000200 to ...203
// 0x0000000000000300 to ...303
// 0x0000000000010000 to ...003
// 0x0000000000010100 to ...103
// ...
// Then insert in the gaps a "base-5" value that varies each byte from 0 to 5
// with the last one being a constant 4 to get a minimal Node16 tree:
// 0x0000000000000004
// 0x0000000000000104
// 0x0000000000000204
// 0x0000000000000304
// 0x0000000000000404
// 0x0000000000010004
// 0x0000000000010104
// ...

void grow_node4_to_node16_sequentially(benchmark::State &state) {
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

  const auto node4_node_count = static_cast<std::uint64_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    unodb::key key_limit = unodb::benchmark::insert_sequentially(
        test_db, node4_node_count * 4,
        unodb::benchmark::dense_node4_key_zero_bits);
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::grow_dense_node4_to_minimal_node16(test_db, key_limit);

    state.PauseTiming();
    growing_tree_stats.get(test_db);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
  growing_tree_stats.publish(state);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

}  // namespace

BENCHMARK(grow_node4_to_node16_sequentially)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
