// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "art_common.hpp"
#include "micro_benchmark.hpp"

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

namespace {

// cppcheck-suppress constParameter
void unpredictable_get_shared_length(benchmark::State &state) {
  std::vector<unodb::key> search_keys{};
  search_keys.reserve(7 * 2 + 7 * 2 - 3);
  unodb::db db;
  for (std::uint8_t top_byte = 0x00; top_byte <= 0x05; ++top_byte) {
    const std::uint64_t first_key = static_cast<std::uint64_t>(top_byte) << 56;
    const std::uint64_t second_key = first_key | (1ULL << ((top_byte + 1) * 8));
    unodb::benchmark::insert_key(db, first_key,
                                 unodb::value_view{unodb::benchmark::value100});
    unodb::benchmark::insert_key(db, second_key,
                                 unodb::value_view{unodb::benchmark::value100});
    search_keys.push_back(first_key);
    search_keys.push_back(second_key);
    if (top_byte > 4) continue;
    const std::uint64_t first_not_found_key =
        first_key | (1ULL << ((top_byte + 2) * 8));
    search_keys.push_back(first_not_found_key);

    if (top_byte > 3) continue;
    const std::uint64_t second_not_found_key = first_key | (1ULL << 48);
    search_keys.push_back(second_not_found_key);
  }

  std::random_device rd;
  std::mt19937 gen{rd()};

  for (auto _ : state) {
    state.PauseTiming();
    std::shuffle(search_keys.begin(), search_keys.end(), gen);
    state.ResumeTiming();
    for (const auto k : search_keys) {
      unodb::benchmark::get_key(db, k);
    }
  }

  state.SetItemsProcessed(
      static_cast<std::int64_t>(state.iterations() * search_keys.size()));
}

}  // namespace

BENCHMARK(unpredictable_get_shared_length)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
