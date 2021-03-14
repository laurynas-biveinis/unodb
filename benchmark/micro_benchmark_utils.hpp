// Copyright 2019-2021 Laurynas Biveinis
#ifndef MICRO_BENCHMARK_UTILS_HPP_
#define MICRO_BENCHMARK_UTILS_HPP_

#include "global.hpp"

#include <array>
#include <cassert>
#ifndef NDEBUG
#include <iostream>
#endif
#include <random>

#include <benchmark/benchmark.h>

#include "art_common.hpp"

namespace unodb {
class db;
class mutex_db;
class olc_db;
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

// PRNG

inline auto &get_prng() {
  static std::random_device rd;
  static std::mt19937 gen{rd()};
  return gen;
}

// Inserts

template <class Db>
void insert_key(Db &db, unodb::key k, unodb::value_view v) {
  const auto result USED_IN_DEBUG = db.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert ";
    detail::dump_key(std::cerr, k);
    std::cerr << "\nCurrent tree:";
    db.dump(std::cerr);
    assert(result);
  }
#endif
  ::benchmark::ClobberMemory();
}

// Deletes

template <class Db>
void delete_key(Db &db, unodb::key k) {
  const auto result USED_IN_DEBUG = db.remove(k);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to delete existing ";
    detail::dump_key(std::cerr, k);
    std::cerr << "\nTree:";
    db.dump(std::cerr);
    assert(result);
  }
#endif
  assert(result);
  ::benchmark::ClobberMemory();
}

// Gets

template <class Db>
void get_existing_key(const Db &db, unodb::key k) {
  const auto result = db.get(k);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to get existing ";
    detail::dump_key(std::cerr, k);
    std::cerr << "\nTree:";
    db.dump(std::cerr);
    assert(result);
  }
#endif
  ::benchmark::DoNotOptimize(result);
}

// Teardown

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state) noexcept;

extern template void destroy_tree<unodb::db>(unodb::db &,
                                             ::benchmark::State &) noexcept;
extern template void destroy_tree<unodb::mutex_db>(
    unodb::mutex_db &, ::benchmark::State &) noexcept;
extern template void destroy_tree<unodb::olc_db>(unodb::olc_db &,
                                                 ::benchmark::State &) noexcept;

}  // namespace unodb::benchmark

#endif  // MICRO_BENCHMARK_UTILS_HPP_
