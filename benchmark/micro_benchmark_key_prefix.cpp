// Copyright 2020-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <string>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "art_common.hpp"
#include "assert.hpp"
#include "micro_benchmark_utils.hpp"

namespace {

/*

Make get_shared_length too hard for the CPU branch predictor:

I16 root keys:
0x0
 I4 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                        0x0
                          L 0x0
                        0x1
                          L 0x0
0x1
 I4 0x0 0x0 0x0 0x0 - prefix, keys:
                    0x0
                      L 0x0 0x0
                    0x1
                      L 0x0 0x0
...
0x4
 I4 0x0 - prefix, keys:
        0x0
          L 0x0 0x0 0x0 0x0 0x0
        0x1
          L 0x0 0x0 0x0 0x0 0x0
0x5
I4 keys:
    0x0
      L 0x0 0x0 0x0 0x0 0x0 0x0
    0x1
      L 0x0 0x0 0x0 0x0 0x0 0x0

Keys to be inserted:    Additional key prefix mismatch keys:
0x0000000000000000      0x0000000000100000
0x0000000000000100      0x0001000000000000
0x0100000000000000      0x0100000010000000
0x0100000000010000      0x0101000000000000
...
0x0400000000000000      0x0401000000000000
0x0400010000000000
0x0500000000000000
0x0501000000000000
 */

template <class Db>
void unpredictable_get_shared_length(benchmark::State &state) {
  std::vector<std::uint64_t> search_keys{};
  // NOLINTNEXTLINE(readability-math-missing-parentheses)
  search_keys.reserve(7 * 2 + 7 * 2 - 3);
  Db test_db;
  for (std::uint8_t top_byte = 0x00; top_byte <= 0x05; ++top_byte) {
    const auto first_key = static_cast<std::uint64_t>(top_byte) << 56U;
    const auto second_key = first_key | (1ULL << ((top_byte + 1) * 8U));
    unodb::benchmark::insert_key(test_db, first_key,
                                 unodb::value_view{unodb::benchmark::value100});
    unodb::benchmark::insert_key(test_db, second_key,
                                 unodb::value_view{unodb::benchmark::value100});
    search_keys.push_back(first_key);
    search_keys.push_back(second_key);
    if (top_byte > 4) continue;
    const auto first_not_found_key =
        first_key | (1ULL << ((top_byte + 2) * 8U));
    search_keys.push_back(first_not_found_key);

    if (top_byte > 3) continue;
    const auto second_not_found_key = first_key | (1ULL << 48U);
    search_keys.push_back(second_not_found_key);
  }

  for (const auto _ : state) {
    state.PauseTiming();
    std::ranges::shuffle(search_keys, unodb::benchmark::get_prng());
    state.ResumeTiming();
    for (const auto k : search_keys) unodb::benchmark::get_key(test_db, k);
  }

  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(search_keys.size()));
}

/*

Make inode_4 two-key constructor too hard for the CPU branch predictor by
inserting every second key in the above tree, and benchmarking inserting of the
rest:

before:

I256 root keys:
0x00
  L  0x0 0x0 0x0 0x0 0x0 0x0 0x0
...
0xFC
  L  0x0 0x0 0x0 0x0 0x0 0x0 0x0

after:

I256 root keys:
0x0
 I4 0x0 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                            0x0
                          L 0x0
                            0x1
                          L 0x1
0x1
 I4 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                        0x0
                          L 0x0
                        0x1
                          L 0x0
0x2
 I4 0x0 0x0 0x0 0x0 - prefix, keys:
                    0x0
                      L 0x0 0x0
                    0x1
                      L 0x0 0x0
...
0x6
I4 keys:
    0x0
      L 0x0 0x0 0x0 0x0 0x0 0x0
    0x1
      L 0x0 0x0 0x0 0x0 0x0 0x0
0x7 to 0xFC: the above repeated

Keys to be inserted in preparation:
0x0000000000000000
0x0100000000000000
...
0x0600000000000000

In benchmark:
0x0000000000000001
0x0100000000000100
...
0x0601000000000000
... and repeated

*/

template <class Db>
void insert_keys(Db &test_db, const std::vector<std::uint64_t> &keys) {
  for (const auto k : keys) {
    unodb::benchmark::insert_key(test_db, k,
                                 unodb::value_view{unodb::benchmark::value100});
  }
}

template <class Db>
void do_insert_benchmark(benchmark::State &state,
                         const std::vector<std::uint64_t> &prepare_keys,
                         std::vector<std::uint64_t> &benchmark_keys) {
  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    insert_keys(test_db, prepare_keys);
    std::ranges::shuffle(benchmark_keys, unodb::benchmark::get_prng());
    state.ResumeTiming();

    insert_keys(test_db, benchmark_keys);

    state.PauseTiming();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(benchmark_keys.size()));
}

template <class Db>
void unpredictable_leaf_key_prefix_split(benchmark::State &state) {
  static constexpr auto stride_len = 7U;
  static constexpr auto num_strides = 36;
  static constexpr auto num_top_bytes = stride_len * num_strides;
  static_assert(num_top_bytes < 256);

  std::vector<std::uint64_t> prepare_keys{};
  prepare_keys.reserve(num_top_bytes);
  std::vector<std::uint64_t> benchmark_keys{};
  benchmark_keys.reserve(num_top_bytes);
  for (std::uint8_t top_byte = 0x00; top_byte < num_top_bytes; ++top_byte) {
    const auto first_key = static_cast<std::uint64_t>(top_byte) << 56U;

    // Quadratic but debug build only
    UNODB_DETAIL_ASSERT(std::ranges::find(prepare_keys, first_key) ==
                        prepare_keys.cend());
    UNODB_DETAIL_ASSERT(std::ranges::find(benchmark_keys, first_key) ==
                        benchmark_keys.cend());
    prepare_keys.push_back(first_key);

    const auto second_key = first_key | (1ULL << (top_byte % stride_len * 8U));
    // Quadratic but debug build only
    UNODB_DETAIL_ASSERT(std::ranges::find(prepare_keys, second_key) ==
                        prepare_keys.cend());
    UNODB_DETAIL_ASSERT(std::ranges::find(benchmark_keys, second_key) ==
                        benchmark_keys.cend());
    benchmark_keys.push_back(second_key);
  }

  do_insert_benchmark<Db>(state, prepare_keys, benchmark_keys);
}

/*

Exercise inode::cut_key_prefix with unpredictable cut length:

before:

I256 root keys:
0x00
  I4 0x0 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1
...
0xFC
  I4 0x0 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1

after:

I256 root keys:
0x00
  I4 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                         0x0
                         I4 keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1
                         0x1
                           L 0x0
...
0x05
  I4 - empty prefix, keys:
     0x0
     I4  0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1
     0x1
   L 0x1 0x0 0x0 0x0 0x0 0x0 0x0
...repeated 42 times until 0xFB

Keys to be inserted in preparation:
0x0000000000000000
0x0000000000000001
...
0xFB00000000000000
0xFB00000000000001

In benchmark:
0x0000000000000100
0x0100000000010000
...
0x0501000000000000
... and repeated


*/

template <class Db>
void unpredictable_cut_key_prefix(benchmark::State &state) {
  static constexpr auto stride_len = 6U;
  static constexpr auto num_strides = 42;
  static constexpr auto num_top_bytes = stride_len * num_strides;
  static constexpr auto vec_size =
      static_cast<std::vector<std::uint64_t>::size_type>(num_top_bytes) * 2;
  static_assert(num_top_bytes < 256);

  std::vector<std::uint64_t> prepare_keys{};
  prepare_keys.reserve(vec_size);
  std::vector<std::uint64_t> benchmark_keys{};
  benchmark_keys.reserve(num_top_bytes);
  for (std::uint8_t top_byte = 0x00; top_byte < num_top_bytes; ++top_byte) {
    const auto first_key = static_cast<std::uint64_t>(top_byte) << 56U;
    prepare_keys.push_back(first_key);
    const auto second_key = first_key | 1U;
    prepare_keys.push_back(second_key);

    const auto third_key =
        first_key | (1ULL << ((top_byte % stride_len + 1U) * 8U));
    benchmark_keys.push_back(third_key);
  }

  do_insert_benchmark<Db>(state, prepare_keys, benchmark_keys);
}

/*

Exercise inode::prepend_key_prefix with unpredictable prepend length:

before (same tree as cut_key_prefix "after" one):

I256 root keys:
0x00
  I4 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                         0x0
                         I4 keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1
                         0x1
                           L 0x0
...
0x05
  I4 - empty prefix, keys:
     0x0
     I4  0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1
     0x1
   L 0x1 0x0 0x0 0x0 0x0 0x0 0x0
...repeated 42 times until 0xFB

after (same tree as cut_key_prefix "before" one):

I256 root keys:
0x00
  I4 0x0 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1
...
0xFB
  I4 0x0 0x0 0x0 0x0 0x0 0x0 - prefix, keys:
                             0x0
                           L 0x0
                             0x1
                           L 0x1

Keys to be inserted in preparation:

0x0000000000000000
0x0000000000000001
0x0000000000000100
0x0100000000000000
0x0100000000000001
0x0100000000010000
...
0x0500000000000000
0x0500000000000001
0x0501000000000000
... and repeated

Keys to be removed in benchmark:

0x0000000000000100
0x0100000000010000
...
0x0501000000000000
... and repeated

*/

template <class Db>
void unpredictable_prepend_key_prefix(benchmark::State &state) {
  static constexpr auto stride_len = 6U;
  static constexpr auto num_strides = 42;
  static constexpr auto num_top_bytes = stride_len * num_strides;
  static constexpr auto vec_size =
      static_cast<std::vector<std::uint64_t>::size_type>(num_top_bytes) * 3;
  static_assert(num_top_bytes < 256);

  std::vector<std::uint64_t> prepare_keys{};
  prepare_keys.reserve(vec_size);
  std::vector<std::uint64_t> benchmark_keys{};
  benchmark_keys.reserve(num_top_bytes);
  for (std::uint8_t top_byte = 0x00; top_byte < num_top_bytes; ++top_byte) {
    const auto first_key = static_cast<std::uint64_t>(top_byte) << 56U;
    prepare_keys.push_back(first_key);
    const auto second_key = first_key | 1U;
    prepare_keys.push_back(second_key);
    const auto third_key =
        first_key | (1ULL << ((top_byte % stride_len + 1U) * 8U));
    prepare_keys.push_back(third_key);

    benchmark_keys.push_back(third_key);
  }

  for (const auto _ : state) {
    state.PauseTiming();
    Db test_db;
    insert_keys(test_db, prepare_keys);
    std::ranges::shuffle(benchmark_keys, unodb::benchmark::get_prng());
    state.ResumeTiming();

    for (const auto k : benchmark_keys) {
      unodb::benchmark::delete_key(test_db, k);
    }

    state.PauseTiming();
    unodb::benchmark::destroy_tree(test_db, state);
  }

  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(benchmark_keys.size()));
}

}  // namespace

UNODB_START_BENCHMARKS()

BENCHMARK_TEMPLATE(unpredictable_get_shared_length, unodb::benchmark::db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_get_shared_length, unodb::benchmark::mutex_db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_get_shared_length, unodb::benchmark::olc_db)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(unpredictable_leaf_key_prefix_split, unodb::benchmark::db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_leaf_key_prefix_split,
                   unodb::benchmark::mutex_db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_leaf_key_prefix_split,
                   unodb::benchmark::olc_db)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(unpredictable_cut_key_prefix, unodb::benchmark::db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_cut_key_prefix, unodb::benchmark::mutex_db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_cut_key_prefix, unodb::benchmark::olc_db)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(unpredictable_prepend_key_prefix, unodb::benchmark::db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_prepend_key_prefix, unodb::benchmark::mutex_db)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(unpredictable_prepend_key_prefix, unodb::benchmark::olc_db)
    ->Unit(benchmark::kMicrosecond);

UNODB_BENCHMARK_MAIN();
