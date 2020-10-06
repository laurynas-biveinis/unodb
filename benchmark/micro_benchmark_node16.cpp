// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <cstdint>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

// Benchmark growing Node4 to Node16: insert to full Node4 tree first:
// 0x0000000000000000 to ...003
// 0x0000000000000100 to ...103
// 0x0000000000000200 to ...203
// 0x0000000000000300 to ...303
// 0x0000000000010000 to ...003
// 0x0000000000010100 to ...103
// ...
// Then insert in the gaps a "base-5" value that varies each byte from 0 to 5
// with the last one being a constant 4 to get a minimal Node16 tree:
// 0x0000000000000004
// 0x0000000000000104
// 0x0000000000000204
// 0x0000000000000304
// 0x0000000000000404
// 0x0000000000010004
// 0x0000000000010104
// ...

void grow_node4_to_node16_sequentially(benchmark::State &state) {
  std::size_t tree_size = 0;

  const auto node4_node_count = static_cast<std::uint64_t>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    unodb::key key_limit = unodb::benchmark::insert_sequentially(
        test_db, node4_node_count * 4,
        unodb::benchmark::full_node4_tree_key_zero_bits);
    unodb::benchmark::assert_node4_only_tree(test_db);
    benchmark::ClobberMemory();
    state.ResumeTiming();

    benchmark_keys_inserted =
        unodb::benchmark::grow_full_node4_to_minimal_leaf_node16(test_db,
                                                                 key_limit);

    state.PauseTiming();
    assert(benchmark_keys_inserted == test_db.get_inode4_to_inode16_count());
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

// Benchmark growing Node4 to Node16: insert to full Node4 tree first. Use 1,
// 3, 5, 7 as the different key byte values, so that a new byte could be
// inserted later at any position.
// 0x0101010101010101 to ...107
// 0x0101010101010301 to ...307
// 0x0101010101010501 to ...507
// 0x0101010101010701 to ...707
// 0x0101010101030101 to ...107
// 0x0101010101030301 to ...307
// ...
// Then in the gaps insert a value that has the last byte randomly chosen from
// 0, 2, 4, 6, and 8, and leading bytes enumerating through 1, 3, 5, 7, and one
// randomly-selected value from 0, 2, 4, 6, and 8.

void grow_node4_to_node16_randomly(benchmark::State &state) {
  std::size_t tree_size = 0;
  const auto node4_node_count = static_cast<unsigned>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = unodb::benchmark::make_node4_tree_with_gaps(
        test_db, node4_node_count * 4);

    const auto node16_keys =
        unodb::benchmark::generate_random_minimal_node16_over_full_node4_keys(
            key_limit);
    state.ResumeTiming();

    unodb::benchmark::insert_keys(test_db, node16_keys);

    state.PauseTiming();
    benchmark_keys_inserted = node16_keys.size();
    assert(test_db.get_inode4_to_inode16_count() == benchmark_keys_inserted);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

// Minimal Node16 tree sequential keys: "base-5" values that vary each byte from
// 0 to 5.
inline constexpr auto number_to_minimal_node16_key(std::uint64_t i) noexcept {
  return unodb::benchmark::to_base_n_value<5>(i);
}

// Full leaf layer Node16 tree over minimal Node16 sequential keys: "base-5"
// values that vary each byte from 0 to 5 with the last byte being varied from 6
// to 16.
inline constexpr auto number_to_full_leaf_over_minimal_node16_key(
    std::uint64_t i) noexcept {
  assert(i / (11 * 5 * 5 * 5 * 5 * 5 * 5) < 5);
  return ((i % 11) + 6) | ((i / 11 % 5) << 8) | ((i / (11 * 5) % 5) << 16) |
         ((i / (11 * 5 * 5) % 5) << 24) | ((i / (11 * 5 * 5 * 5) % 5) << 32) |
         ((i / (11 * 5 * 5 * 5 * 5) % 5) << 40) |
         ((i / (11 * 5 * 5 * 5 * 5 * 5) % 5) << 48) |
         ((i / (11 * 5 * 5 * 5 * 5 * 5 * 5) % 5) << 56);
}

auto make_minimal_node16_tree(unodb::db &db, unsigned node16_node_count) {
  unodb::key key_limit{0};
  for (unsigned i = 0; i < node16_node_count * 5; ++i) {
    key_limit = number_to_minimal_node16_key(i);
    unodb::benchmark::insert_key(db, key_limit,
                                 unodb::value_view{unodb::benchmark::value100});
  }
  unodb::benchmark::assert_mostly_node16_tree(db);
  return key_limit;
}

void node16_sequential_add(benchmark::State &state) {
  std::size_t tree_size{0};
  const auto node16_node_count = static_cast<unsigned>(state.range(0));
  std::int64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = make_minimal_node16_tree(test_db, node16_node_count);
    const unodb::benchmark::tree_shape_snapshot<unodb::db> tree_shape{test_db};
    state.ResumeTiming();

    std::uint64_t i = 0;
    while (true) {
      unodb::key k = number_to_full_leaf_over_minimal_node16_key(i);
      unodb::benchmark::insert_key(
          test_db, k, unodb::value_view{unodb::benchmark::value100});
      if (k > key_limit) {
        benchmark_keys_inserted = static_cast<std::int64_t>(i);
        break;
      }
      ++i;
    }

    state.PauseTiming();
#ifndef NDEBUG
    unodb::benchmark::assert_mostly_node16_tree(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          benchmark_keys_inserted);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

template <typename NumberToKeyFn>
auto generate_keys_up_to_limit(unodb::key key_limit,
                               NumberToKeyFn number_to_key_fn) {
  std::vector<unodb::key> result;
  std::uint64_t i = 0;
  while (true) {
    const auto key = number_to_key_fn(i);
    if (key > key_limit) break;
    ++i;
    result.push_back(key);
  }
  return result;
}

void node16_random_add(benchmark::State &state) {
  std::size_t tree_size{0};
  const auto node16_node_count = static_cast<unsigned>(state.range(0));
  std::int64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = make_minimal_node16_tree(test_db, node16_node_count);
    const unodb::benchmark::tree_shape_snapshot<unodb::db> tree_shape{test_db};
    std::vector<unodb::key> insert_keys = generate_keys_up_to_limit(
        key_limit, number_to_full_leaf_over_minimal_node16_key);
    std::shuffle(insert_keys.begin(), insert_keys.end(),
                 unodb::benchmark::get_prng());
    state.ResumeTiming();

    unodb::benchmark::insert_keys(test_db, insert_keys);

    state.PauseTiming();
#ifndef NDEBUG
    unodb::benchmark::assert_mostly_node16_tree(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
    benchmark_keys_inserted = static_cast<std::int64_t>(insert_keys.size());
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          benchmark_keys_inserted);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void minimal_node16_tree_full_scan(benchmark::State &state) {
  const auto node16_node_count = static_cast<unsigned>(state.range(0));
  unodb::db test_db;
  const auto key_limit = make_minimal_node16_tree(test_db, node16_node_count);
  const auto tree_size = test_db.get_current_memory_use();
  std::int64_t items_processed = 0;

  for (auto _ : state) {
    std::uint64_t i = 0;
    while (true) {
      unodb::key k = number_to_minimal_node16_key(i);
      if (k > key_limit) break;
      unodb::benchmark::get_existing_key(test_db, k);
      ++i;
    }
    items_processed += static_cast<std::int64_t>(i);
  }

  state.SetItemsProcessed(items_processed);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void minimal_node16_tree_random_gets(benchmark::State &state) {
  const auto node16_node_count = static_cast<unsigned>(state.range(0));
  unodb::db test_db;
  const auto key_limit USED_IN_DEBUG =
      make_minimal_node16_tree(test_db, node16_node_count);
  assert(number_to_minimal_node16_key(node16_node_count * 5 - 1) == key_limit);
  const auto tree_size = test_db.get_current_memory_use();
  unodb::benchmark::batched_prng random_key_positions{node16_node_count * 5 -
                                                      1};
  std::int64_t items_processed = 0;

  for (auto _ : state) {
    const auto key_index = random_key_positions.get(state);
    const auto key = number_to_minimal_node16_key(key_index);
    unodb::benchmark::get_existing_key(test_db, key);
    ++items_processed;
  }

  state.SetItemsProcessed(items_processed);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

inline constexpr auto full_node16_key_zero_bits = 0xF0F0F0F0'F0F0F0F0U;

void full_node16_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<unodb::db>(
      state, full_node16_key_zero_bits);
}

void full_node16_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<unodb::db, 16>(
      state, full_node16_key_zero_bits);
}

// Benchmark Node16 delete operation: insert to a full Node16 first, then delete
// the keys with the last byte value ranging from 5 to 15.

inline constexpr auto number_to_full_node16_delete_key(
    std::uint64_t i) noexcept {
  assert(i / (10 * 5 * 5 * 5 * 5 * 5) < 5);
  return (i % 10 + 5) | ((i / 10 % 5) << 8) | ((i / (10 * 5) % 5) << 16) |
         ((i / (10 * 5 * 5) % 5) << 24) | ((i / (10 * 5 * 5 * 5) % 5) << 32) |
         ((i / (10 * 5 * 5 * 5 * 5) % 5) << 40) |
         ((i / (10 * 5 * 5 * 5 * 5 * 5) % 5) << 48) |
         ((i / (10 * 5 * 5 * 5 * 5 * 5 * 5) % 5) << 56);
}

void full_node16_tree_sequential_delete(benchmark::State &state) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  int i{0};
  std::size_t tree_size{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = unodb::benchmark::insert_sequentially(
        test_db, number_of_keys, full_node16_key_zero_bits);
    tree_size = test_db.get_current_memory_use();
    const unodb::benchmark::tree_shape_snapshot<unodb::db> tree_shape{test_db};
    state.ResumeTiming();

    i = 0;
    while (true) {
      const auto delete_key =
          number_to_full_node16_delete_key(static_cast<std::uint64_t>(i));
      if (delete_key > key_limit) break;
      unodb::benchmark::delete_key(test_db, delete_key);
      ++i;
    }

    state.PauseTiming();
#ifndef NDEBUG
    unodb::benchmark::assert_mostly_node16_tree(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * i);
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

void full_node16_tree_random_delete(benchmark::State &state) {
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  std::size_t tree_size{0};
  std::size_t remove_key_count{0};

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    const auto key_limit = unodb::benchmark::insert_sequentially(
        test_db, number_of_keys, full_node16_key_zero_bits);
    tree_size = test_db.get_current_memory_use();
    const unodb::benchmark::tree_shape_snapshot<unodb::db> tree_shape{test_db};
    auto remove_keys =
        generate_keys_up_to_limit(key_limit, number_to_full_node16_delete_key);
    remove_key_count = remove_keys.size();
    std::shuffle(remove_keys.begin(), remove_keys.end(),
                 unodb::benchmark::get_prng());
    state.ResumeTiming();

    unodb::benchmark::delete_keys(test_db, remove_keys);

    state.PauseTiming();
#ifndef NDEBUG
    unodb::benchmark::assert_mostly_node16_tree(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    unodb::benchmark::destroy_tree(test_db, state);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(remove_key_count));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

}  // namespace

BENCHMARK(grow_node4_to_node16_sequentially)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node4_to_node16_randomly)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node16_sequential_add)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(node16_random_add)->Range(10, 16383)->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node16_tree_full_scan)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(minimal_node16_tree_random_gets)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_full_scan)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_random_gets)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_sequential_delete)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(full_node16_tree_random_delete)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_MAIN();
