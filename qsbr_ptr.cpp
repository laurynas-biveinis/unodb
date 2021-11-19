// Copyright (C) 2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "qsbr_ptr.hpp"

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
