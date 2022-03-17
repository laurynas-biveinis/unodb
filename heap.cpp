// Copyright 2022 Laurynas Biveinis

#include "global.hpp"

#include "heap.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace unodb::test {

#ifndef NDEBUG

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<std::uint64_t> allocation_failure_injector::allocation_counter{0};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<std::uint64_t> allocation_failure_injector::fail_on_nth_allocation_{
    0};

#endif  // #ifndef NDEBUG

}  // namespace unodb::test

// - ASan/TSan do not work with replaced global new/delete:
//   https://github.com/llvm/llvm-project/issues/20034
// - Google Test with MSVC standard library tries to allocate memory in the
//   exception-thrown-as-expected path
#if !defined(NDEBUG) && !defined(_MSC_VER) &&   \
    !defined(UNODB_DETAIL_ADDRESS_SANITIZER) && \
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

void* operator new(std::size_t count, std::align_val_t al) {
  return do_new(&unodb::detail::allocate_aligned_nothrow, count,
                static_cast<std::size_t>(al));
}

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

void operator delete(void* ptr, std::align_val_t) noexcept {
  unodb::detail::free_aligned(ptr);
}

#endif  // !defined(NDEBUG) && !defined(_MSC_VER) &&
        // !defined(UNODB_DETAIL_ADDRESS_SANITIZER) &&
        // !defined(UNODB_DETAIL_THREAD_SANITIZER)
