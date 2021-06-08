// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_COMMON_HPP
#define UNODB_DETAIL_ART_COMMON_HPP

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <iosfwd>

#include <gsl/span>

namespace unodb {

// Key type for public API
using key = std::uint64_t;

namespace detail {

[[gnu::cold, gnu::noinline]] void dump_key(std::ostream &os, key k);

}  // namespace detail

// Values are passed as non-owning pointers to memory with associated length
// (gsl::span). The memory is copied upon insertion.
using value_view = gsl::span<const std::byte>;

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_COMMON_HPP
