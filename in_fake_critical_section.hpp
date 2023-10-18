// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP
#define UNODB_DETAIL_IN_FAKE_CRITICAL_SECTION_HPP

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <type_traits>

namespace unodb {

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
  // NOLINTNEXTLINE(*-assignment-signature,misc-unconventional-assign-operator)
  constexpr void operator=(T new_value) noexcept { value = new_value; }

  // NOLINTNEXTLINE(*-assignment-signature,misc-unconventional-assign-operator)
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
