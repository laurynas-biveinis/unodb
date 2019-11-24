// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_MUTEX_ART_HPP_
#define UNODB_MUTEX_ART_HPP_

#include "global.hpp"

#include <mutex>

#include "art.hpp"

namespace unodb {

class mutex_db final {
 public:
  using get_result = db::get_result;
  using tree_depth_type = db::tree_depth_type;

  explicit mutex_db(std::size_t memory_limit = 0) noexcept
      : db_{memory_limit} {}

  [[nodiscard]] auto get(key_type k) const noexcept {
    const std::lock_guard guard{mutex};
    return db_.get(k);
  }

  [[nodiscard]] auto insert(key_type k, value_view v) {
    const std::lock_guard guard{mutex};
    return db_.insert(k, v);
  }

  [[nodiscard]] auto remove(key_type k) {
    const std::lock_guard guard{mutex};
    return db_.remove(k);
  }

#ifndef NDEBUG
  void dump(std::ostream &os) const {
    const std::lock_guard guard{mutex};
    db_.dump(os);
  }
#endif

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
