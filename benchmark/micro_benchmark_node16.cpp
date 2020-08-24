// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <cstdint>
#include <random>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark.hpp"

namespace {

// Benchmark growing Node4 to Node16: insert to dense Node4 tree first:
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
  unodb::benchmark::growing_tree_node_stats<unodb::db> growing_tree_stats;
  std::size_t tree_size = 0;

  const auto node4_node_count = static_cast<std::uint64_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    unodb::key key_limit = unodb::benchmark::insert_sequentially(
        test_db, node4_node_count * 4,
        unodb::benchmark::dense_node4_key_zero_bits);
    benchmark::ClobberMemory();
    state.ResumeTiming();

    unodb::benchmark::grow_dense_node4_to_minimal_node16(test_db, key_limit);

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

// Benchmark growing Node4 to Node16: insert to dense Node4 tree first. Use 1,
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

inline constexpr auto number_to_dense_node4_with_holes_key(
    std::uint64_t i) noexcept {
  assert(i / (4 * 4 * 4 * 4 * 4 * 4 * 4) < 4);
  return (i % 4 * 2 + 1) | ((i / 4 % 4 * 2 + 1) << 8) |
         ((i / (4 * 4) % 4 * 2 + 1) << 16) |
         ((i / (4 * 4 * 4) % 4 * 2 + 1) << 24) |
         ((i / (4 * 4 * 4 * 4) % 4 * 2 + 1) << 32) |
         ((i / (4 * 4 * 4 * 4 * 4) % 4 * 2 + 1) << 40) |
         ((i / (4 * 4 * 4 * 4 * 4 * 4) % 4 * 2 + 1) << 48) |
         ((i / (4 * 4 * 4 * 4 * 4 * 4 * 4) % 4 * 2 + 1) << 56);
}

std::random_device rd;
std::mt19937 gen{rd()};

inline auto rnd_even_0_8() {
  static std::uniform_int_distribution<std::uint8_t> random_key_dist{0, 4ULL};

  const auto result = static_cast<std::uint8_t>(random_key_dist(gen) * 2);
  assert(result <= 8);
  return result;
}

inline std::uint8_t min_node16_over_dense_node4_lead_key_byte(std::uint8_t i) {
  assert(i < 5);
  return (i < 4) ? static_cast<std::uint8_t>(i * 2 + 1) : rnd_even_0_8();
}

std::vector<unodb::key> generate_random_minimal_node16_over_dense_node4_keys(
    unodb::key key_limit) noexcept {
  std::vector<unodb::key> result;
  union {
    std::uint64_t as_int;
    std::array<std::uint8_t, 8> as_bytes;
  } key;

  for (std::uint8_t i = 0; i < 5; ++i) {
    key.as_bytes[7] = min_node16_over_dense_node4_lead_key_byte(i);
    for (std::uint8_t i2 = 0; i2 < 5; ++i2) {
      key.as_bytes[6] = min_node16_over_dense_node4_lead_key_byte(i2);
      for (std::uint8_t i3 = 0; i3 < 5; ++i3) {
        key.as_bytes[5] = min_node16_over_dense_node4_lead_key_byte(i3);
        for (std::uint8_t i4 = 0; i4 < 5; ++i4) {
          key.as_bytes[4] = min_node16_over_dense_node4_lead_key_byte(i4);
          for (std::uint8_t i5 = 0; i5 < 5; ++i5) {
            key.as_bytes[3] = min_node16_over_dense_node4_lead_key_byte(i5);
            for (std::uint8_t i6 = 0; i6 < 5; ++i6) {
              key.as_bytes[2] = min_node16_over_dense_node4_lead_key_byte(i6);
              for (std::uint8_t i7 = 0; i7 < 5; ++i7) {
                key.as_bytes[1] = min_node16_over_dense_node4_lead_key_byte(i7);
                key.as_bytes[0] = rnd_even_0_8();
                const unodb::key k = key.as_int;
                if (k > key_limit) {
                  std::shuffle(result.begin(), result.end(), gen);
                  return result;
                }
                result.push_back(k);
              }
            }
          }
        }
      }
    }
  }
  cannot_happen();
}

void grow_node4_to_node16_randomly(benchmark::State &state) {
  std::size_t tree_size = 0;
  const auto node4_node_count = static_cast<std::uint64_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    unodb::db test_db;
    unodb::key key_limit = 0;
    for (std::uint64_t i = 0; i < node4_node_count * 4; ++i) {
      const unodb::key insert_key = number_to_dense_node4_with_holes_key(i);
      unodb::benchmark::insert_key(
          test_db, insert_key, unodb::value_view{unodb::benchmark::value100});
      key_limit = insert_key;
    }
    unodb::benchmark::assert_node4_only_tree(test_db);

    const auto node16_keys =
        generate_random_minimal_node16_over_dense_node4_keys(key_limit);
    state.ResumeTiming();

    for (const auto k : node16_keys) {
      unodb::benchmark::insert_key(
          test_db, k, unodb::value_view{unodb::benchmark::value100});
    }

    state.PauseTiming();
    unodb::benchmark::assert_mostly_node16_tree(test_db);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

}  // namespace

BENCHMARK(grow_node4_to_node16_sequentially)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(grow_node4_to_node16_randomly)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
