// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_GLOBAL_HPP_
#define UNODB_GLOBAL_HPP_

#include "config.hpp"

#ifdef DEBUG
#define _GLIBCXX_DEBUG
#define _GLIBCXX_DEBUG_PEDANTIC
#else
#define NDEBUG
#define GSL_UNENFORCED_ON_CONTRACT_VIOLATION
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define _GLIBCXX_SANITIZE_VECTOR 1
#endif

#endif  // UNODB_GLOBAL_HPP_
