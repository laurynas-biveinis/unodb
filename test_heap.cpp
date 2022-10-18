// Copyright 2022 Laurynas Biveinis

#include "global.hpp"

// - ASan/TSan do not work with replaced global new/delete:
//   https://github.com/llvm/llvm-project/issues/20034
// - Google Test with MSVC standard library tries to allocate memory in the
//   exception-thrown-as-expected path
#if !defined(NDEBUG) && !defined(_MSC_VER) &&   \
    !defined(UNODB_DETAIL_ADDRESS_SANITIZER) && \
    !defined(UNODB_DETAIL_THREAD_SANITIZER)

#include <cstddef>
#include <new>

#include "heap.hpp"

void* operator new(std::size_t count) {
  unodb::test::allocation_failure_injector::maybe_fail();
  while (true) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
    void* const result = malloc(count);
    if (UNODB_DETAIL_LIKELY(result != nullptr)) return result;
    // LCOV_EXCL_START
    auto* new_handler = std::get_new_handler();
    if (new_handler == nullptr) throw std::bad_alloc{};
    (*new_handler)();
    // LCOV_EXCL_STOP
  }
}

void operator delete(void* ptr) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
  free(ptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-prototypes")
void operator delete(void* ptr, std::size_t size) noexcept {
#ifdef UNODB_DETAIL_USE_JEMALLOC
  unodb::detail::free_sized(ptr, size);
#else
  (void)size;
  free(ptr);
#endif
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

#endif  // !defined(NDEBUG) && !defined(_MSC_VER) &&
        // !defined(UNODB_DETAIL_ADDRESS_SANITIZER) &&
        // !defined(UNODB_DETAIL_THREAD_SANITIZER)
