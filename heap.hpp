// Copyright 2020-2021 Laurynas Biveinis
#ifndef UNODB_HEAP_HPP_
#define UNODB_HEAP_HPP_

#include "global.hpp"

#include <cassert>
#ifdef USE_STD_PMR
#include <memory_resource>
#endif

#ifndef USE_STD_PMR
#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/synchronized_pool_resource.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>
#endif

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
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

namespace unodb::detail {

#ifdef USE_STD_PMR

using pmr_pool = std::pmr::memory_resource;
using pmr_pool_options = std::pmr::pool_options;
inline const auto &pmr_new_delete_resource = std::pmr::new_delete_resource;
using pmr_synchronized_pool_resource = std::pmr::synchronized_pool_resource;
using pmr_unsynchronized_pool_resource = std::pmr::unsynchronized_pool_resource;

#else

using pmr_pool = boost::container::pmr::memory_resource;
using pmr_pool_options = boost::container::pmr::pool_options;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline const auto &pmr_new_delete_resource =
    boost::container::pmr::new_delete_resource;
using pmr_synchronized_pool_resource =
    boost::container::pmr::synchronized_pool_resource;
using pmr_unsynchronized_pool_resource =
    boost::container::pmr::unsynchronized_pool_resource;

#endif

template <typename T>
constexpr auto alignment_for_new() noexcept {
  return std::max(alignof(T),
                  static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}

[[nodiscard]] inline auto *pmr_allocate(
    pmr_pool &pool, std::size_t size,
    std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
  assert(alignment >= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

  auto *const result = pool.allocate(size, alignment);

#if defined(VALGRIND_CLIENT_REQUESTS) && !defined(USE_STD_PMR)
  if (!pool.is_equal(*pmr_new_delete_resource())) {
    VALGRIND_MALLOCLIKE_BLOCK(result, size, 0, 0);
  }
#endif
  ASAN_UNPOISON_MEMORY_REGION(result, size);

  return result;
}

inline void pmr_deallocate(
    pmr_pool &pool, void *pointer, std::size_t size,
    std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
  assert(alignment >= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

  ASAN_POISON_MEMORY_REGION(pointer, size);
#if defined(VALGRIND_CLIENT_REQUESTS) && !defined(USE_STD_PMR)
  if (!pool.is_equal(*pmr_new_delete_resource())) {
    VALGRIND_FREELIKE_BLOCK(pointer, 0);
    VALGRIND_MAKE_MEM_UNDEFINED(pointer, size);
  }
#endif

  pool.deallocate(pointer, size, alignment);
}

}  // namespace unodb::detail

#endif  // UNODB_HEAP_HPP_
