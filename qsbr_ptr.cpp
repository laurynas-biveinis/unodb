// Copyright (C) 2021-2023 Laurynas Biveinis

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
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
