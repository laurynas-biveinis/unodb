// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "art_common.hpp"
#include "micro_benchmark.hpp"

namespace {

constexpr auto dense_node4_key_zero_bits = 0xFCFCFCFC'FCFCFCFCULL;
constexpr auto sparse_node4_key_zero_bits = 0xFEFEFEFE'FEFEFEFEULL;

// In case it's needed later:
// constexpr auto sparse_single_node4_mask = 0x1;
constexpr auto dense_single_node4_mask = 0x3;

constexpr inline auto next_node4_key(unodb::key k,
                                     std::uint64_t key_zero_bits) noexcept {
  assert((k & key_zero_bits) == 0);
  const auto result = ((k | key_zero_bits) + 1) & ~key_zero_bits;
  assert(result > k);
  return result;
}

inline auto number_to_node4_key(std::uint64_t i,
                                std::uint64_t one_node_mask) noexcept {
  const auto result = (i & one_node_mask) |
                      ((i & (one_node_mask << 2)) << (8 - 2)) |
                      ((i & (one_node_mask << 4)) << (16 - 4)) |
                      ((i & (one_node_mask << 6)) << (24 - 6)) |
                      ((i & (one_node_mask << 8)) << (32 - 8)) |
                      ((i & (one_node_mask << 10)) << (40 - 10)) |
                      ((i & (one_node_mask << 12)) << (48 - 12)) |
                      ((i & (one_node_mask << 14)) << (56 - 14));
  assert((result & dense_node4_key_zero_bits) == 0);
  return result;
}

void assert_node4_only_tree(const unodb::db &test_db USED_IN_DEBUG) noexcept {
  assert(test_db.get_inode16_count() == 0);
  assert(test_db.get_inode48_count() == 0);
  assert(test_db.get_inode256_count() == 0);
}

void insert_sequentially(unodb::db &db, std::uint64_t number_of_keys,
                         std::uint64_t key_zero_bits) {
  unodb::key insert_key = 0;
  for (decltype(number_of_keys) i = 0; i < number_of_keys; ++i) {
    unodb::benchmark::insert_key(db, insert_key,
                                 unodb::value_view{unodb::benchmark::value100});
    insert_key = next_node4_key(insert_key, key_zero_bits);
  }
  assert_node4_only_tree(db);
}

std::vector<unodb::key> generate_random_key_sequence(
    std::size_t count, std::uint64_t key_zero_bits) {
  std::vector<unodb::key> result;
  result.reserve(count);

  unodb::key insert_key = 0;
  for (decltype(count) i = 0; i < count; ++i) {
    result.push_back(insert_key);
    insert_key = next_node4_key(insert_key, key_zero_bits);
  }

  return result;
}

void node4_sequential_insert(benchmark::State &state,
                             std::uint64_t key_zero_bits) {
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db{1000ULL * 1000 * 1000 * 1000};
    benchmark::ClobberMemory();
    state.ResumeTiming();

    insert_sequentially(test_db, static_cast<std::uint64_t>(state.range(0)),
                        key_zero_bits);

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

void full_node4_sequential_insert(benchmark::State &state) {
  node4_sequential_insert(state, dense_node4_key_zero_bits);
}

void sparse_node4_sequential_insert(benchmark::State &state) {
  node4_sequential_insert(state, sparse_node4_key_zero_bits);
}

void node4_random_insert(benchmark::State &state, std::uint64_t key_zero_bits) {
  std::random_device rd;
  std::mt19937 gen{rd()};

  auto keys = generate_random_key_sequence(
      static_cast<std::size_t>(state.range(0)), key_zero_bits);

  for (auto _ : state) {
    state.PauseTiming();
    std::shuffle(keys.begin(), keys.end(), gen);
    unodb::db test_db;
    benchmark::ClobberMemory();
    state.ResumeTiming();

    for (const auto k : keys) {
      unodb::benchmark::insert_key(
          test_db, k, unodb::value_view{unodb::benchmark::value100});
    }

    state.PauseTiming();
    assert_node4_only_tree(test_db);
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

void full_node4_random_insert(benchmark::State &state) {
  node4_random_insert(state, dense_node4_key_zero_bits);
}

void sparse_node4_random_insert(benchmark::State &state) {
  node4_random_insert(state, sparse_node4_key_zero_bits);
}

void node4_full_scan(benchmark::State &state) {
  unodb::db test_db;
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));

  insert_sequentially(test_db, number_of_keys, dense_node4_key_zero_bits);

  for (auto _ : state) {
    unodb::key k = 0;
    for (std::uint64_t j = 0; j < number_of_keys; ++j) {
      unodb::benchmark::get_existing_key(test_db, k);
      k = next_node4_key(k, dense_node4_key_zero_bits);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

void node4_random_gets(benchmark::State &state) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  unodb::benchmark::batched_prng random_key_positions{number_of_keys - 1};
  unodb::db test_db;
  insert_sequentially(test_db, number_of_keys, dense_node4_key_zero_bits);

  for (auto _ : state) {
    for (std::uint64_t i = 0; i < number_of_keys; ++i) {
      const auto key_index = random_key_positions.get(state);
      const auto key = number_to_node4_key(key_index, dense_single_node4_mask);
      unodb::benchmark::get_existing_key(test_db, key);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}

void full_node4_sequential_delete(benchmark::State &state) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    insert_sequentially(test_db, number_of_keys, dense_node4_key_zero_bits);
    state.ResumeTiming();

    unodb::key k = 0;
    for (std::uint64_t j = 0; j < number_of_keys; ++j) {
      unodb::benchmark::delete_key(test_db, k);
      k = next_node4_key(k, dense_node4_key_zero_bits);
    }

    assert(test_db.empty());
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

void full_node4_random_deletes(benchmark::State &state) {
  std::random_device rd;
  std::mt19937 gen{rd()};

  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  auto keys =
      generate_random_key_sequence(number_of_keys, dense_node4_key_zero_bits);

  for (auto _ : state) {
    state.PauseTiming();
    std::shuffle(keys.begin(), keys.end(), gen);
    unodb::db test_db;
    insert_sequentially(test_db, number_of_keys, dense_node4_key_zero_bits);
    state.ResumeTiming();

    for (const auto k : keys) {
      unodb::benchmark::delete_key(test_db, k);
    }

    assert(test_db.empty());
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

}  // namespace

// A maximum Node4-only tree can hold 65K values
BENCHMARK(full_node4_sequential_insert)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node4_random_insert)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(sparse_node4_sequential_insert)
    ->Range(16, 255)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(sparse_node4_random_insert)
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

BENCHMARK_MAIN();
