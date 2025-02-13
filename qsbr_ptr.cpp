// Copyright (C) 2021-2025 UnoDB contributors

/// \file
/// Implementation of active pointer registration for QSBR-managed data in debug
/// builds.

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include "qsbr_ptr.hpp"  // IWYU pragma: keep

#ifndef NDEBUG
#include "qsbr.hpp"
#endif

namespace unodb::detail {

#ifndef NDEBUG

void qsbr_ptr_base::register_active_ptr(const void *ptr) {
  if (ptr != nullptr) this_thread().register_active_ptr(ptr);
}

void qsbr_ptr_base::unregister_active_ptr(const void *ptr) {
  if (ptr != nullptr) this_thread().unregister_active_ptr(ptr);
}

#endif

}  // namespace unodb::detail
