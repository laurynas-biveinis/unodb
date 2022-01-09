// Copyright (C) 2019-2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_OPTIMISTIC_LOCK_HPP
#define UNODB_DETAIL_OPTIMISTIC_LOCK_HPP

#include "global.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>
#include <type_traits>

#ifdef UNODB_DETAIL_X86_64
#include <emmintrin.h>
#endif

#include "assert.hpp"

namespace unodb {

// LCOV_EXCL_START
inline void spin_wait_loop_body() noexcept {
#ifdef UNODB_DETAIL_THREAD_SANITIZER
  std::this_thread::yield();
#elif defined(UNODB_DETAIL_X86_64)
  // Dear reader, please don't make fun of this just yet
  _mm_pause();
#else
#error Needs porting
#endif
}
// LCOV_EXCL_STOP

// Optimistic lock as described in V. Leis, F. Schneiber, A. Kemper and T.
// Neumann, "The ART of Practical Synchronization," 2016 Proceedings of the 12th
// International Workshop on Data Management on New Hardware(DaMoN), pages
// 3:1--3:8, 2016. They also seem to be very similar to Linux kernel sequential
// locks, with the addition of the obsolete state. Memory ordering is
// implemented following Boehm's 2012 paper "Can seqlocks get along with
// programming language memory models?"

// A lock is a single machine word, which encodes locked-unlocked state,
// obsolete state, and version number. Locking for write atomically sets the
// locked state and bumps the version number. Locking for read saves the version
// number at the time, and "unlocking" for read checks whether the lock version
// did not advance during the reader's critical section, which has the
// constraint that no pointers may be followed while in there nor any other data
// can be interpreted in a way that may potentially cause faults. Effectively
// this means that reader critical section should copy the data it's interested
// in, and, after unlock (or version check if further actions are needed in a
// longer reader critical section), the data might be used only if the version
// number has not advanced. Otherwise an algorithm restart is necessary. In the
// current implementation, it is possible for a reader to be starved
// indefinitely.

// A lock in obsolete state marks data which is on the deallocation backlog to
// be freed once all the thread epochs have advanced. All algorithms must
// restart upon encountering a lock in obsolete state.

// All bool-returning try_ functions return true on success and false on
// lock version change, which indicates the need to restart
class [[nodiscard]] optimistic_lock final {
 private:
  using version_type = std::uint64_t;

 public:
  class write_guard;

  class [[nodiscard]] read_critical_section final {
   public:
    read_critical_section() noexcept = default;

    read_critical_section(optimistic_lock &lock_,
                          version_type version_) noexcept
        : lock{&lock_}, version{version_} {}

    read_critical_section &operator=(read_critical_section &&other) noexcept {
      lock = other.lock;
      // The current implementation does not need lock == nullptr in the
      // destructor, thus only reset other.lock in debug builds
#ifndef NDEBUG
      other.lock = nullptr;
#endif
      version = other.version;

      return *this;
    }

    [[nodiscard, gnu::always_inline, gnu::flatten]] bool try_read_unlock()
        UNODB_DETAIL_RELEASE_CONST noexcept {
      const auto result = lock->try_read_unlock(version);
#ifndef NDEBUG
      lock = nullptr;
#endif
      return UNODB_DETAIL_LIKELY(result);
    }

    [[nodiscard]] bool check() UNODB_DETAIL_RELEASE_CONST noexcept {
      const auto result = lock->check(version);
#ifndef NDEBUG
      if (UNODB_DETAIL_UNLIKELY(!result)) lock = nullptr;  // LCOV_EXCL_LINE
#endif
      return UNODB_DETAIL_LIKELY(result);
    }

    [[nodiscard]] bool must_restart() const noexcept {
      return UNODB_DETAIL_UNLIKELY(lock == nullptr);
    }

    // If the destructor ever starts doing something in the release build, reset
    // moved-from lock fields in the move and write_guard constructors.
    ~read_critical_section() {
#ifndef NDEBUG
      if (lock != nullptr) (void)lock->try_read_unlock(version);
#endif
    }

    read_critical_section(const read_critical_section &) = delete;
    read_critical_section(read_critical_section &&) = delete;
    read_critical_section &operator=(const read_critical_section &) = delete;

   private:
    optimistic_lock *lock{nullptr};
    version_type version{};

    friend class write_guard;
  };

  class [[nodiscard]] write_guard final {
   public:
    explicit write_guard(read_critical_section &&critical_section) noexcept
        : lock{critical_section.lock} {
#ifndef NDEBUG
      critical_section.lock = nullptr;
#endif
      const auto result =
          lock->try_upgrade_to_write_lock(critical_section.version);
      if (UNODB_DETAIL_UNLIKELY(!result)) lock = nullptr;  // LCOV_EXCL_LINE
    }

    ~write_guard() {
      if (lock == nullptr) return;
      lock->write_unlock();
    }

    [[nodiscard]] bool must_restart() const noexcept {
      return UNODB_DETAIL_UNLIKELY(lock == nullptr);
    }

    void unlock_and_obsolete() {
      lock->write_unlock_and_obsolete();
      lock = nullptr;
    }

    void unlock() {
      lock->write_unlock();
      lock = nullptr;
    }

#ifndef NDEBUG
    [[nodiscard]] bool active() const noexcept { return lock != nullptr; }

    [[nodiscard]] bool guards(const optimistic_lock &lock_) const noexcept {
      return lock == &lock_;
    }
#endif

    write_guard(const write_guard &) = delete;
    write_guard(write_guard &&) = delete;
    write_guard &operator=(const write_guard &) = delete;
    write_guard &operator=(write_guard &&) = delete;

   private:
    optimistic_lock *lock{nullptr};
  };

  optimistic_lock() noexcept = default;

  optimistic_lock(const optimistic_lock &) = delete;
  optimistic_lock(optimistic_lock &&) = delete;
  optimistic_lock &operator=(const optimistic_lock &) = delete;
  optimistic_lock &operator=(optimistic_lock &&) = delete;

  ~optimistic_lock() = default;

  [[nodiscard]] read_critical_section try_read_lock() noexcept {
    while (true) {
      const auto current_version = version.load(std::memory_order_acquire);
      if (UNODB_DETAIL_LIKELY(is_free(current_version))) {
        inc_read_lock_count();
        return read_critical_section{*this, current_version};
      }
      // LCOV_EXCL_START
      if (UNODB_DETAIL_UNLIKELY(is_obsolete(current_version)))
        return read_critical_section{};
      UNODB_DETAIL_ASSERT(is_write_locked(current_version));
      spin_wait_loop_body();
      // LCOV_EXCL_STOP
    }
  }

#ifndef NDEBUG
  void check_on_dealloc() const noexcept {
    UNODB_DETAIL_ASSERT(read_lock_count.load(std::memory_order_acquire) == 0);
  }

  [[nodiscard]] bool is_obsoleted_by_this_thread() const noexcept {
    return is_obsolete(version.load(std::memory_order_acquire)) &&
           std::this_thread::get_id() == obsoleter_thread;
  }

  [[nodiscard]] bool is_write_locked() const noexcept {
    return is_write_locked(version.load(std::memory_order_acquire));
  }
#endif

  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    const auto dump_version = version.load(std::memory_order_acquire);
    os << "lock: version = 0x" << std::hex << std::setfill('0') << std::setw(8)
       << dump_version << std::dec;
    if (is_write_locked(dump_version)) os << " (write locked)";
    if (is_obsolete(dump_version)) os << " (obsoleted)";
#ifndef NDEBUG
    os << " current read lock count = "
       << read_lock_count.load(std::memory_order_acquire);
#endif
  }

 private:
  [[nodiscard]] bool check(version_type locked_version) const noexcept {
    UNODB_DETAIL_ASSERT(read_lock_count.load(std::memory_order_acquire) > 0);

    std::atomic_thread_fence(std::memory_order_acquire);
    const auto result{locked_version ==
                      version.load(std::memory_order_relaxed)};
#ifndef NDEBUG
    if (UNODB_DETAIL_UNLIKELY(!result)) dec_read_lock_count();
#endif
    return UNODB_DETAIL_LIKELY(result);
  }

  [[nodiscard, gnu::always_inline, gnu::flatten]] bool try_read_unlock(
      version_type locked_version) const noexcept {
    const auto result{check(locked_version)};
#ifndef NDEBUG
    if (UNODB_DETAIL_LIKELY(result)) dec_read_lock_count();
#endif
    return UNODB_DETAIL_LIKELY(result);
  }

  [[nodiscard]] bool try_upgrade_to_write_lock(
      version_type locked_version) noexcept {
    const auto result{version.compare_exchange_strong(
        locked_version, set_locked_bit(locked_version),
        std::memory_order_acquire)};
    dec_read_lock_count();
    return UNODB_DETAIL_LIKELY(result);
  }

  void write_unlock() noexcept {
    UNODB_DETAIL_ASSERT(is_write_locked());

    version.fetch_add(2, std::memory_order_release);
  }

  void write_unlock_and_obsolete() noexcept {
    UNODB_DETAIL_ASSERT(is_write_locked());

    version.fetch_add(3, std::memory_order_release);
#ifndef NDEBUG
    obsoleter_thread = std::this_thread::get_id();

    const auto current_version{version.load(std::memory_order_acquire)};
    UNODB_DETAIL_ASSERT(!is_write_locked(current_version));
    UNODB_DETAIL_ASSERT(is_obsolete(current_version));
#endif
  }

  [[nodiscard, gnu::const]] static constexpr bool is_write_locked(
      version_type version) noexcept {
    return (version & 2U) != 0U;
  }

  [[nodiscard, gnu::const]] static constexpr bool is_free(
      version_type version) noexcept {
    return (version & 3U) == 0U;
  }

  [[nodiscard, gnu::const]] static constexpr version_type set_locked_bit(
      version_type version) noexcept {
    UNODB_DETAIL_ASSERT(is_free(version));
    return version + 2;
  }

  [[nodiscard, gnu::const]] static constexpr bool is_obsolete(
      version_type version) noexcept {
    return (version & 1U) != 0U;
  }

  std::atomic<version_type> version{0};

  static_assert(decltype(version)::is_always_lock_free,
                "Must use always lock-free atomics");

#ifndef NDEBUG
  mutable std::atomic<std::int64_t> read_lock_count{0};
  std::thread::id obsoleter_thread{};
#endif

  void inc_read_lock_count() const noexcept {
#ifndef NDEBUG
    read_lock_count.fetch_add(1, std::memory_order_release);
#endif
  }

  void dec_read_lock_count() const noexcept {
#ifndef NDEBUG
    const auto old_value =
        read_lock_count.fetch_sub(1, std::memory_order_release);
    UNODB_DETAIL_ASSERT(old_value > 0);
#endif
  }
};

static_assert(std::is_standard_layout_v<optimistic_lock>);

#ifdef NDEBUG
static_assert(sizeof(optimistic_lock) == 8);
#else
static_assert(sizeof(optimistic_lock) == 24);
#endif

template <typename T>
class [[nodiscard]] in_critical_section final {
 public:
  constexpr in_critical_section() noexcept = default;

  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  constexpr in_critical_section(T value_) noexcept : value{value_} {}

  in_critical_section(const in_critical_section<T> &) = delete;
  in_critical_section(in_critical_section<T> &&) = delete;

  ~in_critical_section() noexcept = default;

  in_critical_section<T> &operator=(T new_value) noexcept {
    store(new_value);
    return *this;
  }

  // NOLINTNEXTLINE(cert-oop54-cpp)
  in_critical_section<T> &operator=(
      const in_critical_section<T> &new_value) noexcept {
    store(new_value.load());
    return *this;
  }

  void operator=(in_critical_section<T> &&) = delete;

  void operator++() noexcept { store(load() + 1); }

  void operator--() noexcept { store(load() - 1); }

  // NOLINTNEXTLINE(cert-dcl21-cpp)
  T operator--(int) noexcept {
    const auto result = load();
    store(result - 1);
    return result;
  }

  template <typename T_ = T,
            typename = std::enable_if_t<!std::is_integral_v<T_>>>
  [[nodiscard]] auto operator==(std::nullptr_t) const noexcept {
    return load() == nullptr;
  }

  template <typename T_ = T,
            typename = std::enable_if_t<!std::is_integral_v<T_>>>
  [[nodiscard]] auto operator!=(std::nullptr_t) const noexcept {
    return load() != nullptr;
  }

  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  operator T() const noexcept { return load(); }

  [[nodiscard]] T load() const noexcept {
    return value.load(std::memory_order_relaxed);
  }

  void store(T new_value) noexcept {
    value.store(new_value, std::memory_order_relaxed);
  }

 private:
  std::atomic<T> value;

  static_assert(std::atomic<T>::is_always_lock_free,
                "Must use always lock-free atomics");
};

}  // namespace unodb

#endif  // UNODB_DETAIL_OPTIMISTIC_LOCK_HPP
