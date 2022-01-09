// Copyright 2019-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_GLOBAL_HPP
#define UNODB_DETAIL_GLOBAL_HPP

// Defines that must precede includes

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

// Architecture

#ifdef __x86_64
#define UNODB_DETAIL_X86_64
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
#define UNODB_DETAIL_UNUSED __attribute__((unused))
#define UNODB_DETAIL_FORCE_INLINE __attribute__((always_inline))
#define UNODB_DETAIL_NOINLINE __attribute__((noinline))

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

#endif  // UNODB_DETAIL_GLOBAL_HPP
