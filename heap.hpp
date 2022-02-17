// Copyright 2020-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_HEAP_HPP
#define UNODB_DETAIL_HEAP_HPP

#include "global.hpp"

#include <algorithm>
#include <cstdlib>

#ifdef _MSC_VER
#include <malloc.h>
#endif

// Do not try to use ASan interface under MSVC until the headers are added to
// the default search paths:
// https://developercommunity.visualstudio.com/t/ASan-API-headers-not-in-include-path-whe/1517192
#ifndef UNODB_DETAIL_MSVC
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#endif
#endif

#ifndef ASAN_POISON_MEMORY_REGION

#define ASAN_POISON_MEMORY_REGION(a, s)
#define ASAN_UNPOISON_MEMORY_REGION(a, s)

#endif

#if !defined(NDEBUG) && defined(VALGRIND_CLIENT_REQUESTS)
#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>
#else
#define VALGRIND_MALLOCLIKE_BLOCK(addr, sizeB, rzB, is_zeroed)
#define VALGRIND_FREELIKE_BLOCK(addr, rzB)
#define VALGRIND_MAKE_MEM_UNDEFINED(_qzz_addr, _qzz_len)
#endif

#include "assert.hpp"

namespace unodb::detail {

template <typename T>
[[nodiscard]] constexpr auto alignment_for_new() noexcept {
  return std::max(alignof(T),
                  static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}

[[nodiscard]] inline void* allocate_aligned(
    std::size_t size,
    std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
  void* result;
#ifndef _MSC_VER
  const auto err = posix_memalign(&result, alignment, size);
#else
  result = _aligned_malloc(size, alignment);
  const auto err = UNODB_DETAIL_LIKELY(result != nullptr) ? 0 : errno;
#endif

  UNODB_DETAIL_ASSERT(err != EINVAL);
  if (UNODB_DETAIL_UNLIKELY(result == nullptr)) {
    UNODB_DETAIL_ASSERT(err == ENOMEM);
    throw std::bad_alloc{};
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
