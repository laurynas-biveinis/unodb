// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_GLOBAL_HPP
#define UNODB_DETAIL_GLOBAL_HPP

#ifndef NDEBUG
#ifndef __clang__

#ifndef _GLIBCXX_DEBUG
#define _GLIBCXX_DEBUG
#endif

#ifndef _GLIBCXX_DEBUG_PEDANTIC
#define _GLIBCXX_DEBUG_PEDANTIC
#endif

#endif  // #ifndef __clang__
#endif  // #ifndef NDEBUG

#if defined(__has_feature) && !defined(__clang__)
#if __has_feature(address_sanitizer)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define UNODB_DETAIL_THREAD_SANITIZER 1
#endif
#elif defined(__SANITIZE_THREAD__)
#define UNODB_DETAIL_THREAD_SANITIZER 1
#endif

#define UNODB_DETAIL_DO_PRAGMA(x) _Pragma(#x)

#define UNODB_DETAIL_DISABLE_WARNING(x) \
  _Pragma("GCC diagnostic push")        \
      UNODB_DETAIL_DO_PRAGMA(GCC diagnostic ignored x)

#define UNODB_DETAIL_RESTORE_WARNINGS() _Pragma("GCC diagnostic pop")

#ifdef __clang__
#define UNODB_DETAIL_DISABLE_CLANG_WARNING(x) UNODB_DETAIL_DISABLE_WARNING(x)
#define UNODB_DETAIL_RESTORE_CLANG_WARNINGS() UNODB_DETAIL_RESTORE_WARNINGS()
#else
#define UNODB_DETAIL_DISABLE_CLANG_WARNING(x)
#define UNODB_DETAIL_RESTORE_CLANG_WARNINGS()
#endif

#if defined(__GNUG__) && !defined(__clang__)
#define UNODB_DETAIL_DISABLE_GCC_WARNING(x) UNODB_DETAIL_DISABLE_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_WARNINGS() UNODB_DETAIL_RESTORE_WARNINGS()
#if __GNUG__ == 10
#define UNODB_DETAIL_DISABLE_GCC_10_WARNING(x) UNODB_DETAIL_DISABLE_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_10_WARNINGS() UNODB_DETAIL_RESTORE_WARNINGS()
#else  // __GNUG__ == 10
#define UNODB_DETAIL_DISABLE_GCC_10_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_10_WARNINGS()
#endif  // __GNUG__ == 10
#if __GNUG__ >= 11
#define UNODB_DETAIL_DISABLE_GCC_11_WARNING(x) UNODB_DETAIL_DISABLE_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_11_WARNINGS() UNODB_DETAIL_RESTORE_WARNINGS()
#else  // __GNUG__ >= 11
#define UNODB_DETAIL_DISABLE_GCC_11_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_11_WARNINGS()
#endif  // __GNUG__ >= 11
#else   // defined(__GNUG__) && !defined(__clang__)
#define UNODB_DETAIL_DISABLE_GCC_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_WARNINGS()
#define UNODB_DETAIL_DISABLE_GCC_10_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_10_WARNINGS()
#define UNODB_DETAIL_DISABLE_GCC_11_WARNING(x)
#define UNODB_DETAIL_RESTORE_GCC_11_WARNINGS()
#endif  // defined(__GNUG__) && !defined(__clang__)

#define UNODB_DETAIL_LIKELY(x) __builtin_expect(x, 1)
#define UNODB_DETAIL_UNLIKELY(x) __builtin_expect(x, 0)

#ifdef NDEBUG
// Cannot do [[gnu::unused]], as that does not play well with structured
// bindings when compiling with GCC.
#define UNODB_DETAIL_USED_IN_DEBUG __attribute__((unused))
#else
#define UNODB_DETAIL_USED_IN_DEBUG
#endif

#ifdef NDEBUG
#define UNODB_DETAIL_RELEASE_CONSTEXPR constexpr
#define UNODB_DETAIL_RELEASE_CONST const
#define UNODB_DETAIL_RELEASE_EXPLICIT explicit
#else
#define UNODB_DETAIL_RELEASE_CONSTEXPR
#define UNODB_DETAIL_RELEASE_CONST
#define UNODB_DETAIL_RELEASE_EXPLICIT
#endif

#include <cstdlib>
#include <new>

#if !defined(__cpp_lib_hardware_interference_size) || \
    __cpp_lib_hardware_interference_size < 201703

namespace unodb::detail {

#ifdef __x86_64
inline constexpr std::size_t hardware_constructive_interference_size = 64;
// Two cache lines for destructive interference due to Intel fetching cache
// lines in pairs
inline constexpr std::size_t hardware_destructive_interference_size = 128;
#else
#error Needs porting
#endif

}  // namespace unodb::detail

#else

using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;

#endif

#ifndef NDEBUG
#include <execinfo.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <thread>

// LCOV_EXCL_START
namespace unodb::detail {

[[noreturn, gnu::cold, gnu::noinline]] inline void msg_stacktrace_abort(
    const std::string &msg) noexcept {
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  void *stacktrace[1024];
  const auto frames = backtrace(stacktrace, 1024);

  std::cerr << msg;
  // Ideally I'd like to call abi::__cxa_demangle on the symbol names but that
  // would require parsing the strings which contain more than just symbol names
  backtrace_symbols_fd(stacktrace, frames, STDERR_FILENO);
  std::abort();
}

[[noreturn, gnu::cold, gnu::noinline]] inline void assert_failure(
    const char *file, int line, const char *func, const char *condition) {
  std::ostringstream buf;
  buf << "Assertion \"" << condition << "\" failed at " << file << ':' << line
      << ", function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

[[noreturn, gnu::cold, gnu::noinline]] inline void crash(const char *file,
                                                         int line,
                                                         const char *func) {
  std::ostringstream buf;
  buf << "Crash requested at " << file << ':' << line << ", function \"" << func
      << "\", thread " << std::this_thread::get_id() << '\n';
  msg_stacktrace_abort(buf.str());
}

}  // namespace unodb::detail

#endif

namespace unodb::detail {

[[noreturn]] inline void cannot_happen(
    const char *file UNODB_DETAIL_USED_IN_DEBUG,
    int line UNODB_DETAIL_USED_IN_DEBUG,
    const char *func UNODB_DETAIL_USED_IN_DEBUG) {
#ifndef NDEBUG
  std::ostringstream buf;
  buf << "Execution reached an unreachable point at " << file << ':' << line
      << ": function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
#endif
  __builtin_unreachable();
}
// LCOV_EXCL_STOP

}  // namespace unodb::detail

#define UNODB_DETAIL_CANNOT_HAPPEN() \
  unodb::detail::cannot_happen(__FILE__, __LINE__, __func__)

#define UNODB_DETAIL_CRASH() unodb::detail::crash(__FILE__, __LINE__, __func__)

#ifdef NDEBUG
#define UNODB_DETAIL_ASSERT(condition) ((void)0)
#else
#define UNODB_DETAIL_ASSERT(condition)                                      \
  UNODB_DETAIL_UNLIKELY(!(condition))                                       \
  ? unodb::detail::assert_failure(__FILE__, __LINE__, __func__, #condition) \
  : ((void)0)
#endif

#endif  // UNODB_DETAIL_GLOBAL_HPP
