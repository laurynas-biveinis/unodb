// Copyright (C) 2021 Laurynas Biveinis
#ifndef UNODB_ART_MAP_DB_HPP_
#define UNODB_ART_MAP_DB_HPP_

#include "global.hpp"

#include <art/map.h>

#include "art_common.hpp"
#include "node_type.hpp"

namespace unodb {

class art_map_db final {
  static constexpr auto value8{std::array<std::byte, 8>{}};

 public:
  using get_result = std::optional<value_view>;

  [[nodiscard]] auto get(key k) const noexcept {
    const auto itr = db_.find(k);
    if (itr == db_.cend()) return get_result{};
    return get_result{{&value8[0], 8}};
  }

  [[nodiscard]] static constexpr auto key_found(
      const get_result &result) noexcept {
    return static_cast<bool>(result);
  }

  // Shenanigans!
  UNODB_DETAIL_DISABLE_GCC_WARNING("-Wcast-align")
  [[nodiscard]] bool insert(key insert_key, value_view v) {
    assert(v.size() == sizeof(uint64_t));
    const auto result = db_.try_emplace(
        insert_key, *(reinterpret_cast<const std::uint64_t *>(&v[0])));
    return result.second;
  }
  UNODB_DETAIL_RESTORE_GCC_WARNINGS()

  [[nodiscard]] bool remove(key remove_key) {
    return db_.erase(remove_key) == 1;
  }

  void clear() { db_.clear(); }

  [[nodiscard]] auto empty() const noexcept {
    return db_.empty();
  }

  [[nodiscard]] auto get_current_memory_use() const noexcept {
    return db_.current_memory_use();
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    db_.dump(os);
  }

  [[nodiscard]] auto get_node_counts() const noexcept {
    // Diverged API
    return node_type_counter_array();
  }

  [[nodiscard]] auto get_growing_inode_counts() const noexcept {
    return inode_type_counter_array();
  }

  template <node_type NodeType>
  [[nodiscard]] constexpr auto get_growing_inode_count() const noexcept {
    return 0ULL;
  }

  template <node_type NodeType>
  [[nodiscard]] constexpr auto get_shrinking_inode_count() const noexcept {
    return 0ULL;
  }

  [[nodiscard]] auto get_key_prefix_splits() const noexcept { return 0ULL; }

 private:
  art::map<unodb::key, std::uint64_t> db_;
};

}  // namespace unodb

#endif  // UNODB_ART_MAP_DB_HPP_
