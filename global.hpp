// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_GLOBAL_HPP_
#define UNODB_GLOBAL_HPP_

#include "config.hpp"

#ifdef DEBUG
#ifndef __clang__
#define _GLIBCXX_DEBUG
#define _GLIBCXX_DEBUG_PEDANTIC
#endif
#else
#ifndef NDEBUG
#define NDEBUG
#endif
#define GSL_UNENFORCED_ON_CONTRACT_VIOLATION
#endif

#if defined(__has_feature) && !defined(__clang__)
#if __has_feature(address_sanitizer)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif

#define DO_PRAGMA(x) _Pragma(#x)

#ifdef __clang__
#define DISABLE_CLANG_WARNING(x) \
  _Pragma("GCC diagnostic push") DO_PRAGMA(GCC diagnostic ignored x)
#define RESTORE_CLANG_WARNINGS() _Pragma("GCC diagnostic pop")
#else
#define DISABLE_CLANG_WARNING(x)
#define RESTORE_CLANG_WARNINGS()
#endif

#if defined(__GNUG__) && !defined(__clang__)
#define DISABLE_GCC_WARNING(x) \
  _Pragma("GCC diagnostic push") DO_PRAGMA(GCC diagnostic ignored x)
#define RESTORE_GCC_WARNINGS() _Pragma("GCC diagnostic pop")
#else
#define DISABLE_GCC_WARNING(x)
#define RESTORE_GCC_WARNINGS()
#endif

#define likely(x) __builtin_expect(x, 1)
#define unlikely(x) __builtin_expect(x, 0)

#endif  // UNODB_GLOBAL_HPP_
