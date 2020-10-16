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

namespace unodb {
class db;
class mutex_db;
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

// Key manipulation

inline constexpr auto full_node4_tree_key_zero_bits = 0xFCFCFCFC'FCFCFCFCULL;

inline constexpr auto next_key(unodb::key k,
                               std::uint64_t key_zero_bits) noexcept {
  assert((k & key_zero_bits) == 0);

  const auto result = ((k | key_zero_bits) + 1) & ~key_zero_bits;

  assert(result > k);
  assert((result & key_zero_bits) == 0);

  return result;
}

template <unsigned B, unsigned S, unsigned O>
inline constexpr auto to_scaled_base_n_value(std::uint64_t i) noexcept {
  assert(i / (B * B * B * B * B * B * B) < B);
  return (i % B * S + O) | (i / B % B * S + O) << 8 |
         ((i / (B * B) % B * S + O) << 16) |
         ((i / (B * B * B) % B * S + O) << 24) |
         ((i / (B * B * B * B) % B * S + O) << 32) |
         ((i / (B * B * B * B * B) % B * S + O) << 40) |
         ((i / (B * B * B * B * B * B) % B * S + O) << 48) |
         ((i / (B * B * B * B * B * B * B) % B * S + O) << 56);
}

template <unsigned B>
inline constexpr auto to_base_n_value(std::uint64_t i) noexcept {
  assert(i / (B * B * B * B * B * B * B) < B);
  return to_scaled_base_n_value<B, 1, 0>(i);
}

// Minimal leaf-level Node16 tree keys over full Node4 tree keys: "base-4"
// values that vary each byte from 0 to 3 with the last byte being a constant 4.
inline constexpr auto number_to_minimal_leaf_node16_over_node4_key(
    std::uint64_t i) noexcept {
  assert(i / (4 * 4 * 4 * 4 * 4 * 4) < 4);
  return 4ULL | to_base_n_value<4>(i) << 8;
}

// Full Node4 tree keys with 1, 3, 5, & 7 as the different key byte values so
// that a new byte could be inserted later at any position:
// 0x0101010101010101 to ...107
// 0x0101010101010301 to ...307
// 0x0101010101010501 to ...507
// 0x0101010101010701 to ...707
// 0x0101010101030101 to ...107
// 0x0101010101030301 to ...307
inline constexpr auto number_to_full_node4_with_gaps_key(
    std::uint64_t i) noexcept {
  return to_scaled_base_n_value<4, 2, 1>(i);
}

// Full Node16 tree keys with 1, 3, 5, ..., 33 as the different key byte values
// so that a new byte could be inserted later at any position.
inline constexpr auto number_to_full_node16_with_gaps_key(
    std::uint64_t i) noexcept {
  return to_scaled_base_n_value<16, 2, 1>(i);
}

// Key vectors

std::vector<unodb::key> generate_random_minimal_node16_over_full_node4_keys(
    unodb::key key_limit);

std::vector<unodb::key> generate_random_minimal_node48_over_full_node16_keys(
    unodb::key key_limit);

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
  tree_stats(void) noexcept = default;

  explicit tree_stats(const Db &test_db) noexcept
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
  }

  bool operator==(const tree_stats<Db> &other) const noexcept {
    return leaf_count == other.leaf_count && internal_levels_equal(other);
  }

  bool internal_levels_equal(const tree_stats<Db> &other) const noexcept {
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
  void get(const Db &test_db) noexcept {
    stats.get(test_db);
#ifndef NDEBUG
    get_called = true;
    db = &test_db;
#endif
  }

  void publish(::benchmark::State &state) const noexcept {
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

inline void print_key(std::ostream &out, unodb::key k) {
  out << "0x" << std::hex << k << std::dec;
}

#endif

template <class Db>
void assert_node4_only_tree(const Db &test_db USED_IN_DEBUG) noexcept;

extern template void assert_node4_only_tree<unodb::db>(
    const unodb::db &) noexcept;

// In a mostly-Node16 tree a few Node4 are allowed on the rightmost tree edge,
// including the root
template <class Db>
void assert_mostly_node16_tree(const Db &test_db USED_IN_DEBUG) noexcept;

extern template void assert_mostly_node16_tree<unodb::db>(
    const unodb::db &) noexcept;

// Do not bother with extern templates due to large parameter space
template <class Db, unsigned NodeSize>
void assert_node_size_tree(const Db &test_db USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  static_assert(NodeSize == 4 || NodeSize == 16);
  if constexpr (NodeSize == 4) {
    assert_node4_only_tree(test_db);
  } else if constexpr (NodeSize == 16) {
    assert_mostly_node16_tree(test_db);
  }
#endif
}

// Do not bother with extern templates due to large parameter space
template <class Db, unsigned NodeSize>
void assert_growing_nodes(const Db &test_db USED_IN_DEBUG,
                          std::uint64_t number_of_nodes
                              USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  static_assert(NodeSize == 4 || NodeSize == 16);
  if constexpr (NodeSize == 4) {
    assert(number_of_nodes == test_db.get_inode4_to_inode16_count());
  } else if constexpr (NodeSize == 16) {
    assert(number_of_nodes == test_db.get_inode16_to_inode48_count());
  }
#endif
}

template <class Db>
class tree_shape_snapshot final {
 public:
  explicit tree_shape_snapshot(const Db &test_db USED_IN_DEBUG) noexcept
#ifndef NDEBUG
      : db{test_db}, stats {
    test_db
  }
#endif
  {}

  void assert_internal_levels_same() const noexcept {
#ifndef NDEBUG
    const tree_stats<Db> current_stats{db};
    assert(stats.internal_levels_equal(current_stats));
#endif
  }

 private:
#ifndef NDEBUG
  const Db &db;
  const tree_stats<Db> stats;
#endif
};

// Insertion

template <class Db>
void insert_key(Db &db, unodb::key k, unodb::value_view v) {
  const auto result USED_IN_DEBUG = db.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert key ";
    print_key(std::cerr, k);
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

template <class Db>
unodb::key insert_sequentially(Db &db, std::uint64_t number_of_keys,
                               std::uint64_t key_zero_bits) {
  unodb::key k = 0;
  for (decltype(number_of_keys) i = 0; i < number_of_keys; ++i) {
    insert_key(db, k, unodb::value_view{value100});
    k = next_key(k, key_zero_bits);
  }
  return k;
}

template <class Db>
void insert_keys(Db &db, const std::vector<unodb::key> &keys) {
  for (const auto k : keys) {
    insert_key(db, k, unodb::value_view{value100});
  }
}

template <class Db, typename NumberToKeyFn>
auto insert_keys_to_limit(Db &db, unodb::key key_limit,
                          NumberToKeyFn number_to_key_fn) {
  std::uint64_t i{0};
  while (true) {
    unodb::key key = number_to_key_fn(i);
    if (key > key_limit) break;
    insert_key(db, key, unodb::value_view{value100});
    ++i;
  }
  return i;
}

template <class Db, typename NumberToKeyFn>
auto insert_n_keys(Db &db, unsigned n, NumberToKeyFn number_to_key_fn) {
  unodb::key last_inserted_key{0};

  for (decltype(n) i = 0; i < n; ++i) {
    last_inserted_key = number_to_key_fn(i);
    insert_key(db, last_inserted_key, unodb::value_view{value100});
  }

  return last_inserted_key;
}

template <class Db, unsigned SmallerNodeSize, typename NumberToKeyFn>
auto grow_full_node_tree_to_minimal_next_size_leaf_level(
    Db &db, unodb::key key_limit, NumberToKeyFn number_to_key_fn) {
  static_assert(SmallerNodeSize == 4 || SmallerNodeSize == 16);

#ifndef NDEBUG
  assert_node_size_tree<Db, SmallerNodeSize>(db);
  const auto created_node4_count = db.get_created_inode4_count();
  size_t created_node16_count{0};
  if constexpr (SmallerNodeSize == 16) {
    created_node16_count = db.get_inode4_to_inode16_count();
  }
#endif

  const auto keys_inserted =
      insert_keys_to_limit(db, key_limit, number_to_key_fn);

#ifndef NDEBUG
  assert_growing_nodes<Db, SmallerNodeSize>(db, keys_inserted);
  assert(created_node4_count == db.get_created_inode4_count());
  if constexpr (SmallerNodeSize == 16) {
    assert(created_node16_count == db.get_inode4_to_inode16_count());
  }
#endif

  return keys_inserted;
}

template <class Db>
unodb::key make_node4_tree_with_gaps(Db &db, unsigned number_of_keys);

extern template unodb::key make_node4_tree_with_gaps<unodb::db>(unodb::db &,
                                                                unsigned);

template <class Db>
unodb::key make_node16_tree_with_gaps(Db &db, unsigned number_of_keys);

extern template unodb::key make_node16_tree_with_gaps<unodb::db>(unodb::db &,
                                                                 unsigned);

// Gets

template <class Db>
void get_existing_key(const Db &db, unodb::key k) {
  const auto result = db.get(k);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to get existing key ";
    print_key(std::cerr, k);
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
    print_key(std::cerr, k);
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

// Benchmarks

template <class Db, unsigned NodeSize>
void full_node_scan_benchmark(::benchmark::State &state,
                              std::uint64_t key_zero_bits);

extern template void full_node_scan_benchmark<unodb::db, 4>(
    ::benchmark::State &, std::uint64_t);
extern template void full_node_scan_benchmark<unodb::db, 16>(
    ::benchmark::State &, std::uint64_t);

template <class Db, unsigned Full_Key_Base>
void full_node_random_get_benchmark(::benchmark::State &state,
                                    std::uint64_t key_zero_bits);

extern template void full_node_random_get_benchmark<unodb::db, 4>(
    ::benchmark::State &, std::uint64_t);
extern template void full_node_random_get_benchmark<unodb::db, 16>(
    ::benchmark::State &, std::uint64_t);

// Do not bother with extern templates due to large parameter space
template <class Db, unsigned SmallerNodeSize,
          std::uint64_t SmallerNodeKeyZeroBits, typename NumberToKeyFn>
void grow_node_sequentially_benchmark(::benchmark::State &state,
                                      NumberToKeyFn number_to_key_fn) {
  std::size_t tree_size{0};
  const auto smaller_node_count = static_cast<std::uint64_t>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    unodb::key key_limit = insert_sequentially(
        test_db, smaller_node_count * SmallerNodeSize, SmallerNodeKeyZeroBits);
    assert_node_size_tree<Db, SmallerNodeSize>(test_db);
    ::benchmark::ClobberMemory();
    state.ResumeTiming();

    benchmark_keys_inserted =
        grow_full_node_tree_to_minimal_next_size_leaf_level<Db, SmallerNodeSize,
                                                            NumberToKeyFn>(
            test_db, key_limit, number_to_key_fn);

    state.PauseTiming();
    unodb::benchmark::assert_growing_nodes<Db, SmallerNodeSize>(
        test_db, benchmark_keys_inserted);
    tree_size = test_db.get_current_memory_use();
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  set_size_counter(state, "size", tree_size);
}

// Do not bother with extern templates due to large parameter space
template <class Db, unsigned SmallerNodeSize, typename MakeSmallerTreeFn,
          typename GenerateKeysFn>
void grow_node_randomly_benchmark(::benchmark::State &state,
                                  MakeSmallerTreeFn make_smaller_tree_fn,
                                  GenerateKeysFn generate_keys_fn) {
  std::size_t tree_size{0};
  const auto smaller_node_count = static_cast<unsigned>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit =
        make_smaller_tree_fn(test_db, smaller_node_count * 4);

    const auto larger_tree_keys = generate_keys_fn(key_limit);
    state.ResumeTiming();

    unodb::benchmark::insert_keys(test_db, larger_tree_keys);

    state.PauseTiming();
    benchmark_keys_inserted = larger_tree_keys.size();
    unodb::benchmark::assert_growing_nodes<Db, SmallerNodeSize>(
        test_db, benchmark_keys_inserted);
    tree_size = test_db.get_current_memory_use();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  unodb::benchmark::set_size_counter(state, "size", tree_size);
}

}  // namespace unodb::benchmark

#endif  // MICRO_BENCHMARK_HPP_
