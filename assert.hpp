// Copyright 2022-2025 UnoDB contributors
#ifndef UNODB_DETAIL_ASSERT_HPP
#define UNODB_DETAIL_ASSERT_HPP

/// \file assert.hpp
/// \brief Internal macros for assertions, assumptions & intentional crashing
///
/// If compiling as a part of another project, they will expand to C++ standard
/// symbols (assert & std::abort). Otherwise, custom implementations are
/// used that will show stacktraces if Boost.Stacktrace is available.

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>

#ifdef UNODB_DETAIL_BOOST_STACKTRACE
#if defined(__linux__) && !defined(__clang__)
#define BOOST_STACKTRACE_USE_BACKTRACE
#elif defined(__APPLE__)
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif
#include <boost/stacktrace.hpp>
#endif

#include "test_heap.hpp"

namespace unodb::detail {

// LCOV_EXCL_START

UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

/// Print a message and a stacktrace to std::cerr, then abort.
[[noreturn, gnu::cold]] UNODB_DETAIL_HEADER_NOINLINE void msg_stacktrace_abort(
    std::string_view msg) noexcept {
  UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(0);
  std::ostringstream buf;
  buf << msg;
#ifdef UNODB_DETAIL_BOOST_STACKTRACE
  buf << boost::stacktrace::stacktrace();
#else
  buf << "(stacktrace not available, not compiled with Boost.Stacktrace)\n";
#endif
  std::cerr << buf.str();
  std::abort();
}

/// Intentionally crash from a given source location.
[[noreturn, gnu::cold]] UNODB_DETAIL_C_STRING_ARG(1)
    UNODB_DETAIL_C_STRING_ARG(3) UNODB_DETAIL_HEADER_NOINLINE
    void crash(const char *file, int line, const char *func) noexcept {
  UNODB_DETAIL_FAIL_ON_NTH_ALLOCATION(0);
  std::ostringstream buf;
  buf << "Crash requested at " << file << ':' << line << ", function \"" << func
      << "\", thread " << std::this_thread::get_id() << '\n';
  msg_stacktrace_abort(buf.str());
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

// Definitions that only depend on Debug vs Release
#ifndef NDEBUG

/// Crash with a stacktrace on debug build, do nothing on release build.
#define UNODB_DETAIL_DEBUG_CRASH() UNODB_DETAIL_CRASH()

/// Implementation for marking a source code location as unreachable.
[[noreturn]] UNODB_DETAIL_C_STRING_ARG(1)
    UNODB_DETAIL_C_STRING_ARG(3) inline void cannot_happen(
        const char *file, int line, const char *func) noexcept {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(0);
  std::ostringstream buf;
  buf << "Execution reached an unreachable point at " << file << ':' << line
      << ": function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

#else  // !NDEBUG

#define UNODB_DETAIL_DEBUG_CRASH()

[[noreturn]] UNODB_DETAIL_C_STRING_ARG(1) UNODB_DETAIL_C_STRING_ARG(
    3) inline void cannot_happen(const char *, int, const char *) noexcept {
  UNODB_DETAIL_UNREACHABLE();
}

#endif  // !NDEBUG

// Definitions that only depend on standalone vs part of another project
#ifdef UNODB_DETAIL_STANDALONE

/// Intentionally crash.
#define UNODB_DETAIL_CRASH() unodb::detail::crash(__FILE__, __LINE__, __func__)

#else  // UNODB_DETAIL_STANDALONE

#define UNODB_DETAIL_CRASH() std::abort()

#endif  // UNODB_DETAIL_STANDALONE

// Definitions that depend on both Debug vs Release and standalone vs part of
// another project
#ifndef UNODB_DETAIL_STANDALONE

#define UNODB_DETAIL_ASSERT(condition) assert(condition)

#elif !defined(NDEBUG)

UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

/// Assert failure implementation for standalone debug build.
[[noreturn, gnu::cold]] UNODB_DETAIL_C_STRING_ARG(1)
    UNODB_DETAIL_C_STRING_ARG(3)
        UNODB_DETAIL_C_STRING_ARG(4) UNODB_DETAIL_HEADER_NOINLINE
    void assert_failure(const char *file, int line, const char *func,
                        const char *condition) noexcept {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(0);
  std::ostringstream buf;
  buf << "Assertion \"" << condition << "\" failed at " << file << ':' << line
      << ", function \"" << func << "\", thread " << std::this_thread::get_id()
      << '\n';
  msg_stacktrace_abort(buf.str());
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

/// Assert a condition.
///
/// Should be used everywhere instead of the standard assert macro and will
/// expand to it if building as a part of another project, thus using its
/// replaced declaration, if any. If building standalone, will print a
/// stacktrace on failures if Boost.Stacktrace is available.
#define UNODB_DETAIL_ASSERT(condition)                                      \
  UNODB_DETAIL_UNLIKELY(!(condition))                                       \
  ? unodb::detail::assert_failure(__FILE__, __LINE__, __func__, #condition) \
  : ((void)0)

#else  // !defined(NDEBUG)

#define UNODB_DETAIL_ASSERT(condition) ((void)0)

#endif  // !defined(NDEBUG)

}  // namespace unodb::detail

/// Provide an assumption for the compiler.
///
/// The assumption is expressed as a condition that always holds, i.e. an
/// allowed value range for a variable, which may allow the compiler to e.g.
/// optimize a redundant check away or silence a warning. Plain assertions
/// should be used almost always instead, and replaced with assumptions only
/// with provable effect on the diagnostics or generated code.
#define UNODB_DETAIL_ASSUME(x)      \
  do {                              \
    UNODB_DETAIL_ASSERT(x);         \
    UNODB_DETAIL_BUILTIN_ASSUME(x); \
  } while (0)

/// Mark this source code location as unreachable.
///
/// Under release build the location is annotated for the compiler as
/// unreachable, potentially enabling more optimizations. Under debug build, if
/// execution comes here, it will crash with a stacktrace.
#define UNODB_DETAIL_CANNOT_HAPPEN() \
  unodb::detail::cannot_happen(__FILE__, __LINE__, __func__)

// LCOV_EXCL_STOP

#endif
