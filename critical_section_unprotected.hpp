// Copyright 2019-2021 Laurynas Biveinis
#ifndef CRITICAL_SECTION_UNPROTECTED_HPP_
#define CRITICAL_SECTION_UNPROTECTED_HPP_

#include "global.hpp"

#include <cstddef>
#include <type_traits>

namespace unodb {

// Provide access to T with critical_section_protected<T>-like interface, except
// that loads and stores are direct instead of relaxed atomic. It enables having
// a common templatized implementation of single-threaded and OLC node
// algorithms.
template <typename T>
class critical_section_unprotected final {
 public:
  constexpr critical_section_unprotected() noexcept = default;
  // cppcheck-suppress noExplicitConstructor
  constexpr critical_section_unprotected(T value_) noexcept : value{value_} {}
  constexpr critical_section_unprotected(
      const critical_section_unprotected<T> &) = default;
  constexpr critical_section_unprotected(critical_section_unprotected<T> &&) =
      default;

  // Return nothing as we never chain assignments for now.
  constexpr void operator=(T new_value) noexcept { value = new_value; }

  constexpr void operator=(critical_section_unprotected<T> new_value) noexcept {
    value = new_value;
  }

  constexpr void operator++() noexcept { ++value; }

  constexpr void operator--() noexcept { --value; }

  constexpr T operator--(int) noexcept { return value--; }

  template <typename T_ = T,
            typename = std::enable_if_t<!std::is_integral_v<T_>>>
  [[nodiscard]] constexpr auto operator==(std::nullptr_t) const noexcept {
    return value == nullptr;
  }

  template <typename T_ = T,
            typename = std::enable_if_t<!std::is_integral_v<T_>>>
  [[nodiscard]] constexpr auto operator!=(std::nullptr_t) const noexcept {
    return value != nullptr;
  }

  constexpr operator T() const noexcept { return value; }

  constexpr T load() const noexcept { return value; }

 private:
  T value;
};

}  // namespace unodb

#endif  // CRITICAL_SECTION_UNPROTECTED_HPP_
