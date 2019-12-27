// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include "art.hpp"
#include "micro_benchmark.hpp"
#include "mutex_art.hpp"

#include <benchmark/benchmark.h>

namespace {

std::unique_ptr<unodb::mutex_db> test_db;

constexpr auto parallel_get_tree_size = 10000000;

void parallel_get(benchmark::State &state) {
  if (state.thread_index == 0) {
    test_db = std::make_unique<unodb::mutex_db>();
    for (unodb::key i = 0; i < static_cast<unodb::key>(parallel_get_tree_size);
         ++i) {
      (void)test_db->insert(i, values[i % values.size()]);
    }
  }

  const auto length = parallel_get_tree_size / state.threads;
  const auto start = state.thread_index * length;
  for (auto _ : state) {
    for (unodb::key i = static_cast<unodb::key>(start);
         i < static_cast<unodb::key>(start + length); ++i) {
      benchmark::DoNotOptimize(test_db->get(i));
    }
  }

  if (state.thread_index == 0) {
    test_db.reset(nullptr);
  }
}

constexpr auto parallel_insert_tree_size = 10000000;

void parallel_insert_disjoint_ranges(benchmark::State &state) {
  if (state.thread_index == 0) {
    test_db = std::make_unique<unodb::mutex_db>();
  }

  const auto length = parallel_insert_tree_size / state.threads;
  const auto start = state.thread_index * length;
  for (auto _ : state) {
    for (unodb::key i = static_cast<unodb::key>(start);
         i < static_cast<unodb::key>(start + length); ++i) {
      benchmark::DoNotOptimize(test_db->insert(i, values[i % values.size()]));
    }
  }

  if (state.thread_index == 0) {
    test_db.reset(nullptr);
  }
}

constexpr auto parallel_delete_tree_size = 10000000;

void parallel_delete_disjoint_ranges(benchmark::State &state) {
  if (state.thread_index == 0) {
    test_db = std::make_unique<unodb::mutex_db>();
    for (unodb::key i = 0; i < static_cast<unodb::key>(parallel_get_tree_size);
         ++i) {
      (void)test_db->insert(i, values[i % values.size()]);
    }
  }

  const auto length = parallel_delete_tree_size / state.threads;
  const auto start = state.thread_index * length;
  for (auto _ : state) {
    for (unodb::key i = static_cast<unodb::key>(start);
         i < static_cast<unodb::key>(start + length); ++i) {
      benchmark::DoNotOptimize(test_db->remove(i));
    }
  }

  if (state.thread_index == 0) {
    test_db.reset(nullptr);
  }
}

}  // namespace

BENCHMARK(parallel_get)
    ->ThreadRange(1, 4096)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK(parallel_insert_disjoint_ranges)
    ->ThreadRange(1, 512)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK(parallel_delete_disjoint_ranges)
    ->ThreadRange(1, 1024)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();
