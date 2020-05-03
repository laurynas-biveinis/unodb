// Copyright 2019-2020 Laurynas Biveinis
#ifndef UNODB_MUTEX_ART_HPP_
#define UNODB_MUTEX_ART_HPP_

#include "global.hpp"

#include <mutex>

#include "art.hpp"

namespace unodb {

class mutex_db final {
 public:
  explicit mutex_db(std::size_t memory_limit = 0) noexcept
      : db_{memory_limit} {}

  [[nodiscard]] auto get(key k) const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get(k);
  }

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

  void dump(std::ostream &os) const {
    const std::lock_guard guard{mutex};
    db_.dump(os);
  }

  [[nodiscard]] auto empty() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.empty();
  }

  [[nodiscard]] auto get_current_memory_use() const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get_current_memory_use();
  }

 private:
  db db_;
  mutable std::mutex mutex;
};

}  // namespace unodb

#endif  // UNODB_MUTEX_ART_HPP_
