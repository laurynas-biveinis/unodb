// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_OLC_ART_HPP_
#define UNODB_OLC_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <atomic>
#include <cassert>
#include <cstddef>  // IWYU pragma: keep
#include <cstdint>
#include <iostream>
#include <optional>

#include "art_common.hpp"
#include "art_internal.hpp"
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

template <class, class, template <class, class> class>
struct db_defs;

struct olc_node_header;

class olc_inode;

class olc_inode_4;
class olc_inode_16;
class olc_inode_48;
class olc_inode_256;

using olc_inode_defs =
    basic_inode_def<olc_inode_4, olc_inode_16, olc_inode_48, olc_inode_256>;

using olc_node_ptr = basic_node_ptr<olc_node_header, olc_inode, olc_inode_defs>;

template <class, class>
class db_leaf_qsbr_deleter;  // IWYU pragma: keep

template <class Header, class Db>
auto make_db_leaf_ptr(art_key, value_view, Db &);

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

  [[nodiscard]] auto get_leaf_count() const noexcept {
    return leaf_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode4_count() const noexcept {
    return inode4_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode16_count() const noexcept {
    return inode16_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode48_count() const noexcept {
    return inode48_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode256_count() const noexcept {
    return inode256_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_created_inode4_count() const noexcept {
    return created_inode4_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode4_to_inode16_count() const noexcept {
    return inode4_to_inode16_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode16_to_inode48_count() const noexcept {
    return inode16_to_inode48_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode48_to_inode256_count() const noexcept {
    return inode48_to_inode256_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_deleted_inode4_count() const noexcept {
    return deleted_inode4_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode16_to_inode4_count() const noexcept {
    return inode16_to_inode4_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode48_to_inode16_count() const noexcept {
    return inode48_to_inode16_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_inode256_to_inode48_count() const noexcept {
    return inode256_to_inode48_count.load(std::memory_order_relaxed);
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

  void delete_subtree(detail::olc_node_ptr) noexcept;

  void delete_root_subtree() noexcept;

  void increase_memory_use(std::size_t delta);
  void decrease_memory_use(std::size_t delta) noexcept;

  void increment_leaf_count(std::size_t leaf_size) noexcept {
    increase_memory_use(leaf_size);
    leaf_count.fetch_add(1, std::memory_order_relaxed);
  }

  void decrement_leaf_count(std::size_t leaf_size) noexcept {
    decrease_memory_use(leaf_size);

    const auto USED_IN_DEBUG old_leaf_count =
        leaf_count.fetch_sub(1, std::memory_order_relaxed);
    assert(old_leaf_count > 0);
  }

  inline void increment_inode4_count() noexcept;
  inline void increment_inode16_count() noexcept;
  inline void increment_inode48_count() noexcept;
  inline void increment_inode256_count() noexcept;

  inline void decrement_inode4_count() noexcept;
  inline void decrement_inode16_count() noexcept;
  inline void decrement_inode48_count() noexcept;
  inline void decrement_inode256_count() noexcept;

  mutable optimistic_lock root_pointer_lock;

  critical_section_protected<detail::olc_node_ptr> root{nullptr};

  // Current logically allocated memory that is not scheduled to be reclaimed.
  // The total memory currently allocated is this plus the QSBR deallocation
  // backlog (qsbr::previous_interval_total_dealloc_size +
  // qsbr::current_interval_total_dealloc_size).
  std::atomic<std::size_t> current_memory_use{0};

  std::atomic<std::uint64_t> leaf_count{0};
  std::atomic<std::uint64_t> inode4_count{0};
  std::atomic<std::uint64_t> inode16_count{0};
  std::atomic<std::uint64_t> inode48_count{0};
  std::atomic<std::uint64_t> inode256_count{0};

  std::atomic<std::uint64_t> created_inode4_count{0};
  std::atomic<std::uint64_t> inode4_to_inode16_count{0};
  std::atomic<std::uint64_t> inode16_to_inode48_count{0};
  std::atomic<std::uint64_t> inode48_to_inode256_count{0};

  std::atomic<std::uint64_t> deleted_inode4_count{0};
  std::atomic<std::uint64_t> inode16_to_inode4_count{0};
  std::atomic<std::uint64_t> inode48_to_inode16_count{0};
  std::atomic<std::uint64_t> inode256_to_inode48_count{0};

  std::atomic<std::uint64_t> key_prefix_splits{0};

  friend auto detail::make_db_leaf_ptr<detail::olc_node_header, olc_db>(
      detail::art_key, value_view, olc_db &);

  template <class>
  friend struct detail::basic_leaf;

  template <class, class>
  friend class detail::basic_db_leaf_deleter;

  template <class, class>
  friend class detail::db_leaf_qsbr_deleter;

  template <class, class, template <class, class> class>
  friend struct detail::db_defs;

  template <class, class, class>
  friend class detail::basic_db_inode_deleter;

  template <class>
  friend class detail::basic_inode_4;

  template <class>
  friend class detail::basic_inode_16;

  template <class>
  friend class detail::basic_inode_48;

  template <class>
  friend class detail::basic_inode_256;
};

}  // namespace unodb

#endif  // UNODB_OLC_ART_HPP_
