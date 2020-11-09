// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include "micro_benchmark_utils.hpp"

#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

#include "art.hpp"
#include "mutex_art.hpp"

namespace {

// Node sizes

template <unsigned NodeCapacity>
constexpr inline auto node_capacity_to_minimum_size() noexcept {
  static_assert(NodeCapacity == 16 || NodeCapacity == 48);
  if constexpr (NodeCapacity == 16) {
    return 5;
  } else if constexpr (NodeCapacity == 48) {
    return 17;
  }
}

template <unsigned NodeCapacity>
constexpr inline auto node_capacity_over_minimum() noexcept {
  static_assert(NodeCapacity == 16 || NodeCapacity == 48);
  return NodeCapacity - node_capacity_to_minimum_size<NodeCapacity>();
}

template <unsigned NodeSize>
constexpr inline auto node_size_has_key_zero_bits() noexcept {
  // If node size is a power of two, then can use key zero bit-based operations
  return (NodeSize & (NodeSize - 1)) == 0;
}

// Key manipulation

template <unsigned B, unsigned S, unsigned O>
inline constexpr auto to_scaled_base_n_value(std::uint64_t i) noexcept {
  assert(i / (static_cast<std::uint64_t>(B) * B * B * B * B * B * B) < B);
  return (i % B * S + O) | (i / B % B * S + O) << 8U |
         ((i / (B * B) % B * S + O) << 16U) |
         ((i / (B * B * B) % B * S + O) << 24U) |
         ((i / (B * B * B * B) % B * S + O) << 32U) |
         ((i / (B * B * B * B * B) % B * S + O) << 40U) |
         ((i / (static_cast<std::uint64_t>(B) * B * B * B * B * B) % B * S + O)
          << 48U) |
         ((i / (static_cast<std::uint64_t>(B) * B * B * B * B * B * B) % B * S +
           O)
          << 56U);
}

template <unsigned B>
inline constexpr auto to_base_n_value(std::uint64_t i) noexcept {
  assert(i / (static_cast<std::uint64_t>(B) * B * B * B * B * B * B) < B);
  return to_scaled_base_n_value<B, 1, 0>(i);
}

template <unsigned NodeSize>
inline constexpr auto number_to_full_node_size_tree_key(
    std::uint64_t i) noexcept {
  return to_base_n_value<NodeSize>(i);
}

template <unsigned NodeSize>
inline constexpr auto number_to_minimal_node_size_tree_key(
    std::uint64_t i) noexcept {
  return to_base_n_value<node_capacity_to_minimum_size<NodeSize>()>(i);
}

template <unsigned NodeSize>
inline constexpr auto number_to_full_leaf_over_minimal_tree_key(
    std::uint64_t i) noexcept {
  constexpr auto min = node_capacity_to_minimum_size<NodeSize>();
  constexpr auto delta = node_capacity_over_minimum<NodeSize>();
  assert(i / (delta * min * min * min * min * min * min) < min);
  return ((i % delta) + min) |
         number_to_minimal_node_size_tree_key<NodeSize>(i / delta) << 8U;
}

template <unsigned NodeSize>
inline constexpr auto number_to_minimal_leaf_over_smaller_node_tree(
    std::uint64_t i) noexcept {
  constexpr auto N = static_cast<std::uint64_t>(NodeSize);
  assert(i / (N * N * N * N * N * N) < N);
  return N | number_to_full_node_size_tree_key<N>(i) << 8U;
}

template <unsigned NodeSize>
inline constexpr auto number_to_full_node_tree_with_gaps_key(
    std::uint64_t i) noexcept {
  static_assert(NodeSize == 4 || NodeSize == 16 || NodeSize == 48);
  // Full Node4 tree keys with 1, 3, 5, & 7 as the different key byte values
  // so that a new byte could be inserted later at any position:
  // 0x0101010101010101 to ...107
  // 0x0101010101010301 to ...307
  // 0x0101010101010501 to ...507
  // 0x0101010101010701 to ...707
  // 0x0101010101030101 to ...107
  // 0x0101010101030301 to ...307
  // Full Node16 tree keys with 1, 3, 5, ..., 33 as the different key byte
  // values so that a new byte could be inserted later at any position.
  return to_scaled_base_n_value<NodeSize, 2, 1>(i);
}

// Key vectors

template <typename NumberToKeyFn>
auto generate_keys_to_limit(unodb::key key_limit,
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

template <std::uint8_t NumByteValues>
std::vector<unodb::key> generate_random_keys_over_full_smaller_tree(
    unodb::key key_limit) {
  // The last byte at the limit will be randomly-generated and may happen to
  // fall above or below the limit. Reset the limit so that any byte value will
  // pass.
  key_limit |= 0xFFU;
  std::uniform_int_distribution<std::uint8_t> prng_byte_values{0,
                                                               NumByteValues};

  std::vector<unodb::key> result;
  union {
    std::uint64_t as_int;                  // cppcheck-suppress shadowVariable
    std::array<std::uint8_t, 8> as_bytes;  // cppcheck-suppress shadowVariable
  } key;

  for (std::uint8_t i = 0; i < NumByteValues; ++i) {
    key.as_bytes[7] = static_cast<std::uint8_t>(i * 2 + 1);
    for (std::uint8_t i2 = 0; i2 < NumByteValues; ++i2) {
      key.as_bytes[6] = static_cast<std::uint8_t>(i2 * 2 + 1);
      for (std::uint8_t i3 = 0; i3 < NumByteValues; ++i3) {
        key.as_bytes[5] = static_cast<std::uint8_t>(i3 * 2 + 1);
        for (std::uint8_t i4 = 0; i4 < NumByteValues; ++i4) {
          key.as_bytes[4] = static_cast<std::uint8_t>(i4 * 2 + 1);
          for (std::uint8_t i5 = 0; i5 < NumByteValues; ++i5) {
            key.as_bytes[3] = static_cast<std::uint8_t>(i5 * 2 + 1);
            for (std::uint8_t i6 = 0; i6 < NumByteValues; ++i6) {
              key.as_bytes[2] = static_cast<std::uint8_t>(i6 * 2 + 1);
              for (std::uint8_t i7 = 0; i7 < NumByteValues; ++i7) {
                key.as_bytes[1] = static_cast<std::uint8_t>(i7 * 2 + 1);
                key.as_bytes[0] = static_cast<std::uint8_t>(
                    prng_byte_values(unodb::benchmark::get_prng()) * 2);

                const unodb::key k = key.as_int;
                if (k > key_limit) {
                  result.shrink_to_fit();
                  std::shuffle(result.begin(), result.end(),
                               unodb::benchmark::get_prng());
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

// Asserts

#ifndef NDEBUG

// In a mostly-Node16 tree a few Node4 are allowed on the rightmost tree edge,
// including the root
template <class Db>
void assert_mostly_node16_tree(const Db &test_db USED_IN_DEBUG) noexcept {
  if (test_db.get_inode4_count() > 8) {
    std::cerr << "Too many I4 nodes found in mostly-I16 tree:\n";
    test_db.dump(std::cerr);
    assert(test_db.get_inode4_count() <= 8);
  }
  assert(test_db.get_inode48_count() == 0);
  assert(test_db.get_inode256_count() == 0);
}

template <class Db>
void assert_mostly_node48_tree(const Db &test_db USED_IN_DEBUG) noexcept {
  if (test_db.get_inode4_count() + test_db.get_inode16_count() > 8) {
    std::cerr << "Too many I4/I16 nodes found in mostly-I48 tree:\n";
    test_db.dump(std::cerr);
    assert(test_db.get_inode4_count() + test_db.get_inode16_count() <= 8);
  }
  assert(test_db.get_inode256_count() == 0);
}

#endif  // #ifndef NDEBUG

template <class Db, unsigned NodeSize>
void assert_node_size_tree(const Db &test_db USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  static_assert(NodeSize == 4 || NodeSize == 16 || NodeSize == 48);
  if constexpr (NodeSize == 4) {
    unodb::benchmark::assert_node4_only_tree(test_db);
  } else if constexpr (NodeSize == 16) {
    assert_mostly_node16_tree(test_db);
  } else if constexpr (NodeSize == 48) {
    assert_mostly_node48_tree(test_db);
  }
#endif
}

template <class Db, unsigned SmallerNodeSize>
void assert_growing_nodes(const Db &test_db USED_IN_DEBUG,
                          std::uint64_t number_of_nodes
                              USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  static_assert(SmallerNodeSize == 4 || SmallerNodeSize == 16 ||
                SmallerNodeSize == 48);
  if constexpr (SmallerNodeSize == 4) {
    assert(number_of_nodes == test_db.get_inode4_to_inode16_count());
  } else if constexpr (SmallerNodeSize == 16) {
    assert(number_of_nodes == test_db.get_inode16_to_inode48_count());
  } else {
    if (number_of_nodes != test_db.get_inode48_to_inode256_count()) {
      std::cerr << "Difference between inserts: " << number_of_nodes
                << ", N48 -> N256: " << test_db.get_inode48_to_inode256_count();
      std::cerr << "\nTree:";
      test_db.dump(std::cerr);
      assert(number_of_nodes == test_db.get_inode48_to_inode256_count());
    }
  }
#endif
}

template <class Db, unsigned SmallerNodeSize>
void assert_shrinking_nodes(const Db &test_db USED_IN_DEBUG,
                            std::uint64_t number_of_nodes
                                USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  static_assert(SmallerNodeSize == 4 || SmallerNodeSize == 16 ||
                SmallerNodeSize == 48);
  if constexpr (SmallerNodeSize == 4) {
    assert(number_of_nodes == test_db.get_inode16_to_inode4_count());
    unodb::benchmark::assert_node4_only_tree(test_db);
  } else if constexpr (SmallerNodeSize == 16) {  // NOLINT(readability/braces)
    assert(number_of_nodes == test_db.get_inode48_to_inode16_count());
    assert_mostly_node16_tree(test_db);
  } else {
    assert(number_of_nodes == test_db.get_inode256_to_inode48_count());
    assert_mostly_node48_tree(test_db);
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
    const unodb::benchmark::tree_stats<Db> current_stats{db};
    assert(stats.internal_levels_equal(current_stats));
#endif
  }

 private:
#ifndef NDEBUG
  const Db &db;
  const unodb::benchmark::tree_stats<Db> stats;
#endif
};

// Insertion

template <class Db, typename NumberToKeyFn>
auto insert_keys_to_limit(Db &db, unodb::key key_limit,
                          NumberToKeyFn number_to_key_fn) {
  std::uint64_t i{0};
  while (true) {
    unodb::key key = number_to_key_fn(i);
    if (key > key_limit) break;
    unodb::benchmark::insert_key(db, key,
                                 unodb::value_view{unodb::benchmark::value100});
    ++i;
  }
  return i;
}

template <class Db, typename NumberToKeyFn>
auto insert_n_keys(Db &db, unsigned n, NumberToKeyFn number_to_key_fn) {
  unodb::key last_inserted_key{0};

  for (decltype(n) i = 0; i < n; ++i) {
    last_inserted_key = number_to_key_fn(i);
    unodb::benchmark::insert_key(db, last_inserted_key,
                                 unodb::value_view{unodb::benchmark::value100});
  }

  return last_inserted_key;
}

template <class Db, unsigned NodeSize, typename NumberToKeyFn>
auto insert_n_keys_to_empty_tree(Db &db, unsigned n,
                                 NumberToKeyFn number_to_key_fn) {
  assert(db.empty());
  const auto result = insert_n_keys(db, n, number_to_key_fn);
  assert_node_size_tree<Db, NodeSize>(db);
  return result;
}

template <class Db, unsigned NodeSize>
auto make_full_node_size_tree(Db &db, unsigned key_count) {
  static_assert(NodeSize == 4 || NodeSize == 16 || NodeSize == 48);

  unodb::key key_limit;
  if constexpr (node_size_has_key_zero_bits<NodeSize>()) {
    key_limit =
        unodb::benchmark::insert_sequentially<Db, NodeSize>(db, key_count);
  } else {
    key_limit = insert_n_keys_to_empty_tree<
        unodb::db, NodeSize,
        decltype(number_to_full_node_size_tree_key<NodeSize>)>(
        db, key_count, number_to_full_node_size_tree_key<NodeSize>);
  }

  assert_node_size_tree<Db, NodeSize>(db);
  return key_limit;
}

template <class Db, unsigned NodeCapacity>
std::tuple<unodb::key, const tree_shape_snapshot<Db>> make_base_tree_for_add(
    Db &test_db, unsigned node_count) {
  const auto key_limit = insert_n_keys_to_empty_tree<
      Db, NodeCapacity,
      decltype(number_to_minimal_node_size_tree_key<NodeCapacity>)>(
      test_db, node_count * (node_capacity_to_minimum_size<NodeCapacity>() + 1),
      number_to_minimal_node_size_tree_key<NodeCapacity>);
  return std::make_tuple(key_limit, tree_shape_snapshot<unodb::db>{test_db});
}

template <class Db, unsigned NodeSize>
auto make_minimal_node_size_tree(unodb::db &db, unsigned key_count) {
  return insert_n_keys_to_empty_tree<
      Db, NodeSize, decltype(number_to_minimal_node_size_tree_key<NodeSize>)>(
      db, key_count * node_capacity_to_minimum_size<NodeSize>(),
      number_to_minimal_node_size_tree_key<NodeSize>);
}

template <class Db, unsigned SmallerNodeSize>
auto grow_full_node_tree_to_minimal_next_size_leaf_level(Db &db,
                                                         unodb::key key_limit) {
  static_assert(SmallerNodeSize == 4 || SmallerNodeSize == 16 ||
                SmallerNodeSize == 48);

#ifndef NDEBUG
  assert_node_size_tree<Db, SmallerNodeSize>(db);
  const auto created_node4_count = db.get_created_inode4_count();
  size_t created_node16_count{0};
  if constexpr (SmallerNodeSize >= 16) {
    created_node16_count = db.get_inode4_to_inode16_count();
  }
  size_t created_node48_count{0};
  if constexpr (SmallerNodeSize == 48) {
    created_node48_count = db.get_inode16_to_inode48_count();
  }
#endif

  const auto keys_inserted = insert_keys_to_limit<
      Db,
      decltype(number_to_minimal_leaf_over_smaller_node_tree<SmallerNodeSize>)>(
      db, key_limit,
      number_to_minimal_leaf_over_smaller_node_tree<SmallerNodeSize>);

#ifndef NDEBUG
  assert_growing_nodes<Db, SmallerNodeSize>(db, keys_inserted);
  assert(created_node4_count == db.get_created_inode4_count());
  if constexpr (SmallerNodeSize >= 16) {
    assert(created_node16_count == db.get_inode4_to_inode16_count());
  }
  if constexpr (SmallerNodeSize == 48) {
    assert(created_node48_count == db.get_inode16_to_inode48_count());
  }
#endif

  return keys_inserted;
}

// Querying

template <class Db, typename NumberToKeyFn>
auto get_key_loop(Db &db, unodb::key key_limit,
                  NumberToKeyFn number_to_key_fn) {
  std::uint64_t i{0};
  while (true) {
    const auto key = number_to_key_fn(i);
    if (key > key_limit) break;
    unodb::benchmark::get_existing_key(db, key);
    ++i;
  }
  return static_cast<std::int64_t>(i);
}

}  // namespace

namespace unodb::benchmark {

// PRNG

batched_prng::batched_prng(result_type max_value)
    : random_key_dist{0ULL, max_value} {
  refill();
}

void batched_prng::refill() {
  std::generate(random_keys.begin(), random_keys.end(),
                [this]() { return random_key_dist(get_prng()); });
  random_key_ptr = random_keys.cbegin();
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
  assert(test_db.get_inode48_count() == 0);
  assert(test_db.get_inode256_count() == 0);
#endif
}

template void assert_node4_only_tree<unodb::db>(const unodb::db &) noexcept;

// Teardown

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state) noexcept {
  // Timer must be stopped on entry
  db.clear();
  ::benchmark::ClobberMemory();
  state.ResumeTiming();
}

template void destroy_tree<unodb::db>(unodb::db &,
                                      ::benchmark::State &) noexcept;
template void destroy_tree<unodb::mutex_db>(unodb::mutex_db &,
                                            ::benchmark::State &) noexcept;

// Benchmarks

template <class Db, unsigned NodeSize>
void full_node_scan_benchmark(::benchmark::State &state) {
  const auto key_count = static_cast<unsigned>(state.range(0));
  Db test_db;

  const auto key_limit =
      make_full_node_size_tree<Db, NodeSize>(test_db, key_count);
  const auto tree_size = test_db.get_current_memory_use();

  std::int64_t items_processed{0};
  for (auto _ : state) {
    if constexpr (node_size_has_key_zero_bits<NodeSize>()) {
      unodb::key k = 0;
      for (std::uint64_t j = 0; j < key_count; ++j) {
        assert(k <= key_limit);
        get_existing_key(test_db, k);
        k = next_key(k, node_size_to_key_zero_bits<NodeSize>());
      }
      items_processed += key_count;
    } else {
      items_processed += get_key_loop(
          test_db, key_limit, number_to_full_node_size_tree_key<NodeSize>);
    }
  }

  state.SetItemsProcessed(items_processed);
  set_size_counter(state, "size", tree_size);
}

template void full_node_scan_benchmark<unodb::db, 4>(::benchmark::State &);
template void full_node_scan_benchmark<unodb::db, 16>(::benchmark::State &);
template void full_node_scan_benchmark<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void full_node_random_get_benchmark(::benchmark::State &state) {
  Db test_db;
  const auto key_count = static_cast<unsigned>(state.range(0));

  make_full_node_size_tree<Db, NodeSize>(test_db, key_count);
  const auto tree_size = test_db.get_current_memory_use();

  batched_prng random_key_positions{key_count - 1};

  for (auto _ : state) {
    for (std::uint64_t i = 0; i < key_count; ++i) {
      const auto key_index = random_key_positions.get(state);
      const auto key = number_to_full_node_size_tree_key<NodeSize>(key_index);
      get_existing_key(test_db, key);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          key_count);
  set_size_counter(state, "size", tree_size);
}

template void full_node_random_get_benchmark<unodb::db, 4>(
    ::benchmark::State &);
template void full_node_random_get_benchmark<unodb::db, 16>(
    ::benchmark::State &);
template void full_node_random_get_benchmark<unodb::db, 48>(
    ::benchmark::State &);

template <class Db, unsigned NodeSize>
void minimal_tree_full_scan(::benchmark::State &state) {
  const auto key_count = static_cast<unsigned>(state.range(0));
  Db test_db;

  const auto key_limit =
      make_minimal_node_size_tree<Db, NodeSize>(test_db, key_count);
  const auto tree_size = test_db.get_current_memory_use();

  std::int64_t items_processed = 0;
  for (auto _ : state) {
    // cppcheck-suppress useStlAlgorithm
    items_processed += get_key_loop(
        test_db, key_limit, number_to_minimal_node_size_tree_key<NodeSize>);
  }

  state.SetItemsProcessed(items_processed);
  set_size_counter(state, "size", tree_size);
}

template void minimal_tree_full_scan<unodb::db, 16>(::benchmark::State &);
template void minimal_tree_full_scan<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void minimal_tree_random_gets(::benchmark::State &state) {
  const auto node_count = static_cast<unsigned>(state.range(0));
  Db test_db;
  const auto key_limit USED_IN_DEBUG =
      make_minimal_node_size_tree<Db, NodeSize>(test_db, node_count);
  assert(number_to_minimal_node_size_tree_key<NodeSize>(
             node_count * node_capacity_to_minimum_size<NodeSize>() - 1) ==
         key_limit);
  const auto tree_size = test_db.get_current_memory_use();
  batched_prng random_key_positions{
      node_count * node_capacity_to_minimum_size<16>() - 1};
  std::int64_t items_processed = 0;

  for (auto _ : state) {
    const auto key_index = random_key_positions.get(state);
    const auto key = number_to_minimal_node_size_tree_key<NodeSize>(key_index);
    get_existing_key(test_db, key);
    ++items_processed;
  }

  state.SetItemsProcessed(items_processed);
  set_size_counter(state, "size", tree_size);
}

template void minimal_tree_random_gets<unodb::db, 16>(::benchmark::State &);
template void minimal_tree_random_gets<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void sequential_add_benchmark(::benchmark::State &state) {
  std::size_t tree_size{0};
  const auto node_count = static_cast<unsigned>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    auto [key_limit, tree_shape] =
        make_base_tree_for_add<Db, NodeSize>(test_db, node_count);
    state.ResumeTiming();

    benchmark_keys_inserted = insert_keys_to_limit<
        Db, decltype(number_to_full_leaf_over_minimal_tree_key<NodeSize>)>(
        test_db, key_limit,
        number_to_full_leaf_over_minimal_tree_key<NodeSize>);

    state.PauseTiming();
#ifndef NDEBUG
    assert_node_size_tree<Db, NodeSize>(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    tree_size = test_db.get_current_memory_use();
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  set_size_counter(state, "size", tree_size);
}

template void sequential_add_benchmark<unodb::db, 16>(::benchmark::State &);
template void sequential_add_benchmark<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void random_add_benchmark(::benchmark::State &state) {
  std::size_t tree_size{0};
  const auto node_count = static_cast<unsigned>(state.range(0));
  std::int64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    auto [key_limit, tree_shape] =
        make_base_tree_for_add<Db, NodeSize>(test_db, node_count);
    auto benchmark_keys = generate_keys_to_limit(
        key_limit, number_to_full_leaf_over_minimal_tree_key<NodeSize>);
    std::shuffle(benchmark_keys.begin(), benchmark_keys.end(), get_prng());
    state.ResumeTiming();

    insert_keys(test_db, benchmark_keys);

    state.PauseTiming();
#ifndef NDEBUG
    assert_node_size_tree<Db, NodeSize>(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    tree_size = test_db.get_current_memory_use();
    benchmark_keys_inserted = static_cast<std::int64_t>(benchmark_keys.size());
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          benchmark_keys_inserted);
  set_size_counter(state, "size", tree_size);
}

template void random_add_benchmark<unodb::db, 16>(::benchmark::State &);
template void random_add_benchmark<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void sequential_delete_benchmark(::benchmark::State &state) {
  const auto key_count = static_cast<unsigned>(state.range(0));
  int i{0};
  std::size_t tree_size{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit =
        make_full_node_size_tree<Db, NodeSize>(test_db, key_count);
    tree_size = test_db.get_current_memory_use();
    const tree_shape_snapshot<Db> tree_shape{test_db};
    state.ResumeTiming();

    i = 0;
    while (true) {
      const auto k = number_to_full_leaf_over_minimal_tree_key<NodeSize>(
          static_cast<std::uint64_t>(i));
      if (k > key_limit) break;
      delete_key(test_db, k);
      ++i;
    }

    state.PauseTiming();
#ifndef NDEBUG
    assert_node_size_tree<Db, NodeSize>(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * i);
  set_size_counter(state, "size", tree_size);
}

template void sequential_delete_benchmark<unodb::db, 16>(::benchmark::State &);
template void sequential_delete_benchmark<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned NodeSize>
void random_delete_benchmark(::benchmark::State &state) {
  const auto key_count{static_cast<unsigned>(state.range(0))};
  std::size_t tree_size{0};
  std::size_t remove_key_count{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit =
        make_full_node_size_tree<Db, NodeSize>(test_db, key_count);
    tree_size = test_db.get_current_memory_use();
    const tree_shape_snapshot<Db> tree_shape{test_db};
    auto remove_keys = generate_keys_to_limit(
        key_limit, number_to_full_leaf_over_minimal_tree_key<NodeSize>);
    remove_key_count = remove_keys.size();
    std::shuffle(remove_keys.begin(), remove_keys.end(), get_prng());
    state.ResumeTiming();

    delete_keys(test_db, remove_keys);

    state.PauseTiming();
#ifndef NDEBUG
    assert_node_size_tree<Db, NodeSize>(test_db);
    tree_shape.assert_internal_levels_same();
#endif
    destroy_tree(test_db, state);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(remove_key_count));
  set_size_counter(state, "size", tree_size);
}

template void random_delete_benchmark<unodb::db, 16>(::benchmark::State &);
template void random_delete_benchmark<unodb::db, 48>(::benchmark::State &);

template <class Db, unsigned SmallerNodeSize>
void shrink_node_sequentially_benchmark(::benchmark::State &state) {
  std::size_t tree_size{0};
  std::uint64_t removed_key_count{0};

  const auto smaller_node_count = static_cast<unsigned>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    unodb::key key_limit = make_full_node_size_tree<Db, SmallerNodeSize>(
        test_db, smaller_node_count * SmallerNodeSize);

    const auto node_growing_keys_inserted =
        grow_full_node_tree_to_minimal_next_size_leaf_level<Db,
                                                            SmallerNodeSize>(
            test_db, key_limit);
    assert_growing_nodes<Db, SmallerNodeSize>(test_db,
                                              node_growing_keys_inserted);
    tree_size = test_db.get_current_memory_use();
    state.ResumeTiming();

    for (removed_key_count = 0; removed_key_count < node_growing_keys_inserted;
         ++removed_key_count) {
      const auto remove_key =
          number_to_minimal_leaf_over_smaller_node_tree<SmallerNodeSize>(
              removed_key_count);
      delete_key(test_db, remove_key);
    }

    state.PauseTiming();
    assert_shrinking_nodes<Db, SmallerNodeSize>(test_db, removed_key_count);
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(
      static_cast<std::int64_t>(state.iterations() * removed_key_count));
  set_size_counter(state, "size", tree_size);
}

template void shrink_node_sequentially_benchmark<unodb::db, 4>(
    ::benchmark::State &);
template void shrink_node_sequentially_benchmark<unodb::db, 16>(
    ::benchmark::State &);
template void shrink_node_sequentially_benchmark<unodb::db, 48>(
    ::benchmark::State &);

template <class Db, unsigned SmallerNodeSize>
void shrink_node_randomly_benchmark(::benchmark::State &state) {
  std::size_t tree_size{0};
  std::uint64_t removed_key_count{0};

  const auto smaller_node_count = static_cast<unsigned>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit = insert_n_keys_to_empty_tree<
        Db, SmallerNodeSize,
        decltype(number_to_full_node_tree_with_gaps_key<SmallerNodeSize>)>(
        test_db, smaller_node_count * SmallerNodeSize,
        number_to_full_node_tree_with_gaps_key<SmallerNodeSize>);

    const auto node_growing_keys =
        generate_random_keys_over_full_smaller_tree<SmallerNodeSize>(key_limit);
    insert_keys(test_db, node_growing_keys);
    assert_growing_nodes<Db, SmallerNodeSize>(test_db,
                                              node_growing_keys.size());
    tree_size = test_db.get_current_memory_use();
    state.ResumeTiming();

    delete_keys(test_db, node_growing_keys);

    state.PauseTiming();
    removed_key_count = node_growing_keys.size();
    assert_shrinking_nodes<Db, SmallerNodeSize>(test_db, removed_key_count);
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(
      static_cast<std::int64_t>(state.iterations() * removed_key_count));
  set_size_counter(state, "size", tree_size);
}

template void shrink_node_randomly_benchmark<unodb::db, 4>(
    ::benchmark::State &);
template void shrink_node_randomly_benchmark<unodb::db, 16>(
    ::benchmark::State &);
template void shrink_node_randomly_benchmark<unodb::db, 48>(
    ::benchmark::State &);

template <class Db, unsigned SmallerNodeSize>
void grow_node_sequentially_benchmark(::benchmark::State &state) {
  std::size_t tree_size{0};
  const auto smaller_node_count = static_cast<unsigned>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    unodb::key key_limit = make_full_node_size_tree<Db, SmallerNodeSize>(
        test_db, smaller_node_count * SmallerNodeSize);
    ::benchmark::ClobberMemory();
    state.ResumeTiming();

    benchmark_keys_inserted =
        grow_full_node_tree_to_minimal_next_size_leaf_level<Db,
                                                            SmallerNodeSize>(
            test_db, key_limit);

    state.PauseTiming();
    assert_growing_nodes<Db, SmallerNodeSize>(test_db, benchmark_keys_inserted);
    tree_size = test_db.get_current_memory_use();
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  set_size_counter(state, "size", tree_size);
}

template void grow_node_sequentially_benchmark<unodb::db, 4>(
    ::benchmark::State &);
template void grow_node_sequentially_benchmark<unodb::db, 16>(
    ::benchmark::State &);
template void grow_node_sequentially_benchmark<unodb::db, 48>(
    ::benchmark::State &);

template <class Db, unsigned SmallerNodeSize>
void grow_node_randomly_benchmark(::benchmark::State &state) {
  std::size_t tree_size{0};
  const auto smaller_node_count = static_cast<unsigned>(state.range(0));
  std::uint64_t benchmark_keys_inserted{0};

  for (auto _ : state) {
    state.PauseTiming();
    Db test_db;
    const auto key_limit = insert_n_keys_to_empty_tree<Db, SmallerNodeSize>(
        test_db, smaller_node_count * SmallerNodeSize,
        number_to_full_node_tree_with_gaps_key<SmallerNodeSize>);

    const auto larger_tree_keys =
        generate_random_keys_over_full_smaller_tree<SmallerNodeSize>(key_limit);
    state.ResumeTiming();

    insert_keys(test_db, larger_tree_keys);

    state.PauseTiming();
    benchmark_keys_inserted = larger_tree_keys.size();
    assert_growing_nodes<Db, SmallerNodeSize>(test_db, benchmark_keys_inserted);
    tree_size = test_db.get_current_memory_use();
    destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(benchmark_keys_inserted));
  set_size_counter(state, "size", tree_size);
}

template void grow_node_randomly_benchmark<unodb::db, 4>(::benchmark::State &);
template void grow_node_randomly_benchmark<unodb::db, 16>(::benchmark::State &);
template void grow_node_randomly_benchmark<unodb::db, 48>(::benchmark::State &);

}  // namespace unodb::benchmark
