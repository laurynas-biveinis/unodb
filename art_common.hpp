// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_COMMON_HPP_
#define UNODB_ART_COMMON_HPP_

#include "global.hpp"

#include <cstdint>
#include <optional>

#include <gsl/span>

namespace unodb {

// Key type for public API
using key_type = uint64_t;

// Value type for public API. Values are passed as non-owning pointers to
// memory with associated length (gsl::span). The memory is copied upon
// insertion.
using value_view_type = gsl::span<const std::byte>;

// Search result type. If value is not present, it was not found
using get_result_type = std::optional<value_view_type>;

}  // namespace unodb

#endif  // UNODB_ART_COMMON_HPP_
