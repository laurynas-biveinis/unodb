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

constexpr auto sparse_node4_key_zero_bits = 0xFEFEFEFE'FEFEFEFEULL;

// In case it's needed later:
// constexpr auto sparse_single_node4_mask = 0x1;
constexpr auto dense_single_node4_mask = 0x3;

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
  assert((result & unodb::benchmark::dense_node4_key_zero_bits) == 0);
  return result;
}

std::vector<unodb::key> generate_random_key_sequence(
    std::size_t count, std::uint64_t key_zero_bits) {
  std::vector<unodb::key> result;
  result.reserve(count);

  unodb::key insert_key = 0;
  for (decltype(count) i = 0; i < count; ++i) {
    result.push_back(insert_key);
    insert_key = unodb::benchmark::next_key(insert_key, key_zero_bits);
  }

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
  node4_sequential_insert(state, unodb::benchmark::dense_node4_key_zero_bits);
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

    unodb::benchmark::insert_keys(test_db, keys);

    state.PauseTiming();
    unodb::benchmark::assert_node4_only_tree(test_db);
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

void full_node4_random_insert(benchmark::State &state) {
  node4_random_insert(state, unodb::benchmark::dense_node4_key_zero_bits);
}

void sparse_node4_random_insert(benchmark::State &state) {
  node4_random_insert(state, sparse_node4_key_zero_bits);
}

void node4_full_scan(benchmark::State &state) {
  unodb::db test_db;
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));

  unodb::benchmark::insert_sequentially(
      test_db, number_of_keys, unodb::benchmark::dense_node4_key_zero_bits);

  for (auto _ : state) {
    unodb::key k = 0;
    for (std::uint64_t j = 0; j < number_of_keys; ++j) {
      unodb::benchmark::get_existing_key(test_db, k);
      k = unodb::benchmark::next_key(
          k, unodb::benchmark::dense_node4_key_zero_bits);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

void node4_random_gets(benchmark::State &state) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  unodb::benchmark::batched_prng random_key_positions{number_of_keys - 1};
  unodb::db test_db;
  unodb::benchmark::insert_sequentially(
      test_db, number_of_keys, unodb::benchmark::dense_node4_key_zero_bits);

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
    unodb::benchmark::insert_sequentially(
        test_db, number_of_keys, unodb::benchmark::dense_node4_key_zero_bits);
    state.ResumeTiming();

    unodb::key k = 0;
    for (std::uint64_t j = 0; j < number_of_keys; ++j) {
      unodb::benchmark::delete_key(test_db, k);
      k = unodb::benchmark::next_key(
          k, unodb::benchmark::dense_node4_key_zero_bits);
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
  auto keys = generate_random_key_sequence(
      number_of_keys, unodb::benchmark::dense_node4_key_zero_bits);

  for (auto _ : state) {
    state.PauseTiming();
    std::shuffle(keys.begin(), keys.end(), gen);
    unodb::db test_db;
    unodb::benchmark::insert_sequentially(
        test_db, number_of_keys, unodb::benchmark::dense_node4_key_zero_bits);
    state.ResumeTiming();

    unodb::benchmark::delete_keys(test_db, keys);

    assert(test_db.empty());
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
}

// Benchmark shrinking Node16 to Node4: insert to minimal Node16 first:
// 0x0000000000000000 to ...004
// 0x0000000000000100 to ...104
// 0x0000000000000200 to ...204
// 0x0000000000000300 to ...304
// 0x0000000000000404 (note that no 0x0400..0x403, these would create sparse but
// not minimal Node16 tree).
//
// Then remove the minimal Node16 over dense Node4 key subset, see
// number_to_minimal_node16_over_node4_key.

class shrinking_tree_node_stats final {
 public:
  void get(const unodb::db &test_db) noexcept {
    inode16_to_inode4_count = test_db.get_inode16_to_inode4_count();
  }

  void publish(::benchmark::State &state) const noexcept {
    state.counters["16v"] = static_cast<double>(inode16_to_inode4_count);
  }

 private:
  std::uint64_t inode16_to_inode4_count{0};
};

void shrink_node16_to_node4_sequentially(benchmark::State &state) {
  shrinking_tree_node_stats shrinking_tree_stats;
  std::size_t tree_size = 0;
  std::uint64_t removed_key_count{0};

  const auto node4_insert_count = static_cast<std::uint64_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    unodb::key key_limit = unodb::benchmark::insert_sequentially(
        test_db, node4_insert_count,
        unodb::benchmark::dense_node4_key_zero_bits);

    const auto n4_to_n16_keys_inserted =
        unodb::benchmark::grow_dense_node4_to_minimal_leaf_node16(test_db,
                                                                  key_limit);

    tree_size = test_db.get_current_memory_use();
    state.ResumeTiming();

    for (removed_key_count = 0; removed_key_count < n4_to_n16_keys_inserted;
         ++removed_key_count) {
      const auto remove_key =
          unodb::benchmark::number_to_minimal_leaf_node16_over_node4_key(
              removed_key_count);
      unodb::benchmark::delete_key(test_db, remove_key);
    }

    state.PauseTiming();
#ifndef NDEBUG
    assert(removed_key_count == test_db.get_inode16_to_inode4_count());
    unodb::benchmark::assert_node4_only_tree(test_db);
#endif
    shrinking_tree_stats.get(test_db);
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(
      static_cast<std::int64_t>(state.iterations() * removed_key_count));
  shrinking_tree_stats.publish(state);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void shrink_node16_to_node4_randomly(benchmark::State &state) {
  shrinking_tree_node_stats shrinking_tree_stats;
  std::size_t tree_size{0};
  std::uint64_t removed_key_count{0};

  const auto node4_insert_count = static_cast<unsigned>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = unodb::benchmark::make_node4_tree_with_gaps(
        test_db, node4_insert_count);

#ifndef NDEBUG
    const auto node4_created_count = test_db.get_created_inode4_count();
#endif
    const auto node16_keys =
        unodb::benchmark::generate_random_minimal_node16_over_dense_node4_keys(
            key_limit);
    unodb::benchmark::insert_keys(test_db, node16_keys);
    assert(node4_created_count == test_db.get_created_inode4_count());
    tree_size = test_db.get_current_memory_use();
    state.ResumeTiming();

    unodb::benchmark::delete_keys(test_db, node16_keys);

    state.PauseTiming();
#ifndef NDEBUG
    unodb::benchmark::assert_node4_only_tree(test_db);
    assert(test_db.get_inode16_to_inode4_count() == node16_keys.size());
#endif
    removed_key_count = node16_keys.size();
    shrinking_tree_stats.get(test_db);
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(
      static_cast<std::int64_t>(state.iterations() * removed_key_count));
  shrinking_tree_stats.publish(state);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
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
BENCHMARK(shrink_node16_to_node4_sequentially)
    ->Range(100, 65535)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(shrink_node16_to_node4_randomly)
    ->Range(100, 65532)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
