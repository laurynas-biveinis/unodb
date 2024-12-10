// Copyright 2020-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_THREAD_SYNC_HPP
#define UNODB_DETAIL_THREAD_SYNC_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <array>
#include <condition_variable>
#include <mutex>

#include "assert.hpp"

namespace unodb::detail {

class [[nodiscard]] thread_sync final {
 public:
  thread_sync() noexcept = default;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  ~thread_sync() noexcept { UNODB_DETAIL_ASSERT(is_reset()); }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  [[nodiscard]] bool is_reset() const {
    const std::lock_guard lock{sync_mutex};
    return !flag;
  }

  void notify() {
    {
      const std::lock_guard lock{sync_mutex};
      flag = true;
    }
    sync.notify_one();
  }

  void wait() {
    std::unique_lock lock{sync_mutex};
    sync.wait(lock, [&inner_flag = flag] { return inner_flag; });
    flag = false;
    lock.unlock();
  }

  thread_sync(const thread_sync &) = delete;
  thread_sync(thread_sync &&) = delete;
  thread_sync &operator=(const thread_sync &) = delete;
  thread_sync &operator=(thread_sync &&) = delete;

 private:
  // Replace with a C++20 latch when that's available
  std::condition_variable sync;
  mutable std::mutex sync_mutex;
  bool flag{false};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,fuchsia-statically-constructed-objects)
inline std::array<thread_sync, 6> thread_syncs;

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_THREAD_SYNC_HPP
