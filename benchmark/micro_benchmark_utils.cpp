// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include "micro_benchmark_utils.hpp"

#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

#include "art.hpp"
#include "mutex_art.hpp"

namespace unodb::benchmark {

// Key vectors

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
                key.as_bytes[0] =
                    static_cast<std::uint8_t>(prng_byte_values(get_prng()) * 2);

                const unodb::key k = key.as_int;
                if (k > key_limit) {
                  result.shrink_to_fit();
                  std::shuffle(result.begin(), result.end(), get_prng());
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

template std::vector<unodb::key> generate_random_keys_over_full_smaller_tree<4>(
    unodb::key);

template std::vector<unodb::key>
    generate_random_keys_over_full_smaller_tree<16>(unodb::key);

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

template void assert_mostly_node16_tree<unodb::db>(const unodb::db &) noexcept;

template <class Db>
void assert_mostly_node48_tree(const Db &test_db USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
  if (test_db.get_inode4_count() + test_db.get_inode16_count() > 8) {
    std::cerr << "Too many I4/I16 nodes found in mostly-I48 tree:\n";
    test_db.dump(std::cerr);
    assert(test_db.get_inode4_count() + test_db.get_inode16_count() <= 8);
  }
  assert(test_db.get_inode256_count() == 0);
#endif
}

template void assert_mostly_node48_tree<unodb::db>(const unodb::db &) noexcept;

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
void full_node_scan_benchmark(::benchmark::State &state,
                              std::uint64_t key_zero_bits) {
  Db test_db;
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));

  insert_sequentially(test_db, number_of_keys, key_zero_bits);
  assert_node_size_tree<Db, NodeSize>(test_db);
  const auto tree_size = test_db.get_current_memory_use();

  for (auto _ : state) {
    unodb::key k = 0;
    for (std::uint64_t j = 0; j < number_of_keys; ++j) {
      get_existing_key(test_db, k);
      k = next_key(k, key_zero_bits);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
  set_size_counter(state, "size", tree_size);
}

template void full_node_scan_benchmark<unodb::db, 4>(::benchmark::State &,
                                                     std::uint64_t);
template void full_node_scan_benchmark<unodb::db, 16>(::benchmark::State &,
                                                      std::uint64_t);

// TODO(laurynas): can derive zero bits from base?
template <class Db, unsigned Full_Key_Base>
void full_node_random_get_benchmark(::benchmark::State &state,
                                    std::uint64_t key_zero_bits) {
  Db test_db;
  const auto number_of_keys = static_cast<std::uint64_t>(state.range(0));
  batched_prng random_key_positions{number_of_keys - 1};
  insert_sequentially(test_db, number_of_keys, key_zero_bits);
  const auto tree_size = test_db.get_current_memory_use();
  // TODO(laurynas): assert desired tree shape here?

  for (auto _ : state) {
    for (std::uint64_t i = 0; i < number_of_keys; ++i) {
      const auto key_index = random_key_positions.get(state);
      const auto key = to_base_n_value<Full_Key_Base>(key_index);
      get_existing_key(test_db, key);
    }
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          state.range(0));
  set_size_counter(state, "size", tree_size);
}

template void full_node_random_get_benchmark<unodb::db, 4>(::benchmark::State &,
                                                           std::uint64_t);
template void full_node_random_get_benchmark<unodb::db, 16>(
    ::benchmark::State &, std::uint64_t);

template <class Db, unsigned NodeCapacity>
std::tuple<unodb::key, const tree_shape_snapshot<Db>> make_base_tree_for_add(
    Db &test_db, unsigned node_count) {
  const auto key_limit = insert_n_keys<
      Db, decltype(number_to_minimal_node_size_tree_key<NodeCapacity>)>(
      test_db, node_count * (node_capacity_to_minimum_size<NodeCapacity>() + 1),
      number_to_minimal_node_size_tree_key<NodeCapacity>);
  assert_node_size_tree<Db, NodeCapacity>(test_db);
  return std::make_tuple(key_limit, tree_shape_snapshot<unodb::db>{test_db});
}

template std::tuple<unodb::key, const tree_shape_snapshot<unodb::db>>
make_base_tree_for_add<unodb::db, 16>(unodb::db &, unsigned);

template std::tuple<unodb::key, const tree_shape_snapshot<unodb::db>>
make_base_tree_for_add<unodb::db, 48>(unodb::db &, unsigned);

}  // namespace unodb::benchmark
