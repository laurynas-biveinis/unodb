// Copyright (C) 2021-2025 UnoDB contributors
#ifndef UNODB_DETAIL_QSBR_PTR_HPP
#define UNODB_DETAIL_QSBR_PTR_HPP

/// \file
/// Pointers and spans to QSBR-managed data.
///
/// \ingroup optimistic-lock
///
/// They are debugging helpers to catch the QSBR contract violation of declaring
/// a quiescent state while having an active pointer to the shared data.

// Should be the first include
#include "global.hpp"

// IWYU pragma: no_include <__cstddef/byte.h>

#include <compare>  // IWYU pragma: keep
#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace unodb {

namespace detail {

/// Base class for QSBR pointers that provides per-thread active pointer
/// registration functionality in debug builds.
class qsbr_ptr_base {
 protected:
  /// Default-construct the base.
  qsbr_ptr_base() = default;

#ifndef NDEBUG
  /// Register an active pointer \a ptr to QSBR-managed data in this thread. A
  /// no-op with `nullptr`.
  static void register_active_ptr(const void *ptr);

  /// Unregister an active pointer \a ptr to QSBR-managed data in this thread. A
  /// no-op with `nullptr`.
  static void unregister_active_ptr(const void *ptr);
#endif
};

}  // namespace detail

/// Raw pointer-like smart pointer to QSBR-managed shared data. Crashes debug
/// builds if a thread goes through a quiescent state while having an active
/// pointer. Meets C++ contiguous iterator requirements.
// Implemented the minimum necessary operations, extend as needed.
template <typename T>
class [[nodiscard]] qsbr_ptr : public detail::qsbr_ptr_base {
 public:
  /// \name Type aliases for iterator support
  /// \{

  /// Type of values pointed to.
  using value_type = T;
  /// Raw pointer type.
  using pointer = T *;
  /// Reference type.
  using reference = std::add_lvalue_reference_t<T>;
  /// Type for pointer arithmetic.
  using difference_type = std::ptrdiff_t;
  /// Iterator category - explicitly tagged as contiguous iterator.
  using iterator_category = std::contiguous_iterator_tag;
  /// \}

  /// Default-construct a `nullptr` pointer.
  qsbr_ptr() noexcept = default;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

  /// Construct from raw pointer \a ptr_ to QSBR-managed data.
  UNODB_DETAIL_RELEASE_CONSTEXPR explicit qsbr_ptr(
      pointer ptr_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : ptr{ptr_} {
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
  }

  /// Copy-construct from \a other.
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr(const qsbr_ptr &other) noexcept
      : ptr{other.ptr} {
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Move-construct from \a other, leaving it `nullptr`.
  constexpr qsbr_ptr(qsbr_ptr &&other) noexcept
      : ptr{std::exchange(other.ptr, nullptr)} {}

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

  /// Destruct the pointer.
  ~qsbr_ptr() noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
  }

  /// Copy-assign from \a other.
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

  /// Move-assign from \a other, leaving it `nullptr`.
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr &operator=(
      qsbr_ptr &&other) noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    ptr = std::exchange(other.ptr, nullptr);
    return *this;
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Dereference the pointer.
  [[nodiscard]] constexpr reference operator*() const noexcept { return *ptr; }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)

  /// Array subscript operator for the element at \a n.
  [[nodiscard]] constexpr reference operator[](
      difference_type n) const noexcept {
    return ptr[n];
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Member access operator.
  [[nodiscard, gnu::pure]] constexpr T *operator->() const noexcept {
    return ptr;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)
  /// Pre-increment operator.
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

  /// Post-increment operator.
  [[nodiscard]] constexpr qsbr_ptr operator++(int) noexcept {
    const auto result = *this;
    ++(*this);
    return result;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)

  /// Pre-decrement operator.
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

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Post-decrement operator.
  [[nodiscard]] constexpr qsbr_ptr operator--(int) noexcept {
    const auto result = *this;
    --(*this);
    return result;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)

  /// Add offset \a n to this pointer.
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

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Return a new pointer with offset \a n added to this pointer.
  [[nodiscard]] constexpr qsbr_ptr operator+(difference_type n) const noexcept {
    auto result = *this;
    result += n;
    return result;
  }

  /// Return a new pointer which is a sum of offset \a n and pointer \a other.
  [[nodiscard]] friend constexpr qsbr_ptr operator+(difference_type n,
                                                    qsbr_ptr other) noexcept {
    return other + n;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)

  /// Subtract offset \a n from this pointer.
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

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Return a new pointer with offset \a n subtracted from this pointer.
  [[nodiscard]] constexpr qsbr_ptr operator-(difference_type n) const noexcept {
    auto result = *this;
    result -= n;
    return result;
  }

  /// Return the distance between this pointer and \a other.
  [[nodiscard, gnu::pure]] constexpr difference_type operator-(
      qsbr_ptr other) const noexcept {
    return get() - other.get();
  }

  /// Compare equal to \a other.
  [[nodiscard, gnu::pure]] constexpr bool operator==(
      qsbr_ptr other) const noexcept {
    return get() == other.get();
  }

  /// Three-way compare to \a other.
  [[nodiscard, gnu::pure]] constexpr auto operator<=>(
      qsbr_ptr other) const noexcept {
    return get() <=> other.get();
  }

  /// Get the raw pointer.
  [[nodiscard, gnu::pure]] constexpr pointer get() const noexcept {
    return ptr;
  }

 private:
  /// Raw pointer.
  pointer ptr{nullptr};
};

static_assert(std::contiguous_iterator<unodb::qsbr_ptr<std::byte>>);

/// `std::span`, but with unodb::qsbr_ptr instead of a raw pointer, that is, a
/// span over QSBR-managed data. Crashes debug builds if a thread goes through a
/// quiescent state while having an active span. Meets C++ contiguous range
/// requirements.
// Implemented the bare minimum to get things to work, expand as necessary.
template <typename T>
class qsbr_ptr_span : public std::ranges::view_base {
 public:
  /// Construct an empty span.
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span() noexcept
      : start{nullptr}, length{0} {}

  /// Construct from a regular span \a other over QSBR-managed data.
  UNODB_DETAIL_RELEASE_CONSTEXPR
  explicit qsbr_ptr_span(const std::span<T> &other) noexcept
      : start{other.data()}, length{static_cast<std::size_t>(other.size())} {}

  /// Copy-construct from \a other.
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span(const qsbr_ptr_span &) noexcept =
      default;

  /// Move-construct from \a other.
  constexpr qsbr_ptr_span(qsbr_ptr_span &&) noexcept = default;

  /// Destruct the span.
  ~qsbr_ptr_span() noexcept = default;

  /// Copy-assign from \a other.
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span &operator=(
      const qsbr_ptr_span &) noexcept = default;

  /// Move-assign from \a other.
  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr_span &operator=(
      qsbr_ptr_span &&) noexcept = default;

  /// Get the start iterator.
  [[nodiscard, gnu::pure]] constexpr qsbr_ptr<T> begin() const noexcept {
    return start;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)
  /// Get the past-the-end iterator.
  [[nodiscard, gnu::pure]] constexpr qsbr_ptr<T> end() const noexcept {
    return qsbr_ptr<T>{start.get() + length};
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Get the number of elements.
  [[nodiscard, gnu::pure]] constexpr std::size_t size() const noexcept {
    return length;
  }

 private:
  /// QSBR pointer to the start of the span.
  qsbr_ptr<T> start;

  /// Number of elements.
  std::size_t length;
};

/// Deduction guide for constructing from std::span with the same element type.
template <typename T>
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
