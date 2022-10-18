// Copyright 2020-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_HEAP_HPP
#define UNODB_DETAIL_HEAP_HPP

#include "global.hpp"

#include <algorithm>  // IWYU pragma: keep
#ifndef NDEBUG
#include <atomic>
#include <cerrno>
#endif
#include <cstdint>
#include <cstdlib>
#include <new>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#include "assert.hpp"

namespace unodb::test {

class allocation_failure_injector final {
 public:
  static void reset() noexcept {
#ifndef NDEBUG
    fail_on_nth_allocation_.store(0, std::memory_order_relaxed);
    allocation_counter.store(0, std::memory_order_release);
#endif
  }

  static void fail_on_nth_allocation(
      std::uint64_t n UNODB_DETAIL_USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
    fail_on_nth_allocation_.store(n, std::memory_order_release);
#endif
  }

#ifndef NDEBUG

  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wanalyzer-malloc-leak")

  static void maybe_fail() {
    const auto fail_counter =
        fail_on_nth_allocation_.load(std::memory_order_acquire);
    if (UNODB_DETAIL_UNLIKELY(fail_counter != 0) &&
        (allocation_counter.fetch_add(1, std::memory_order_relaxed) >=
         fail_counter - 1)) {
      throw std::bad_alloc{};
    }
  }

  UNODB_DETAIL_RESTORE_GCC_WARNINGS()

 private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  inline static std::atomic<std::uint64_t> allocation_counter{0};
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  inline static std::atomic<std::uint64_t> fail_on_nth_allocation_{0};

#endif  // #ifndef NDEBUG
};

}  // namespace unodb::test

namespace unodb::detail {

template <typename T>
[[nodiscard]] constexpr auto alignment_for_new() noexcept {
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
