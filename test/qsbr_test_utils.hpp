// Copyright 2022-2025 UnoDB contributors
#ifndef UNODB_DETAIL_QSBR_TEST_UTILS_HPP
#define UNODB_DETAIL_QSBR_TEST_UTILS_HPP

/// \file
/// QSBR test utilities.
///
/// \ingroup test-internals
///
/// Utilities for testing \ref qsbr system.

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

namespace unodb::test {

/// Expects in Google Test that \ref qsbr is idle.
///
/// Idle QSBR has no pending deallocation requests, and no more than one
/// QSBR-registered thread.
void expect_idle_qsbr() noexcept;

}  // namespace unodb::test

#endif  // UNODB_DETAIL_QSBR_TEST_UTILS_HPP
