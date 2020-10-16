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

template <std::uint8_t NumByteValues>
auto generate_random_keys_over_full_smaller_tree(unodb::key key_limit) {
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
}  // namespace

namespace unodb::benchmark {

// Key vectors

std::vector<unodb::key> generate_random_minimal_node16_over_full_node4_keys(
    unodb::key key_limit) {
  return generate_random_keys_over_full_smaller_tree<4>(key_limit);
}

std::vector<unodb::key> generate_random_minimal_node48_over_full_node16_keys(
    unodb::key key_limit) {
  return generate_random_keys_over_full_smaller_tree<16>(key_limit);
}

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

// Insertion

template <class Db>
unodb::key make_node4_tree_with_gaps(Db &db, unsigned number_of_keys) {
  const auto last_inserted_key =
      insert_n_keys(db, number_of_keys, number_to_full_node4_with_gaps_key);
  assert_node4_only_tree(db);
  return last_inserted_key;
}

template unodb::key make_node4_tree_with_gaps<unodb::db>(unodb::db &, unsigned);

template <class Db>
unodb::key make_node16_tree_with_gaps(Db &db, unsigned number_of_keys) {
  const auto last_inserted_key =
      insert_n_keys(db, number_of_keys, number_to_full_node16_with_gaps_key);
  assert_mostly_node16_tree(db);
  return last_inserted_key;
}

template unodb::key make_node16_tree_with_gaps<unodb::db>(unodb::db &,
                                                          unsigned);

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

}  // namespace unodb::benchmark
