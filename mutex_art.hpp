// Copyright 2019-2020 Laurynas Biveinis
#ifndef UNODB_MUTEX_ART_HPP_
#define UNODB_MUTEX_ART_HPP_

#include "global.hpp"

#include <mutex>

#include "art.hpp"

namespace unodb {

class mutex_db final {
 public:
  // Creation and destruction
  constexpr mutex_db() noexcept : db_{} {}

  // Querying
  [[nodiscard]] auto get(key k) const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get(k);
  }

  [[nodiscard]] auto empty() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.empty();
  }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] auto insert(key k, value_view v) {
    const std::lock_guard guard{mutex};
    return db_.insert(k, v);
  }

  [[nodiscard]] auto remove(key k) {
    const std::lock_guard guard{mutex};
    return db_.remove(k);
  }

  void clear() {
    const std::lock_guard guard{mutex};
    db_.clear();
  }

  // Stats
  [[nodiscard]] auto get_current_memory_use() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_current_memory_use();
  }

  [[nodiscard]] auto get_leaf_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_leaf_count();
  }

  [[nodiscard]] auto get_inode4_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode4_count();
  }

  [[nodiscard]] auto get_inode16_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode16_count();
  }

  [[nodiscard]] auto get_inode48_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode48_count();
  }

  [[nodiscard]] auto get_inode256_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode256_count();
  }

  [[nodiscard]] auto get_created_inode4_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_created_inode4_count();
  }

  [[nodiscard]] auto get_inode4_to_inode16_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode4_to_inode16_count();
  }

  [[nodiscard]] auto get_inode16_to_inode48_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode16_to_inode48_count();
  }

  [[nodiscard]] auto get_inode48_to_inode256_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode48_to_inode256_count();
  }

  [[nodiscard]] auto get_deleted_inode4_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_deleted_inode4_count();
  }

  [[nodiscard]] auto get_inode16_to_inode4_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode16_to_inode4_count();
  }

  [[nodiscard]] auto get_inode48_to_inode16_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode48_to_inode16_count();
  }

  [[nodiscard]] auto get_inode256_to_inode48_count() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_inode256_to_inode48_count();
  }

  [[nodiscard]] auto get_key_prefix_splits() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_key_prefix_splits();
  }

  // Debugging
  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    const std::lock_guard guard{mutex};
    db_.dump(os);
  }

 private:
  db db_;
  mutable std::mutex mutex;
};

}  // namespace unodb

#endif  // UNODB_MUTEX_ART_HPP_
