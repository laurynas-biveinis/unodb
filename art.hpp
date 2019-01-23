// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include <cstddef>  // for uint64_t
#include <cstdint>  // IWYU pragma: keep
#include <gsl/span>

namespace unodb {

using key_type = uint64_t;
using value_type = gsl::span<const std::byte>;

class db {
public:
    db() noexcept { }
    void insert(key_type k, value_type t);
};

} // namespace unodb

#endif // UNODB_ART_HPP_
