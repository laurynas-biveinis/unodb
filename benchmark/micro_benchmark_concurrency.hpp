// Copyright 2020-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_MICRO_BENCHMARK_CONCURRENCY_HPP
#define UNODB_DETAIL_MICRO_BENCHMARK_CONCURRENCY_HPP

#include "global.hpp"

#include <memory>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

#include "art_common.hpp"
#include "micro_benchmark_utils.hpp"

namespace unodb::benchmark {

// Something small for CI quick checks
static constexpr auto small_concurrent_tree_size = 70000;
// Do not OOM on a 16GB Linux test server
static constexpr auto large_concurrent_tree_size = 2000000;

constexpr void concurrency_ranges(::benchmark::internal::Benchmark *b,
                                  int max_concurrency) {
  for (auto i = 1; i <= max_concurrency; i *= 2)
    b->Args({i, small_concurrent_tree_size});
  for (auto i = 1; i <= max_concurrency; i *= 2)
    b->Args({i, large_concurrent_tree_size});
}

constexpr void concurrency_ranges16(::benchmark::internal::Benchmark *b) {
  concurrency_ranges(b, 16);
}

constexpr void concurrency_ranges32(::benchmark::internal::Benchmark *b) {
  concurrency_ranges(b, 32);
}

template <typename T>
[[nodiscard]] constexpr auto to_counter(T value) {
  return ::benchmark::Counter{static_cast<double>(value)};
}

template <class Db, class Thread>
class [[nodiscard]] concurrent_benchmark {
 protected:
  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-final-methods")
  virtual void setup() {}

  virtual void end_workload_in_main_thread() {}

  virtual void teardown() noexcept {}
  UNODB_DETAIL_RESTORE_GCC_WARNINGS()

 public:
  concurrent_benchmark() noexcept = default;
  virtual ~concurrent_benchmark() = default;

  void parallel_get(::benchmark::State &state) {
    const auto num_of_threads = static_cast<std::size_t>(state.range(0));
    const auto tree_size = static_cast<unodb::key>(state.range(1));

    test_db = std::make_unique<Db>();

    for (unodb::key i = 0; i < tree_size; ++i)
      insert_key(*test_db, i, values[i % values.size()]);

    for (const auto _ : state) {
      state.PauseTiming();
      do_parallel_test(*test_db, num_of_threads, tree_size, parallel_get_worker,
                       state);
      state.ResumeTiming();
    }

    test_db.reset(nullptr);
  }

  void parallel_insert_disjoint_ranges(::benchmark::State &state) {
    const auto num_of_threads = static_cast<std::size_t>(state.range(0));
    const auto tree_size = static_cast<unodb::key>(state.range(1));

    for (const auto _ : state) {
      state.PauseTiming();
      test_db = std::make_unique<Db>();

      do_parallel_test(*test_db, num_of_threads, tree_size,
                       parallel_insert_worker, state);

      destroy_tree(*test_db, state);
      state.ResumeTiming();
    }

    test_db.reset(nullptr);
  }

  void parallel_delete_disjoint_ranges(::benchmark::State &state) {
    const auto num_of_threads = static_cast<std::size_t>(state.range(0));
    const auto tree_size = static_cast<unodb::key>(state.range(1));

    for (const auto _ : state) {
      state.PauseTiming();

      test_db = std::make_unique<Db>();
      for (unodb::key i = 0; i < tree_size; ++i)
        insert_key(*test_db, i, values[i % values.size()]);

      do_parallel_test(*test_db, num_of_threads, tree_size,
                       parallel_delete_worker, state);
      destroy_tree(*test_db, state);
      state.ResumeTiming();
    }

    test_db.reset(nullptr);
  }

  concurrent_benchmark(const concurrent_benchmark<Db, Thread> &) = delete;
  concurrent_benchmark(concurrent_benchmark<Db, Thread> &&) = delete;
  concurrent_benchmark<Db, Thread> &operator=(
      const concurrent_benchmark<Db, Thread> &) = delete;
  concurrent_benchmark<Db, Thread> &operator=(
      concurrent_benchmark<Db, Thread> &&) = delete;

 private:
  template <typename Worker>
  void do_parallel_test(Db &db, std::size_t num_of_threads,
                        std::size_t tree_size, Worker worker,
                        ::benchmark::State &state) {
    setup();

    std::vector<Thread> threads{num_of_threads - 1};
    const unodb::key length{tree_size / num_of_threads};

    state.ResumeTiming();

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      const unodb::key start = i * length;
      threads[i - 1] = Thread{worker, std::ref(db), start, length};
    }

    worker(db, 0, length);

    for (std::size_t i = 1; i < num_of_threads; ++i) {
      threads[i - 1].join();
    }

    end_workload_in_main_thread();

    state.PauseTiming();

    teardown();
  }

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

#endif  // UNODB_DETAIL_MICRO_BENCHMARK_CONCURRENCY_HPP
