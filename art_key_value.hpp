// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_KEY_VALUE_HPP_
#define UNODB_ART_KEY_VALUE_HPP_

#include "global.hpp"

#include <cstdint>

#include <gsl/span>

namespace unodb {

// Key type for public API
using key_type = uint64_t;

// Value type for public API. Values are passed as non-owning pointers to
// memory with associated length (gsl::span). The memory is copied upon
// insertion.
using value_view = gsl::span<const std::byte>;

}  // namespace unodb

#endif  // UNODB_ART_KEY_VALUE_HPP_
