// Copyright 2024 Laurynas Biveinis

// A simple example showing unodb::db statistics. For simplicity &
// self-containedness does not concern with exception handling and refactoring
// the duplicated code with other examples.

#include "global.hpp"  // IWYU pragma: keep

#include <iostream>
#include <string_view>

#include "art.hpp"

namespace {

constexpr std::string_view value = "Value";

[[nodiscard, gnu::pure]] unodb::value_view from_string_view(
    std::string_view sv) {
  return {reinterpret_cast<const std::byte *>(sv.data()), sv.length()};
}

}  // namespace

int main() {
  unodb::db tree;
  auto insert_result = tree.insert(1, from_string_view(value));
  insert_result &= tree.insert(2, from_string_view(value));
  insert_result &= tree.insert(3, from_string_view(value));
  insert_result &= tree.insert(4, from_string_view(value));
  insert_result &= tree.insert(5, from_string_view(value));

  std::cerr << "All inserts succeeded: " << insert_result << '\n';
  const auto node_counts = tree.get_node_counts();
  std::cerr << "Current memory usage: " << tree.get_current_memory_use() << '\n'
            << "Key prefix splits: " << tree.get_key_prefix_splits() << '\n'
            << "Leaf count: "
            << node_counts[unodb::as_i<unodb::node_type::LEAF>] << '\n'
            << "I4 count: " << node_counts[unodb::as_i<unodb::node_type::I4>]
            << '\n'
            << "I16 count: " << node_counts[unodb::as_i<unodb::node_type::I16>]
            << '\n'
            << "I48 count: " << node_counts[unodb::as_i<unodb::node_type::I48>]
            << '\n'
            << "I256 count: "
            << node_counts[unodb::as_i<unodb::node_type::I256>] << '\n';

  const auto growing_inode_counts = tree.get_growing_inode_counts();
  std::cerr
      << "Promotions to I4: "
      << growing_inode_counts[unodb::internal_as_i<unodb::node_type::I4>]
      << '\n'
      << "Promotions to I16: "
      << growing_inode_counts[unodb::internal_as_i<unodb::node_type::I16>]
      << '\n'
      << "Promotions to I48: "
      << growing_inode_counts[unodb::internal_as_i<unodb::node_type::I48>]
      << '\n'
      << "Promotions to I256: "
      << growing_inode_counts[unodb::internal_as_i<unodb::node_type::I256>]
      << '\n';

  const auto shrinking_inode_counts = tree.get_shrinking_inode_counts();
  std::cerr
      << "Demotions from I4: "
      << shrinking_inode_counts[unodb::internal_as_i<unodb::node_type::I4>]
      << '\n'
      << "Demotions from I16: "
      << shrinking_inode_counts[unodb::internal_as_i<unodb::node_type::I16>]
      << '\n'
      << "Demotions from I48: "
      << shrinking_inode_counts[unodb::internal_as_i<unodb::node_type::I48>]
      << '\n'
      << "Demotions from I256: "
      << shrinking_inode_counts[unodb::internal_as_i<unodb::node_type::I256>]
      << '\n';
}
