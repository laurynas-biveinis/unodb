// Copyright (C) 2021-2024 Laurynas Biveinis

#include "global.hpp"

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
