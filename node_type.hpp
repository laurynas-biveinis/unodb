// Copyright 2021-2025 UnoDB contributors
#ifndef UNODB_DETAIL_NODE_TYPE_HPP
#define UNODB_DETAIL_NODE_TYPE_HPP

/// \file
/// Adaptive Radix Tree node types.
/// Defines the node types and, if compiling with stats, counter arrays indexed
/// by the types.

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <cstdint>

#ifdef UNODB_DETAIL_WITH_STATS

#include <array>

#include "assert.hpp"

#endif

namespace unodb {

/// Node type in the Adaptive Radix Tree.
/// The type of an internal node depends on its number of children.
enum class [[nodiscard]] node_type : std::uint8_t {
  LEAF,  ///< Leaf node for a single value
  I4,    ///< Internal node for 2-4 children
  I16,   ///< Internal node for 5-16 children
  I48,   ///< Internal node for 17-48 children
  I256   ///< Internal node for 49-256 children
};

namespace detail {

// C++ has five value categories and IIRC thousands of ways to initialize
// but no way to count the number of enum elements.

/// The number of different node types.
constexpr std::size_t node_type_count{5};

}  // namespace detail

// The rest are used only if stats are compiled in
#ifdef UNODB_DETAIL_WITH_STATS

namespace detail {

/// The number of different internal node types.
constexpr std::size_t inode_type_count{4};

/// Statically assert that \a NodeType is one of the internal node types.
/// This function may not be executed, and only holds the static assert, because
/// it is not an expression and thus cannot appear in expression contexts by
/// itself.
template <node_type NodeType>
void is_internal_static_assert() noexcept {
  static_assert(NodeType != node_type::LEAF);
  UNODB_DETAIL_CANNOT_HAPPEN();
}

}  // namespace detail

/// An `std::array` of `std::uint64_t` values for each node type.
/// Use as_i() for indexing.
using node_type_counter_array =
    std::array<std::uint64_t, detail::node_type_count>;

/// Convert \a NodeType to a value suitable for use as an index.
/// Meant for using together with node_type_counter_array.
template <node_type NodeType>
inline constexpr auto as_i{static_cast<std::size_t>(NodeType)};

/// An `std::array` of `std::uint64_t` values for each internal node type.
/// Use internal_as_i() for indexing.
using inode_type_counter_array =
    std::array<std::uint64_t, detail::inode_type_count>;

// function call before comma missing argument list
UNODB_DETAIL_DISABLE_MSVC_WARNING(4546)

/// Convert internal \a NodeType to a value suitable for use as an index.
/// Meant for using together with inode_type_counter_array.
template <node_type NodeType>
inline constexpr auto internal_as_i{
    static_cast<std::size_t>(
        detail::is_internal_static_assert<NodeType>,  // -V521
        NodeType) -
    1};

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#endif  // UNODB_DETAIL_WITH_STATS

}  // namespace unodb

#endif  // UNODB_DETAIL_NODE_TYPE_HPP
