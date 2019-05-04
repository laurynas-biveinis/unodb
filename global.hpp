// Copyright 2019-2020 Laurynas Biveinis
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

#if defined(__GNUG__) && !defined(__clang__)
#define USE_STD_PMR
#endif

#endif  // UNODB_GLOBAL_HPP_
