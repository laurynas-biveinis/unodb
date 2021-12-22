// Copyright (C) 2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_LOCK_FREE_STATS_HPP
#define UNODB_DETAIL_LOCK_FREE_STATS_HPP

#include "global.hpp"

#include <atomic>
#include <cstdint>
#include <limits>

namespace unodb {

// The first data point is always 0, to reduce branching

class [[nodiscard]] lock_free_stats {
 public:
  lock_free_stats() noexcept { count_and_mean.reset(); }

  // The implementation uses the deprecated __sync GCC builtins because they are
  // the only ones that compile directly to LOCK CMPXCHG16B with GCC on x86_64.
  // FIXME(laurynas): copy-paste with the child class add_value
  void add_value(std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::int64_t>::max())
      __builtin_unreachable();
    const auto double_value = static_cast<double>(value);

    auto seen_count = count_and_mean.f.count.load(std::memory_order_relaxed);
    auto seen_mean = count_and_mean.f.mean.load(std::memory_order_relaxed);
    while (true) {
      const auto new_count = seen_count + 1;
      const auto delta = double_value - seen_mean;
      if (new_count > std::numeric_limits<std::int64_t>::max())
        __builtin_unreachable();
      const auto new_mean = seen_mean + delta / static_cast<double>(new_count);

      count_and_mean_dword old_count_and_mean;
      old_count_and_mean.f.count.store(seen_count, std::memory_order_relaxed);
      old_count_and_mean.f.mean.store(seen_mean, std::memory_order_relaxed);

      count_and_mean_dword new_count_and_mean;
      new_count_and_mean.f.count.store(new_count, std::memory_order_relaxed);
      new_count_and_mean.f.mean.store(new_mean, std::memory_order_relaxed);

      count_and_mean_dword actual_old_count_and_mean;
      UNODB_DETAIL_DISABLE_CLANG_WARNING("-Watomic-implicit-seq-cst")
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      actual_old_count_and_mean.dword = __sync_val_compare_and_swap(
          &count_and_mean.dword, old_count_and_mean.dword,
          new_count_and_mean.dword);
      UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

      const auto actual_old_count =
          actual_old_count_and_mean.f.count.load(std::memory_order_relaxed);

      if (UNODB_DETAIL_LIKELY(actual_old_count == seen_count)) {
        break;
      }

      seen_count = actual_old_count;
      seen_mean =
          actual_old_count_and_mean.f.mean.load(std::memory_order_relaxed);
    }
  }

  void reset() noexcept {
    count_and_mean.reset();
    std::atomic_thread_fence(std::memory_order_release);
  }

  [[nodiscard]] double get_mean() const noexcept {
    return count_and_mean.f.mean.load(std::memory_order_acquire);
  }

 protected:
  [[nodiscard]] std::uint64_t get_count() const noexcept {
    return count_and_mean.f.count.load(std::memory_order_acquire);
  }

  // FIXME(laurynas): can make it private again?
  union count_and_mean_dword {
    __extension__ __int128 dword;
    struct {
      std::atomic<std::uint64_t> count;
      std::atomic<double> mean;
    } f;

    count_and_mean_dword() noexcept {}

    void reset() noexcept {
      f.count.store(1, std::memory_order_relaxed);
      f.mean.store(0.0, std::memory_order_relaxed);
    }
  } count_and_mean;
  static_assert(sizeof(count_and_mean) == 16);
};

class [[nodiscard]] lock_free_max_variance_stats : public lock_free_stats {
 public:
  // The implementation uses the deprecated __sync GCC builtins because they are
  // the only ones that compile directly to LOCK CMPXCHG16B with GCC on x86_64.
  void add_value(std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::int64_t>::max())
      __builtin_unreachable();
    const auto double_value = static_cast<double>(value);

    auto seen_count = count_and_mean.f.count.load(std::memory_order_relaxed);
    auto seen_mean = count_and_mean.f.mean.load(std::memory_order_relaxed);

    double delta;
    count_and_mean_dword new_count_and_mean;

    while (true) {
      const auto new_count = seen_count + 1;
      delta = double_value - seen_mean;
      if (new_count > std::numeric_limits<std::int64_t>::max())
        __builtin_unreachable();
      const auto new_mean = seen_mean + delta / static_cast<double>(new_count);

      count_and_mean_dword old_count_and_mean;
      old_count_and_mean.f.count.store(seen_count, std::memory_order_relaxed);
      old_count_and_mean.f.mean.store(seen_mean, std::memory_order_relaxed);

      new_count_and_mean.f.count.store(new_count, std::memory_order_relaxed);
      new_count_and_mean.f.mean.store(new_mean, std::memory_order_relaxed);

      count_and_mean_dword actual_old_count_and_mean;
      UNODB_DETAIL_DISABLE_CLANG_WARNING("-Watomic-implicit-seq-cst")
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      actual_old_count_and_mean.dword = __sync_val_compare_and_swap(
          &count_and_mean.dword, old_count_and_mean.dword,
          new_count_and_mean.dword);
      UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

      const auto actual_old_count =
          actual_old_count_and_mean.f.count.load(std::memory_order_relaxed);

      if (UNODB_DETAIL_LIKELY(actual_old_count == seen_count)) {
        break;
      }

      seen_count = actual_old_count;
      seen_mean =
          actual_old_count_and_mean.f.mean.load(std::memory_order_relaxed);
    }

    const auto msq_delta = delta * (double_value - new_count_and_mean.f.mean);
    auto current_msq = msq.load(std::memory_order_acquire);
    while (true) {
      const auto new_msq = current_msq + msq_delta;
      if (UNODB_DETAIL_LIKELY(msq.compare_exchange_weak(
              current_msq, new_msq, std::memory_order_acq_rel,
              std::memory_order_acquire)))
        break;
    }

    auto current_max = max.load(std::memory_order_acquire);
    while (true) {
      if (value <= current_max) break;
      if (UNODB_DETAIL_LIKELY(max.compare_exchange_weak(
              current_max, value, std::memory_order_acq_rel,
              std::memory_order_acquire)))
        break;
    }
  }

  void reset() noexcept {
    max.store(0, std::memory_order_relaxed);
    msq.store(0.0, std::memory_order_relaxed);
    // The parent reset will fence the above stores
    lock_free_stats::reset();
  }

  [[nodiscard]] std::uint64_t get_max() const noexcept {
    return max.load(std::memory_order_acquire);
  }
  [[nodiscard]] double get_variance() const noexcept {
    const auto count = get_count();
    // Replace NaN with 0
    if (UNODB_DETAIL_UNLIKELY(count == 1)) return 0.0;
    return msq.load(std::memory_order_acquire) /
           (static_cast<double>(count) - 1);
  }

 private:
  std::atomic<std::uint64_t> max{0};
  std::atomic<double> msq{0.0};
};

}  // namespace unodb

#endif  // UNODB_DETAIL_CLOK_FREE_STATS_HPP
