// Copyright 2020-2023 Laurynas Biveinis

// IWYU pragma: no_include <algorithm>
// IWYU pragma: no_include <vector>

#include "global.hpp"  // IWYU pragma: keep

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_node_utils.hpp"
#include "micro_benchmark_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace {

template <class Db>
void grow_n48_to_n256_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<Db, 48>(state);
}

template <class Db>
void grow_n48_to_n256_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<Db, 48>(state);
}

template <class Db>
void n256_sequential_add(benchmark::State &state) {
  unodb::benchmark::sequential_add_benchmark<Db, 256>(state);
}

template <class Db>
void n256_random_add(benchmark::State &state) {
  unodb::benchmark::random_add_benchmark<Db, 256>(state);
}

template <class Db>
void minimal_n256_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::minimal_tree_full_scan<Db, 256>(state);
}

template <class Db>
void minimal_n256_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::minimal_tree_random_gets<Db, 256>(state);
}

template <class Db>
void full_n256_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<Db, 256>(state);
}

template <class Db>
void full_n256_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<Db, 256>(state);
}

template <class Db>
void full_n256_tree_sequential_delete(benchmark::State &state) {
  unodb::benchmark::sequential_delete_benchmark<Db, 256>(state);
}

template <class Db>
void full_n256_tree_random_delete(benchmark::State &state) {
  unodb::benchmark::random_delete_benchmark<Db, 256>(state);
}

}  // namespace

UNODB_START_BENCHMARKS()

BENCHMARK_TEMPLATE(grow_n48_to_n256_sequentially, unodb::db)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n48_to_n256_sequentially, unodb::mutex_db)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n48_to_n256_sequentially, unodb::olc_db)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(grow_n48_to_n256_randomly, unodb::db)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n48_to_n256_randomly, unodb::mutex_db)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n48_to_n256_randomly, unodb::olc_db)
    ->Range(2, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n256_sequential_add, unodb::db)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n256_sequential_add, unodb::mutex_db)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n256_sequential_add, unodb::olc_db)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n256_random_add, unodb::db)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n256_random_add, unodb::mutex_db)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n256_random_add, unodb::olc_db)
    ->Range(1, 1024)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n256_tree_full_scan, unodb::db)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n256_tree_full_scan, unodb::mutex_db)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n256_tree_full_scan, unodb::olc_db)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n256_tree_random_gets, unodb::db)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n256_tree_random_gets, unodb::mutex_db)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n256_tree_random_gets, unodb::olc_db)
    ->Range(4, 4096)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n256_tree_full_scan, unodb::db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_full_scan, unodb::mutex_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_full_scan, unodb::olc_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n256_tree_random_gets, unodb::db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_random_gets, unodb::mutex_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_random_gets, unodb::olc_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n256_tree_sequential_delete, unodb::db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_sequential_delete, unodb::mutex_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_sequential_delete, unodb::olc_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n256_tree_random_delete, unodb::db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_random_delete, unodb::mutex_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n256_tree_random_delete, unodb::olc_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);

UNODB_BENCHMARK_MAIN();
