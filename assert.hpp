// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_ASSERT_HPP
#define UNODB_DETAIL_ASSERT_HPP

#include "global.hpp"

#ifndef NDEBUG
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

namespace unodb::detail {

// LCOV_EXCL_START

[[noreturn, gnu::cold]] UNODB_DETAIL_NOINLINE inline void msg_stacktrace_abort(
    const std::string &msg) noexcept {
  std::ostringstream buf;
  buf << msg << boost::stacktrace::stacktrace();
  std::cerr << buf.str();
  std::abort();
}

[[noreturn, gnu::cold]] UNODB_DETAIL_NOINLINE inline void assert_failure(
    const char *file, int line, const char *func, const char *condition) {
  std::ostringstream buf;
  buf << "Assertion \"" << condition << "\" failed at " << file << ':' << line
      << ", function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

[[noreturn, gnu::cold]] UNODB_DETAIL_NOINLINE inline void crash(
    const char *file, int line, const char *func) {
  std::ostringstream buf;
  buf << "Crash requested at " << file << ':' << line << ", function \"" << func
      << "\", thread " << std::this_thread::get_id() << '\n';
  msg_stacktrace_abort(buf.str());
}

[[noreturn]] inline void cannot_happen(const char *file, int line,
                                       const char *func) {
  std::ostringstream buf;
  buf << "Execution reached an unreachable point at " << file << ':' << line
      << ": function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

#define UNODB_DETAIL_ASSERT(condition)                                      \
  UNODB_DETAIL_UNLIKELY(!(condition))                                       \
  ? unodb::detail::assert_failure(__FILE__, __LINE__, __func__, #condition) \
  : ((void)0)

}  // namespace unodb::detail

#else  // #ifndef NDEBUG

namespace unodb::detail {

[[noreturn]] inline void cannot_happen(const char *, int, const char *) {
  UNODB_DETAIL_UNREACHABLE();
}

}  // namespace unodb::detail

#define UNODB_DETAIL_ASSERT(condition) ((void)0)

#endif  // #ifndef NDEBUG

#define UNODB_DETAIL_CANNOT_HAPPEN() \
  unodb::detail::cannot_happen(__FILE__, __LINE__, __func__)

#define UNODB_DETAIL_CRASH() unodb::detail::crash(__FILE__, __LINE__, __func__)

// LCOV_EXCL_STOP

#endif
