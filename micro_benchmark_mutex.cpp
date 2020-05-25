// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include <thread>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark.hpp"
#include "mutex_art.hpp"

namespace {

std::unique_ptr<unodb::mutex_db> test_db;

void parallel_get_worker(unodb::key start, unodb::key length) {
  for (unodb::key i = start; i < start + length; ++i)
    unodb::benchmark::get_existing_key(*test_db, i);
}

void parallel_get(benchmark::State &state) {
  test_db = std::make_unique<unodb::mutex_db>();
  const auto tree_size = static_cast<unodb::key>(state.range(1));
  for (unodb::key i = 0; i < tree_size; ++i)
    unodb::benchmark::insert_key(
        *test_db, i,
        unodb::benchmark::values[i % unodb::benchmark::values.size()]);

  const auto num_of_threads = static_cast<std::size_t>(state.range(0));
  const unodb::key length = tree_size / num_of_threads;
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

void parallel_insert_worker(unodb::key start, unodb::key length) {
  for (unodb::key i = start; i < start + length; ++i)
    unodb::benchmark::insert_key(
        *test_db, i,
        unodb::benchmark::values[i % unodb::benchmark::values.size()]);
}

void parallel_insert_disjoint_ranges(benchmark::State &state) {
  const auto num_of_threads = static_cast<std::size_t>(state.range(0));
  const auto tree_size = static_cast<unodb::key>(state.range(1));
  const unodb::key length = tree_size / num_of_threads;

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::thread> threads{num_of_threads};
    test_db = std::make_unique<unodb::mutex_db>();
    state.ResumeTiming();

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      const unodb::key start = i * length;
      threads[i] = std::thread{parallel_insert_worker, start, length};
    }

    parallel_insert_worker(0, length);

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      threads[i].join();
    }

    state.PauseTiming();
    unodb::benchmark::destroy_tree(*test_db, state);
  }

  test_db.reset(nullptr);
}

void parallel_delete_worker(unodb::key start, unodb::key length) {
  for (unodb::key i = start; i < start + length; ++i)
    unodb::benchmark::delete_key(*test_db, i);
}

void parallel_delete_disjoint_ranges(benchmark::State &state) {
  const auto tree_size = static_cast<unodb::key>(state.range(1));
  const auto num_of_threads = static_cast<std::size_t>(state.range(0));
  const unodb::key length = tree_size / num_of_threads;

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::thread> threads{num_of_threads};
    test_db = std::make_unique<unodb::mutex_db>();
    for (unodb::key i = 0; i < tree_size; ++i)
      unodb::benchmark::insert_key(
          *test_db, i,
          unodb::benchmark::values[i % unodb::benchmark::values.size()]);
    state.ResumeTiming();

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      const unodb::key start = i * length;
      threads[i] = std::thread{parallel_delete_worker, start, length};
    }

    parallel_delete_worker(0, length);

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      threads[i].join();
    }

    state.PauseTiming();
    unodb::benchmark::destroy_tree(*test_db, state);
  }

  test_db.reset(nullptr);
}

// Something small for Travis CI quick checks
constexpr auto small_tree_size = 70000;
constexpr auto large_tree_size = 3000000;

static void ranges(benchmark::internal::Benchmark *b, int max_concurrency) {
  for (auto i = 1; i <= max_concurrency; i *= 2) b->Args({i, small_tree_size});
  for (auto i = 1; i <= max_concurrency; i *= 2) b->Args({i, large_tree_size});
}

static void ranges16(benchmark::internal::Benchmark *b) { ranges(b, 16); }

static void ranges32(benchmark::internal::Benchmark *b) { ranges(b, 32); }

}  // namespace

BENCHMARK(parallel_get)
    ->Apply(ranges16)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK(parallel_insert_disjoint_ranges)
    ->Apply(ranges32)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK(parallel_delete_disjoint_ranges)
    ->Apply(ranges32)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

BENCHMARK_MAIN();
