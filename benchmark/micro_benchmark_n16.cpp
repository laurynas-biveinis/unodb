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

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "micro_benchmark_node_utils.hpp"
#include "micro_benchmark_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace {

template <class Db>
void grow_n4_to_n16_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<Db, 4>(state);
}

template <class Db>
void grow_n4_to_n16_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<Db, 4>(state);
}

template <class Db>
void n16_sequential_add(benchmark::State &state) {
  unodb::benchmark::sequential_add_benchmark<Db, 16>(state);
}

template <class Db>
void n16_random_add(benchmark::State &state) {
  unodb::benchmark::random_add_benchmark<Db, 16>(state);
}

template <class Db>
void minimal_n16_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::minimal_tree_full_scan<Db, 16>(state);
}

template <class Db>
void minimal_n16_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::minimal_tree_random_gets<Db, 16>(state);
}

template <class Db>
void full_n16_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<Db, 16>(state);
}

template <class Db>
void full_n16_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<Db, 16>(state);
}

template <class Db>
void full_n16_tree_sequential_delete(benchmark::State &state) {
  unodb::benchmark::sequential_delete_benchmark<Db, 16>(state);
}

template <class Db>
void full_n16_tree_random_delete(benchmark::State &state) {
  unodb::benchmark::random_delete_benchmark<Db, 16>(state);
}

template <class Db>
void shrink_n48_to_n16_sequentially(benchmark::State &state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<Db, 16>(state);
}

template <class Db>
void shrink_n48_to_n16_randomly(benchmark::State &state) {
  unodb::benchmark::shrink_node_randomly_benchmark<Db, 16>(state);
}

}  // namespace

UNODB_START_BENCHMARKS()

BENCHMARK_TEMPLATE(grow_n4_to_n16_sequentially, unodb::benchmark::db)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n4_to_n16_sequentially, unodb::benchmark::mutex_db)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n4_to_n16_sequentially, unodb::benchmark::olc_db)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(grow_n4_to_n16_randomly, unodb::benchmark::db)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n4_to_n16_randomly, unodb::benchmark::mutex_db)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n4_to_n16_randomly, unodb::benchmark::olc_db)
    ->Range(20, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n16_sequential_add, unodb::benchmark::db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n16_sequential_add, unodb::benchmark::mutex_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n16_sequential_add, unodb::benchmark::olc_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n16_random_add, unodb::benchmark::db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n16_random_add, unodb::benchmark::mutex_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n16_random_add, unodb::benchmark::olc_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n16_tree_full_scan, unodb::benchmark::db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n16_tree_full_scan, unodb::benchmark::mutex_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n16_tree_full_scan, unodb::benchmark::olc_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n16_tree_random_gets, unodb::benchmark::db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n16_tree_random_gets, unodb::benchmark::mutex_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n16_tree_random_gets, unodb::benchmark::olc_db)
    ->Range(10, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n16_tree_full_scan, unodb::benchmark::db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_full_scan, unodb::benchmark::mutex_db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_full_scan, unodb::benchmark::olc_db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n16_tree_random_gets, unodb::benchmark::db)
    ->Range(64, 24600)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_random_gets, unodb::benchmark::mutex_db)
    ->Range(64, 24600)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_random_gets, unodb::benchmark::olc_db)
    ->Range(64, 24600)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n16_tree_sequential_delete, unodb::benchmark::db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_sequential_delete, unodb::benchmark::mutex_db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_sequential_delete, unodb::benchmark::olc_db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n16_tree_random_delete, unodb::benchmark::db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_random_delete, unodb::benchmark::mutex_db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n16_tree_random_delete, unodb::benchmark::olc_db)
    ->Range(64, 246000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(shrink_n48_to_n16_sequentially, unodb::benchmark::db)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n48_to_n16_sequentially, unodb::benchmark::mutex_db)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n48_to_n16_sequentially, unodb::benchmark::olc_db)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(shrink_n48_to_n16_randomly, unodb::benchmark::db)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n48_to_n16_randomly, unodb::benchmark::mutex_db)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n48_to_n16_randomly, unodb::benchmark::olc_db)
    ->Range(4, 16383)
    ->Unit(benchmark::kMicrosecond);

UNODB_BENCHMARK_MAIN();
