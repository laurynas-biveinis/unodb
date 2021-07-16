// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_OLC_ART_HPP
#define UNODB_DETAIL_OLC_ART_HPP

#include "global.hpp"  // IWYU pragma: keep

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>  // IWYU pragma: keep
#include <cstdint>
#include <iostream>
#include <optional>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "node_type.hpp"
#include "optimistic_lock.hpp"
#include "qsbr_ptr.hpp"

namespace unodb {

namespace detail {

template <class>
class basic_inode_4;  // IWYU pragma: keep

template <class>
class basic_inode_16;  // IWYU pragma: keep

template <class>
class basic_inode_48;  // IWYU pragma: keep

template <class>
class basic_inode_256;  // IWYU pragma: keep

template <class, template <class> class, class, template <class> class,
          template <class, class> class, template <class> class>
struct basic_art_policy;  // IWYU pragma: keep

struct olc_node_header;

class olc_inode;

class olc_inode_4;
class olc_inode_16;
class olc_inode_48;
class olc_inode_256;

using olc_inode_defs =
    basic_inode_def<olc_inode_4, olc_inode_16, olc_inode_48, olc_inode_256>;

using olc_node_ptr = basic_node_ptr<olc_node_header, olc_inode, olc_inode_defs>;

template <class>
class db_inode_qsbr_deleter;  // IWYU pragma: keep

template <class, class>
class db_leaf_qsbr_deleter;  // IWYU pragma: keep

template <class Header, class Db>
auto make_db_leaf_ptr(art_key, value_view, Db &);

struct olc_impl_helpers;

}  // namespace detail

using qsbr_value_view = qsbr_ptr_span<const std::byte>;

// A concurrent Adaptive Radix Tree that is synchronized using optimistic lock
// coupling. At any time, at most two directly-related tree nodes can be
// write-locked by the insert algorithm and three by the delete algorithm. The
// lock used is optimistic lock (see optimistic_lock.hpp), where only writers
// lock and readers access nodes optimistically with node version checks. For
// deleted node reclamation, Quiescent State-Based Reclamation is used.
class olc_db final {
 public:
  using get_result = std::optional<qsbr_value_view>;

  // Creation and destruction
  constexpr explicit olc_db() noexcept {}

  ~olc_db() noexcept;

  // Querying
  [[nodiscard]] get_result get(key search_key) const noexcept;

  [[nodiscard]] auto empty() const noexcept { return root == nullptr; }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] bool insert(key insert_key, value_view v);

  [[nodiscard]] bool remove(key remove_key);

  // Only legal in single-threaded context, as destructor
  void clear();

  // Stats

  // Return current memory use by tree nodes in bytes
  [[nodiscard]] auto get_current_memory_use() const noexcept {
    return current_memory_use.load(std::memory_order_relaxed);
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_node_count() const noexcept {
    return node_counts[as_i<NodeType>].load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_node_counts() const noexcept {
    node_type_counter_array result;
    for (decltype(node_counts)::size_type i = 0; i < node_counts.size(); ++i) {
      result[i] = node_counts[i].load(std::memory_order_relaxed);
    }
    return result;
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_growing_inode_count() const noexcept {
    return growing_inode_counts[internal_as_i<NodeType>].load(
        std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_growing_inode_counts() const noexcept {
    inode_type_counter_array result;
    for (decltype(growing_inode_counts)::size_type i = 0;
         i < growing_inode_counts.size(); ++i) {
      result[i] = growing_inode_counts[i].load(std::memory_order_relaxed);
    }
    return result;
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_shrinking_inode_count() const noexcept {
    return shrinking_inode_counts[internal_as_i<NodeType>].load(
        std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_shrinking_inode_counts() const noexcept {
    inode_type_counter_array result;
    for (decltype(shrinking_inode_counts)::size_type i = 0;
         i < shrinking_inode_counts.size(); ++i) {
      result[i] = shrinking_inode_counts[i].load(std::memory_order_relaxed);
    }
    return result;
  }

  [[nodiscard]] auto get_key_prefix_splits() const noexcept {
    return key_prefix_splits.load(std::memory_order_relaxed);
  }

  // Public utils
  [[nodiscard]] static constexpr auto key_found(
      const get_result &result) noexcept {
    return static_cast<bool>(result);
  }

  // Debugging
  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const;

  olc_db(const olc_db &) noexcept = delete;
  olc_db(olc_db &&) noexcept = delete;
  olc_db &operator=(const olc_db &) noexcept = delete;
  olc_db &operator=(olc_db &&) noexcept = delete;

 private:
  // If get_result is not present, the search was interrupted. Yes, this
  // resolves to std::optional<std::optional<value_view>>, but IMHO both
  // levels of std::optional are clear here
  using try_get_result_type = std::optional<get_result>;

  using try_update_result_type = std::optional<bool>;

  [[nodiscard]] try_get_result_type try_get(detail::art_key k) const noexcept;

  [[nodiscard]] try_update_result_type try_insert(detail::art_key k,
                                                  value_view v);

  [[nodiscard]] try_update_result_type try_remove(detail::art_key k);

  void delete_root_subtree() noexcept;

  void increase_memory_use(std::size_t delta);
  void decrease_memory_use(std::size_t delta) noexcept;

  void increment_leaf_count(std::size_t leaf_size) noexcept {
    increase_memory_use(leaf_size);
    node_counts[as_i<node_type::LEAF>].fetch_add(1, std::memory_order_relaxed);
  }

  void decrement_leaf_count(std::size_t leaf_size) noexcept {
    decrease_memory_use(leaf_size);

    const auto UNODB_DETAIL_USED_IN_DEBUG old_leaf_count =
        node_counts[as_i<node_type::LEAF>].fetch_sub(1,
                                                     std::memory_order_relaxed);
    assert(old_leaf_count > 0);
  }

  template <class INode>
  constexpr void increment_inode_count() noexcept {
    static_assert(detail::olc_inode_defs::is_inode<INode>());

    node_counts[as_i<INode::type>].fetch_add(1, std::memory_order_relaxed);
    increase_memory_use(sizeof(INode));
  }

  template <class INode>
  constexpr void decrement_inode_count() noexcept;

  template <node_type NodeType>
  constexpr void account_growing_inode() noexcept;

  template <node_type NodeType>
  constexpr void account_shrinking_inode() noexcept;

  mutable optimistic_lock root_pointer_lock;

  in_critical_section<detail::olc_node_ptr> root{detail::olc_node_ptr{nullptr}};

  // Current logically allocated memory that is not scheduled to be reclaimed.
  // The total memory currently allocated is this plus the QSBR deallocation
  // backlog (qsbr::previous_interval_total_dealloc_size +
  // qsbr::current_interval_total_dealloc_size).
  std::atomic<std::size_t> current_memory_use{0};

  template <class T>
  using atomic_array = std::array<std::atomic<typename T::value_type>,
                                  std::tuple_size<T>::value>;

  atomic_array<node_type_counter_array> node_counts{};
  atomic_array<inode_type_counter_array> growing_inode_counts{};
  atomic_array<inode_type_counter_array> shrinking_inode_counts{};

  std::atomic<std::uint64_t> key_prefix_splits{0};

  friend auto detail::make_db_leaf_ptr<detail::olc_node_header, olc_db>(
      detail::art_key, value_view, olc_db &);

  template <class>
  friend struct detail::basic_leaf;

  template <class, class>
  friend class detail::basic_db_leaf_deleter;

  template <class, class>
  friend class detail::db_leaf_qsbr_deleter;

  template <class>
  friend class detail::db_inode_qsbr_deleter;

  template <class, template <class> class, class, template <class> class,
            template <class, class> class, template <class> class>
  friend struct detail::basic_art_policy;

  template <class, class, class, template <class> class>
  friend class detail::basic_db_inode_deleter;

  friend struct detail::olc_impl_helpers;
};

}  // namespace unodb

#endif  // UNODB_DETAIL_OLC_ART_HPP
