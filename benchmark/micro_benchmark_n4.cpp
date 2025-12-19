// Copyright 2020-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <string>
// IWYU pragma: no_include <__vector/vector.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>  // IWYU pragma: keep

#include <benchmark/benchmark.h>

#include "micro_benchmark_node_utils.hpp"
#include "micro_benchmark_utils.hpp"
#include "node_type.hpp"

namespace {

template <unsigned NodeSize>
[[nodiscard]] std::vector<std::uint64_t> make_n_key_sequence(std::size_t n) {
  std::vector<std::uint64_t> result;
  result.reserve(n);

  std::uint64_t insert_key = 0;
  for (decltype(n) i = 0; i < n; ++i) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(
        insert_key, unodb::benchmark::node_size_to_key_zero_bits<NodeSize>());
  }

  return result;
}

[[nodiscard]] std::vector<std::uint64_t> make_limited_key_sequence(
    std::uint64_t limit, std::uint64_t key_zero_bits) {
  std::vector<std::uint64_t> result;

  std::uint64_t insert_key = 0;
  while (insert_key <= limit) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(insert_key, key_zero_bits);
  }

  result.shrink_to_fit();
  return result;
}

template <class Db, unsigned NodeSize>
void node4_sequential_insert(benchmark::State& state) {
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::growing_tree_node_stats<Db> growing_tree_stats;
  std::size_t tree_size = 0;
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::insert_sequentially<Db, NodeSize>(
        test_db, static_cast<unsigned>(state.range(0)));

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
void full_n4_sequential_insert(benchmark::State& state) {
  node4_sequential_insert<Db, 4>(state);
}

template <class Db>
void minimal_n4_sequential_insert(benchmark::State& state) {
  node4_sequential_insert<Db, 2>(state);
}

template <class Db, unsigned NodeSize>
void node4_random_insert(benchmark::State& state) {
  auto keys =
      make_n_key_sequence<NodeSize>(static_cast<std::size_t>(state.range(0)));

  for (const auto _ : state) {
    state.PauseTiming();
    std::ranges::shuffle(keys, unodb::benchmark::get_prng());
    Db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::insert_keys(test_db, keys);

    state.PauseTiming();
#ifdef UNODB_DETAIL_WITH_STATS
    unodb::benchmark::assert_dominating_inode_tree<Db, unodb::node_type::I4>(
        test_db);
#endif  // UNODB_DETAIL_WITH_STATS
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

template <class Db>
void full_n4_random_insert(benchmark::State& state) {
  node4_random_insert<Db, 4>(state);
}

template <class Db>
void minimal_n4_random_insert(benchmark::State& state) {
  node4_random_insert<Db, 2>(state);
}

template <class Db>
void n4_full_scan(benchmark::State& state) {
  unodb::benchmark::full_node_scan_benchmark<Db, 4>(state);
}

template <class Db>
void n4_random_gets(benchmark::State& state) {
  unodb::benchmark::full_node_random_get_benchmark<Db, 4>(state);
}

template <class Db, unsigned NodeSize>
void node4_sequential_delete_benchmark(benchmark::State& state,
                                       std::uint64_t delete_key_zero_bits) {
  const auto key_count = static_cast<unsigned>(state.range(0));
  int keys_deleted{0};
#ifdef UNODB_DETAIL_WITH_STATS
  std::size_t tree_size{0};
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit =
        unodb::benchmark::insert_sequentially<Db, NodeSize>(test_db, key_count);
#ifdef UNODB_DETAIL_WITH_STATS
    tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS
    state.ResumeTiming();

    std::uint64_t k = 0;
    keys_deleted = 0;
    while (k <= key_limit) {
      unodb::benchmark::delete_key(test_db, k);
      ++keys_deleted;
      k = unodb::benchmark::next_key(k, delete_key_zero_bits);
    }
  }

  state.SetItemsProcessed(state.iterations() * keys_deleted);
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

template <class Db>
void full_n4_sequential_delete(benchmark::State& state) {
  node4_sequential_delete_benchmark<Db, 4>(
      state, unodb::benchmark::node_size_to_key_zero_bits<4>());
}

template <class Db, unsigned NodeSize>
void node4_random_delete_benchmark(benchmark::State& state,
                                   std::uint64_t delete_key_zero_bits) {
  const auto key_count = static_cast<unsigned>(state.range(0));
#ifdef UNODB_DETAIL_WITH_STATS
  std::size_t tree_size{0};
#endif  // UNODB_DETAIL_WITH_STATS

  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit =
        unodb::benchmark::insert_sequentially<Db, NodeSize>(test_db, key_count);
#ifdef UNODB_DETAIL_WITH_STATS
    tree_size = test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS

    auto keys = make_limited_key_sequence(key_limit, delete_key_zero_bits);
    std::ranges::shuffle(keys, unodb::benchmark::get_prng());
    state.ResumeTiming();

    unodb::benchmark::delete_keys(test_db, keys);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
#ifdef UNODB_DETAIL_WITH_STATS
  unodb::benchmark::set_size_counter(state, "size", tree_size);
#endif  // UNODB_DETAIL_WITH_STATS
}

template <class Db>
void full_n4_random_deletes(benchmark::State& state) {
  node4_random_delete_benchmark<Db, 4>(
      state, unodb::benchmark::node_size_to_key_zero_bits<4>());
}

constexpr auto minimal_node4_tree_full_leaf_level_key_zero_bits =
    0xFCFC'FCFC'FCFC'FCFEULL;

template <class Db>
void full_n4_to_minimal_sequential_delete(benchmark::State& state) {
  node4_sequential_delete_benchmark<Db, 4>(
      state, minimal_node4_tree_full_leaf_level_key_zero_bits);
}

template <class Db>
void full_n4_to_minimal_random_delete(benchmark::State& state) {
  node4_random_delete_benchmark<Db, 4>(
      state, minimal_node4_tree_full_leaf_level_key_zero_bits);
}

template <class Db>
void shrink_node16_to_n4_sequentially(benchmark::State& state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<Db, 4>(state);
}

template <class Db>
void shrink_node16_to_n4_randomly(benchmark::State& state) {
  unodb::benchmark::shrink_node_randomly_benchmark<Db, 4>(state);
}

}  // namespace

UNODB_START_BENCHMARKS()

// A maximum Node4-only tree can hold 65K values
BENCHMARK_TEMPLATE(full_n4_sequential_insert, unodb::benchmark::db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_sequential_insert, unodb::benchmark::mutex_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_sequential_insert, unodb::benchmark::olc_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n4_random_insert, unodb::benchmark::db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_random_insert, unodb::benchmark::mutex_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_random_insert, unodb::benchmark::olc_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n4_sequential_insert, unodb::benchmark::db)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n4_sequential_insert, unodb::benchmark::mutex_db)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n4_sequential_insert, unodb::benchmark::olc_db)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n4_random_insert, unodb::benchmark::db)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n4_random_insert, unodb::benchmark::mutex_db)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n4_random_insert, unodb::benchmark::olc_db)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n4_full_scan, unodb::benchmark::db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n4_full_scan, unodb::benchmark::mutex_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n4_full_scan, unodb::benchmark::olc_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n4_random_gets, unodb::benchmark::db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n4_random_gets, unodb::benchmark::mutex_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n4_random_gets, unodb::benchmark::olc_db)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n4_sequential_delete, unodb::benchmark::db)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_sequential_delete, unodb::benchmark::mutex_db)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_sequential_delete, unodb::benchmark::olc_db)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n4_random_deletes, unodb::benchmark::db)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_random_deletes, unodb::benchmark::mutex_db)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_random_deletes, unodb::benchmark::olc_db)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n4_to_minimal_sequential_delete, unodb::benchmark::db)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_to_minimal_sequential_delete,
                   unodb::benchmark::mutex_db)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_to_minimal_sequential_delete,
                   unodb::benchmark::olc_db)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n4_to_minimal_random_delete, unodb::benchmark::db)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_to_minimal_random_delete, unodb::benchmark::mutex_db)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n4_to_minimal_random_delete, unodb::benchmark::olc_db)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(shrink_node16_to_n4_sequentially, unodb::benchmark::db)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_node16_to_n4_sequentially, unodb::benchmark::mutex_db)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_node16_to_n4_sequentially, unodb::benchmark::olc_db)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(shrink_node16_to_n4_randomly, unodb::benchmark::db)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_node16_to_n4_randomly, unodb::benchmark::mutex_db)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_node16_to_n4_randomly, unodb::benchmark::olc_db)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);

UNODB_BENCHMARK_MAIN();
