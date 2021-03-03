// Copyright 2019-2021 Laurynas Biveinis
#ifndef NOT_ATOMIC_HPP_
#define NOT_ATOMIC_HPP_

#include "global.hpp"

#include <cstddef>
#include <type_traits>

namespace unodb {

// A template wrapper providing access to T with std::atomic-like interface
// (like relaxed_atomic<T>), which is not actually atomic. It enables having a
// common templatized non-atomic and relaxed atomic implementation.
template <typename T>
class not_atomic final {
 public:
  constexpr not_atomic() noexcept = default;
  // cppcheck-suppress noExplicitConstructor
  constexpr not_atomic(T value_) noexcept : value{value_} {}
  constexpr not_atomic(const not_atomic<T> &) = default;
  constexpr not_atomic(not_atomic<T> &&) = default;

  // Regular C++ assignment operators return ref to this, std::atomic returns
  // the assigned value, we return nothing as we never chain assignments.
  constexpr void operator=(T new_value) noexcept { value = new_value; }

  constexpr void operator=(not_atomic<T> new_value) noexcept {
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

#endif  // NOT_ATOMIC_HPP_
