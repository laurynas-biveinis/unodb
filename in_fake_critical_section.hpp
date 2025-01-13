// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP
#define UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP

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
#include <type_traits>

namespace unodb {

// fake version of read_critical_section used to align unodb::db and
// unodb::olc_db code.
class [[nodiscard]] fake_read_critical_section final {
 public:
  fake_read_critical_section() noexcept = default;

  [[nodiscard]] inline fake_read_critical_section &operator=(
      fake_read_critical_section &&) noexcept = default;

  // Always equal.
  [[nodiscard]] inline bool operator==(
      const fake_read_critical_section &) const noexcept {
    return true;
  }

  // Always succeeds.
  [[nodiscard, gnu::flatten]] UNODB_DETAIL_FORCE_INLINE bool try_read_unlock()
      const noexcept {
    return true;
  }

  // Always returns true since the lock is never updated.
  [[nodiscard]] inline bool check() UNODB_DETAIL_RELEASE_CONST noexcept {
    return true;
  }

  // Always returns false.
  [[nodiscard]] inline bool must_restart() const noexcept { return false; }
};  // class fake_read_critical_section

// fake class for taking a lock.
class [[nodiscard]] fake_lock final {
 public:
  // Acquire and return a fake critical section for a fake lock.
  [[nodiscard]] fake_read_critical_section try_read_lock() noexcept {
    return fake_read_critical_section{};
  }
};  // class fake_policy

// Provide access to T with in_critical_section<T>-like interface, except that
// loads and stores are direct instead of relaxed atomic. It enables having a
// common templatized implementation of single-threaded and OLC node algorithms.
template <typename T>
class [[nodiscard]] in_fake_critical_section final {
 public:
  constexpr in_fake_critical_section() noexcept = default;
  // cppcheck-suppress noExplicitConstructor
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  constexpr in_fake_critical_section(T value_) noexcept : value{value_} {}
  constexpr in_fake_critical_section(const in_fake_critical_section<T> &) =
      default;
  constexpr in_fake_critical_section(in_fake_critical_section<T> &&) noexcept =
      default;

  ~in_fake_critical_section() noexcept = default;

  // Enable when needed
  in_fake_critical_section &operator=(in_fake_critical_section &&) = delete;

  // Return nothing as we never chain assignments for now.
  // NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
  constexpr void operator=(T new_value) noexcept { value = new_value; }

  // NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
  constexpr void operator=(in_fake_critical_section<T> new_value) noexcept {
    value = new_value;
  }

  constexpr void operator++() noexcept { ++value; }

  constexpr void operator--() noexcept { --value; }

  // NOLINTNEXTLINE(cert-dcl21-cpp)
  constexpr T operator--(int) noexcept { return value--; }

  template <typename T_ = T,
            typename = std::enable_if_t<!std::is_integral_v<T_>>>
  [[nodiscard, gnu::pure]] constexpr auto operator==(
      std::nullptr_t) const noexcept {
    return value == nullptr;
  }

  template <typename T_ = T,
            typename = std::enable_if_t<!std::is_integral_v<T_>>>
  [[nodiscard, gnu::pure]] constexpr auto operator!=(
      std::nullptr_t) const noexcept {
    return value != nullptr;
  }

  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  [[nodiscard]] constexpr operator T() const noexcept { return value; }

  [[nodiscard]] constexpr T load() const noexcept { return value; }

 private:
  T value;
};

}  // namespace unodb

#endif  // UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP
