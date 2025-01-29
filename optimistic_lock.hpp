// Copyright (C) 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_OPTIMISTIC_LOCK_HPP
#define UNODB_DETAIL_OPTIMISTIC_LOCK_HPP

/// \file
/// The optimistic lock

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

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

/// The optimistic spinlock wait loop algorithm implementation.
/// The implementation is selected by #UNODB_DETAIL_SPINLOCK_LOOP_VALUE, set by
/// CMake, and can be either #UNODB_DETAIL_SPINLOCK_LOOP_PAUSE or
/// #UNODB_DETAIL_SPINLOCK_LOOP_EMPTY
// TODO(laurynas): move to unodb::detail namespace
// LCOV_EXCL_START
inline void spin_wait_loop_body() noexcept {
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
}
// LCOV_EXCL_STOP

/// The underlying integer type used to store optimistic lock word, including
/// its version and lock state information.
//
// TODO(laurynas) can we use optimistic_lock::version_type instead?
using version_tag_type = std::uint64_t;

/// A version-based optimistic lock that supports single-writer/multiple-readers
/// concurrency without shared memory writes during read operations.
///
/// Writers bump the version counter and readers detect concurrent writes
/// by comparing the version counter before and after the reads.
///
/// ## Examples
///
/// The simplest read locking example:
/// \code{.cpp}
/// // Spin until lock is not write-locked nor obsolete
/// auto foo_read_critical_section = lock.try_read_lock();
/// if (foo_read_critical_section.must_restart()) {
///   // Obsolete, restart
///   return false;
/// }
/// // Read
/// const auto read_foo = foo.data;
/// // Try unlock
/// if (!foo_read_critical_section.try_read_unlock()) {
///    // The lock was write-locked while we were accessing data. Do not act on
///    // the read data, restart.
///    return false;
/// }
/// // Act on read_foo and return success
/// // ...
/// return true;
/// \endcode
///
/// An example of read locking with interim checks:
/// \code{.cpp}
/// auto foo_rcs = lock.try_read_lock();
/// if (foo_rcs.must_restart()) return false;
/// const auto read_foo_1 = foo.data_1;
/// // Check whether read_foo_1 was read consistently but do not end the read
/// // critical section
/// if (!foo_rcs.check()) {
///   // The check failed because the lock was write-locked while we were
///   // accessing data. Do not act on it, restart.
///   return false;
/// }
/// // Act on read_foo_1
/// // ...
/// const auto read_foo_2 = foo.data_2;
/// if (!foo_rcs.try_read_unlock()) return false;
/// // Both read_foo_1 and read_foo_2 were read consistently together, act on
/// // them.
/// // ...
/// return true;
/// \endcode
///
/// An example of write locking:
/// \code{.cpp}
/// // Write lock critical sections always start out as read lock ones.
/// auto foo_rcs = lock.try_read_lock();
/// if (foo_rcs.must_restart()) return false;
/// // Read current data state if needed
/// // ...
/// // Try upgrading the lock
/// const auto foo_write_guard =
///   optimistic_lock::write_guard{std::move(foo_rcs)};
/// if (foo_write_guard.must_restart()) {
///   // The lock upgrade failed because somebody else write-locked it
///   // first. Restart, also don't act on the read data in the read critical
///   // section since the last check.
///   return false;
/// }
/// // We have the exclusive write lock, freely write the data. The lock will be
/// // released on scope exit.
/// // ...
/// return true;
/// \endcode
///
/// An example of write locking that ends with data deletion:
/// \code{.cpp}
/// auto foo_rcs = lock.try_read_lock();
/// if (foo_rcs.must_restart()) return false;
/// auto foo_wg = optimistic_lock::write_guard{std::move(foo_rcs)};
/// if (foo_wg.must_restart()) return false;
/// // Act on write-locked data before marking it for deletion
/// // ...
/// foo_wg.unlock_and_obsolete();
/// // Mark data to be reclaimed when it is safe to do so
/// // ...
/// \endcode
///
/// ## API conventions
///
/// All `bool`-returning `try_` methods return true on success and false when
/// a concurrent write lock requires the operation to be restarted.
///
/// ## Read protocol
///
/// A read critical section (RCS) is created by
/// unodb::optimistic_lock::try_read_lock(), which will either spin until the
/// lock is not write-locked, or will return immediately if the lock goes to the
/// obsolete state.
///
/// The obsolete state must be checked for by calling
/// unodb::optimistic_lock::read_critical_section::must_restart() immediately
/// after creating the RCS.
///
/// No pointers may be dereferenced in an RCS before a successful read unlock
/// (unodb::optimistic_lock::try_read_unlock()) or an interim check
/// (unodb::optimistic_lock::check()) call. Similarly, no non-pointer data may
/// be accessed in any fault-causing way if it's illegal.
///
/// To follow the above rules, first copy the data of interest, then verify
/// consistency via unlock or version check call. Only use the copied data if
/// these operations succeeded. Otherwise an algorithm restart is necessary.
///
/// In the current implementation, it is possible for a reader to be starved
/// indefinitely.
///
/// ## Write protocol
///
/// After a successful write lock acquisition by
/// unodb::optimistic_lock::write_guard(), the protected data may be accessed
/// freely, as if under a regular write lock, with the exception of data
/// deletion, discussed below. The write lock object is a C++ scope guard which
/// will unlock on leaving the scope.
///
/// Since read locking does not write to the shared memory, readers can have
/// active pointers to the data without the writer knowing about them.
/// Therefore, lock-protected heap data cannot be deallocated immediately.
/// Instead of immediate deallocation, the data is marked as obsolete
/// (unodb::optimistic_lock::write_guard::write_unlock_and_obsolete) and
/// reclaimed later when it is safe to do so. This is implemented by \ref qsbr.
///
/// ## Internals
///
/// A lock is a single machine word, that encodes locked-unlocked state,
/// obsolete state, and version number.
///
/// Locking for write (unodb::optimistic_lock::write_guard())
/// atomically sets the locked state and bumps the version number.
///
/// Locking for read (unodb::optimistic_lock::try_read_lock()) saves the version
/// number at the time, and unlocking for read
/// (unodb::optimistic_lock::read_critical_section::try_read_unlock()) checks
/// whether the lock version did not advance since the read lock. It is also
/// possible to check this in a middle of an RCS
/// (unodb::optimistic_lock::read_critical_section::check()), which has exactly
/// the same semantics under a different name for descriptive code.
///
/// A lock in obsolete state marks data which is on the deallocation backlog to
/// be freed once all the thread epochs have advanced. All algorithms must
/// immediately stop retrying read locking such data and restart.
///
/// ## Literature
///
/// Based on the design from:
/// - V. Leis et al., "The ART of Practical Synchronization," DaMoN 2016, for
///   the algorithms.
/// - H. Boehm, "Can seqlocks get along with programming language memory
///   models?", MSPC 2012, for the critical section data access memory ordering
///   rules.
///
/// The optimistic lock is also similar to Linux kernel sequential locks with
/// the addition of an obsolete state for data marked for reclamation.
class [[nodiscard]] optimistic_lock final {
 public:
  /// Non-atomic lock word representation. Used for copying and manipulating
  /// snapshots of the atomic lock word.
  /// The lock word consists of:
  /// - Bit 0: obsolete state
  /// - Bit 1: write lock
  /// - Bits 2-63: version counter
  // TODO(laurynas): rename to lock_word
  class [[nodiscard]] version_type final {
   public:
    /// Create a new lock word from a raw \a version_val value.
    explicit constexpr version_type(version_tag_type version_val) noexcept
        : version{version_val} {}

    /// Return whether the lock word has the write lock bit set.
    // TODO(laurynas): introduce the precondition of not being obsolete
    [[nodiscard, gnu::const]] constexpr bool is_write_locked() const noexcept {
      return (version & 2U) != 0U;
    }

    /// Return whether the lock word indicates a free lock that is available for
    /// acquisition - neither write-locked nor obsolete.
    [[nodiscard, gnu::const]] constexpr bool is_free() const noexcept {
      return (version & 3U) == 0U;
    }

    /// Return whether the lock word has the obsolete bit set.
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

    /// Return a lock word with the current version and lock bit set.
    /// \pre the lock word must be free.
    [[nodiscard, gnu::const]] constexpr version_type set_locked_bit()
        const noexcept {
      UNODB_DETAIL_ASSERT(is_free());
      return version_type{version + 2};
    }

    /// Return the version_tag_type (just the data, including both the version
    /// and the write lock / obsolete bits).
    // TODO(laurynas): will go away once this class is used directly instead of
    // version_tag_type?
    [[nodiscard]] constexpr version_tag_type get() const noexcept {
      return version;
    }

    /// Compare two lock words for equality, including all the version and lock
    /// / obsolete bits.
    [[nodiscard]] constexpr bool operator==(version_type other) const noexcept {
      return version == other.version;
    }

    /// Output the lock word to \a os output stream. Should only be used
    /// for debug dumping.
    [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
      os << "version = 0x" << std::hex << std::setfill('0') << std::setw(8)
         << version << std::dec;
      if (is_write_locked()) os << " (write locked)";
      if (is_obsolete()) os << " (obsoleted)";
    }

   private:
    /// The raw lock word value.
    version_tag_type version{0};
  };  // class version_type

 private:
  /// The atomic lock word and its operations.
  class [[nodiscard]] atomic_version_type final {
   public:
    /// Atomically load the lock word with acquire memory ordering.
    [[nodiscard]] version_type load() const noexcept {
      return version_type{version.load(std::memory_order_acquire)};
    }

    /// Atomically load the lock word with relaxed memory ordering.
    [[nodiscard]] version_type load_relaxed() const noexcept {
      return version_type{version.load(std::memory_order_relaxed)};
    }

    /// Atomically compare-and-exchange the lock word with acquire ordering on
    /// success. May not fail spuriously.
    /// \param expected The expected current lock word value
    /// \param new_val The new lock word value to set
    /// \return true if the exchange was successful
    [[nodiscard]] bool cas_acquire(version_type expected,
                                   version_type new_val) noexcept {
      auto expected_val = expected.get();
      return UNODB_DETAIL_LIKELY(version.compare_exchange_strong(
          expected_val, new_val.get(), std::memory_order_acquire,
          std::memory_order_relaxed));
    }

    /// Atomically clear the write lock bit with release memory ordering.
    /// The version number is preserved.
    /// \pre The write lock bit must be set.
    void write_unlock() noexcept {
      UNODB_DETAIL_ASSERT(load().is_write_locked());

      version.fetch_add(2, std::memory_order_release);
    }

    /// Atomically clear the set write lock bit and set the obsolete bit with
    /// release memory ordering.
    /// \pre The obsolete bit must be clear
    /// \pre The write lock bit must be set
    // TODO(laurynas): we don't care about the version number, can we just
    // store 1 atomically?
    void write_unlock_and_obsolete() noexcept {
#ifndef NDEBUG
      const auto old_lock_word{load()};
      UNODB_DETAIL_ASSERT(!old_lock_word.is_obsolete());
      UNODB_DETAIL_ASSERT(old_lock_word.is_write_locked());
#endif

      version.fetch_add(3, std::memory_order_release);

#ifndef NDEBUG
      const auto current_lock_word{load()};
      UNODB_DETAIL_ASSERT(!current_lock_word.is_write_locked());
      UNODB_DETAIL_ASSERT(current_lock_word.is_obsolete());
#endif
    }

   private:
    /// The raw atomic lock word.
    std::atomic<std::uint64_t> version;

    static_assert(decltype(version)::is_always_lock_free,
                  "Must use always lock-free atomics");
  };  // class atomic_version_type

 public:
  class write_guard;

  // A read_critical_section (RCS) encapsulates a lock on some node
  // and the version information that was read for that lock.  There
  // are three different states for an RCS.
  //
  // (1) The backing node was obsolete when the RCS was returned by
  // optimistic_lock::try_read_lock().  This is currently signaled by
  // [lock==nullptr] internally.
  //
  // (2) The RCS was acquired and is valid.
  //
  // (3) The RCS has been unlocked and is no longer valid.  Note that
  // in a debug build this also sets [lock=nullptr].
  class [[nodiscard]] read_critical_section final {
   public:
    // construct an RCS for an obsolete node.
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

    // Unlock iff it is not yet unlocked.  The read_critical_section
    // is invalidated by this method and must not be used again by the
    // caller.
    //
    // Note: In a DEBUG build, this clears the [lock] pointer, causing
    // subsequent use of the RCS to result in a fault, and decrements
    // the read_lock_count.
    //
    // @return true iff the [version] on the optimistic_lock is still
    // the version that was used to construct this
    // read_critical_section.
    [[nodiscard, gnu::flatten]] UNODB_DETAIL_FORCE_INLINE bool try_read_unlock()
        const noexcept {
      const auto result = lock->try_read_unlock(version);
#ifndef NDEBUG
      lock = nullptr;
#endif
      return UNODB_DETAIL_LIKELY(result);
    }

    // Return true iff the version on the optimistic lock is still the
    // same version that was used to construct this
    // read_critical_section (RCS).
    //
    // Note: By contract, it is not legal to call this method if the
    // RCS was marked obsolete when it was constructed.  You MUST
    // detect this situation by calling must_restart() immediately on
    // obtaining an RCS from optimistic_lock::try_read_lock(). A
    // failure to do this can lead to the dereference of a nullptr for
    // the [lock] when you call check().
    //
    // Note: By contract, it is not legal to call this method if the
    // check has already failed.  To help catch such situations, in a
    // DEBUG build, this will clear the [lock] pointer if the check
    // fails.  A subsequent check() call will then dereference a
    // nullptr and fault the process.
    //
    // @return true if the version is unchanged and false if the
    // caller MUST restart because the version has been changed.
    [[nodiscard]] bool check() const noexcept {
      const auto result = lock->check(version);
#ifndef NDEBUG
      if (UNODB_DETAIL_UNLIKELY(!result)) lock = nullptr;
#endif
      return UNODB_DETAIL_LIKELY(result);
    }

    // The optimistic_lock::try_read_lock() method MAY return a
    // read_critical_section (RCS) for an obsolete node.  Upon
    // obtaining the RCS, the caller MUST call this method to
    // determine whether the node was obsolete and MUST restart if the
    // method returns false.
    //
    // @return false if the node was obsolete at the time that the RCS
    // was obtained.
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

    // The version tag backing the read_critical_section.
    [[nodiscard]] constexpr version_tag_type get() const noexcept {
      return version.get();
    }

    read_critical_section(const read_critical_section &) = delete;
    read_critical_section(read_critical_section &&) = delete;
    read_critical_section &operator=(const read_critical_section &) = delete;

   private:
#ifndef NDEBUG
    mutable
#endif
        optimistic_lock *lock{nullptr};

    version_type version{0};

    friend class write_guard;
  };  // class read_critical_section

  // Every write lock critical section starts out as a read lock critical
  // section which then is attempted to upgrade.
  class [[nodiscard]] write_guard final {
   public:
    explicit write_guard(read_critical_section &&critical_section) noexcept
        : lock{critical_section.lock} {
#ifndef NDEBUG
      critical_section.lock = nullptr;
#endif
      const auto result =
          lock->try_upgrade_to_write_lock(critical_section.version);
      if (UNODB_DETAIL_UNLIKELY(!result)) lock = nullptr;
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
  };  // class write_guard

  optimistic_lock() noexcept = default;

  optimistic_lock(const optimistic_lock &) = delete;
  optimistic_lock(optimistic_lock &&) = delete;
  optimistic_lock &operator=(const optimistic_lock &) = delete;
  optimistic_lock &operator=(optimistic_lock &&) = delete;

  ~optimistic_lock() noexcept = default;

  // Acquire and return a read_critical_section for some lock.  This
  // is done without writing anything on the lock, but it can spin if
  // the lock is in a transient state (e.g., locked by a writer).
  //
  // Note: The returned read_critical_section MAY be marked
  // [obsolete].
  //
  // Note: The caller MUST call read_critical_section::must_restart()
  // immediately on the result of this method in order to determine if
  // the node is obsolete.
  //
  // @return a read_critical_section which MAY be invalid.
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

  // Return a read_critical_section for this optimistic_lock using a
  // version_tag_type which had been obtained previously.  The use case
  // for this is to fix up the optimistic_lock when a version_tag_type is
  // read from the stack for an OLC itertor.  It bumps the read lock
  // count to make the code happy but does not do any spin waits or
  // even look at the current version_tag_type associated with the lock.
  // When the caller calls read_critical_section::check() on the
  // returned lock they will figure out whether or not the version is
  // still valid.
  [[nodiscard]] read_critical_section rehydrate_read_lock(
      version_tag_type version_tag) noexcept {
    // TODO(laurynas) The inc_read_lock_count call should be
    // refactored to a RCS-creating factory method in optimistic_lock,
    // removing the need for this comment and cleaning up usage. Not
    // necessary to do now.
    inc_read_lock_count();
    return read_critical_section{*this, version_type(version_tag)};
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
  // return true if the version has not changed.
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
        version.cas_acquire(locked_version, locked_version.set_locked_bit())};
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
};  // class optimistic_lock

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

// A gloss for the atomic semantics used to guard loads and stores.
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
};  // class in_critical_section

}  // namespace unodb

#endif  // UNODB_DETAIL_OPTIMISTIC_LOCK_HPP
