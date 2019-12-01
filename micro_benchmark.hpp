// Copyright 2019 Laurynas Biveinis
#ifndef MICRO_BENCHMARK_HPP_
#define MICRO_BENCHMARK_HPP_

#include <array>
#include <cstddef>

#include "art_common.hpp"

constexpr auto value1 = std::array<std::byte, 1>{};
constexpr auto value10 = std::array<std::byte, 10>{};
constexpr auto value100 = std::array<std::byte, 100>{};
constexpr auto value1000 = std::array<std::byte, 1000>{};
constexpr auto value10000 = std::array<std::byte, 10000>{};

constexpr std::array<unodb::value_view_type, 5> values = {
    unodb::value_view_type{value1}, unodb::value_view_type{value10},
    unodb::value_view_type{value100}, unodb::value_view_type{value1000},
    unodb::value_view_type{value10000}};

#endif  // MICRO_BENCHMARK_HPP_