// Copyright 2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_NODE_TYPE_HPP
#define UNODB_DETAIL_NODE_TYPE_HPP

#include "global.hpp"

#include <array>
#include <cstdint>

namespace unodb {

enum class node_type : std::uint8_t { LEAF, I4, I16, I48, I256 };

namespace detail {

// C++ has five value categories and IIRC thousands of ways to initialize but no
// way to count the number of enum elements.
constexpr auto node_type_count{5};
constexpr auto inode_type_count{4};

template <node_type NodeType>
void is_internal() noexcept {
  static_assert(NodeType != node_type::LEAF);
  // This function is not for execution, but to wrap the static_assert, which is
  // not an expression for some reason.
  UNODB_DETAIL_CANNOT_HAPPEN();
}

}  // namespace detail

using node_type_counter_array =
    std::array<std::uint64_t, detail::node_type_count>;

template <node_type NodeType>
inline constexpr auto as_i{static_cast<std::size_t>(NodeType)};

using inode_type_counter_array =
    std::array<std::uint64_t, detail::inode_type_count>;

template <node_type NodeType>
inline constexpr auto internal_as_i{
    static_cast<std::size_t>(detail::is_internal<NodeType>, NodeType) - 1};

}  // namespace unodb

#endif  // UNODB_DETAIL_NODE_TYPE_HPP
