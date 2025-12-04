// Copyright 2019-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__vector/vector.h>
// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>

#include <cstddef>
#include <cstdint>
#include <tuple>

#include <benchmark/benchmark.h>

#include "art_common.hpp"
#include "art_internal.hpp"  // IWYU pragma: keep
#include "micro_benchmark_node_utils.hpp"
#include "micro_benchmark_utils.hpp"
#include "node_type.hpp"

namespace {

template <class Db>
void dense_insert(benchmark::State &state) {
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::growing_tree_node_stats<Db> growing_tree_stats;
  std::size_t tree_size = 0;
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (std::uint64_t i = 0; i < static_cast<std::uint64_t>(state.range(0));
         ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});

    state.PauseTiming();
#ifdef UNODB_DETAIL_WITH_STATS
    growing_tree_stats.get(test_db);
    tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
#ifdef UNODB_DETAIL_WITH_STATS
  growing_tree_stats.publish(state);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

template <class Db>
void sparse_insert_dups_allowed(benchmark::State &state) {
  unodb::benchmark::batched_prng random_keys;
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::growing_tree_node_stats<Db> growing_tree_stats;
  std::size_t tree_size = 0;
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (auto i = 0; i < state.range(0); ++i) {
      const auto random_key = random_keys.get(state);
      unodb::benchmark::insert_key_ignore_dups(
          test_db, random_key, unodb::value_view{unodb::benchmark::value100});
    }

    state.PauseTiming();
#ifdef UNODB_DETAIL_WITH_STATS
    growing_tree_stats.get(test_db);
    tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
#ifdef UNODB_DETAIL_WITH_STATS
  growing_tree_stats.publish(state);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

constexpr auto full_scan_multiplier = 50;

// inserts a sequences of keys and a fixed value then runs over the
// tree fetching the each key.
template <class Db>
void dense_full_scan(benchmark::State &state) {
  Db test_db;
  const auto key_limit = static_cast<std::uint64_t>(state.range(0));

  for (std::uint64_t i = 0; i < key_limit; ++i)
    unodb::benchmark::insert_key(test_db, i,
                                 unodb::value_view{unodb::benchmark::value100});
#ifdef UNODB_DETAIL_WITH_STATS
  const auto tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state)
    for (auto i = 0; i < full_scan_multiplier; ++i)
      for (std::uint64_t j = 0; j < key_limit; ++j)
        unodb::benchmark::get_existing_key(test_db, j);

  state.SetItemsProcessed(state.iterations() * state.range(0) *
                          full_scan_multiplier);
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

// decode a uint64_t key.
[[nodiscard]] std::uint64_t decode(unodb::key_view akey) noexcept {
  unodb::key_decoder dec{akey};
  std::uint64_t k;
  dec.decode(k);
  return k;
}

// inserts keys and a constant value and then scans all entries in the
// tree using db::scan(), reading both the keys and the values.
template <class Db>
void dense_iter_full_fwd_scan(benchmark::State &state) {
  Db test_db;
  const auto key_limit = static_cast<std::uint64_t>(state.range(0));

  for (std::uint64_t i = 0; i < key_limit; ++i)
    unodb::benchmark::insert_key(test_db, i,
                                 unodb::value_view{unodb::benchmark::value100});
#ifdef UNODB_DETAIL_WITH_STATS
  const auto tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    for (auto i = 0; i < full_scan_multiplier; ++i) {
      std::uint64_t sum = 0;
      auto fn =
          [&sum](const unodb::visitor<typename Db::iterator> &v) noexcept {
            sum += decode(v.get_key());
            std::ignore = v.get_value();
            return false;
          };
      test_db.scan(fn);
      ::benchmark::DoNotOptimize(sum);  // ensure that the keys were retrieved.
    }
  }

  state.SetItemsProcessed(state.iterations() * state.range(0) *
                          full_scan_multiplier);
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

// inserts keys and a constant value and scans the entries in a
// key-range in the tree using db::scan(), reading both the keys and
// the values (this variant has more overhead than a full scan since
// it must check for the end of the range).
template <class Db>
void dense_iter_keyrange_fwd_scan(benchmark::State &state) {
  Db test_db;
  const auto key_limit = static_cast<std::uint64_t>(state.range(0));

  for (std::uint64_t i = 0; i < key_limit; ++i)
    unodb::benchmark::insert_key(test_db, i,
                                 unodb::value_view{unodb::benchmark::value100});
#ifdef UNODB_DETAIL_WITH_STATS
  const auto tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    for (auto i = 0; i < full_scan_multiplier; ++i) {
      std::uint64_t sum = 0;
      auto fn =
          [&sum](const unodb::visitor<typename Db::iterator> &v) noexcept {
            sum += decode(v.get_key());
            std::ignore = v.get_value();
            return false;
          };
      test_db.scan_range(0, key_limit,
                         fn);           // scan all keys, but using a key-range.
      ::benchmark::DoNotOptimize(sum);  // ensure that the keys were retrieved.
    }
  }

  state.SetItemsProcessed(state.iterations() * state.range(0) *
                          full_scan_multiplier);
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

void dense_tree_sparse_deletes_args(benchmark::internal::Benchmark *b) {
  for (auto i = 1000; i <= 5000000; i *= 8) {
    b->Args({i, 800});
    b->Args({i, i});
  }
}

template <class Db>
void dense_tree_sparse_deletes(benchmark::State &state) {
  // Node shrinking stats almost always zero, thus this test only tests
  // non-shrinking Node256 delete
#ifdef UNODB_DETAIL_WITH_STATS
  std::size_t start_tree_size = 0;
  std::size_t end_tree_size = 0;
  std::uint64_t start_leaf_count = 0;
  std::uint64_t end_leaf_count = 0;
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    state.PauseTiming();
    unodb::benchmark::batched_prng random_keys{
        static_cast<std::uint64_t>(state.range(0) - 1)};
    Db test_db;
    for (std::uint64_t i = 0; i < static_cast<std::uint64_t>(state.range(0));
         ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});
#ifdef UNODB_DETAIL_WITH_STATS
    start_tree_size = test_db.get_current_memory_use();
    start_leaf_count =
        test_db.template get_node_count<unodb::node_type::LEAF>();
#endif  // UNODB_DETAIL_WITH_STATS
    state.ResumeTiming();

    for (auto j = 0; j < state.range(1); ++j) {
      const auto random_key = random_keys.get(state);
      unodb::benchmark::delete_key_if_exists(test_db, random_key);
    }

#ifdef UNODB_DETAIL_WITH_STATS
    end_tree_size = test_db.get_current_memory_use();
    end_leaf_count = test_db.template get_node_count<unodb::node_type::LEAF>();
#endif  // UNODB_DETAIL_WITH_STATS
  }

  state.SetItemsProcessed(state.iterations() * state.range(1));
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "start size", start_tree_size);
  unodb::benchmark::set_size_counter(state, "end size", end_tree_size);
  state.counters["start L"] = static_cast<double>(start_leaf_count);
  state.counters["end L"] = static_cast<double>(end_leaf_count);
#endif  // UNODB_DETAIL_WITH_STATS
}

constexpr auto dense_tree_increasing_keys_delete_insert_pairs = 1000000;

template <class Db>
void dense_tree_increasing_keys(benchmark::State &state) {
  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    std::uint64_t key_to_insert;
    for (key_to_insert = 0;
         key_to_insert < static_cast<std::uint64_t>(state.range(0));
         ++key_to_insert) {
      unodb::benchmark::insert_key(
          test_db, key_to_insert,
          unodb::value_view{unodb::benchmark::value100});
    }
    std::uint64_t key_to_delete = 0;
    state.ResumeTiming();

    for (auto i = 0; i < dense_tree_increasing_keys_delete_insert_pairs; ++i) {
      unodb::benchmark::delete_key(test_db, key_to_delete);
      ++key_to_delete;
      unodb::benchmark::insert_key(
          test_db, key_to_insert,
          unodb::value_view{unodb::benchmark::value100});
      ++key_to_insert;
    }

    state.PauseTiming();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() *
                          dense_tree_increasing_keys_delete_insert_pairs * 2);
}

void dense_insert_value_lengths_args(benchmark::internal::Benchmark *b) {
  for (auto i = 100; i <= 1000000; i *= 8)
    for (auto j = 0; j < static_cast<int64_t>(unodb::benchmark::values.size());
         ++j)
      b->Args({i, j});
}

template <class Db>
void dense_insert_value_lengths(benchmark::State &state) {
#ifdef UNODB_DETAIL_WITH_STATS
  std::size_t tree_size = 0;
#endif  // UNODB_DETAIL_WITH_STATS
  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (std::uint64_t i = 0; i < static_cast<std::uint64_t>(state.range(0));
         ++i)
      unodb::benchmark::insert_key(
          test_db, i,
          unodb::benchmark::values[static_cast<
              decltype(unodb::benchmark::values)::size_type>(state.range(1))]);

    state.PauseTiming();
#ifdef UNODB_DETAIL_WITH_STATS
    tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetBytesProcessed(
      state.iterations() * state.range(0) *
      (state.range(1) + static_cast<std::int64_t>(sizeof(std::uint64_t))));
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

template <class Db>
void dense_insert_dup_attempts(benchmark::State &state) {
  for (const auto _ : state) {
    state.PauseTiming();
    const auto key_limit = static_cast<std::uint64_t>(state.range(0));
    Db test_db;
    for (std::uint64_t i = 0; i < key_limit; ++i)
      unodb::benchmark::insert_key(
          test_db, i, unodb::value_view{unodb::benchmark::value100});
    state.ResumeTiming();

    for (std::uint64_t i = 0; i < key_limit; ++i)
      unodb::benchmark::insert_key_ignore_dups(
          test_db, i, unodb::value_view{unodb::benchmark::value100});

    state.PauseTiming();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

}  // namespace

UNODB_START_BENCHMARKS()

// TODO(laurynas): only dense Node256 trees have reasonable coverage, need
// to handle sparse Node16/Node48 trees too
BENCHMARK_TEMPLATE(dense_insert, unodb::benchmark::db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_insert, unodb::benchmark::mutex_db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_insert, unodb::benchmark::olc_db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(sparse_insert_dups_allowed, unodb::benchmark::db)
    ->Range(100, 10000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(sparse_insert_dups_allowed, unodb::benchmark::mutex_db)
    ->Range(100, 10000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(sparse_insert_dups_allowed, unodb::benchmark::olc_db)
    ->Range(100, 10000000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(dense_full_scan, unodb::benchmark::db)
    ->Range(100, 20000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_full_scan, unodb::benchmark::mutex_db)
    ->Range(100, 20000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_full_scan, unodb::benchmark::olc_db)
    ->Range(100, 20000000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(dense_iter_full_fwd_scan, unodb::benchmark::db)
    ->Range(128, 1 << 28)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_iter_full_fwd_scan, unodb::benchmark::mutex_db)
    ->Range(128, 1 << 20)  // less scale since just not interesting.
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_iter_full_fwd_scan, unodb::benchmark::olc_db)
    ->Range(128, 1 << 28)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(dense_iter_keyrange_fwd_scan, unodb::benchmark::db)
    ->Range(128, 1 << 28)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_iter_keyrange_fwd_scan, unodb::benchmark::mutex_db)
    ->Range(128, 1 << 20)  // less scale since just not interesting.
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_iter_keyrange_fwd_scan, unodb::benchmark::olc_db)
    ->Range(128, 1 << 28)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(dense_tree_sparse_deletes, unodb::benchmark::db)
    ->ArgNames({"", "deletes"})
    ->Apply(dense_tree_sparse_deletes_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_tree_sparse_deletes, unodb::benchmark::mutex_db)
    ->ArgNames({"", "deletes"})
    ->Apply(dense_tree_sparse_deletes_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_tree_sparse_deletes, unodb::benchmark::olc_db)
    ->ArgNames({"", "deletes"})
    ->Apply(dense_tree_sparse_deletes_args)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(dense_tree_increasing_keys, unodb::benchmark::db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(dense_tree_increasing_keys, unodb::benchmark::mutex_db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(dense_tree_increasing_keys, unodb::benchmark::olc_db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(dense_insert_value_lengths, unodb::benchmark::db)
    ->ArgNames({"", "value len log10"})
    ->Apply(dense_insert_value_lengths_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_insert_value_lengths, unodb::benchmark::mutex_db)
    ->ArgNames({"", "value len log10"})
    ->Apply(dense_insert_value_lengths_args)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_insert_value_lengths, unodb::benchmark::olc_db)
    ->ArgNames({"", "value len log10"})
    ->Apply(dense_insert_value_lengths_args)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(dense_insert_dup_attempts, unodb::benchmark::db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_insert_dup_attempts, unodb::benchmark::mutex_db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(dense_insert_dup_attempts, unodb::benchmark::olc_db)
    ->Range(100, 30000000)
    ->Unit(benchmark::kMicrosecond);

UNODB_BENCHMARK_MAIN();
