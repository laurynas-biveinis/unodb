// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include <string>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

void dense_insert(benchmark::State &state) {
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});

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

void sparse_insert_dups_allowed(benchmark::State &state) {
  unodb::benchmark::batched_prng random_keys;
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

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

constexpr auto full_scan_multiplier = 50;

void dense_full_scan(benchmark::State &state) {
  unodb::db test_db;
  const auto key_limit = static_cast<unodb::key>(state.range(0));

  for (unodb::key i = 0; i < key_limit; ++i)
    unodb::benchmark::insert_key(test_db, i,
                                 unodb::value_view{unodb::benchmark::value100});
  std::size_t tree_size = test_db.get_current_memory_use();

  for (auto _ : state)
    for (auto i = 0; i < full_scan_multiplier; ++i)
      for (unodb::key j = 0; j < key_limit; ++j)
        unodb::benchmark::get_existing_key(test_db, j);

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0) * full_scan_multiplier);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}
void dense_tree_sparse_deletes_args(benchmark::internal::Benchmark *b) {
  for (auto i = 1000; i <= 5000000; i *= 8) {
    b->Args({i, 800});
    b->Args({i, i});
  }
}

void dense_tree_sparse_deletes(benchmark::State &state) {
  // Node shrinking stats almost always zero, thus this test only tests
  // non-shrinking Node256 delete
  std::size_t start_tree_size = 0;
  std::size_t end_tree_size = 0;
  std::uint64_t start_leaf_count = 0;
  std::uint64_t end_leaf_count = 0;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::benchmark::batched_prng random_keys{
        static_cast<std::uint64_t>(state.range(0) - 1)};
    unodb::db test_db;
    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});
    start_tree_size = test_db.get_current_memory_use();
    start_leaf_count = test_db.get_leaf_count();
    state.ResumeTiming();

    for (auto j = 0; j < state.range(1); ++j) {
      const auto random_key = random_keys.get(state);
      unodb::benchmark::delete_key_if_exists(test_db, random_key);
    }

    end_tree_size = test_db.get_current_memory_use();
    end_leaf_count = test_db.get_leaf_count();
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(1));
  unodb::benchmark::set_size_counter(state, "start size", start_tree_size);
  unodb::benchmark::set_size_counter(state, "end size", end_tree_size);
  state.counters["start L"] = static_cast<double>(start_leaf_count);
  state.counters["end L"] = static_cast<double>(end_leaf_count);
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

    state.PauseTiming();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          dense_tree_increasing_keys_delete_insert_pairs * 2);
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
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (unodb::key i = 0; i < static_cast<unodb::key>(state.range(0)); ++i)
      unodb::benchmark::insert_key(
          test_db, i,
          unodb::benchmark::values[static_cast<decltype(
              unodb::benchmark::values)::size_type>(state.range(1))]);

    state.PauseTiming();
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetBytesProcessed(
      static_cast<std::int64_t>(state.iterations()) * state.range(0) *
      (state.range(1) + static_cast<std::int64_t>(sizeof(unodb::key))));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
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

    state.PauseTiming();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

}  // namespace

// TODO(laurynas): only dense Node256 trees have reasonable coverage, need
// to handle sparse Node16/Node48 trees too
BENCHMARK(dense_insert)->Range(100, 30000000)->Unit(benchmark::kMicrosecond);
BENCHMARK(sparse_insert_dups_allowed)
    ->Range(100, 10000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_full_scan)->Range(100, 20000000)->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_tree_sparse_deletes)
    ->ArgNames({"", "deletes"})
    ->Apply(dense_tree_sparse_deletes_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_tree_increasing_keys)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(dense_insert_value_lengths)
    ->ArgNames({"", "value len log10"})
    ->Apply(dense_insert_value_lengths_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(dense_insert_dup_attempts)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
