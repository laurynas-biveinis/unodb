// Copyright 2020-2025 UnoDB contributors
#ifndef UNODB_DETAIL_THREAD_SYNC_HPP
#define UNODB_DETAIL_THREAD_SYNC_HPP

/// \file
/// Thread synchronization for concurrent tests.

// Should be the first include
#include "global.hpp"

#include <array>
#include <condition_variable>
#include <mutex>

#include "assert.hpp"

namespace unodb::detail {

/// A simple one-way synchronization mechanism to make one thread wait until
/// another one signals it.
///
/// Used in concurrent Google Test and fuzzer tests.
class [[nodiscard]] thread_sync final {
 public:
  /// Default constructor creates the synchronization primitive in reset state.
  thread_sync() noexcept = default;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  /// Destructor, asserting that the sync object, if signaled, was actually
  /// waited-for.
  ~thread_sync() noexcept { UNODB_DETAIL_ASSERT(is_reset()); }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  /// Check if the synchronization primitive is in reset state.
  [[nodiscard]] bool is_reset() const {
    const std::lock_guard lock{sync_mutex};
    return !flag;
  }

  /// Signal to allow a waiting thread to proceed.
  void notify() {
    {
      const std::lock_guard lock{sync_mutex};
      flag = true;
    }
    sync.notify_one();
  }

  /// Wait until notified, then reset.
  void wait() {
    std::unique_lock lock{sync_mutex};
    sync.wait(lock, [&inner_flag = flag] { return inner_flag; });
    flag = false;
    lock.unlock();
  }

  /// Copy construction is disabled.
  thread_sync(const thread_sync &) = delete;

  /// Move construction is disabled.
  thread_sync(thread_sync &&) = delete;

  /// Copy assignment is disabled.
  thread_sync &operator=(const thread_sync &) = delete;

  /// Move assignment is disabled.
  thread_sync &operator=(thread_sync &&) = delete;

 private:
  /// Underlying condition variable.
  std::condition_variable sync;

  /// Mutex for protecting the flag.
  mutable std::mutex sync_mutex;

  /// Flag indicating whether the thread_sync has been notified.
  bool flag{false};
};

/// Global array of thread synchronization objects.
///
/// The array size is determined by test needs.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,fuchsia-statically-constructed-objects)
inline std::array<thread_sync, 6> thread_syncs;

}  // namespace unodb::detail

#endif  // UNODB_DETAIL_THREAD_SYNC_HPP
