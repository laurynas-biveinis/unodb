// Copyright 2020-2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include <benchmark/benchmark.h>

#include "art.hpp"
#include "art_map_db.hpp"
#include "micro_benchmark_node_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace {

template <class Db>
void grow_n16_to_n48_sequentially(benchmark::State &state) {
  unodb::benchmark::grow_node_sequentially_benchmark<Db, 16>(state);
}

template <class Db>
void grow_n16_to_n48_randomly(benchmark::State &state) {
  unodb::benchmark::grow_node_randomly_benchmark<Db, 16>(state);
}

template <class Db>
void n48_sequential_add(benchmark::State &state) {
  unodb::benchmark::sequential_add_benchmark<Db, 48>(state);
}

template <class Db>
void n48_random_add(benchmark::State &state) {
  unodb::benchmark::random_add_benchmark<Db, 48>(state);
}

template <class Db>
void minimal_n48_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::minimal_tree_full_scan<Db, 48>(state);
}

template <class Db>
void minimal_n48_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::minimal_tree_random_gets<Db, 48>(state);
}

template <class Db>
void full_n48_tree_full_scan(benchmark::State &state) {
  unodb::benchmark::full_node_scan_benchmark<Db, 48>(state);
}

template <class Db>
void full_n48_tree_random_gets(benchmark::State &state) {
  unodb::benchmark::full_node_random_get_benchmark<Db, 48>(state);
}

template <class Db>
void full_n48_tree_sequential_delete(benchmark::State &state) {
  unodb::benchmark::sequential_delete_benchmark<Db, 48>(state);
}

template <class Db>
void full_n48_tree_random_delete(benchmark::State &state) {
  unodb::benchmark::random_delete_benchmark<Db, 48>(state);
}

template <class Db>
void shrink_n256_to_n48_sequentially(benchmark::State &state) {
  unodb::benchmark::shrink_node_sequentially_benchmark<Db, 48>(state);
}

template <class Db>
void shrink_n256_to_n48_randomly(benchmark::State &state) {
  unodb::benchmark::shrink_node_randomly_benchmark<Db, 48>(state);
}

}  // namespace

BENCHMARK_TEMPLATE(grow_n16_to_n48_sequentially, unodb::db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n16_to_n48_sequentially, unodb::mutex_db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n16_to_n48_sequentially, unodb::olc_db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n16_to_n48_sequentially, unodb::art_map_db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(grow_n16_to_n48_randomly, unodb::db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n16_to_n48_randomly, unodb::mutex_db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n16_to_n48_randomly, unodb::olc_db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(grow_n16_to_n48_randomly, unodb::art_map_db)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n48_sequential_add, unodb::db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n48_sequential_add, unodb::mutex_db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n48_sequential_add, unodb::olc_db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n48_sequential_add, unodb::art_map_db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(n48_random_add, unodb::db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n48_random_add, unodb::mutex_db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n48_random_add, unodb::olc_db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(n48_random_add, unodb::art_map_db)
    ->Range(2, 4096)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n48_tree_full_scan, unodb::db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n48_tree_full_scan, unodb::mutex_db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n48_tree_full_scan, unodb::olc_db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n48_tree_full_scan, unodb::art_map_db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(minimal_n48_tree_random_gets, unodb::db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n48_tree_random_gets, unodb::mutex_db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n48_tree_random_gets, unodb::olc_db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(minimal_n48_tree_random_gets, unodb::art_map_db)
    ->Range(4, 6144)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n48_tree_full_scan, unodb::db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_full_scan, unodb::mutex_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_full_scan, unodb::olc_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_full_scan, unodb::art_map_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n48_tree_random_gets, unodb::db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_random_gets, unodb::mutex_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_random_gets, unodb::olc_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_random_gets, unodb::art_map_db)
    ->Range(128, 131064)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n48_tree_sequential_delete, unodb::db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_sequential_delete, unodb::mutex_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_sequential_delete, unodb::olc_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_sequential_delete, unodb::art_map_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(full_n48_tree_random_delete, unodb::db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_random_delete, unodb::mutex_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_random_delete, unodb::olc_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(full_n48_tree_random_delete, unodb::art_map_db)
    ->Range(192, 196608)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(shrink_n256_to_n48_sequentially, unodb::db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n256_to_n48_sequentially, unodb::mutex_db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n256_to_n48_sequentially, unodb::olc_db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n256_to_n48_sequentially, unodb::art_map_db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(shrink_n256_to_n48_randomly, unodb::db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n256_to_n48_randomly, unodb::mutex_db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n256_to_n48_randomly, unodb::olc_db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_TEMPLATE(shrink_n256_to_n48_randomly, unodb::art_map_db)
    ->Range(4, 2048)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
