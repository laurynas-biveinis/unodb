// Copyright (C) 2019-2021 Laurynas Biveinis
#ifndef OPTIMISTIC_LOCK_HPP_
#define OPTIMISTIC_LOCK_HPP_

#include "global.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#ifdef __x86_64
#include <emmintrin.h>
#endif
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>
#include <type_traits>

namespace unodb {

// Optimistic lock as described in V. Leis, F. Schneiber, A. Kemper and T.
// Neumann, "The ART of Practical Synchronization," 2016 Proceedings of the 12th
// International Workshop on Data Management on New Hardware(DaMoN), pages
// 3:1--3:8, 2016. They also seem to be very similar to Linux kernel sequential
// locks, with the addition of the obsolete state. Memory ordering is
// implementing following Boehm's 2012 paper "Can seqlocks get along with
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
class optimistic_lock final {
 public:
  class version_type final {
   public:
    using version_number_type = std::uint64_t;

    constexpr version_type(version_number_type version_,
                           USED_IN_DEBUG const optimistic_lock *owner_) noexcept
        : version {
      version_
    }
#ifndef NDEBUG
    , owner { owner_ }
#endif
    {}

    constexpr version_type(const version_type &other) noexcept : version {
      other.version
    }
#ifndef NDEBUG
    , owner { other.owner }
#endif
    {}

    constexpr version_type &operator=(version_type other) noexcept {
      version = other.version;
#ifndef NDEBUG
      owner = other.owner;
#endif
      return *this;
    }

    [[nodiscard]] constexpr operator version_number_type() const noexcept {
      return version;
    }

    [[nodiscard]] constexpr operator version_number_type &() noexcept {
      return version;
    }

    constexpr void assert_owner(
        USED_IN_DEBUG const optimistic_lock *owner_) const noexcept {
#ifndef NDEBUG
      assert(owner_ == owner);
#endif
    }

   private:
    version_number_type version;
#ifndef NDEBUG
    const optimistic_lock *owner;
#endif
  };

#ifdef NDEBUG
  static_assert(
      sizeof(version_type) == sizeof(std::uint64_t),
      "class version_type must have no overhead over std::uint64_t in "
      "release build ");
  static_assert(sizeof(version_type) == 8);
#else
  static_assert(sizeof(version_type) == 16);
#endif

  optimistic_lock() noexcept = default;

  optimistic_lock(const optimistic_lock &) = delete;
  optimistic_lock(optimistic_lock &&) = delete;

  optimistic_lock &operator=(const optimistic_lock &) = delete;
  optimistic_lock &operator=(optimistic_lock &&) = delete;

  // If QSBR starts running destructors for objects being deallocated, add one
  // here that asserts read_lock_count == 0
  ~optimistic_lock() = default;

  [[nodiscard]] std::optional<version_type> try_read_lock() const noexcept {
    const auto current_version = await_node_unlocked();
    if (unlikely(is_obsolete(current_version))) return {};
    inc_read_lock_count();
    return std::optional<version_type>{std::in_place, current_version, this};
  }

  [[nodiscard]] bool check(const version_type locked_version) const noexcept {
    assert(read_lock_count.load(std::memory_order_relaxed) > 0);
    locked_version.assert_owner(this);

    std::atomic_thread_fence(std::memory_order_acquire);
    return likely(locked_version == version.load());
  }

  [[nodiscard]] bool try_read_unlock(
      const version_type locked_version) const noexcept {
    const auto result{check(locked_version)};
    dec_read_lock_count();
    return result;
  }

  [[nodiscard]] bool try_upgrade_to_write_lock(
      version_type locked_version) noexcept {
    locked_version.assert_owner(this);

    const auto result{likely(version.compare_exchange_strong(
        locked_version, set_locked_bit(locked_version),
        std::memory_order_acquire))};
    dec_read_lock_count();
    return result;
  }

  void write_unlock() noexcept {
    assert(is_write_locked());

    version.fetch_add(2, std::memory_order_release);
  }

  void write_unlock_and_obsolete() noexcept {
    assert(is_write_locked());

    version.fetch_add(3, std::memory_order_release);
#ifndef NDEBUG
    obsoleter_thread = std::this_thread::get_id();
#endif
  }

#ifndef NDEBUG
  [[nodiscard]] bool is_obsoleted_by_this_thread() const noexcept {
    return is_obsolete(version.load(std::memory_order_acquire)) &&
           std::this_thread::get_id() == obsoleter_thread;
  }

  [[nodiscard]] bool is_write_locked() const noexcept {
    return is_write_locked(version.load(std::memory_order_acquire));
  }
#endif

  __attribute__((cold, noinline)) void dump(std::ostream &os) const {
    const auto dump_version = version.load();
    os << "lock: version = 0x" << std::hex << std::setfill('0') << std::setw(8)
       << dump_version << std::dec;
    if (is_write_locked(dump_version)) os << " (write locked)";
    if (is_obsolete(dump_version)) os << " (obsoleted)";
#ifndef NDEBUG
    os << " current read lock count = " << read_lock_count;
#endif
  }

 private:
  [[nodiscard]] static constexpr bool is_write_locked(
      version_type::version_number_type version) noexcept {
    return version & 2;
  }

  [[nodiscard]] version_type::version_number_type await_node_unlocked()
      const noexcept {
    version_type::version_number_type current_version =
        version.load(std::memory_order_acquire);
    while (is_write_locked(current_version)) {
#ifdef UNODB_THREAD_SANITIZER
      std::this_thread::yield();
#elif defined(__x86_64)
      // Dear reader, please don't make fun of this just yet
      _mm_pause();
#else
#error Needs porting
#endif
      current_version = version.load(std::memory_order_acquire);
    }
    return current_version;
  }

  [[nodiscard]] static constexpr version_type::version_number_type
  set_locked_bit(const version_type version) noexcept {
    assert(!is_write_locked(version));
    return version + 2;
  }

  [[nodiscard]] static constexpr bool is_obsolete(
      version_type::version_number_type version) noexcept {
    return version & 1;
  }

  std::atomic<version_type::version_number_type> version{0};

  static_assert(decltype(version)::is_always_lock_free,
                "Must use always lock-free atomics");

#ifndef NDEBUG
  mutable std::atomic<std::int64_t> read_lock_count{0};
  std::thread::id obsoleter_thread{};
#endif

  void inc_read_lock_count() const noexcept {
#ifndef NDEBUG
    read_lock_count.fetch_add(1, std::memory_order_relaxed);
#endif
  }

  void dec_read_lock_count() const noexcept {
#ifndef NDEBUG
    const auto old_value =
        read_lock_count.fetch_sub(1, std::memory_order_relaxed);
    assert(old_value > 0);
#endif
  }
};

static_assert(std::is_standard_layout_v<optimistic_lock>);

#ifdef NDEBUG
static_assert(sizeof(optimistic_lock) == sizeof(optimistic_lock::version_type));
static_assert(sizeof(optimistic_lock) == 8);
#else
static_assert(sizeof(optimistic_lock) == 24);
#endif

class optimistic_write_lock_guard final {
 public:
  explicit constexpr optimistic_write_lock_guard(optimistic_lock &lock_)
      : lock{lock_} {
    assert(lock.is_write_locked());
  }

  ~optimistic_write_lock_guard() { lock.write_unlock(); }

 private:
  optimistic_lock &lock;

  optimistic_write_lock_guard() = delete;
  optimistic_write_lock_guard(const optimistic_write_lock_guard &) = delete;
  optimistic_write_lock_guard(optimistic_write_lock_guard &&) = delete;
  optimistic_write_lock_guard &operator=(const optimistic_write_lock_guard &) =
      delete;
  optimistic_write_lock_guard &operator=(optimistic_write_lock_guard &&) =
      delete;
};

class unique_write_lock_obsoleting_guard final {
 public:
  explicit unique_write_lock_obsoleting_guard(optimistic_lock &lock_) noexcept
      : lock{&lock_}, exceptions_at_ctor{std::uncaught_exceptions()} {
    assert(lock->is_write_locked());
  }

  unique_write_lock_obsoleting_guard(
      unique_write_lock_obsoleting_guard &&other) noexcept
      : lock{other.lock}, exceptions_at_ctor{other.exceptions_at_ctor} {
    other.lock = nullptr;
  }

  ~unique_write_lock_obsoleting_guard() {
    if (lock == nullptr) return;
    // FIXME(laurynas): remove uncaught_exceptions() calls and require commit()
    // to be called in the success paths (which is what the user code has to do
    // anyway).
    if (likely(exceptions_at_ctor == std::uncaught_exceptions()))
      lock->write_unlock_and_obsolete();
    else
      lock->write_unlock();
  }

  void commit() {
    assert(active());

    lock->write_unlock_and_obsolete();
    lock = nullptr;
  }

  void abort() {
    assert(active());

    lock->write_unlock();
    lock = nullptr;
  }

#ifndef NDEBUG
  bool active() const noexcept { return lock != nullptr; }

  bool guards(const optimistic_lock &lock_) const noexcept {
    return lock == &lock_;
  }
#endif

 private:
  optimistic_lock *lock;
  const int exceptions_at_ctor;

  unique_write_lock_obsoleting_guard() = delete;
  unique_write_lock_obsoleting_guard(
      const unique_write_lock_obsoleting_guard &) = delete;
  unique_write_lock_obsoleting_guard &operator=(
      const unique_write_lock_obsoleting_guard &) = delete;
  unique_write_lock_obsoleting_guard &operator=(
      unique_write_lock_obsoleting_guard &&) = delete;
};

template <typename T>
class critical_section_protected final {
 public:
  constexpr critical_section_protected() noexcept = default;

  explicit constexpr critical_section_protected(T value_) noexcept
      : value{value_} {}

  // Regular C++ assignment operators return ref to this, std::atomic returns
  // the assigned value, we return nothing as we never chain assignments.
  void operator=(T new_value) noexcept { store(new_value); }

  void operator=(const critical_section_protected<T> &new_value) noexcept {
    store(new_value.load());
  }

  void operator++() noexcept { store(load() + 1); }

  void operator--() noexcept { store(load() - 1); }

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

  operator T() const noexcept { return load(); }

  T load() const noexcept { return value.load(std::memory_order_relaxed); }

  void store(T new_value) noexcept {
    value.store(new_value, std::memory_order_relaxed);
  }

 private:
  std::atomic<T> value;

  static_assert(std::atomic<T>::is_always_lock_free,
                "Must use always lock-free atomics");
};

}  // namespace unodb

#endif  // OPTIMISTIC_LOCK_HPP_
