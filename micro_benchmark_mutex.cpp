// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include <thread>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark.hpp"
#include "mutex_art.hpp"

namespace {

std::unique_ptr<unodb::mutex_db> test_db;

// Do not swap on my 16GB RAM laptop
constexpr auto parallel_get_tree_size = 7000000;

void parallel_get_worker(unodb::key start, unodb::key length) {
  for (unodb::key i = start; i < start + length; ++i) {
    benchmark::DoNotOptimize(test_db->get(i));
  }
}

void parallel_get(benchmark::State &state) {
  test_db = std::make_unique<unodb::mutex_db>();
  for (unodb::key i = 0; i < static_cast<unodb::key>(parallel_get_tree_size);
       ++i) {
    (void)test_db->insert(
        i, unodb::benchmark::values[i % unodb::benchmark::values.size()]);
  }

  const std::size_t num_of_threads = static_cast<std::size_t>(state.range());
  const unodb::key length = parallel_get_tree_size / num_of_threads;
  std::vector<std::thread> threads{num_of_threads};

  for (auto _ : state) {
    for (std::size_t i = 1; i < num_of_threads; ++i) {
      const unodb::key start = i * length;
      threads[i] = std::thread{parallel_get_worker, start, length};
    }

    parallel_get_worker(0, length);

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      threads[i].join();
    }
  }

  test_db.reset(nullptr);
}

constexpr auto parallel_insert_tree_size = 7000000;

void parallel_insert_worker(unodb::key start, unodb::key length) {
  for (unodb::key i = start; i < start + length; ++i) {
    benchmark::DoNotOptimize(test_db->insert(
        i, unodb::benchmark::values[i % unodb::benchmark::values.size()]));
  }
}

void parallel_insert_disjoint_ranges(benchmark::State &state) {
  test_db = std::make_unique<unodb::mutex_db>();

  const std::size_t num_of_threads = static_cast<std::size_t>(state.range());
  const unodb::key length = parallel_insert_tree_size / num_of_threads;
  std::vector<std::thread> threads{num_of_threads};

  for (auto _ : state) {
    for (std::size_t i = 1; i < num_of_threads; ++i) {
      const unodb::key start = i * length;
      threads[i] = std::thread{parallel_insert_worker, start, length};
    }

    parallel_insert_worker(0, length);

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      threads[i].join();
    }
  }

  test_db.reset(nullptr);
}

constexpr auto parallel_delete_tree_size = 7000000;

void parallel_delete_worker(unodb::key start, unodb::key length) {
  for (unodb::key i = start; i < start + length; ++i) {
    benchmark::DoNotOptimize(test_db->remove(i));
  }
}

void parallel_delete_disjoint_ranges(benchmark::State &state) {
  test_db = std::make_unique<unodb::mutex_db>();
  for (unodb::key i = 0; i < static_cast<unodb::key>(parallel_delete_tree_size);
       ++i) {
    (void)test_db->insert(
        i, unodb::benchmark::values[i % unodb::benchmark::values.size()]);
  }

  const std::size_t num_of_threads = static_cast<std::size_t>(state.range());
  const unodb::key length = parallel_delete_tree_size / num_of_threads;
  std::vector<std::thread> threads{num_of_threads};

  for (auto _ : state) {
    for (std::size_t i = 1; i < num_of_threads; ++i) {
      const unodb::key start = i * length;
      threads[i] = std::thread{parallel_delete_worker, start, length};
    }

    parallel_delete_worker(0, length);

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      threads[i].join();
    }
  }

  test_db.reset(nullptr);
}

}  // namespace

BENCHMARK(parallel_get)
    ->RangeMultiplier(2)
    ->Range(1, 16)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK(parallel_insert_disjoint_ranges)
    ->RangeMultiplier(2)
    ->Range(1, 32)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK(parallel_delete_disjoint_ranges)
    ->RangeMultiplier(2)
    ->Range(1, 32)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

BENCHMARK_MAIN();
