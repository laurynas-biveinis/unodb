// Copyright 2022-2023 Laurynas Biveinis
#ifndef UNODB_DETAIL_ASSERT_HPP
#define UNODB_DETAIL_ASSERT_HPP

#include "global.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__) && !defined(__clang__)
#define BOOST_STACKTRACE_USE_BACKTRACE
#elif defined(__APPLE__)
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif
#include <boost/stacktrace.hpp>

#include "test_heap.hpp"

namespace unodb::detail {

// LCOV_EXCL_START

UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

[[noreturn, gnu::cold]] UNODB_DETAIL_NOINLINE inline void msg_stacktrace_abort(
    const std::string &msg) noexcept {
  UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(0);
  std::ostringstream buf;
  buf << msg << boost::stacktrace::stacktrace();
  std::cerr << buf.str();
  std::abort();
}

[[noreturn, gnu::cold]] UNODB_DETAIL_NOINLINE inline void crash(
    const char *file, int line, const char *func) noexcept {
  UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(0);
  std::ostringstream buf;
  buf << "Crash requested at " << file << ':' << line << ", function \"" << func
      << "\", thread " << std::this_thread::get_id() << '\n';
  msg_stacktrace_abort(buf.str());
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#ifndef NDEBUG

UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

[[noreturn, gnu::cold]] UNODB_DETAIL_NOINLINE inline void assert_failure(
    const char *file, int line, const char *func,
    const char *condition) noexcept {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(0);
  std::ostringstream buf;
  buf << "Assertion \"" << condition << "\" failed at " << file << ':' << line
      << ", function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

[[noreturn]] inline void cannot_happen(const char *file, int line,
                                       const char *func) noexcept {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(0);
  std::ostringstream buf;
  buf << "Execution reached an unreachable point at " << file << ':' << line
      << ": function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#define UNODB_DETAIL_ASSERT(condition)                                      \
  UNODB_DETAIL_UNLIKELY(!(condition))                                       \
  ? unodb::detail::assert_failure(__FILE__, __LINE__, __func__, #condition) \
  : ((void)0)

#define UNODB_DETAIL_DEBUG_CRASH() UNODB_DETAIL_CRASH()

#else  // #ifndef NDEBUG

[[noreturn]] inline void cannot_happen(const char *, int,
                                       const char *) noexcept {
  UNODB_DETAIL_UNREACHABLE();
}

#define UNODB_DETAIL_ASSERT(condition) ((void)0)

#define UNODB_DETAIL_DEBUG_CRASH()

#endif  // #ifndef NDEBUG

}  // namespace unodb::detail

#define UNODB_DETAIL_ASSUME(x)      \
  do {                              \
    UNODB_DETAIL_ASSERT(x);         \
    UNODB_DETAIL_BUILTIN_ASSUME(x); \
  } while (0)

#define UNODB_DETAIL_CANNOT_HAPPEN() \
  unodb::detail::cannot_happen(__FILE__, __LINE__, __func__)

#define UNODB_DETAIL_CRASH() unodb::detail::crash(__FILE__, __LINE__, __func__)

// LCOV_EXCL_STOP

#endif
