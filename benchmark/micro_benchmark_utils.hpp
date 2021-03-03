// Copyright 2019-2021 Laurynas Biveinis
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
#include <tuple>
#include <vector>

#include <benchmark/benchmark.h>

#include "art_common.hpp"

namespace unodb {
class db;
class mutex_db;
class olc_db;
}  // namespace unodb

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

// Key manipulation with key zero bits

template <unsigned NodeSize>
constexpr inline auto node_size_to_key_zero_bits() noexcept {
  static_assert(NodeSize == 2 || NodeSize == 4 || NodeSize == 16 ||
                NodeSize == 256);
  if constexpr (NodeSize == 2) {
    return 0xFEFEFEFE'FEFEFEFEULL;
  } else if constexpr (NodeSize == 4) {
    return 0xFCFCFCFC'FCFCFCFCULL;
  } else if constexpr (NodeSize == 16) {
    return 0xF0F0F0F0'F0F0F0F0ULL;
  }
  return 0ULL;
}

inline constexpr auto next_key(unodb::key k,
                               std::uint64_t key_zero_bits) noexcept {
  assert((k & key_zero_bits) == 0);

  const auto result = ((k | key_zero_bits) + 1) & ~key_zero_bits;

  assert(result > k);
  assert((result & key_zero_bits) == 0);

  return result;
}

// PRNG

inline auto &get_prng() {
  static std::random_device rd;
  static std::mt19937 gen{rd()};
  return gen;
}

class batched_prng final {
  using result_type = std::uint64_t;

 public:
  batched_prng(result_type max_value = std::numeric_limits<result_type>::max());

  auto get(::benchmark::State &state) {
    if (random_key_ptr == random_keys.cend()) {
      state.PauseTiming();
      refill();
      state.ResumeTiming();
    }
    return *(random_key_ptr++);
  }

 private:
  void refill();

  static constexpr auto random_batch_size = 10000;

  std::vector<result_type> random_keys{random_batch_size};
  decltype(random_keys)::const_iterator random_key_ptr;

  std::uniform_int_distribution<result_type> random_key_dist;
};

// Stats

template <class Db>
struct tree_stats final {
  constexpr tree_stats(void) noexcept = default;

  explicit constexpr tree_stats(const Db &test_db) noexcept
      : leaf_count{test_db.get_leaf_count()},
        inode4_count{test_db.get_inode4_count()},
        inode16_count{test_db.get_inode16_count()},
        inode48_count{test_db.get_inode48_count()},
        inode256_count{test_db.get_inode256_count()},
        created_inode4_count{test_db.get_created_inode4_count()},
        inode4_to_inode16_count{test_db.get_inode4_to_inode16_count()},
        inode16_to_inode48_count{test_db.get_inode16_to_inode48_count()},
        inode48_to_inode256_count{test_db.get_inode48_to_inode256_count()},
        key_prefix_splits{test_db.get_key_prefix_splits()} {}

  constexpr void get(const Db &test_db) noexcept {
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
  }

  constexpr bool operator==(const tree_stats<Db> &other) const noexcept {
    return leaf_count == other.leaf_count && internal_levels_equal(other);
  }

  constexpr bool internal_levels_equal(
      const tree_stats<Db> &other) const noexcept {
    return inode4_count == other.inode4_count &&
           inode16_count == other.inode16_count &&
           inode48_count == other.inode48_count &&
           inode256_count == other.inode256_count &&
           created_inode4_count == other.created_inode4_count &&
           inode4_to_inode16_count == other.inode4_to_inode16_count &&
           inode16_to_inode48_count == other.inode16_to_inode48_count &&
           inode48_to_inode256_count == other.inode48_to_inode256_count &&
           key_prefix_splits == other.key_prefix_splits;
  }

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
};

template <class Db>
class growing_tree_node_stats final {
 public:
  constexpr void get(const Db &test_db) noexcept {
    stats.get(test_db);
#ifndef NDEBUG
    get_called = true;
    db = &test_db;
#endif
  }

  constexpr void publish(::benchmark::State &state) const noexcept {
    assert(get_called);
    state.counters["L"] = static_cast<double>(stats.leaf_count);
    state.counters["4"] = static_cast<double>(stats.inode4_count);
    state.counters["16"] = static_cast<double>(stats.inode16_count);
    state.counters["48"] = static_cast<double>(stats.inode48_count);
    state.counters["256"] = static_cast<double>(stats.inode256_count);
    state.counters["+4"] = static_cast<double>(stats.created_inode4_count);
    state.counters["4^"] = static_cast<double>(stats.inode4_to_inode16_count);
    state.counters["16^"] = static_cast<double>(stats.inode16_to_inode48_count);
    state.counters["48^"] =
        static_cast<double>(stats.inode48_to_inode256_count);
    state.counters["KPfS"] = static_cast<double>(stats.key_prefix_splits);
  }

 private:
  tree_stats<Db> stats;
#ifndef NDEBUG
  bool get_called{false};
  const Db *db{nullptr};
#endif
};

template <class Db>
class shrinking_tree_node_stats final {
 public:
  constexpr void get(const Db &test_db) noexcept {
    inode16_to_inode4_count = test_db.get_inode16_to_inode4_count();
    inode48_to_inode16_count = test_db.get_inode48_to_inode16_count();
    inode256_to_inode48_count = test_db.get_inode256_to_inode48_count();
  }

  constexpr void publish(::benchmark::State &state) const noexcept {
    state.counters["16v"] = static_cast<double>(inode16_to_inode4_count);
    state.counters["48v"] = static_cast<double>(inode48_to_inode16_count);
    state.counters["256v"] = static_cast<double>(inode256_to_inode48_count);
  }

 private:
  std::uint64_t inode16_to_inode4_count{0};
  std::uint64_t inode48_to_inode16_count{0};
  std::uint64_t inode256_to_inode48_count{0};
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

#ifndef NDEBUG

namespace detail {

inline void print_key(std::ostream &out, unodb::key k) {
  out << "0x" << std::hex << k << std::dec;
}

}  // namespace detail

#endif

template <class Db>
void assert_node4_only_tree(const Db &test_db USED_IN_DEBUG) noexcept;

extern template void assert_node4_only_tree<unodb::db>(
    const unodb::db &) noexcept;

// Insertion

template <class Db>
void insert_key(Db &db, unodb::key k, unodb::value_view v) {
  const auto result USED_IN_DEBUG = db.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert key ";
    detail::print_key(std::cerr, k);
    std::cerr << "\nCurrent tree:";
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

template <class Db, unsigned NodeSize>
unodb::key insert_sequentially(Db &db, unsigned key_count) {
  unodb::key k = 0;
  decltype(key_count) i = 0;
  while (true) {
    insert_key(db, k, unodb::value_view{value100});
    if (i == key_count) break;
    ++i;
    k = next_key(k, node_size_to_key_zero_bits<NodeSize>());
  }
  return k;
}

template <class Db>
void insert_keys(Db &db, const std::vector<unodb::key> &keys) {
  for (const auto k : keys) {
    insert_key(db, k, unodb::value_view{value100});
  }
}

// Gets

template <class Db>
void get_existing_key(const Db &db, unodb::key k) {
  const auto result = db.get(k);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to get existing key ";
    detail::print_key(std::cerr, k);
    std::cerr << "\nTree:";
    db.dump(std::cerr);
    assert(result);
  }
#endif
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
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to delete existing key ";
    detail::print_key(std::cerr, k);
    std::cerr << "\nTree:";
    db.dump(std::cerr);
    assert(result);
  }
#endif
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
void destroy_tree(Db &db, ::benchmark::State &state) noexcept;

extern template void destroy_tree<unodb::db>(unodb::db &,
                                             ::benchmark::State &) noexcept;
extern template void destroy_tree<unodb::mutex_db>(
    unodb::mutex_db &, ::benchmark::State &) noexcept;
extern template void destroy_tree<unodb::olc_db>(unodb::olc_db &,
                                                 ::benchmark::State &) noexcept;

// Benchmarks

template <class Db, unsigned NodeSize>
void full_node_scan_benchmark(::benchmark::State &state);

extern template void full_node_scan_benchmark<unodb::db, 4>(
    ::benchmark::State &);
extern template void full_node_scan_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void full_node_scan_benchmark<unodb::db, 48>(
    ::benchmark::State &);
extern template void full_node_scan_benchmark<unodb::db, 256>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void full_node_random_get_benchmark(::benchmark::State &state);

extern template void full_node_random_get_benchmark<unodb::db, 4>(
    ::benchmark::State &);
extern template void full_node_random_get_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void full_node_random_get_benchmark<unodb::db, 48>(
    ::benchmark::State &);
extern template void full_node_random_get_benchmark<unodb::db, 256>(
    ::benchmark::State &);

// Benchmark e.g. growing Node4 to Node16: insert to full Node4 tree first:
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
// Node16 to Node48: insert to full Node16 tree first:
// 0x0000000000000000 to ...0000F
// 0x0000000000000100 to ...0010F
// ...
// 0x0000000000000F00 to ...00F0F
// 0x0000000000010000 to ...1000F
// ...
// 0x00000000000F0000 to ...F000F
// 0x0000000000010100 to ...1010F
// 0x0000000000010200 to ...1020F
// ...
// The insert in the gaps a "base-17" value with the last byte being a constant
// 10 to get a minimal Node48 tree:
// 0x0000000000000010
// 0x0000000000000110
// ...
// 0x0000000000000F10
// 0x0000000000010010
// ...

// Do not bother with extern templates due to large parameter space
template <class Db, unsigned SmallerNodeSize>
void grow_node_sequentially_benchmark(::benchmark::State &state);

extern template void grow_node_sequentially_benchmark<unodb::db, 4>(
    ::benchmark::State &);
extern template void grow_node_sequentially_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void grow_node_sequentially_benchmark<unodb::db, 48>(
    ::benchmark::State &);

// Benchmark e.g. growing Node4 to Node16: insert to full Node4 tree first. Use
// 1, 3, 5, 7 as the different key byte values, so that a new byte could be
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

// Do not bother with extern templates due to large parameter space
template <class Db, unsigned SmallerNodeSize>
void grow_node_randomly_benchmark(::benchmark::State &state);

extern template void grow_node_randomly_benchmark<unodb::db, 4>(
    ::benchmark::State &);
extern template void grow_node_randomly_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void grow_node_randomly_benchmark<unodb::db, 48>(
    ::benchmark::State &);

// Benchmark e.g. shrinking Node16 to Node4: insert to minimal Node16 first:
// 0x0000000000000000 to ...004
// 0x0000000000000100 to ...104
// 0x0000000000000200 to ...204
// 0x0000000000000300 to ...304
// 0x0000000000000404 (note that no 0x0400..0x403 to avoid creating Node4).
//
// Then remove the minimal Node16 over full Node4 key subset, see
// number_to_minimal_node16_over_node4_key.

template <class Db, unsigned SmallerNodeSize>
void shrink_node_sequentially_benchmark(::benchmark::State &state);

extern template void shrink_node_sequentially_benchmark<unodb::db, 4>(
    ::benchmark::State &);
extern template void shrink_node_sequentially_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void shrink_node_sequentially_benchmark<unodb::db, 48>(
    ::benchmark::State &);

template <class Db, unsigned SmallerNodeSize>
void shrink_node_randomly_benchmark(::benchmark::State &state);

extern template void shrink_node_randomly_benchmark<unodb::db, 4>(
    ::benchmark::State &);
extern template void shrink_node_randomly_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void shrink_node_randomly_benchmark<unodb::db, 48>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void sequential_add_benchmark(::benchmark::State &state);

extern template void sequential_add_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void sequential_add_benchmark<unodb::db, 48>(
    ::benchmark::State &);
extern template void sequential_add_benchmark<unodb::db, 256>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void random_add_benchmark(::benchmark::State &state);

extern template void random_add_benchmark<unodb::db, 16>(::benchmark::State &);
extern template void random_add_benchmark<unodb::db, 48>(::benchmark::State &);
extern template void random_add_benchmark<unodb::db, 256>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void minimal_tree_full_scan(::benchmark::State &state);

extern template void minimal_tree_full_scan<unodb::db, 16>(
    ::benchmark::State &);
extern template void minimal_tree_full_scan<unodb::db, 48>(
    ::benchmark::State &);
extern template void minimal_tree_full_scan<unodb::db, 256>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void minimal_tree_random_gets(::benchmark::State &state);

extern template void minimal_tree_random_gets<unodb::db, 16>(
    ::benchmark::State &);
extern template void minimal_tree_random_gets<unodb::db, 48>(
    ::benchmark::State &);
extern template void minimal_tree_random_gets<unodb::db, 256>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void sequential_delete_benchmark(::benchmark::State &state);

extern template void sequential_delete_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void sequential_delete_benchmark<unodb::db, 48>(
    ::benchmark::State &);
extern template void sequential_delete_benchmark<unodb::db, 256>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void random_delete_benchmark(::benchmark::State &state);

extern template void random_delete_benchmark<unodb::db, 16>(
    ::benchmark::State &);
extern template void random_delete_benchmark<unodb::db, 48>(
    ::benchmark::State &);
extern template void random_delete_benchmark<unodb::db, 256>(
    ::benchmark::State &);

}  // namespace unodb::benchmark

#endif  // MICRO_BENCHMARK_HPP_
