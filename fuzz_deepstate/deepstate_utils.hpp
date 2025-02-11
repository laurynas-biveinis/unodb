// Copyright 2021-2025 Laurynas Biveinis
#ifndef UNODB_DETAIL_DEEPSTATE_UTILS_HPP
#define UNODB_DETAIL_DEEPSTATE_UTILS_HPP

// Should be the first include
#include "global.hpp"

#include <cstddef>
#include <ctime>

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

/// The DeepState command line-specified timeout in seconds. We need it, but
/// it is not exposed through the public DeepState API, hence take the risk
/// and declare it ourselves.
extern "C" int FLAGS_timeout;

namespace unodb::test {

/// Check whether the DeepState test timeout has been reached.
/// \param[in] start_tm Test start timestamp in seconds since epoch
/// \return true if current time exceeds \a start_tm by more than the timeout
/// value.
///
/// The timeout value is specified via DeepState command line. Since the test
/// harness only checks it between the tests, we need additional checks for
/// long-running tests.
[[nodiscard]] bool inline timeout_reached(std::time_t start_tm) {
  const auto current_tm = std::time(nullptr);
  return current_tm - start_tm > FLAGS_timeout;
}

}  // namespace unodb::test

#endif  // UNODB_DETAIL_DEEPSTATE_UTILS_HPP
