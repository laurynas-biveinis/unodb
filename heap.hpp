// Copyright 2020-2025 UnoDB contributors
#ifndef UNODB_DETAIL_HEAP_HPP
#define UNODB_DETAIL_HEAP_HPP

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <algorithm>
#ifndef NDEBUG
#include <cerrno>
#endif
#include <cstdlib>
#include <new>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#include "assert.hpp"
#include "test_heap.hpp"

namespace unodb::detail {

template <typename T>
[[nodiscard]] consteval auto alignment_for_new() noexcept {
  return std::max(alignof(T),
                  static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}

[[nodiscard]] inline void* allocate_aligned(
    std::size_t size,
    std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
#ifndef NDEBUG
  unodb::test::allocation_failure_injector::maybe_fail();
#endif

  void* result;

#ifndef _MSC_VER
  const auto err = posix_memalign(&result, alignment, size);
  if (UNODB_DETAIL_UNLIKELY(err != 0)) result = nullptr;
#else
  result = _aligned_malloc(size, alignment);
#ifndef NDEBUG
  const auto err = UNODB_DETAIL_LIKELY(result != nullptr) ? 0 : errno;
#endif
#endif

  UNODB_DETAIL_ASSERT(err != EINVAL);
  // NOLINTNEXTLINE(readability-simplify-boolean-expr)
  UNODB_DETAIL_ASSERT(result != nullptr || err == ENOMEM);

  if (UNODB_DETAIL_UNLIKELY(result == nullptr)) {
    throw std::bad_alloc{};  // LCOV_EXCL_LINE
  }

  return result;
}

inline void free_aligned(void* ptr) noexcept {
#ifndef _MSC_VER
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
  free(ptr);
#else
  _aligned_free(ptr);
#endif
}

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_HEAP_HPP
