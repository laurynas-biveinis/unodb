// Copyright (C) 2019-2023 Laurynas Biveinis
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
#include <tuple>
#include <type_traits>

#ifdef UNODB_DETAIL_X86_64
#include <emmintrin.h>
#endif

#include "assert.hpp"

namespace unodb {

// LCOV_EXCL_START
inline void spin_wait_loop_body() noexcept {
#ifdef UNODB_DETAIL_THREAD_SANITIZER

  // std::this_thread::yield();

#else  // UNODB_DETAIL_THREAD_SANITIZER

#if UNODB_SPINLOCK_LOOP_VALUE == UNODB_DETAIL_SPINLOCK_LOOP_PAUSE

#if defined(UNODB_DETAIL_X86_64)
  _mm_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield\n");
#else
#error Needs porting
#endif

#elif UNODB_SPINLOCK_LOOP_VALUE == UNODB_DETAIL_SPINLOCK_LOOP_EMPTY

  // Empty

#else  // UNODB_SPINLOCK_LOOP_VALUE

#error Unknown SPINLOCK_LOOP value in CMake

#endif  // UNODB_SPINLOCK_LOOP_VALUE

#endif  // UNODB_DETAIL_THREAD_SANITIZER
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
  class [[nodiscard]] version_type final {
   public:
    explicit constexpr version_type(std::uint64_t version_val) noexcept
        : version{version_val} {}

    [[nodiscard, gnu::const]] constexpr bool is_write_locked() const noexcept {
      return (version & 2U) != 0U;
    }

    [[nodiscard, gnu::const]] constexpr bool is_free() const noexcept {
      return (version & 3U) == 0U;
    }

    // Force inline because LLVM 14-17 and possibly later versions generate a
    // call to outline version from optimistic_lock::try_lock in release build
    // with UBSan. That same method is apparently miscompiled in that its loop
    // only checks whether the lock is free but never if it's obsolete,
    // resulting in hangs. Forcing to inline seems to make that issue to go away
    // too.
    [[nodiscard, gnu::const]] UNODB_DETAIL_FORCE_INLINE constexpr bool
    is_obsolete() const noexcept {
      return (version & 1U) != 0U;
    }

    [[nodiscard, gnu::const]] constexpr version_type set_locked_bit()
        const noexcept {
      UNODB_DETAIL_ASSERT(is_free());
      return version_type{version + 2};
    }

    [[nodiscard]] constexpr std::uint64_t get() const noexcept {
      return version;
    }

    [[nodiscard]] constexpr bool operator==(version_type other) const noexcept {
      return version == other.version;
    }

    [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
      os << "version = 0x" << std::hex << std::setfill('0') << std::setw(8)
         << version << std::dec;
      if (is_write_locked()) os << " (write locked)";
      if (is_obsolete()) os << " (obsoleted)";
    }

   private:
    std::uint64_t version{0};
  };

  class [[nodiscard]] atomic_version_type final {
   public:
    [[nodiscard]] version_type load() const noexcept {
      return version_type{version.load(std::memory_order_acquire)};
    }

    [[nodiscard]] version_type load_relaxed() const noexcept {
      return version_type{version.load(std::memory_order_relaxed)};
    }

    [[nodiscard]] bool cas(version_type expected,
                           version_type new_val) noexcept {
      auto expected_val = expected.get();
      return UNODB_DETAIL_LIKELY(version.compare_exchange_strong(
          expected_val, new_val.get(), std::memory_order_acquire,
          std::memory_order_relaxed));
    }

    void write_unlock() noexcept {
      UNODB_DETAIL_ASSERT(load().is_write_locked());

      version.fetch_add(2, std::memory_order_release);
    }

    void write_unlock_and_obsolete() noexcept {
      UNODB_DETAIL_ASSERT(load().is_write_locked());

      version.fetch_add(3, std::memory_order_release);
#ifndef NDEBUG
      const auto current_version{load()};
      UNODB_DETAIL_ASSERT(!current_version.is_write_locked());
      UNODB_DETAIL_ASSERT(current_version.is_obsolete());
#endif
    }

   private:
    std::atomic<std::uint64_t> version;

    static_assert(decltype(version)::is_always_lock_free,
                  "Must use always lock-free atomics");
  };

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

    [[nodiscard, gnu::flatten]] UNODB_DETAIL_FORCE_INLINE bool try_read_unlock()
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
    ~read_critical_section() noexcept {
#ifndef NDEBUG
      if (lock != nullptr) std::ignore = lock->try_read_unlock(version);
#endif
    }

    read_critical_section(const read_critical_section &) = delete;
    read_critical_section(read_critical_section &&) = delete;
    read_critical_section &operator=(const read_critical_section &) = delete;

   private:
    optimistic_lock *lock{nullptr};
    version_type version{0};

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

    ~write_guard() noexcept {
      if (lock == nullptr) return;
      lock->write_unlock();
    }

    [[nodiscard]] bool must_restart() const noexcept {
      return UNODB_DETAIL_UNLIKELY(lock == nullptr);
    }

    void unlock_and_obsolete() noexcept {
      lock->write_unlock_and_obsolete();
      lock = nullptr;
    }

    void unlock() noexcept {
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

  ~optimistic_lock() noexcept = default;

  [[nodiscard]] read_critical_section try_read_lock() noexcept {
    while (true) {
      const auto current_version = version.load();
      if (UNODB_DETAIL_LIKELY(current_version.is_free())) {
        inc_read_lock_count();
        return read_critical_section{*this, current_version};
      }
      // LCOV_EXCL_START
      if (UNODB_DETAIL_UNLIKELY(current_version.is_obsolete()))
        return read_critical_section{};
      UNODB_DETAIL_ASSERT(current_version.is_write_locked());
      spin_wait_loop_body();
      // LCOV_EXCL_STOP
    }
  }

#ifndef NDEBUG
  void check_on_dealloc() const noexcept {
    UNODB_DETAIL_ASSERT(read_lock_count.load(std::memory_order_acquire) == 0);
  }

  [[nodiscard]] bool is_obsoleted_by_this_thread() const noexcept {
    return version.load().is_obsolete() &&
           std::this_thread::get_id() == obsoleter_thread;
  }

  [[nodiscard]] bool is_write_locked() const noexcept {
    return version.load().is_write_locked();
  }
#endif

  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    const auto dump_version = version.load();
    os << "lock: ";
    dump_version.dump(os);
#ifndef NDEBUG
    os << " current read lock count = "
       << read_lock_count.load(std::memory_order_acquire);
#endif
  }

 private:
  [[nodiscard]] bool check(version_type locked_version) const noexcept {
    UNODB_DETAIL_ASSERT(read_lock_count.load(std::memory_order_acquire) > 0);

#ifndef UNODB_DETAIL_THREAD_SANITIZER
    std::atomic_thread_fence(std::memory_order_acquire);
#endif
    const auto result{locked_version == version.load_relaxed()};
#ifndef NDEBUG
    if (UNODB_DETAIL_UNLIKELY(!result)) dec_read_lock_count();
#endif
    return UNODB_DETAIL_LIKELY(result);
  }

  [[nodiscard, gnu::flatten]] UNODB_DETAIL_FORCE_INLINE bool try_read_unlock(
      version_type locked_version) const noexcept {
    const auto result{check(locked_version)};
#ifndef NDEBUG
    if (UNODB_DETAIL_LIKELY(result)) dec_read_lock_count();
#endif
    return UNODB_DETAIL_LIKELY(result);
  }

  [[nodiscard]] bool try_upgrade_to_write_lock(
      version_type locked_version) noexcept {
    const auto result{
        version.cas(locked_version, locked_version.set_locked_bit())};
    dec_read_lock_count();
    return UNODB_DETAIL_LIKELY(result);
  }

  void write_unlock() noexcept { version.write_unlock(); }

  void write_unlock_and_obsolete() noexcept {
    version.write_unlock_and_obsolete();
#ifndef NDEBUG
    obsoleter_thread = std::this_thread::get_id();
#endif
  }

  atomic_version_type version{};

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

#define UNODB_DETAIL_ASSERT_INACTIVE(guard)   \
  do {                                        \
    UNODB_DETAIL_DISABLE_MSVC_WARNING(26800); \
    UNODB_DETAIL_ASSERT(!(guard).active());   \
    UNODB_DETAIL_RESTORE_MSVC_WARNINGS();     \
  } while (0)

#ifdef NDEBUG
static_assert(sizeof(optimistic_lock) == 8);
#else
static_assert(sizeof(optimistic_lock) == 24);
#endif

template <typename T>
class [[nodiscard]] in_critical_section final {
 public:
  constexpr in_critical_section() noexcept = default;

  // cppcheck-suppress noExplicitConstructor
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

  void operator--() noexcept { store(static_cast<T>(load() - 1)); }

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
