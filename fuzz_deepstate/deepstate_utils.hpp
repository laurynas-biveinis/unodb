// Copyright 2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_DEEPSTATE_UTILS_HPP
#define UNODB_DETAIL_DEEPSTATE_UTILS_HPP

#include "global.hpp"

#include <cstddef>

#include <deepstate/DeepState.hpp>

// warning: function 'DeepState_Run_ART_DeepState_fuzz' could be declared with
// attribute 'noreturn' [-Wmissing-noreturn]
#define UNODB_START_DEEPSTATE_TESTS() \
  UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-noreturn")

[[nodiscard]] inline std::size_t DeepState_SizeTInRange(std::size_t min,
                                                        std::size_t max) {
  return DeepState_UInt64InRange(min, max);
}

template <class T>
[[nodiscard]] auto DeepState_ContainerIndex(const T &container) {
  ASSERT(!container.empty());
  return DeepState_SizeTInRange(0, std::size(container) - 1);
}

#endif  // UNODB_DETAIL_DEEPSTATE_UTILS_HPP
