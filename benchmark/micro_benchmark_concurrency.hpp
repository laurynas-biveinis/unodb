// Copyright 2020-2021 Laurynas Biveinis
#ifndef UNODB_MICRO_BENCHMARK_CONCURRENCY_HPP_
#define UNODB_MICRO_BENCHMARK_CONCURRENCY_HPP_

#include "global.hpp"

#include <memory>
#include <thread>

#include <benchmark/benchmark.h>

#include "art_common.hpp"
#include "micro_benchmark_utils.hpp"

namespace unodb::benchmark {

// Something small for CI quick checks
static constexpr auto small_concurrent_tree_size = 70000;
// Do not OOM on a 16GB Linux test server
static constexpr auto large_concurrent_tree_size = 5000000;

inline constexpr void concurrency_ranges(::benchmark::internal::Benchmark *b,
                                         int max_concurrency) {
  for (auto i = 1; i <= max_concurrency; i *= 2)
    b->Args({i, small_concurrent_tree_size});
  for (auto i = 1; i <= max_concurrency; i *= 2)
    b->Args({i, large_concurrent_tree_size});
}

inline constexpr void concurrency_ranges16(
    ::benchmark::internal::Benchmark *b) {
  concurrency_ranges(b, 16);
}

inline constexpr void concurrency_ranges32(
    ::benchmark::internal::Benchmark *b) {
  concurrency_ranges(b, 32);
}

template <typename T>
inline constexpr auto to_counter(T value) {
  return ::benchmark::Counter{static_cast<double>(value)};
}

template <class Db, class Thread>
class concurrent_benchmark {
 protected:
  virtual void setup() {}

  virtual void end_workload_in_main_thread() {}

  virtual void teardown() {}

 public:
  virtual ~concurrent_benchmark() {}

  void parallel_get(::benchmark::State &state) {
    test_db = std::make_unique<Db>();

    const auto tree_size = static_cast<unodb::key>(state.range(1));
    for (unodb::key i = 0; i < tree_size; ++i)
      insert_key(*test_db, i, values[i % values.size()]);

    const auto num_of_threads = static_cast<std::size_t>(state.range(0));
    const unodb::key length = tree_size / num_of_threads;
    // FIXME(laurynas): copy-paste
    std::vector<Thread> threads{num_of_threads - 1};

    for (auto _ : state) {
      setup();

      for (std::size_t i = 0; i < num_of_threads - 1; ++i) {
        const unodb::key start = i * length;
        threads[i] =
            Thread{parallel_get_worker, std::ref(*test_db), start, length};
      }

      parallel_get_worker(*test_db, 0, length);

      for (std::size_t i = 0; i < num_of_threads - 1; ++i) {
        threads[i].join();
      }

      end_workload_in_main_thread();

      teardown();
    }

    test_db.reset(nullptr);
  }

  void parallel_insert_disjoint_ranges(::benchmark::State &state) {
    const auto num_of_threads = static_cast<std::size_t>(state.range(0));
    const auto tree_size = static_cast<unodb::key>(state.range(1));
    const unodb::key length = tree_size / num_of_threads;

    for (auto _ : state) {
      state.PauseTiming();
      std::vector<Thread> threads{num_of_threads};
      test_db = std::make_unique<Db>();
      setup();
      state.ResumeTiming();

      for (std::size_t i = 1; i < num_of_threads; ++i) {
        const unodb::key start = i * length;
        threads[i] =
            Thread{parallel_insert_worker, std::ref(*test_db), start, length};
      }

      parallel_insert_worker(*test_db, 0, length);

      for (std::size_t i = 1; i < num_of_threads; ++i) {
        threads[i].join();
      }

      end_workload_in_main_thread();

      state.PauseTiming();
      destroy_tree(*test_db, state);
      teardown();
    }

    test_db.reset(nullptr);
  }

  void parallel_delete_disjoint_ranges(::benchmark::State &state) {
    const auto tree_size = static_cast<unodb::key>(state.range(1));
    const auto num_of_threads = static_cast<std::size_t>(state.range(0));
    const unodb::key length = tree_size / num_of_threads;

    for (auto _ : state) {
      state.PauseTiming();
      std::vector<Thread> threads{num_of_threads};
      test_db = std::make_unique<Db>();
      for (unodb::key i = 0; i < tree_size; ++i)
        insert_key(*test_db, i, values[i % values.size()]);
      setup();
      state.ResumeTiming();

      for (std::size_t i = 1; i < num_of_threads; ++i) {
        const unodb::key start = i * length;
        threads[i] =
            Thread{parallel_delete_worker, std::ref(*test_db), start, length};
      }

      parallel_delete_worker(*test_db, 0, length);

      for (std::size_t i = 1; i < num_of_threads; ++i) {
        threads[i].join();
      }

      end_workload_in_main_thread();

      state.PauseTiming();
      destroy_tree(*test_db, state);
      teardown();
    }

    test_db.reset(nullptr);
  }

 private:
  static void parallel_get_worker(const Db &test_db, unodb::key start,
                                  unodb::key length) {
    for (unodb::key i = start; i < start + length; ++i)
      get_existing_key(test_db, i);
  }

  static void parallel_insert_worker(Db &test_db, unodb::key start,
                                     unodb::key length) {
    for (unodb::key i = start; i < start + length; ++i)
      insert_key(test_db, i, values[i % values.size()]);
  }

  static void parallel_delete_worker(Db &test_db, unodb::key start,
                                     unodb::key length) {
    for (unodb::key i = start; i < start + length; ++i) delete_key(test_db, i);
  }

  std::unique_ptr<Db> test_db;
};

}  // namespace unodb::benchmark

#endif
