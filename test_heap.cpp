// Copyright 2022-2025 UnoDB contributors

/// \file
/// Custom global new & delete operators for allocation failure injection tests.
///
/// \ingroup test-internals
///
/// They are used:
/// - In debug builds only
/// - When not building with AddressSanitizer or ThreadSanitizer, because they
///   do not work with replaced global new & delete operators:
///   https://github.com/llvm/llvm-project/issues/20034
/// - When not building with MSVC because its standard library allocates heap
///   memory in the Google Test exception-thrown-as-expected path.

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__new/new_handler.h>

#include "test_heap.hpp"  // IWYU pragma: keep

#if !defined(NDEBUG) && !defined(_MSC_VER) &&       \
        !defined(UNODB_DETAIL_ADDRESS_SANITIZER) && \
        !defined(UNODB_DETAIL_THREAD_SANITIZER) ||  \
    defined(UNODB_DETAIL_DOXYGEN)

#include <cstddef>
#include <cstdlib>
#include <new>  // IWYU pragma: keep

/// Global debug build replacement for `operator new`.
///
/// Intercepts all memory allocations to:
/// - Check if this allocation should fail via test::allocation_failure_injector
/// - Use `malloc` for actual memory allocation
/// - Handle out-of-memory situations with standard `new_handler` protocol
///
/// \param count Number of bytes to allocate
/// \return Pointer to allocated memory
/// \throws `std::bad_alloc` When allocation fails or is deliberately injected
/// to fail
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

/// Global debug build replacement for `operator delete`.
///
/// Uses `free` to match the `malloc` used in ::operator new.
///
/// \param ptr Pointer to memory to be deallocated
void operator delete(void* ptr) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
  free(ptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-prototypes")
/// Global debug build replacement for sized `operator delete`.
///
/// Uses `free` to match the `malloc` used in ::operator new.
///
/// \param ptr Pointer to memory to be deallocated
void operator delete(void* ptr, std::size_t) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
  free(ptr);
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

#endif  // !defined(NDEBUG) && !defined(_MSC_VER) &&
        // !defined(UNODB_DETAIL_ADDRESS_SANITIZER) &&
        // !defined(UNODB_DETAIL_THREAD_SANITIZER) ||
// defined(UNODB_DETAIL_DOXYGEN)
