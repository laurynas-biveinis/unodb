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

constexpr auto minimal_node4_key_zero_bits = 0xFEFEFEFE'FEFEFEFEULL;

std::vector<unodb::key> make_n_key_sequence(std::size_t n,
                                            std::uint64_t key_zero_bits) {
  std::vector<unodb::key> result;
  result.reserve(n);

  unodb::key insert_key = 0;
  for (decltype(n) i = 0; i < n; ++i) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(insert_key, key_zero_bits);
  }

  return result;
}

std::vector<unodb::key> make_limited_key_sequence(unodb::key limit,
                                                  std::uint64_t key_zero_bits) {
  std::vector<unodb::key> result;

  unodb::key insert_key = 0;
  while (insert_key < limit) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(insert_key, key_zero_bits);
  }

  result.shrink_to_fit();
  return result;
}

void node4_sequential_insert(benchmark::State &state,
                             std::uint64_t key_zero_bits) {
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::insert_sequentially(
        test_db, static_cast<std::uint64_t>(state.range(0)), key_zero_bits);

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
  node4_sequential_insert(state,
                          unodb::benchmark::full_node4_tree_key_zero_bits);
}

void minimal_node4_sequential_insert(benchmark::State &state) {
  node4_sequential_insert(state, minimal_node4_key_zero_bits);
}

void node4_random_insert(benchmark::State &state, std::uint64_t key_zero_bits) {
  auto keys = make_n_key_sequence(static_cast<std::size_t>(state.range(0)),
                                  key_zero_bits);

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
  node4_random_insert(state, unodb::benchmark::full_node4_tree_key_zero_bits);
}

void minimal_node4_random_insert(benchmark::State &state) {
  node4_random_insert(state, minimal_node4_key_zero_bits);
}

void node4_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<unodb::db, 4>(
      state, unodb::benchmark::full_node4_tree_key_zero_bits);
}

void node4_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<unodb::db, 4>(
      state, unodb::benchmark::full_node4_tree_key_zero_bits);
}

void node4_sequential_delete_benchmark(benchmark::State &state,
                                       std::uint64_t insert_key_zero_bits,
                                       std::uint64_t delete_key_zero_bits) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  int keys_deleted{0};
  std::size_t tree_size{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = unodb::benchmark::insert_sequentially(
        test_db, number_of_keys, insert_key_zero_bits);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::assert_node4_only_tree(test_db);
    state.ResumeTiming();

    unodb::key k = 0;
    keys_deleted = 0;
    while (k < key_limit) {
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
  node4_sequential_delete_benchmark(
      state, unodb::benchmark::full_node4_tree_key_zero_bits,
      unodb::benchmark::full_node4_tree_key_zero_bits);
}

void node4_random_delete_benchmark(benchmark::State &state,
                                   std::uint64_t insert_key_zero_bits,
                                   std::uint64_t delete_key_zero_bits) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  std::size_t tree_size{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = unodb::benchmark::insert_sequentially(
        test_db, number_of_keys, insert_key_zero_bits);
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
  node4_random_delete_benchmark(
      state, unodb::benchmark::full_node4_tree_key_zero_bits,
      unodb::benchmark::full_node4_tree_key_zero_bits);
}

constexpr auto minimal_node4_tree_full_leaf_level_key_zero_bits =
    0xFCFCFCFC'FCFCFCFEULL;

void full_node4_to_minimal_sequential_delete(benchmark::State &state) {
  node4_sequential_delete_benchmark(
      state, unodb::benchmark::full_node4_tree_key_zero_bits,
      minimal_node4_tree_full_leaf_level_key_zero_bits);
}

void full_node4_to_minimal_random_delete(benchmark::State &state) {
  node4_random_delete_benchmark(
      state, unodb::benchmark::full_node4_tree_key_zero_bits,
      minimal_node4_tree_full_leaf_level_key_zero_bits);
}

// Benchmark shrinking Node16 to Node4: insert to minimal Node16 first:
// 0x0000000000000000 to ...004
// 0x0000000000000100 to ...104
// 0x0000000000000200 to ...204
// 0x0000000000000300 to ...304
// 0x0000000000000404 (note that no 0x0400..0x403 to avoid creating Node4).
//
// Then remove the minimal Node16 over full Node4 key subset, see
// number_to_minimal_node16_over_node4_key.

void shrink_node16_to_node4_sequentially(benchmark::State &state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<
      unodb::db, 4, unodb::benchmark::full_node4_tree_key_zero_bits>(
      state, unodb::benchmark::number_to_minimal_leaf_node16_over_node4_key);
}

void shrink_node16_to_node4_randomly(benchmark::State &state) {
  unodb::benchmark::shrink_node_randomly_benchmark<unodb::db, 4>(
      state, unodb::benchmark::number_to_full_node4_with_gaps_key);
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
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_random_deletes)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_to_minimal_sequential_delete)
    ->Range(100, 65533)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_to_minimal_random_delete)
    ->Range(100, 65533)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node16_to_node4_sequentially)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node16_to_node4_randomly)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
