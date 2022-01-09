// Copyright 2019-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_HPP
#define UNODB_DETAIL_ART_HPP

#include "global.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "assert.hpp"
#include "node_type.hpp"

namespace unodb {

namespace detail {

struct node_header;

template <class, template <class> class, class, class, template <class> class,
          template <class, class> class>
struct basic_art_policy;  // IWYU pragma: keep

using node_ptr = basic_node_ptr<node_header>;

template <class Header, class Db>
[[nodiscard]] auto make_db_leaf_ptr(art_key, value_view, Db &);

struct impl_helpers;

}  // namespace detail

class db final {
 public:
  using get_result = std::optional<value_view>;

  // Creation and destruction
  db() noexcept = default;

  ~db() noexcept;

  // TODO(laurynas): implement copy and move operations
  db(const db &) = delete;
  db(db &&) = delete;
  db &operator=(const db &) = delete;
  db &operator=(db &&) = delete;

  // Querying
  [[nodiscard, gnu::pure]] get_result get(key search_key) const noexcept;

  [[nodiscard, gnu::pure]] auto empty() const noexcept {
    return root == nullptr;
  }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] bool insert(key insert_key, value_view v);

  [[nodiscard]] bool remove(key remove_key);

  void clear();

  // Stats

  // Return current memory use by tree nodes in bytes.
  [[nodiscard, gnu::pure]] constexpr auto get_current_memory_use()
      const noexcept {
    return current_memory_use;
  }

  template <node_type NodeType>
  [[nodiscard, gnu::pure]] constexpr auto get_node_count() const noexcept {
    return node_counts[as_i<NodeType>];
  }

  [[nodiscard, gnu::pure]] constexpr auto get_node_counts() const noexcept {
    return node_counts;
  }

  template <node_type NodeType>
  [[nodiscard, gnu::pure]] constexpr auto get_growing_inode_count()
      const noexcept {
    return growing_inode_counts[internal_as_i<NodeType>];
  }

  [[nodiscard, gnu::pure]] constexpr auto get_growing_inode_counts()
      const noexcept {
    return growing_inode_counts;
  }

  template <node_type NodeType>
  [[nodiscard, gnu::pure]] constexpr auto get_shrinking_inode_count()
      const noexcept {
    return shrinking_inode_counts[internal_as_i<NodeType>];
  }

  [[nodiscard, gnu::pure]] constexpr auto get_shrinking_inode_counts()
      const noexcept {
    return shrinking_inode_counts;
  }

  [[nodiscard, gnu::pure]] constexpr auto get_key_prefix_splits()
      const noexcept {
    return key_prefix_splits;
  }

  // Public utils
  [[nodiscard, gnu::const]] static constexpr auto key_found(
      const get_result &result) noexcept {
    return static_cast<bool>(result);
  }

  // Debugging
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const;

 private:
  void delete_root_subtree() noexcept;

  constexpr void increase_memory_use(std::size_t delta) noexcept {
    current_memory_use += delta;
  }

  constexpr void decrease_memory_use(std::size_t delta) noexcept {
    UNODB_DETAIL_ASSERT(delta <= current_memory_use);
    current_memory_use -= delta;
  }

  constexpr void increment_leaf_count(std::size_t leaf_size) noexcept {
    increase_memory_use(leaf_size);
    ++node_counts[as_i<node_type::LEAF>];
  }

  constexpr void decrement_leaf_count(std::size_t leaf_size) noexcept {
    decrease_memory_use(leaf_size);

    UNODB_DETAIL_ASSERT(node_counts[as_i<node_type::LEAF>] > 0);
    --node_counts[as_i<node_type::LEAF>];
  }

  template <class INode>
  constexpr void increment_inode_count() noexcept;

  template <class INode>
  constexpr void decrement_inode_count() noexcept;

  template <node_type NodeType>
  constexpr void account_growing_inode() noexcept;

  template <node_type NodeType>
  constexpr void account_shrinking_inode() noexcept;

  detail::node_ptr root{nullptr};

  std::size_t current_memory_use{0};

  node_type_counter_array node_counts{};
  inode_type_counter_array growing_inode_counts{};
  inode_type_counter_array shrinking_inode_counts{};

  std::uint64_t key_prefix_splits{0};

  friend auto detail::make_db_leaf_ptr<detail::node_header, db>(detail::art_key,
                                                                value_view,
                                                                db &);

  template <class, class>
  friend class detail::basic_db_leaf_deleter;

  template <class, template <class> class, class, class, template <class> class,
            template <class, class> class>
  friend struct detail::basic_art_policy;

  template <class, class>
  friend class detail::basic_db_inode_deleter;

  friend struct detail::impl_helpers;
};

}  // namespace unodb

#endif  // UNODB_DETAIL_ART_HPP
