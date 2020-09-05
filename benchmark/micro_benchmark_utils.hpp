// Copyright 2019-2020 Laurynas Biveinis
#ifndef MICRO_BENCHMARK_HPP_
#define MICRO_BENCHMARK_HPP_

#include "global.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#ifndef NDEBUG
#include <iostream>
#endif
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "art_common.hpp"

namespace unodb::benchmark {

// Values

constexpr auto value1 = std::array<std::byte, 1>{};
constexpr auto value10 = std::array<std::byte, 10>{};
constexpr auto value100 = std::array<std::byte, 100>{};
constexpr auto value1000 = std::array<std::byte, 1000>{};
constexpr auto value10000 = std::array<std::byte, 10000>{};

inline constexpr std::array<unodb::value_view, 5> values = {
    unodb::value_view{value1}, unodb::value_view{value10},
    unodb::value_view{value100}, unodb::value_view{value1000},
    unodb::value_view{value10000}};

// Key manipulation

constexpr inline auto dense_node4_key_zero_bits = 0xFCFCFCFC'FCFCFCFCULL;

constexpr inline auto next_key(unodb::key k,
                               std::uint64_t key_zero_bits) noexcept {
  assert((k & key_zero_bits) == 0);

  const auto result = ((k | key_zero_bits) + 1) & ~key_zero_bits;

  assert(result > k);
  assert((result & key_zero_bits) == 0);

  return result;
}

// Minimal Node16 tree keys over dense Node4 keys: "base-5" values that vary
// each byte from 0 to 5 with the last byte being a constant 0x04.
inline constexpr auto number_to_minimal_node16_over_node4_key(
    std::uint64_t i) noexcept {
  assert(i / (5 * 5 * 5 * 5 * 5 * 5) < 5);
  return 4ULL | ((i % 5) << 8) | ((i / 5 % 5) << 16) |
         ((i / (5 * 5) % 5) << 24) | ((i / (5 * 5 * 5) % 5) << 32) |
         ((i / (5 * 5 * 5 * 5) % 5) << 40) |
         ((i / (5 * 5 * 5 * 5 * 5) % 5) << 48) |
         ((i / (5 * 5 * 5 * 5 * 5 * 5) % 5) << 56);
}

// Dense Node4 tree keys with 1, 3, 5, & 7 as the different key byte values so
// that a new byte could be inserted later at any position:
// 0x0101010101010101 to ...107
// 0x0101010101010301 to ...307
// 0x0101010101010501 to ...507
// 0x0101010101010701 to ...707
// 0x0101010101030101 to ...107
// 0x0101010101030301 to ...307
inline constexpr auto number_to_dense_node4_with_gaps_key(
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

// Key vectors

std::vector<unodb::key> generate_random_minimal_node16_over_dense_node4_keys(
    unodb::key key_limit) noexcept;

// PRNG

class batched_prng final {
  using result_type = std::uint64_t;

 public:
  batched_prng(result_type max_value = std::numeric_limits<result_type>::max())
      : random_key_dist{0ULL, max_value} {
    refill();
  }

  auto get(::benchmark::State &state) {
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

  std::vector<result_type> random_keys{random_batch_size};
  decltype(random_keys)::const_iterator random_key_ptr;

  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<result_type> random_key_dist;
};

// Stats

template <class Db>
class growing_tree_node_stats final {
 public:
  void get(const Db &test_db) noexcept {
    leaf_count = test_db.get_leaf_count();
    inode4_count = test_db.get_inode4_count();
    inode16_count = test_db.get_inode16_count();
    inode48_count = test_db.get_inode48_count();
    inode256_count = test_db.get_inode256_count();
    created_inode4_count = test_db.get_created_inode4_count();
    inode4_to_inode16_count = test_db.get_inode4_to_inode16_count();
    inode16_to_inode48_count = test_db.get_inode16_to_inode48_count();
    inode48_to_inode256_count = test_db.get_inode48_to_inode256_count();
    key_prefix_splits = test_db.get_key_prefix_splits();
#ifndef NDEBUG
    get_called = true;
#endif
  }

  void publish(::benchmark::State &state) const noexcept {
    assert(get_called);
    state.counters["L"] = static_cast<double>(leaf_count);
    state.counters["4"] = static_cast<double>(inode4_count);
    state.counters["16"] = static_cast<double>(inode16_count);
    state.counters["48"] = static_cast<double>(inode48_count);
    state.counters["256"] = static_cast<double>(inode256_count);
    state.counters["+4"] = static_cast<double>(created_inode4_count);
    state.counters["4^"] = static_cast<double>(inode4_to_inode16_count);
    state.counters["16^"] = static_cast<double>(inode16_to_inode48_count);
    state.counters["48^"] = static_cast<double>(inode48_to_inode256_count);
    state.counters["KPfS"] = static_cast<double>(key_prefix_splits);
  }

 private:
  std::uint64_t leaf_count{0};
  std::uint64_t inode4_count{0};
  std::uint64_t inode16_count{0};
  std::uint64_t inode48_count{0};
  std::uint64_t inode256_count{0};
  std::uint64_t created_inode4_count{0};
  std::uint64_t inode4_to_inode16_count{0};
  std::uint64_t inode16_to_inode48_count{0};
  std::uint64_t inode48_to_inode256_count{0};
  std::uint64_t key_prefix_splits{0};

#ifndef NDEBUG
  bool get_called{false};
#endif
};

inline void set_size_counter(::benchmark::State &state,
                             const std::string &label, std::size_t value) {
  // state.SetLabel might be a better logical fit but the automatic k/M/G
  // suffix is too nice
  state.counters[label] = ::benchmark::Counter(
      static_cast<double>(value), ::benchmark::Counter::Flags::kDefaults,
      ::benchmark::Counter::OneK::kIs1024);
}

// Asserts

template <class Db>
void assert_node4_only_tree(const Db &test_db USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  if (test_db.get_inode16_count() > 0) {
    std::cerr << "I16 node found in I4-only tree:\n";
    test_db.dump(std::cerr);
    assert(test_db.get_inode16_count() == 0);
  }
  assert(test_db.get_inode16_count() == 0);
  assert(test_db.get_inode48_count() == 0);
  assert(test_db.get_inode256_count() == 0);
#endif
}

// In a mostly-Node16 tree a few Node4 are allowed on the rightmost tree edge
template <class Db>
void assert_mostly_node16_tree(const Db &test_db USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  if (test_db.get_inode4_count() > 8) {
    std::cerr << "Too many I4 nodes found in mostly-I16 tree:\n";
    test_db.dump(std::cerr);
    assert(test_db.get_inode4_count() <= 8);
  }
  assert(test_db.get_inode48_count() == 0);
  assert(test_db.get_inode256_count() == 0);
#endif
}

// Insertion

template <class Db>
void insert_key(Db &db, unodb::key k, unodb::value_view v) {
  const auto result USED_IN_DEBUG = db.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert key 0x" << std::hex << k << std::dec << '\n';
    std::cerr << "Current tree:";
    db.dump(std::cerr);
    assert(result);
  }
#endif
  ::benchmark::ClobberMemory();
}

template <class Db>
void insert_key_ignore_dups(Db &db, unodb::key k, unodb::value_view v) {
  (void)db.insert(k, v);
  ::benchmark::ClobberMemory();
}

template <class Db>
unodb::key insert_sequentially(Db &db, std::uint64_t number_of_keys,
                               std::uint64_t key_zero_bits) {
  unodb::key k = 0;
  for (decltype(number_of_keys) i = 0; i < number_of_keys; ++i) {
    insert_key(db, k, unodb::value_view{value100});
    k = next_key(k, key_zero_bits);
  }
  assert_node4_only_tree(db);
  return k;
}

template <class Db>
void insert_keys(Db &db, const std::vector<unodb::key> &keys) {
  for (const auto k : keys) {
    insert_key(db, k, unodb::value_view{value100});
  }
}

template <class Db>
auto grow_dense_node4_to_minimal_node16(Db &db, unodb::key key_limit) {
  assert_node4_only_tree(db);

  std::uint64_t i = 0;
  while (true) {
    unodb::key key = number_to_minimal_node16_over_node4_key(i);
    insert_key(db, key, unodb::value_view{value100});
    if (key > key_limit) break;
    ++i;
  }

  assert_mostly_node16_tree(db);
  return i;
}

template <class Db>
auto make_node4_tree_with_gaps(Db &db, unsigned number_of_keys) {
  unodb::key k{0};
  for (unsigned i = 0; i < number_of_keys; ++i) {
    k = number_to_dense_node4_with_gaps_key(i);
    insert_key(db, k, unodb::value_view{value100});
  }

  assert_node4_only_tree(db);
  return k;
}

// Gets

template <class Db>
void get_existing_key(const Db &db, unodb::key k) {
  const auto result = db.get(k);
  assert(result);
  ::benchmark::DoNotOptimize(result);
}

template <class Db>
void get_key(const Db &db, unodb::key k) {
  static_cast<void>(db.get(k));
}

// Deletes

template <class Db>
void delete_key(Db &db, unodb::key k) {
  const auto result USED_IN_DEBUG = db.remove(k);
  assert(result);
  ::benchmark::ClobberMemory();
}

template <class Db>
void delete_key_if_exists(Db &db, unodb::key k) {
  (void)db.remove(k);
  ::benchmark::ClobberMemory();
}

template <class Db>
void delete_keys(Db &db, const std::vector<unodb::key> &keys) {
  for (const auto k : keys) delete_key(db, k);
}

// Teardown

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state) noexcept {
  // Timer must be stopped on entry
  db.clear();
  ::benchmark::ClobberMemory();
  state.ResumeTiming();
}

}  // namespace unodb::benchmark

#endif  // MICRO_BENCHMARK_HPP_
