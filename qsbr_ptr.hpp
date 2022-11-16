// Copyright (C) 2021-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_PTR_HPP
#define UNODB_DETAIL_QSBR_PTR_HPP

#include "global.hpp"

#include <cstddef>
#include <iterator>     // IWYU pragma: keep
#include <type_traits>  // IWYU pragma: keep
#include <utility>

#include <gsl/span>

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
  using pointer_type = T *;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

  UNODB_DETAIL_RELEASE_CONSTEXPR explicit qsbr_ptr(pointer_type ptr_) noexcept
      : ptr{ptr_} {
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr(const qsbr_ptr<T> &other) noexcept
      : ptr{other.ptr} {
#ifndef NDEBUG
    register_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr qsbr_ptr(qsbr_ptr<T> &&other) noexcept
      : ptr{std::exchange(other.ptr, nullptr)} {}

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)

  ~qsbr_ptr() noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26456)

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr<T> &operator=(
      const qsbr_ptr<T> &other) noexcept {
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

  UNODB_DETAIL_RELEASE_CONSTEXPR qsbr_ptr<T> &operator=(
      qsbr_ptr<T> &&other) noexcept {
#ifndef NDEBUG
    unregister_active_ptr(ptr);
#endif
    ptr = std::exchange(other.ptr, nullptr);
    return *this;
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard, gnu::pure]] constexpr std::add_lvalue_reference_t<T> operator*()
      const {
    return *ptr;
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26481)
  constexpr qsbr_ptr<T> &operator++() noexcept {
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

  [[nodiscard, gnu::pure]] constexpr std::ptrdiff_t operator-(
      qsbr_ptr<T> other) const noexcept {
    return get() - other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator==(
      qsbr_ptr<T> other) const noexcept {
    return get() == other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator!=(
      qsbr_ptr<T> other) const noexcept {
    return get() != other.get();
  }

  [[nodiscard, gnu::pure]] constexpr bool operator<=(
      qsbr_ptr<T> other) const noexcept {
    return get() <= other.get();
  }

  [[nodiscard, gnu::pure]] constexpr T *get() const noexcept { return ptr; }

 private:
  pointer_type ptr;
};

}  // namespace unodb

namespace std {

template <typename T>
struct iterator_traits<unodb::qsbr_ptr<T>> {
  using difference_type = ptrdiff_t;
  using value_type = T;
  using pointer = T *;
  using reference = T &;
  using iterator_category = random_access_iterator_tag;
};

}  // namespace std

namespace unodb {

// A gsl::span (or std::span), but with qsbr_ptr instead of raw pointer.
// Implemented bare minimum to get things to work, expand as necessary.
template <class T>
class qsbr_ptr_span {
 public:
  UNODB_DETAIL_RELEASE_CONSTEXPR explicit qsbr_ptr_span(
      const gsl::span<T> &other) noexcept
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

 private:
  qsbr_ptr<T> start;
  std::size_t length;
};

template <class T>
qsbr_ptr_span(const gsl::span<T> &) -> qsbr_ptr_span<T>;

}  // namespace unodb

#endif  // UNODB_DETAIL_QSBR_PTR_HPP
