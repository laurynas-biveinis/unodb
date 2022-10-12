// Copyright 2022 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include "heap.hpp"

#ifndef NDEBUG

#include <atomic>
#include <cstdint>

#if !defined(_MSC_VER) && !defined(UNODB_DETAIL_ADDRESS_SANITIZER) && \
    !defined(UNODB_DETAIL_THREAD_SANITIZER)
#include <cstdlib>
#include <new>
#endif

namespace unodb::test {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<std::uint64_t> allocation_failure_injector::allocation_counter{0};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<std::uint64_t> allocation_failure_injector::fail_on_nth_allocation_{
    0};

}  // namespace unodb::test

// - ASan/TSan do not work with replaced global new/delete:
//   https://github.com/llvm/llvm-project/issues/20034
// - Google Test with MSVC standard library tries to allocate memory in the
//   exception-thrown-as-expected path
#if !defined(_MSC_VER) && !defined(UNODB_DETAIL_ADDRESS_SANITIZER) && \
    !defined(UNODB_DETAIL_THREAD_SANITIZER)

namespace {

template <typename Alloc, typename... Args>
void* do_new(Alloc alloc, std::size_t count, Args... args) {
  unodb::test::allocation_failure_injector::maybe_fail();
  while (true) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    void* const result = alloc(count, args...);
    if (UNODB_DETAIL_LIKELY(result != nullptr)) return result;
    // LCOV_EXCL_START
    auto* new_handler = std::get_new_handler();
    if (new_handler == nullptr) throw std::bad_alloc{};
    (*new_handler)();
    // LCOV_EXCL_STOP
  }
}

}  // namespace

void* operator new(std::size_t count) { return do_new(&malloc, count); }

void operator delete(void* ptr) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
  free(ptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-prototypes")
void operator delete(void* ptr, std::size_t) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
  free(ptr);
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

#endif  // !defined(_MSC_VER) && !defined(UNODB_DETAIL_ADDRESS_SANITIZER) &&
        // !defined(UNODB_DETAIL_THREAD_SANITIZER)

#endif  // !defined(NDEBUG)
