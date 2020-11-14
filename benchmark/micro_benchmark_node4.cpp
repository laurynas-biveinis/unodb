// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "art_common.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

template <unsigned NodeSize>
std::vector<unodb::key> make_n_key_sequence(std::size_t n) {
  std::vector<unodb::key> result;
  result.reserve(n);

  unodb::key insert_key = 0;
  for (decltype(n) i = 0; i < n; ++i) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(
        insert_key, unodb::benchmark::node_size_to_key_zero_bits<NodeSize>());
  }

  return result;
}

std::vector<unodb::key> make_limited_key_sequence(unodb::key limit,
                                                  std::uint64_t key_zero_bits) {
  std::vector<unodb::key> result;

  unodb::key insert_key = 0;
  while (insert_key <= limit) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(insert_key, key_zero_bits);
  }

  result.shrink_to_fit();
  return result;
}

template <unsigned NodeSize>
void node4_sequential_insert(benchmark::State &state) {
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::insert_sequentially<unodb::db, NodeSize>(
        test_db, static_cast<unsigned>(state.range(0)));

    state.PauseTiming();
    unodb::benchmark::assert_node4_only_tree(test_db);
    growing_tree_stats.get(test_db);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
  growing_tree_stats.publish(state);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void full_node4_sequential_insert(benchmark::State &state) {
  node4_sequential_insert<4>(state);
}

void minimal_node4_sequential_insert(benchmark::State &state) {
  node4_sequential_insert<2>(state);
}

template <unsigned NodeSize>
void node4_random_insert(benchmark::State &state) {
  auto keys =
      make_n_key_sequence<NodeSize>(static_cast<std::size_t>(state.range(0)));

  for (auto _ : state) {
    state.PauseTiming();
    std::shuffle(keys.begin(), keys.end(), unodb::benchmark::get_prng());
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::insert_keys(test_db, keys);

    state.PauseTiming();
    unodb::benchmark::assert_node4_only_tree(test_db);
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

void full_node4_random_insert(benchmark::State &state) {
  node4_random_insert<4>(state);
}

void minimal_node4_random_insert(benchmark::State &state) {
  node4_random_insert<2>(state);
}

void node4_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<unodb::db, 4>(state);
}

void node4_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<unodb::db, 4>(state);
}

template <unsigned NodeSize>
void node4_sequential_delete_benchmark(benchmark::State &state,
                                       std::uint64_t delete_key_zero_bits) {
  const auto key_count = static_cast<unsigned>(state.range(0));
  int keys_deleted{0};
  std::size_t tree_size{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit =
        unodb::benchmark::insert_sequentially<unodb::db, NodeSize>(test_db,
                                                                   key_count);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::assert_node4_only_tree(test_db);
    state.ResumeTiming();

    unodb::key k = 0;
    keys_deleted = 0;
    while (k <= key_limit) {
      unodb::benchmark::delete_key(test_db, k);
      ++keys_deleted;
      k = unodb::benchmark::next_key(k, delete_key_zero_bits);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          keys_deleted);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void full_node4_sequential_delete(benchmark::State &state) {
  node4_sequential_delete_benchmark<4>(
      state, unodb::benchmark::node_size_to_key_zero_bits<4>());
}

template <unsigned NodeSize>
void node4_random_delete_benchmark(benchmark::State &state,
                                   std::uint64_t delete_key_zero_bits) {
  const auto key_count = static_cast<unsigned>(state.range(0));
  std::size_t tree_size{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit =
        unodb::benchmark::insert_sequentially<unodb::db, NodeSize>(test_db,
                                                                   key_count);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::assert_node4_only_tree(test_db);

    auto keys = make_limited_key_sequence(key_limit, delete_key_zero_bits);
    std::shuffle(keys.begin(), keys.end(), unodb::benchmark::get_prng());
    state.ResumeTiming();

    unodb::benchmark::delete_keys(test_db, keys);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void full_node4_random_deletes(benchmark::State &state) {
  node4_random_delete_benchmark<4>(
      state, unodb::benchmark::node_size_to_key_zero_bits<4>());
}

constexpr auto minimal_node4_tree_full_leaf_level_key_zero_bits =
    0xFCFCFCFC'FCFCFCFEULL;

void full_node4_to_minimal_sequential_delete(benchmark::State &state) {
  node4_sequential_delete_benchmark<4>(
      state, minimal_node4_tree_full_leaf_level_key_zero_bits);
}

void full_node4_to_minimal_random_delete(benchmark::State &state) {
  node4_random_delete_benchmark<4>(
      state, minimal_node4_tree_full_leaf_level_key_zero_bits);
}

void shrink_node16_to_node4_sequentially(benchmark::State &state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<unodb::db, 4>(state);
}

void shrink_node16_to_node4_randomly(benchmark::State &state) {
  unodb::benchmark::shrink_node_randomly_benchmark<unodb::db, 4>(state);
}

}  // namespace

// A maximum Node4-only tree can hold 65K values
BENCHMARK(full_node4_sequential_insert)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_random_insert)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node4_sequential_insert)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node4_random_insert)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node4_full_scan)->Range(100, 65535)->Unit(benchmark::kMicrosecond);
BENCHMARK(node4_random_gets)->Range(100, 65535)->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_sequential_delete)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_random_deletes)
    ->Range(100, 65534)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_to_minimal_sequential_delete)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_to_minimal_random_delete)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node16_to_node4_sequentially)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node16_to_node4_randomly)
    ->Range(25, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
