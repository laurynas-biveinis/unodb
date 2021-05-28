// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_GLOBAL_HPP_
#define UNODB_GLOBAL_HPP_

#include "config.hpp"

#ifdef DEBUG
#ifndef __clang__

#ifndef _GLIBCXX_DEBUG
#define _GLIBCXX_DEBUG
#endif

#ifndef _GLIBCXX_DEBUG_PEDANTIC
#define _GLIBCXX_DEBUG_PEDANTIC
#endif

#endif  // #ifndef __clang__

#else  // #ifdef DEBUG

#ifndef NDEBUG
#define NDEBUG
#endif

#endif  // #ifdef DEBUG

#if defined(__has_feature) && !defined(__clang__)
#if __has_feature(address_sanitizer)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define UNODB_THREAD_SANITIZER 1
#endif
#elif defined(__SANITIZE_THREAD__)
#define UNODB_THREAD_SANITIZER 1
#endif

#define DO_PRAGMA(x) _Pragma(#x)

#define DISABLE_WARNING(x) \
  _Pragma("GCC diagnostic push") DO_PRAGMA(GCC diagnostic ignored x)

#define RESTORE_WARNINGS(x) _Pragma("GCC diagnostic pop")

#ifdef __clang__
#define DISABLE_CLANG_WARNING(x) DISABLE_WARNING(x)
#define RESTORE_CLANG_WARNINGS() RESTORE_WARNINGS()
#else
#define DISABLE_CLANG_WARNING(x)
#define RESTORE_CLANG_WARNINGS()
#endif

#if defined(__GNUG__) && !defined(__clang__)
#define DISABLE_GCC_WARNING(x) DISABLE_WARNING(x)
#define RESTORE_GCC_WARNINGS() RESTORE_WARNINGS()
#if __GNUG__ == 10
#define DISABLE_GCC_10_WARNING(x) DISABLE_WARNING(x)
#define RESTORE_GCC_10_WARNINGS() RESTORE_WARNINGS()
#else  // __GNUG__ == 10
#define DISABLE_GCC_10_WARNING(x)
#define RESTORE_GCC_10_WARNINGS()
#endif  // __GNUG__ == 10
#if __GNUG__ >= 11
#define DISABLE_GCC_11_WARNING(x) DISABLE_WARNING(x)
#define RESTORE_GCC_11_WARNINGS() RESTORE_WARNINGS()
#else  // __GNUG__ >= 11
#define DISABLE_GCC_11_WARNING(x)
#define RESTORE_GCC_11_WARNINGS()
#endif  // __GNUG__ >= 11
#else   // defined(__GNUG__) && !defined(__clang__)
#define DISABLE_GCC_WARNING(x)
#define RESTORE_GCC_WARNINGS()
#define DISABLE_GCC_10_WARNING(x)
#define RESTORE_GCC_10_WARNINGS()
#define DISABLE_GCC_11_WARNING(x)
#define RESTORE_GCC_11_WARNINGS()
#endif  // defined(__GNUG__) && !defined(__clang__)

#define likely(x) __builtin_expect(x, 1)
#define unlikely(x) __builtin_expect(x, 0)

#ifdef NDEBUG
// Cannot do [[gnu::unused]], as that does not play well with structured
// bindings when compiling with GCC.
#define USED_IN_DEBUG __attribute__((unused))
#else
#define USED_IN_DEBUG
#endif

#ifdef NDEBUG
#define RELEASE_CONSTEXPR constexpr
#define RELEASE_CONST const
#else
#define RELEASE_CONSTEXPR
#define RELEASE_CONST
#endif

#if defined(__GNUG__) && !defined(__clang__)
#define USE_STD_PMR
#endif

#ifndef NDEBUG
#include <cstdlib>
#include <iostream>
#endif

// LCOV_EXCL_START
namespace unodb::detail {

[[noreturn]] inline void cannot_happen(const char *file USED_IN_DEBUG,
                                       int line USED_IN_DEBUG,
                                       const char *func USED_IN_DEBUG) {
#ifndef NDEBUG
  std::cerr << "Execution reached an unreachable point at " << file << ':'
            << line << ": " << func << '\n';
  std::abort();
#endif
  __builtin_unreachable();
}
// LCOV_EXCL_STOP

}  // namespace unodb::detail

#define CANNOT_HAPPEN() \
  unodb::detail::cannot_happen(__FILE__, __LINE__, __func__)

#endif  // UNODB_GLOBAL_HPP_
