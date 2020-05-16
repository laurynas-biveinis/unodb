// Copyright 2019-2020 Laurynas Biveinis
#ifndef MICRO_BENCHMARK_HPP_
#define MICRO_BENCHMARK_HPP_

#include "global.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#ifndef NDEBUG
#include <iostream>
#endif
#ifdef __linux__
#include <malloc.h>
#endif

#include <benchmark/benchmark.h>

#include "art_common.hpp"

namespace unodb::benchmark {

constexpr auto value1 = std::array<std::byte, 1>{};
constexpr auto value10 = std::array<std::byte, 10>{};
constexpr auto value100 = std::array<std::byte, 100>{};
constexpr auto value1000 = std::array<std::byte, 1000>{};
constexpr auto value10000 = std::array<std::byte, 10000>{};

constexpr std::array<unodb::value_view, 5> values = {
    unodb::value_view{value1}, unodb::value_view{value10},
    unodb::value_view{value100}, unodb::value_view{value1000},
    unodb::value_view{value10000}};

template <class Db>
void insert_key(Db &db, unodb::key k, unodb::value_view v) {
  const auto result USED_IN_DEBUG = db.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert key " << k << '\n';
    std::cerr << "Current tree:";
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
void get_existing_key(const Db &db, unodb::key k) {
  const auto result = db.get(k);
  assert(result);
  ::benchmark::DoNotOptimize(result);
}

template <class Db>
void delete_key(Db &db, unodb::key k) {
  const auto result USED_IN_DEBUG = db.remove(k);
  assert(result);
  ::benchmark::ClobberMemory();
}

template <class Db>
void delete_key_if_exists(Db &db, unodb::key k) {
  (void)db.remove(k);
  ::benchmark::ClobberMemory();
}

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state) noexcept {
  state.PauseTiming();
  db.clear();
  ::benchmark::ClobberMemory();
  state.ResumeTiming();
}

inline void reset_heap() {
#ifdef __linux__
  malloc_trim(0);
#endif
}

}  // namespace unodb::benchmark

#endif  // MICRO_BENCHMARK_HPP_
