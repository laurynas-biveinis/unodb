// Copyright 2021 Laurynas Biveinis
#ifndef DEEPSTATE_UTILS_HPP_
#define DEEPSTATE_UTILS_HPP_

#include "global.hpp"

#include <cstddef>

#include <deepstate/DeepState.hpp>

// warning: function 'DeepState_Run_ART_DeepState_fuzz' could be declared with
// attribute 'noreturn' [-Wmissing-noreturn]
#define UNODB_START_DEEPSTATE_TESTS() \
  DISABLE_CLANG_WARNING("-Wmissing-noreturn")

// Cast for logging to std::uint64_t to workaround
// https://github.com/trailofbits/deepstate/issues/138, but then
// error: useless cast to type ‘uint64_t’ {aka ‘long unsigned int’}
// [-Werror=useless-cast]
DISABLE_GCC_WARNING("-Wuseless-cast")

inline auto DeepState_SizeTInRange(std::size_t min, std::size_t max) {
  return static_cast<std::size_t>(DeepState_UInt64InRange(min, max));
}

template <class T>
auto DeepState_ContainerIndex(const T &container) {
  ASSERT(!container.empty());
  return DeepState_SizeTInRange(0, container.size() - 1);
}

#endif  // DEEPSTATE_UTILS_HPP_
