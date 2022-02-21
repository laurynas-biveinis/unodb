// Copyright 2020-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_HEAP_HPP
#define UNODB_DETAIL_HEAP_HPP

#include "global.hpp"

#include <algorithm>
#ifndef NDEBUG
#include <atomic>
#endif
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

#ifndef NDEBUG

namespace unodb::test {

class allocation_failure_injector final {
 public:
  static void reset() noexcept {
    fail_on_nth_allocation_ = 0;
    allocation_counter.store(0, std::memory_order_release);
  }

  static void fail_on_nth_allocation(std::uint64_t n) noexcept {
    fail_on_nth_allocation_ = n;
  }

  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wanalyzer-malloc-leak")

  static void maybe_fail() {
    if (UNODB_DETAIL_UNLIKELY(fail_on_nth_allocation_ != 0) &&
        (allocation_counter.fetch_add(1, std::memory_order_acq_rel) >=
         fail_on_nth_allocation_ - 1)) {
      throw std::bad_alloc{};
    }
  }

  UNODB_DETAIL_RESTORE_GCC_WARNINGS()

 private:
  static inline std::atomic<std::uint64_t> allocation_counter{0};
  static inline std::uint64_t fail_on_nth_allocation_{0};
};

}  // namespace unodb::test

#endif

namespace unodb::detail {

template <typename T>
[[nodiscard]] constexpr auto alignment_for_new() noexcept {
  return std::max(alignof(T),
                  static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}

[[nodiscard]] inline void* allocate_aligned_nothrow(
    std::size_t size,
    std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) noexcept {
  void* result;

#ifndef _MSC_VER
  const auto err UNODB_DETAIL_USED_IN_DEBUG =
      posix_memalign(&result, alignment, size);
#else
  result = _aligned_malloc(size, alignment);
#ifndef NDEBUG
  const auto err = UNODB_DETAIL_LIKELY(result != nullptr) ? 0 : errno;
#endif
#endif

  UNODB_DETAIL_ASSERT(err != EINVAL);
  UNODB_DETAIL_ASSERT(result != nullptr || err == ENOMEM);

  return result;
}

[[nodiscard]] inline void* allocate_aligned(
    std::size_t size,
    std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
#ifndef NDEBUG
  unodb::test::allocation_failure_injector::maybe_fail();
#endif

  void* result = allocate_aligned_nothrow(size, alignment);

  if (UNODB_DETAIL_UNLIKELY(result == nullptr)) {
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
