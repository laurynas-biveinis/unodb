// Copyright 2021-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_NODE_TYPE_HPP
#define UNODB_DETAIL_NODE_TYPE_HPP

#include "global.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "assert.hpp"

namespace unodb {

enum class [[nodiscard]] node_type : std::uint8_t { LEAF, I4, I16, I48, I256 };

namespace detail {

// C++ has five value categories and IIRC thousands of ways to initialize but no
// way to count the number of enum elements.
constexpr std::size_t node_type_count{5};
constexpr std::size_t inode_type_count{4};

template <node_type NodeType>
void is_internal_static_assert() noexcept {
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

// function call before comma missing argument list
UNODB_DETAIL_DISABLE_MSVC_WARNING(4546)

template <node_type NodeType>
inline constexpr auto internal_as_i{
    static_cast<std::size_t>(
        detail::is_internal_static_assert<NodeType>,  // -V521
        NodeType) -
    1};

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace unodb

#endif  // UNODB_DETAIL_NODE_TYPE_HPP
