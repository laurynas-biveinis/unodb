// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_MICRO_BENCHMARK_UTILS_HPP
#define UNODB_DETAIL_MICRO_BENCHMARK_UTILS_HPP

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include <array>
#include <cstddef>
#ifndef NDEBUG
#include <iostream>
#endif
#include <random>

#include <benchmark/benchmark.h>

#include "art_common.hpp"
#ifndef NDEBUG
#include "assert.hpp"
#endif
#include "olc_art.hpp"
#include "qsbr.hpp"

// TODO(laurynas): std::uint64_t-specific

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
template <typename>
class db;  // IWYU pragma: keep

template <typename>
class mutex_db;  // IWYU pragma: keep
}  // namespace unodb

namespace unodb::benchmark {

// Benchmarked tree types

using db = unodb::db<std::uint64_t>;
using mutex_db = unodb ::mutex_db<std::uint64_t>;
using olc_db = unodb::olc_db<std::uint64_t>;

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
void do_insert_key_ignore_dups(Db &instance, std::uint64_t k,
                               unodb::value_view v) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = instance.insert(k, v);
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

template <class Db>
void do_insert_key(Db &instance, std::uint64_t k, unodb::value_view v) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = instance.insert(k, v);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to insert ";
    ::unodb::detail::dump_key(std::cerr, k);
    std::cerr << "\nCurrent tree:";
    instance.dump(std::cerr);
    UNODB_DETAIL_ASSERT(result);
  }
#endif
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

}  // namespace detail

template <class Db>
void insert_key_ignore_dups(Db &instance, std::uint64_t k,
                            unodb::value_view v) {
  detail::do_insert_key_ignore_dups(instance, k, v);
}

template <>
inline void insert_key_ignore_dups(unodb::olc_db<std ::uint64_t> &instance,
                                   std::uint64_t k, unodb::value_view v) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_insert_key_ignore_dups(instance, k, v);
}

template <class Db>
void insert_key(Db &instance, std::uint64_t k, unodb::value_view v) {
  detail::do_insert_key(instance, k, v);
}

template <>
inline void insert_key(unodb::olc_db<std::uint64_t> &instance, std::uint64_t k,
                       unodb::value_view v) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_insert_key(instance, k, v);
}

// Deletes

namespace detail {

template <class Db>
void do_delete_key_if_exists(Db &instance, std::uint64_t k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = instance.remove(k);
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

template <class Db>
void do_delete_key(Db &instance, std::uint64_t k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = instance.remove(k);
#ifndef NDEBUG
  if (!result) {
    std::cerr << "Failed to delete existing ";
    ::unodb::detail::dump_key(std::cerr, k);
    std::cerr << "\nTree:";
    instance.dump(std::cerr);
    UNODB_DETAIL_ASSERT(result);
  }
#endif
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

}  // namespace detail

template <class Db>
void delete_key_if_exists(Db &instance, std::uint64_t k) {
  detail::do_delete_key_if_exists(instance, k);
}

template <>
inline void delete_key_if_exists(unodb::olc_db<std::uint64_t> &instance,
                                 std::uint64_t k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_delete_key_if_exists(instance, k);
}

template <class Db>
void delete_key(Db &instance, std::uint64_t k) {
  detail::do_delete_key(instance, k);
}

template <>
inline void delete_key(unodb::olc_db<std::uint64_t> &instance,
                       std::uint64_t k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_delete_key(instance, k);
}

// Gets

namespace detail {

template <class Db>
void do_get_key(const Db &instance, std::uint64_t k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = instance.get(k);
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

template <class Db>
void do_get_existing_key(const Db &instance, std::uint64_t k) {
  // Args to ::benchmark::DoNoOptimize cannot be const, thus silence MSVC static
  // analyzer on that
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  auto result = instance.get(k);

#ifndef NDEBUG
  if (!Db::key_found(result)) {
    std::cerr << "Failed to get existing ";
    ::unodb::detail::dump_key(std::cerr, k);
    std::cerr << "\nTree:";
    instance.dump(std::cerr);
    UNODB_DETAIL_CRASH();
  }
#endif
  ::benchmark::DoNotOptimize(result);
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
}

}  // namespace detail

template <class Db>
void get_key(const Db &instance, std::uint64_t k) {
  detail::do_get_key(instance, k);
}

template <>
inline void get_key(const unodb::olc_db<std::uint64_t> &instance,
                    std::uint64_t k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_get_key(instance, k);
}

template <class Db>
void get_existing_key(const Db &instance, std::uint64_t k) {
  detail::do_get_existing_key(instance, k);
}

template <>
inline void get_existing_key(const unodb::olc_db<std::uint64_t> &instance,
                             std::uint64_t k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  detail::do_get_existing_key(instance, k);
}

// Teardown

template <class Db>
void destroy_tree(Db &instance, ::benchmark::State &state);

extern template void destroy_tree<unodb::db<std::uint64_t>>(
    unodb::db<std::uint64_t> &, ::benchmark::State &);
extern template void destroy_tree<unodb::mutex_db<std::uint64_t>>(
    unodb::mutex_db<std::uint64_t> &, ::benchmark::State &);
extern template void destroy_tree<unodb::olc_db<std::uint64_t>>(
    unodb::olc_db<std::uint64_t> &, ::benchmark::State &);

}  // namespace unodb::benchmark

#endif  // UNODB_DETAIL_MICRO_BENCHMARK_UTILS_HPP
