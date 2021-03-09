// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_ART_COMMON_HPP_
#define UNODB_ART_COMMON_HPP_

#include "global.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>

#include <gsl/span>

namespace unodb {

// Key type for public API
using key = std::uint64_t;

namespace detail {

__attribute__((cold, noinline)) void dump_key(std::ostream &os, key k);

}  // namespace detail

// Value type for public API. Values are passed as non-owning pointers to
// memory with associated length (gsl::span). The memory is copied upon
// insertion.
using value_view = gsl::span<const std::byte>;

// Search result type. If value is not present, it was not found
using get_result = std::optional<value_view>;

}  // namespace unodb

#endif  // UNODB_ART_COMMON_HPP_
