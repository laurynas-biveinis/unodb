// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

#include "art_common.hpp"
#include "art_internal.hpp"

namespace unodb {

namespace detail {

struct node_header;

template <class>
class basic_inode_4;  // IWYU pragma: keep

template <class>
class basic_inode_16;  // IWYU pragma: keep

template <class>
class basic_inode_48;  // IWYU pragma: keep

template <class>
class basic_inode_256;  // IWYU pragma: keep

class inode;

class inode_4;
class inode_16;
class inode_48;
class inode_256;

using inode_defs = basic_inode_def<inode_4, inode_16, inode_48, inode_256>;

using node_ptr = basic_node_ptr<node_header, inode, inode_defs>;

template <class Header, class Db>
auto make_db_leaf_ptr(art_key, value_view, Db &);

}  // namespace detail

class db final {
 public:
  using get_result = std::optional<value_view>;

  // Creation and destruction
  constexpr db() noexcept {}

  ~db() noexcept;

  // Querying
  [[nodiscard]] get_result get(key search_key) const noexcept;

  [[nodiscard]] constexpr auto empty() const noexcept {
    return root == nullptr;
  }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] bool insert(key insert_key, value_view v);

  [[nodiscard]] bool remove(key remove_key);

  void clear();

  // Stats

  // Return current memory use by tree nodes in bytes.
  [[nodiscard]] constexpr auto get_current_memory_use() const noexcept {
    return current_memory_use;
  }

  [[nodiscard]] constexpr auto get_leaf_count() const noexcept {
    return leaf_count;
  }

  [[nodiscard]] constexpr auto get_inode4_count() const noexcept {
    return inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode16_count() const noexcept {
    return inode16_count;
  }

  [[nodiscard]] constexpr auto get_inode48_count() const noexcept {
    return inode48_count;
  }

  [[nodiscard]] constexpr auto get_inode256_count() const noexcept {
    return inode256_count;
  }

  [[nodiscard]] constexpr auto get_created_inode4_count() const noexcept {
    return created_inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode4_to_inode16_count() const noexcept {
    return inode4_to_inode16_count;
  }

  [[nodiscard]] constexpr auto get_inode16_to_inode48_count() const noexcept {
    return inode16_to_inode48_count;
  }

  [[nodiscard]] constexpr auto get_inode48_to_inode256_count() const noexcept {
    return inode48_to_inode256_count;
  }

  [[nodiscard]] constexpr auto get_deleted_inode4_count() const noexcept {
    return deleted_inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode16_to_inode4_count() const noexcept {
    return inode16_to_inode4_count;
  }

  [[nodiscard]] constexpr auto get_inode48_to_inode16_count() const noexcept {
    return inode48_to_inode16_count;
  }

  [[nodiscard]] constexpr auto get_inode256_to_inode48_count() const noexcept {
    return inode256_to_inode48_count;
  }

  [[nodiscard]] constexpr auto get_key_prefix_splits() const noexcept {
    return key_prefix_splits;
  }

  // Public utils
  [[nodiscard]] static constexpr auto key_found(
      const get_result &result) noexcept {
    return static_cast<bool>(result);
  }

  // Debugging
  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const;

 private:
  void delete_subtree(detail::node_ptr) noexcept;

  void delete_root_subtree() noexcept;

  constexpr void increase_memory_use(std::size_t delta) noexcept {
    current_memory_use += delta;
  }

  constexpr void decrease_memory_use(std::size_t delta) noexcept {
    assert(delta <= current_memory_use);
    current_memory_use -= delta;
  }

  constexpr void increment_leaf_count(std::size_t leaf_size) noexcept {
    increase_memory_use(leaf_size);
    ++leaf_count;
  }

  constexpr void decrement_leaf_count(std::size_t leaf_size) noexcept {
    decrease_memory_use(leaf_size);

    assert(leaf_count > 0);
    --leaf_count;
  }

  inline constexpr void decrement_inode4_count() noexcept;
  inline constexpr void decrement_inode16_count() noexcept;
  inline constexpr void decrement_inode48_count() noexcept;
  inline constexpr void decrement_inode256_count() noexcept;

  detail::node_ptr root{nullptr};

  std::size_t current_memory_use{0};

  std::uint64_t leaf_count{0};
  std::uint64_t inode4_count{0};
  std::uint64_t inode16_count{0};
  std::uint64_t inode48_count{0};
  std::uint64_t inode256_count{0};

  std::uint64_t created_inode4_count{0};
  std::uint64_t inode4_to_inode16_count{0};
  std::uint64_t inode16_to_inode48_count{0};
  std::uint64_t inode48_to_inode256_count{0};

  std::uint64_t deleted_inode4_count{0};
  std::uint64_t inode16_to_inode4_count{0};
  std::uint64_t inode48_to_inode16_count{0};
  std::uint64_t inode256_to_inode48_count{0};

  std::uint64_t key_prefix_splits{0};

  friend auto detail::make_db_leaf_ptr<detail::node_header, db>(detail::art_key,
                                                                value_view,
                                                                db &);

  template <class, class>
  friend class detail::basic_db_leaf_deleter;

  template <class, class, class, class>
  friend class detail::basic_db_inode_deleter;

  template <class>
  friend class detail::basic_inode_4;

  template <class>
  friend class detail::basic_inode_16;

  template <class>
  friend class detail::basic_inode_48;

  template <class>
  friend class detail::basic_inode_256;

  friend class detail::inode_4;
  friend class detail::inode_16;
  friend class detail::inode_48;
  friend class detail::inode_256;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
