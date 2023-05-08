// Copyright 2019-2023 Laurynas Biveinis
#ifndef UNODB_DETAIL_MICRO_BENCHMARK_UTILS_HPP
#define UNODB_DETAIL_MICRO_BENCHMARK_UTILS_HPP

// IWYU pragma: no_include <__random/mersenne_twister_engine.h>
// IWYU pragma: no_include <__random/random_device.h>

#include "global.hpp"

#include <array>
#include <cstddef>
#ifndef NDEBUG
#include <iostream>
#endif
#include <random>  // IWYU pragma: keep

#include <benchmark/benchmark.h>

#include "art_common.hpp"
#ifndef NDEBUG
#include "assert.hpp"
#endif
#include "olc_art.hpp"
#include "qsbr.hpp"

#define UNODB_START_BENCHMARKS()           \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26409) \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26426)

#define UNODB_BENCHMARK_MAIN()             \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()     \
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26485) \
  BENCHMARK_MAIN()                         \
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

namespace unodb {
class db;        // IWYU pragma: keep
class mutex_db;  // IWYU pragma: keep
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

[[nodiscard]] inline auto &get_prng() {
  static std::random_device rd;
  static std::mt19937 gen{rd()};
  return gen;
}

// Inserts

namespace detail {

template <class Db>
void do_insert_key_ignore_dups(Db &db, unodb::key k, unodb::value_view v) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = db.insert(k, v);
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

template <class Db>
void do_insert_key(Db &db, unodb::key k, unodb::value_view v) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = db.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert ";
    ::unodb::detail::dump_key(std::cerr, k);
    std::cerr << "\nCurrent tree:";
    db.dump(std::cerr);
    UNODB_DETAIL_ASSERT(result);
  }
#endif
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

}  // namespace detail

template <class Db>
void insert_key_ignore_dups(Db &db, unodb::key k, unodb::value_view v) {
  detail::do_insert_key_ignore_dups(db, k, v);
}

template <>
inline void insert_key_ignore_dups(unodb::olc_db &db, unodb::key k,
                                   unodb::value_view v) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_insert_key_ignore_dups(db, k, v);
}

template <class Db>
void insert_key(Db &db, unodb::key k, unodb::value_view v) {
  detail::do_insert_key(db, k, v);
}

template <>
inline void insert_key(unodb::olc_db &db, unodb::key k, unodb::value_view v) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_insert_key(db, k, v);
}

// Deletes

namespace detail {

template <class Db>
void do_delete_key_if_exists(Db &db, unodb::key k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = db.remove(k);
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

template <class Db>
void do_delete_key(Db &db, unodb::key k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = db.remove(k);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to delete existing ";
    ::unodb::detail::dump_key(std::cerr, k);
    std::cerr << "\nTree:";
    db.dump(std::cerr);
    UNODB_DETAIL_ASSERT(result);
  }
#endif
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

}  // namespace detail

template <class Db>
void delete_key_if_exists(Db &db, unodb::key k) {
  detail::do_delete_key_if_exists(db, k);
}

template <>
inline void delete_key_if_exists(unodb::olc_db &db, unodb::key k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_delete_key_if_exists(db, k);
}

template <class Db>
void delete_key(Db &db, unodb::key k) {
  detail::do_delete_key(db, k);
}

template <>
inline void delete_key(unodb::olc_db &db, unodb::key k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_delete_key(db, k);
}

// Gets

namespace detail {

template <class Db>
void do_get_key(const Db &db, unodb::key k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = db.get(k);
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

template <class Db>
void do_get_existing_key(const Db &db, unodb::key k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = db.get(k);

#ifndef NDEBUG
  if (!Db::key_found(result)) {
    std::cerr << "Failed to get existing ";
    ::unodb::detail::dump_key(std::cerr, k);
    std::cerr << "\nTree:";
    db.dump(std::cerr);
    UNODB_DETAIL_CRASH();
  }
#endif
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

}  // namespace detail

template <class Db>
void get_key(const Db &db, unodb::key k) {
  detail::do_get_key(db, k);
}

template <>
inline void get_key(const unodb::olc_db &db, unodb::key k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_get_key(db, k);
}

template <class Db>
void get_existing_key(const Db &db, unodb::key k) {
  detail::do_get_existing_key(db, k);
}

template <>
inline void get_existing_key(const unodb::olc_db &db, unodb::key k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_get_existing_key(db, k);
}

// Teardown

template <class Db>
void destroy_tree(Db &db, ::benchmark::State &state);

extern template void destroy_tree<unodb::db>(unodb::db &, ::benchmark::State &);
extern template void destroy_tree<unodb::mutex_db>(unodb::mutex_db &,
                                                   ::benchmark::State &);
extern template void destroy_tree<unodb::olc_db>(unodb::olc_db &,
                                                 ::benchmark::State &);

}  // namespace unodb::benchmark

#endif  // UNODB_DETAIL_MICRO_BENCHMARK_UTILS_HPP
