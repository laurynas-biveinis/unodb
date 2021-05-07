// Copyright 2019-2020 Laurynas Biveinis
#ifndef UNODB_MUTEX_ART_HPP_
#define UNODB_MUTEX_ART_HPP_

#include "global.hpp"

#include <mutex>
#include <utility>

#include "art.hpp"

namespace unodb {

class mutex_db final {
 public:
  // If the search key was found, that is, the first pair member has a value,
  // then the second member is a locked tree mutex which must be released ASAP
  // after reading the first pair member. Otherwise, the second member is
  // undefined.
  using get_result = std::pair<db::get_result, std::unique_lock<std::mutex>>;

  // Creation and destruction
  constexpr mutex_db() noexcept : db_{} {}

  // Querying
  [[nodiscard]] auto get(key k) const noexcept {
    std::unique_lock<std::mutex> guard{mutex};
    const auto db_get_result{db_.get(k)};
    if (!db_get_result) {
      guard.unlock();
      return std::make_pair(db_get_result, std::unique_lock<std::mutex>{});
    }
    return std::make_pair(db_get_result, std::move(guard));
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

  // Public utils

  // Releases the mutex in the case key was not found, keeps it locked
  // otherwise.
  [[nodiscard]] static auto key_found(get_result &result) {
#ifndef NDEBUG
    auto &lock{result.second};
    assert(!result.first || lock.owns_lock());
#endif

    return static_cast<bool>(result.first);
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
