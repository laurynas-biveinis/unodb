// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include <limits>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark.hpp"

namespace {

class batched_random_key_source final {
 public:
  batched_random_key_source() : random_keys(random_batch_size) { refill(); }

  auto get(benchmark::State &state) {
    if (random_key_ptr == random_keys.cend()) {
      state.PauseTiming();
      refill();
      state.ResumeTiming();
    }
    return *(random_key_ptr++);
  }

 private:
  void refill() {
    for (decltype(random_keys)::size_type i = 0; i < random_keys.size(); ++i)
      random_keys[i] = random_key_dist(gen);
    random_key_ptr = random_keys.cbegin();
  }

  static constexpr auto random_batch_size = 10000;

  std::vector<unodb::key> random_keys;
  std::vector<unodb::key>::const_iterator random_key_ptr;

  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<unodb::key> random_key_dist{
      0ULL, std::numeric_limits<unodb::key>::max()};
};

void dense_insert_no_mem_check(benchmark::State &state) {
  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});

    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
  // TODO(laurynas): add node size / enlarge / shrink stats
  unodb::benchmark::reset_heap();
}

void dense_insert_mem_check(benchmark::State &state) {
  std::size_t tree_size = 0;
  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db{1000ULL * 1000 * 1000 * 1000};
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});
    tree_size = test_db.get_current_memory_use();

    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
  // state.SetLabel might be a better logical fit but the automatic k/M/G
  // suffix is too nice
  // Once Google Benchmark > 1.4.1 is released, use
  // benchmark::Counter::OneK::kIs1024 and drop "(k=1000)"
  state.counters["Size(k=1000)"] =
      benchmark::Counter(static_cast<double>(tree_size));
  unodb::benchmark::reset_heap();
}

void sparse_insert_no_mem_check_dups_allowed(benchmark::State &state) {
  batched_random_key_source random_keys;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (auto i = 0; i < state.range(0); ++i) {
      const auto random_key = random_keys.get(state);
      unodb::benchmark::insert_key_ignore_dups(
          test_db, random_key, unodb::value_view{unodb::benchmark::value100});
    }

    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
  unodb::benchmark::reset_heap();
}

void sparse_insert_mem_check_dups_allowed(benchmark::State &state) {
  batched_random_key_source random_keys;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db{1000ULL * 1000 * 1000 * 1000};
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (auto i = 0; i < state.range(0); ++i) {
      const auto random_key = random_keys.get(state);
      unodb::benchmark::insert_key_ignore_dups(
          test_db, random_key, unodb::value_view{unodb::benchmark::value100});
    }

    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
  unodb::benchmark::reset_heap();
}

constexpr auto full_scan_multiplier = 50;

void dense_full_scan(benchmark::State &state) {
  unodb::db test_db;
  const auto key_limit = static_cast<unodb::key>(state.range(0));
  for (unodb::key i = 0; i < key_limit; ++i)
    unodb::benchmark::insert_key(test_db, i,
                                 unodb::value_view{unodb::benchmark::value100});

  for (auto _ : state)
    for (auto i = 0; i < full_scan_multiplier; ++i)
      for (unodb::key j = 0; j < key_limit; ++j)
        unodb::benchmark::get_existing_key(test_db, j);

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0) * full_scan_multiplier);
  unodb::benchmark::reset_heap();
}

void dense_tree_sparse_deletes_args(benchmark::internal::Benchmark *b) {
  for (auto i = 1000; i <= 5000000; i *= 8)
    for (auto j = 800; j <= 5000000; j *= 8) {
      if (j > i) break;
      b->Args({i, j});
    }
}

void dense_tree_sparse_deletes(benchmark::State &state) {
  batched_random_key_source random_keys;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});
    state.ResumeTiming();

    for (auto j = 0; j < state.range(1); ++j) {
      const auto random_key = random_keys.get(state);
      unodb::benchmark::delete_key_if_exists(test_db, random_key);
    }
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(1));
  unodb::benchmark::reset_heap();
}

constexpr auto dense_tree_increasing_keys_delete_insert_pairs = 1000000;

void dense_tree_increasing_keys(benchmark::State &state) {
  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    unodb::key key_to_insert;
    for (key_to_insert = 0;
         key_to_insert < static_cast<unodb::key>(state.range(0));
         ++key_to_insert) {
      unodb::benchmark::insert_key(
          test_db, key_to_insert,
          unodb::value_view{unodb::benchmark::value100});
    }
    unodb::key key_to_delete = 0;
    state.ResumeTiming();

    for (auto i = 0; i < dense_tree_increasing_keys_delete_insert_pairs; ++i) {
      unodb::benchmark::delete_key(test_db, key_to_delete++);
      unodb::benchmark::insert_key(
          test_db, key_to_insert++,
          unodb::value_view{unodb::benchmark::value100});
    }

    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          dense_tree_increasing_keys_delete_insert_pairs * 2);
  unodb::benchmark::reset_heap();
}

void dense_insert_value_lengths_args(benchmark::internal::Benchmark *b) {
  for (auto i = 100; i <= 1000000; i *= 8)
    for (auto j = 0; j < static_cast<int64_t>(unodb::benchmark::values.size());
         ++j)
      b->Args({i, j});
}

void dense_insert_value_lengths(benchmark::State &state) {
  std::size_t tree_size = 0;
  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db{1000ULL * 1000 * 1000 * 1000};
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i,
          unodb::benchmark::values[static_cast<decltype(
              unodb::benchmark::values)::size_type>(state.range(1))]);
    tree_size = test_db.get_current_memory_use();

    unodb::benchmark::destroy_tree(test_db, state);
  }

  // TODO(laurynas): use value lengths with SetBytesProcessed instead?
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
  state.counters["Size(k=1000)"] =
      benchmark::Counter(static_cast<double>(tree_size));
  unodb::benchmark::reset_heap();
}

void dense_insert_dup_attempts(benchmark::State &state) {
  for (auto _ : state) {
    state.PauseTiming();
    const auto key_limit = static_cast<unodb::key>(state.range(0));
    unodb::db test_db;
    for (unodb::key i = 0; i < key_limit; ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});
    state.ResumeTiming();

    for (unodb::key i = 0; i < key_limit; ++i)
      unodb::benchmark::insert_key_ignore_dups(
          test_db, i, unodb::value_view{unodb::benchmark::value100});

    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
  unodb::benchmark::reset_heap();
}

}  // namespace

// TODO(laurynas): only dense Node256 trees have reasonable coverage, need
// to handle sparse Node4/Node16/Node48 trees too
BENCHMARK(dense_insert_mem_check)
    ->Range(100, 50000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_insert_no_mem_check)
    ->Range(100, 50000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(sparse_insert_mem_check_dups_allowed)
    ->Range(100, 10000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(sparse_insert_no_mem_check_dups_allowed)
    ->Range(100, 10000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_full_scan)->Range(100, 50000000)->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_tree_sparse_deletes)
    ->ArgNames({"", "deletes"})
    ->Apply(dense_tree_sparse_deletes_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_tree_increasing_keys)
    ->Range(100, 50000000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(dense_insert_value_lengths)
    ->ArgNames({"", "value len log10"})
    ->Apply(dense_insert_value_lengths_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_insert_dup_attempts)
    ->Range(100, 50000000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
