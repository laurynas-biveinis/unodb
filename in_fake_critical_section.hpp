// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP
#define UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP

// TODO(laurynas): rename the file. fake_optimistic_lock.hpp ?
// TODO(laurynas): move everything to unodb::detail, together with
// optimistic_lock.hpp

/// \file
/// No-op ("fake") versions of the optimistic lock primitive, its read critical
/// section type, and the protected data declaration wrapper.
///
/// \ingroup optimistic-lock
///
/// The no-op versions or the real versions can be passed as template
/// parameters, resulting in code that can be compiled for both single-threaded
/// and concurrent use cases.

// Should be the first include
#include "global.hpp"

#include <cstddef>

namespace unodb {

/// Fake version of optimistic_lock::read_critical_section used to align
/// unodb::db and unodb::olc_db code. All operations are no-ops.
// TODO(laurynas): move inside fake_lock?
class [[nodiscard]] fake_read_critical_section final {
 public:
  /// Trivially default-construct a fake read critical section.
  fake_read_critical_section() noexcept = default;

  /// Trivially destruct a fake read critical section.
  ~fake_read_critical_section() noexcept = default;

  /// Move-assign from another fake read critical section, a no-op.
  [[nodiscard]] fake_read_critical_section& operator=(
      fake_read_critical_section&&) noexcept = default;

  /// Check whether this fake read critical section is invalid after
  /// construction, always succeeds.
  // cppcheck-suppress functionStatic
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] constexpr bool must_restart() const noexcept { return false; }

  /// Check whether this fake read critical section is still valid, always
  /// succeeds.
  // cppcheck-suppress functionStatic
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] bool check() UNODB_DETAIL_RELEASE_CONST noexcept {
    return true;
  }

  /// Compare with another read fake read critical section for equality, always
  /// returning true.
  [[nodiscard]] constexpr bool operator==(
      const fake_read_critical_section&) const noexcept {
    return true;
  }

  /// Try to read unlock this read fake critical section, always succeeds.
  // cppcheck-suppress functionStatic
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] constexpr bool try_read_unlock() const noexcept { return true; }

  fake_read_critical_section(const fake_read_critical_section&) = delete;
  fake_read_critical_section(fake_read_critical_section&&) = delete;
  fake_read_critical_section& operator=(const fake_read_critical_section&) =
      delete;
};  // class fake_read_critical_section

/// Fake version of unodb::optimistic_lock. All operations are no-ops.
// TODO(laurynas): rename to fake_optimistic_lock.
class [[nodiscard]] fake_lock final {
 public:
  /// Acquire and return an always-valid fake critical section for a fake lock.
  // cppcheck-suppress functionStatic
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] fake_read_critical_section try_read_lock() noexcept {
    return fake_read_critical_section{};
  }
};  // class fake_lock

/// Provide access to \a T with unodb::in_critical_section<T>-like interface,
/// except that loads and stores are direct instead of relaxed atomic. It
/// enables having a common templatized implementation of single-threaded and
/// OLC node algorithms.
template <typename T>
class [[nodiscard]] in_fake_critical_section final {
 public:
  /// Default construct the wrapped \a T value.
  constexpr in_fake_critical_section() noexcept = default;

  /// Construct the wrapped value from the passed \a value_.
  // cppcheck-suppress noExplicitConstructor
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  constexpr in_fake_critical_section(T value_) noexcept : value{value_} {}

  /// Destruct the wrapped value.
  ~in_fake_critical_section() noexcept = default;

  /// Copy-assign another wrapped value.
  // NOLINTNEXTLINE(cert-oop54-cpp)
  constexpr in_fake_critical_section& operator=(
      const in_fake_critical_section& new_value) noexcept {
    value = new_value;
    return *this;
  }

  /// Assign \a new_value to the wrapped value.
  constexpr in_fake_critical_section& operator=(T new_value) noexcept {
    value = new_value;
    return *this;
  }

  /// Pre-increment the wrapped value.
  constexpr void operator++() noexcept { ++value; }

  /// Pre-decrement the wrapped value.
  constexpr void operator--() noexcept { --value; }

  /// Post-decrement the wrapped value, returning the old value.
  // NOLINTNEXTLINE(cert-dcl21-cpp)
  constexpr T operator--(int) noexcept { return value--; }

  /// Checks whether the wrapped pointer is `nullptr`.
  [[nodiscard, gnu::pure]] constexpr bool operator==(
      std::nullptr_t) const noexcept {
    return value == nullptr;
  }

  /// Convert to the wrapped value, implicitly if needed.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  [[nodiscard]] constexpr operator T() const noexcept { return value; }

  /// Explicitly read the wrapped value.
  [[nodiscard]] constexpr T load() const noexcept { return value; }

  in_fake_critical_section(const in_fake_critical_section<T>&) = delete;
  in_fake_critical_section(in_fake_critical_section<T>&&) noexcept = delete;
  in_fake_critical_section& operator=(in_fake_critical_section&&) = delete;

 private:
  /// Wrapped value.
  T value;
};

}  // namespace unodb

#endif  // UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP
