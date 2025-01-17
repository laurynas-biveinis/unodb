// Copyright (C) 2021-2025 UnoDB contributors
#ifndef UNODB_DETAIL_QSBR_PTR_HPP
#define UNODB_DETAIL_QSBR_PTR_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace unodb {

namespace detail {

class qsbr_ptr_base {
 protected:
  qsbr_ptr_base() = default;

#ifndef NDEBUG
  static void register_active_ptr(const void *ptr);
  static void unregister_active_ptr(const void *ptr);
#endif
};

}  // namespace detail

// An active pointer to QSBR-managed shared data. A thread cannot go through a
// quiescent state while at least one is alive, which is asserted in the debug
// build. A smart pointer class that provides a raw pointer-like interface.
// Implemented bare minimum to get things to work, expand as necessary.
template <class T>
class [[nodiscard]] qsbr_ptr : public detail::qsbr_ptr_base {
 public:
  using value_type = T;
  using pointer = T *;
  using reference = std::add_lvalue_reference_t<T>;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::contiguous_iterator_tag;

  qsbr_ptr() noexcept = default;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

  UNODB_DETAIL_RELEASE_CONSTEXPR explicit qsbr_ptr(
      pointer ptr_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : ptr{ptr_} {
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr(const qsbr_ptr &other) noexcept
      : ptr{other.ptr} {
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr qsbr_ptr(qsbr_ptr &&other) noexcept
      : ptr{std::exchange(other.ptr, nullptr)} {}

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

  ~qsbr_ptr() noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26456)

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr &operator=(
      const qsbr_ptr &other) noexcept {
#ifndef NDEBUG
    if (this == &other) return *this;
    unregister_active_ptr(ptr);
#endif
    ptr = other.ptr;
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
    return *this;
  }

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr &operator=(
      qsbr_ptr &&other) noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    ptr = std::exchange(other.ptr, nullptr);
    return *this;
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] constexpr reference operator*() const noexcept { return *ptr; }

  [[nodiscard]] constexpr reference operator[](
      difference_type n) const noexcept {
    return ptr[n];
  }

  [[nodiscard, gnu::pure]] constexpr T *operator->() const noexcept {
    return ptr;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)
  constexpr qsbr_ptr &operator++() noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    ++ptr;
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
    return *this;
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] constexpr qsbr_ptr operator++(int) noexcept {
    const auto result = *this;
    ++(*this);
    return result;
  }

  constexpr qsbr_ptr &operator--() noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    --ptr;
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
    return *this;
  }

  [[nodiscard]] constexpr qsbr_ptr operator--(int) noexcept {
    const auto result = *this;
    --(*this);
    return result;
  }

  constexpr qsbr_ptr &operator+=(difference_type n) noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    ptr += n;
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
    return *this;
  }

  [[nodiscard]] constexpr qsbr_ptr operator+(difference_type n) const noexcept {
    auto result = *this;
    result += n;
    return result;
  }

  [[nodiscard]] friend constexpr qsbr_ptr operator+(difference_type n,
                                                    qsbr_ptr other) noexcept {
    return other + n;
  }

  constexpr qsbr_ptr &operator-=(difference_type n) noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    ptr -= n;
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
    return *this;
  }

  [[nodiscard]] constexpr qsbr_ptr operator-(difference_type n) const noexcept {
    auto result = *this;
    result -= n;
    return result;
  }

  [[nodiscard, gnu::pure]] constexpr difference_type operator-(
      qsbr_ptr other) const noexcept {
    return get() - other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator==(
      qsbr_ptr other) const noexcept {
    return get() == other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator!=(
      qsbr_ptr other) const noexcept {
    return get() != other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator<=(
      qsbr_ptr other) const noexcept {
    return get() <= other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator>=(
      qsbr_ptr other) const noexcept {
    return get() >= other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator<(
      qsbr_ptr other) const noexcept {
    return get() < other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator>(
      qsbr_ptr other) const noexcept {
    return get() > other.get();
  }

  [[nodiscard, gnu::pure]] constexpr pointer get() const noexcept {
    return ptr;
  }

 private:
  pointer ptr{nullptr};
};

static_assert(std::contiguous_iterator<unodb::qsbr_ptr<std::byte>>);

// An std::span, but with qsbr_ptr instead of a raw pointer.
// Implemented the bare minimum to get things to work, expand as necessary.
template <class T>
class qsbr_ptr_span : public std::ranges::view_base {
 public:
  UNODB_DETAIL_RELEASE_CONSTEXPR
  qsbr_ptr_span() noexcept : start{nullptr}, length{0} {}

  UNODB_DETAIL_RELEASE_CONSTEXPR
  explicit qsbr_ptr_span(const std::span<T> &other) noexcept
      : start{other.data()}, length{static_cast<std::size_t>(other.size())} {}

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span(
      const qsbr_ptr_span<T> &) noexcept = default;
  constexpr qsbr_ptr_span(qsbr_ptr_span<T> &&) noexcept = default;
  ~qsbr_ptr_span() noexcept = default;

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span<T> &operator=(
      const qsbr_ptr_span<T> &) noexcept = default;
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span<T> &operator=(
      qsbr_ptr_span<T> &&) noexcept = default;

  [[nodiscard, gnu::pure]] constexpr qsbr_ptr<T> begin() const noexcept {
    return start;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)
  [[nodiscard, gnu::pure]] constexpr qsbr_ptr<T> end() const noexcept {
    return qsbr_ptr<T>{start.get() + length};
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard, gnu::pure]] constexpr std::size_t size() const noexcept {
    return length;
  }

 private:
  qsbr_ptr<T> start;
  std::size_t length;
};

template <class T>
qsbr_ptr_span(const std::span<T> &) -> qsbr_ptr_span<T>;

}  // namespace unodb

template <typename T>
constexpr bool std::ranges::enable_borrowed_range<unodb::qsbr_ptr_span<T>> =
    true;

static_assert(
    std::ranges::random_access_range<unodb::qsbr_ptr_span<std::byte>>);
static_assert(std::ranges::borrowed_range<unodb::qsbr_ptr_span<std::byte>>);
static_assert(std::ranges::contiguous_range<unodb::qsbr_ptr_span<std::byte>>);
static_assert(std::ranges::common_range<unodb::qsbr_ptr_span<std::byte>>);
static_assert(std::ranges::sized_range<unodb::qsbr_ptr_span<std::byte>>);
static_assert(std::ranges::view<unodb::qsbr_ptr_span<std::byte>>);

#endif  // UNODB_DETAIL_QSBR_PTR_HPP
