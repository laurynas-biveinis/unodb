// Copyright (C) 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_QSBR_HPP
#define UNODB_DETAIL_QSBR_HPP

/// \file
/// Quiescent State-based reclamation.
///
/// \ingroup qsbr
///
/// Quiescent state-based reclamation (QSBR) memory reclamation scheme. Instead
/// of freeing memory directly, threads register pending deallocation requests
/// to be executed later. Further, each thread notifies when it's not holding
/// any pointers to the shared data structure (is quiescent with respect to that
/// structure). All the threads having passed through a quiescent state
/// constitute a quiescent period, and an epoch change happens at its boundary.
/// At that point all the pending deallocation requests queued before the start
/// of the just-finished quiescent period can be safely executed.
///
/// For a usage example, see example/example_olc_art.cpp.
///
/// The implementation borrows some of the basic ideas from
/// https://preshing.com/20160726/using-quiescent-states-to-reclaim-memory/

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__fwd/ostream.h>
// IWYU pragma: no_include <__ostream/basic_ostream.h>
// IWYU pragma: no_include <boost/fusion/algorithm/iteration/for_each.hpp>
// IWYU pragma: no_include <boost/fusion/algorithm/query/find_if.hpp>
// IWYU pragma: no_include <boost/fusion/iterator/next.hpp>
// IWYU pragma: no_include <boost/fusion/sequence/intrinsic/begin.hpp>
// IWYU pragma: no_include <boost/fusion/sequence/intrinsic/end.hpp>
// IWYU pragma: no_include <boost/fusion/iterator/deref.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#ifndef NDEBUG
#include <functional>
#endif
#include <iostream>
#include <memory>
#ifndef NDEBUG
#include <optional>
#endif
#include <system_error>
#include <thread>
#include <type_traits>
#ifndef NDEBUG
#include <unordered_set>
#endif
#include <utility>
#include <vector>

#ifdef UNODB_DETAIL_WITH_STATS

#include <mutex>

#include <boost/accumulators/accumulators_fwd.hpp>  // IWYU pragma: keep
#include <boost/accumulators/framework/accumulator_set.hpp>
#include <boost/accumulators/framework/extractor.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#endif  // UNODB_DETAIL_WITH_STATS

#include "assert.hpp"
#include "heap.hpp"
#include "portability_arch.hpp"

namespace unodb {

namespace detail {

/// Two-bit wrapping-around QSBR epoch counter. Two epochs can be compared for
/// equality but otherwise are unordered. One bit counter would be enough too,
/// but with two bits we can check more invariants.
class [[nodiscard]] qsbr_epoch final {
 public:
  /// Underlying integer type for the epoch.
  using epoch_type = std::uint8_t;

  /// Maximum epoch value.
  static constexpr epoch_type max = 3U;

  /// Copy-construct the epoch counter.
  qsbr_epoch(const qsbr_epoch &) noexcept = default;

  /// Move-construct the epoch counter.
  qsbr_epoch(qsbr_epoch &&) noexcept = default;

  /// Copy-assign the epoch counter.
  qsbr_epoch &operator=(const qsbr_epoch &) noexcept = default;

  /// Move-assign the epoch counter.
  qsbr_epoch &operator=(qsbr_epoch &&) noexcept = default;

  /// Trivially destruct the epoch counter.
  ~qsbr_epoch() noexcept = default;

  /// Construct an epoch with a specific value \a epoch_val_.
  constexpr explicit qsbr_epoch(epoch_type epoch_val_) : epoch_val{epoch_val_} {
#ifndef NDEBUG
    assert_invariant();
#endif
  }

  /// Return a new epoch advanced by \a by, wrapping around, if necessary.
  [[nodiscard, gnu::pure]] constexpr qsbr_epoch advance(
      unsigned by = 1) const noexcept {
#ifndef NDEBUG
    assert_invariant();
#endif

    return qsbr_epoch{static_cast<epoch_type>((epoch_val + by) % max_count)};
  }

  /// Return the raw epoch value.
  [[nodiscard, gnu::pure]] constexpr epoch_type get_val() const noexcept {
    return epoch_val;
  }

  /// Compare equal to \a other.
  [[nodiscard, gnu::pure]] constexpr bool operator==(
      qsbr_epoch other) const noexcept {
#ifndef NDEBUG
    assert_invariant();
    other.assert_invariant();
#endif

    return epoch_val == other.epoch_val;
  }

  /// Compare not equal to \a other.
  [[nodiscard, gnu::pure]] constexpr bool operator!=(
      qsbr_epoch other) const noexcept {
#ifndef NDEBUG
    assert_invariant();
    other.assert_invariant();
#endif

    return epoch_val != other.epoch_val;
  }

  /// Output the epoch to \a os. For debug purposes only.
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const;

  /// Prohibit constructing defaulted epoch values.
  qsbr_epoch() noexcept = delete;

 private:
  /// Number of possible epoch values.
  static constexpr auto max_count = max + 1U;

  static_assert((max_count & (max_count - 1U)) == 0,
                "The number of possible epoch values must be a power of 2");

  /// Epoch value.
  epoch_type epoch_val;

#ifndef NDEBUG
  /// Assert that the epoch value is valid.
  constexpr void assert_invariant() const noexcept {
    UNODB_DETAIL_ASSERT(epoch_val <= max);
  }
#endif
};

// LCOV_EXCL_START
/// Print the epoch \a value to the output stream \a os. For debug purposes
/// only.
[[gnu::cold]] inline std::ostream &operator<<(
    std::ostream &os UNODB_DETAIL_LIFETIMEBOUND, qsbr_epoch value) {
  value.dump(os);
  return os;
}
// LCOV_EXCL_STOP

/// Type for counting QSBR-managed threads.
using qsbr_thread_count_type = std::uint32_t;

/// Maximum number of threads that can participate in QSBR simultaneously. Set
/// to 2^29-1, chosen to allow two thread count fields and an epoch counter to
/// fit in a single machine word. It should be enough for everybody, and its
/// overflow is not checked even in Release builds.
inline constexpr qsbr_thread_count_type max_qsbr_threads = (2UL << 29U) - 1U;

}  // namespace detail

// Bits are allocated as follows:
// 0..29: number of threads in the previous epoch
// 30..31: unused
// 32..62: total number of threads
// 63..64: wrapping-around epoch counter
// Special states: if a thread decrements the number of threads in the previous
// epoch and observes zero while the total number of threads is greater than
// zero, then this thread is responsible for the epoch change. The decrement of
// the last thread in the previous epoch and the epoch bump may happen in a
// single step, in which case nobody will observe zero threads in the previous
// epoch.
// TODO(laurynas): move to detail namespace
struct qsbr_state {
  using type = std::uint64_t;

 private:
  [[nodiscard, gnu::const]] static constexpr detail::qsbr_epoch do_get_epoch(
      type word) noexcept {
    return detail::qsbr_epoch{static_cast<detail::qsbr_epoch::epoch_type>(
        word >> epoch_in_word_offset)};
  }

  [[nodiscard, gnu::const]] static constexpr detail::qsbr_thread_count_type
  do_get_thread_count(type word) noexcept {
    const auto result = static_cast<detail::qsbr_thread_count_type>(
        (word & thread_count_in_word_mask) >> thread_count_in_word_offset);
    UNODB_DETAIL_ASSERT(result <= detail::max_qsbr_threads);
    return result;
  }

  [[nodiscard, gnu::const]] static constexpr detail::qsbr_thread_count_type
  do_get_threads_in_previous_epoch(type word) noexcept {
    const auto result = static_cast<detail::qsbr_thread_count_type>(
        word & threads_in_previous_epoch_in_word_mask);
    UNODB_DETAIL_ASSERT(result <= detail::max_qsbr_threads);
    return result;
  }

 public:
  [[nodiscard, gnu::const]] static constexpr detail::qsbr_epoch get_epoch(
      type word) noexcept {
    assert_invariants(word);
    return do_get_epoch(word);
  }

  [[nodiscard, gnu::const]] static constexpr detail::qsbr_thread_count_type
  get_thread_count(type word) noexcept {
    assert_invariants(word);
    return do_get_thread_count(word);
  }

  [[nodiscard, gnu::const]] static constexpr detail::qsbr_thread_count_type
  get_threads_in_previous_epoch(type word) noexcept {
    assert_invariants(word);
    return do_get_threads_in_previous_epoch(word);
  }

  [[nodiscard, gnu::const]] static constexpr bool single_thread_mode(
      type word) noexcept {
    return get_thread_count(word) < 2;
  }

  [[gnu::cold]] UNODB_DETAIL_NOINLINE static void dump(std::ostream &os,
                                                       type word);

 private:
  friend class qsbr;

  [[nodiscard, gnu::const]] static constexpr type make_from_epoch(
      detail::qsbr_epoch epoch) noexcept {
    const auto result = static_cast<type>(epoch.get_val())
                        << epoch_in_word_offset;
    assert_invariants(result);
    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type inc_thread_count(
      type word) noexcept {
    assert_invariants(word);

    const auto result = word + one_thread_in_count;

    assert_invariants(result);
    UNODB_DETAIL_ASSERT(get_epoch(word) == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) ==
                        get_threads_in_previous_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) + 1 == get_thread_count(result));

    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type dec_thread_count(
      type word) noexcept {
    assert_invariants(word);
    UNODB_DETAIL_ASSERT(get_thread_count(word) > 0);

    const auto result = word - one_thread_in_count;

    assert_invariants(result);
    UNODB_DETAIL_ASSERT(get_epoch(word) == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) ==
                        get_threads_in_previous_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) - 1 == get_thread_count(result));

    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type
  inc_thread_count_and_threads_in_previous_epoch(type word) noexcept {
    assert_invariants(word);

    const auto result = word + one_thread_and_one_in_previous;

    assert_invariants(result);
    UNODB_DETAIL_ASSERT(get_epoch(word) == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) + 1 ==
                        get_threads_in_previous_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) + 1 == get_thread_count(result));

    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type
  dec_thread_count_and_threads_in_previous_epoch(type word) noexcept {
    assert_invariants(word);
    UNODB_DETAIL_ASSERT(get_thread_count(word) > 0);
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) > 0);

    const auto result = word - one_thread_and_one_in_previous;

    assert_invariants(result);
    UNODB_DETAIL_ASSERT(get_epoch(word) == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) - 1 ==
                        get_threads_in_previous_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) - 1 == get_thread_count(result));

    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type inc_epoch_reset_previous(
      type word) noexcept {
    assert_invariants(word);
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) == 0);

    const auto old_epoch = get_epoch(word);
    const auto new_epoch_in_word = make_from_epoch(old_epoch.advance());
    const auto new_thread_count_in_word = word & thread_count_in_word_mask;
    const auto new_threads_in_previous = (word >> thread_count_in_word_offset) &
                                         threads_in_previous_epoch_in_word_mask;
    const auto result =
        new_epoch_in_word | new_thread_count_in_word | new_threads_in_previous;

    UNODB_DETAIL_ASSERT(get_epoch(result) == old_epoch.advance());
    UNODB_DETAIL_ASSERT(get_thread_count(result) == get_thread_count(word));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(result) ==
                        get_thread_count(result));
    assert_invariants(result);

    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type
  inc_epoch_dec_thread_count_reset_previous(type word) noexcept {
    assert_invariants(word);
    const auto old_thread_count = get_thread_count(word);
    UNODB_DETAIL_ASSERT(old_thread_count > 0);
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) == 1);

    const auto new_word_with_epoch = make_from_epoch(get_epoch(word).advance());
    const auto new_thread_count = old_thread_count - 1;
    const auto new_word_with_thread_count = static_cast<type>(new_thread_count)
                                            << thread_count_in_word_offset;
    const auto new_threads_in_previous = new_thread_count;
    const auto result = new_word_with_epoch | new_word_with_thread_count |
                        new_threads_in_previous;

    UNODB_DETAIL_ASSERT(get_epoch(word).advance() == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) - 1 == get_thread_count(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(result) ==
                        get_thread_count(result));
    assert_invariants(result);

    return result;
  }

  [[nodiscard, gnu::const]] static constexpr type
  dec_thread_count_threads_in_previous_epoch_maybe_advance(
      type word, bool advance_epoch) noexcept {
    return UNODB_DETAIL_UNLIKELY(advance_epoch)
               ? inc_epoch_dec_thread_count_reset_previous(word)
               : dec_thread_count_and_threads_in_previous_epoch(word);
  }

  [[nodiscard]] static auto atomic_fetch_dec_threads_in_previous_epoch(
      std::atomic<type> &word) noexcept;

  static constexpr void assert_invariants(
      type word UNODB_DETAIL_USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
    const auto thread_count = do_get_thread_count(word);
    UNODB_DETAIL_ASSERT(thread_count <= detail::max_qsbr_threads);
    const auto threads_in_previous = do_get_threads_in_previous_epoch(word);
    UNODB_DETAIL_ASSERT(threads_in_previous <= thread_count);
#endif
  }

  static constexpr auto thread_count_mask = detail::max_qsbr_threads;
  static_assert((thread_count_mask & (thread_count_mask + 1)) == 0);

  static constexpr auto threads_in_previous_epoch_in_word_mask =
      static_cast<std::uint64_t>(thread_count_mask);

  static constexpr auto thread_count_in_word_offset = 32U;
  static constexpr auto thread_count_in_word_mask =
      static_cast<std::uint64_t>(thread_count_mask)
      << thread_count_in_word_offset;

  static constexpr auto epoch_in_word_offset = 62U;

  static constexpr auto one_thread_in_count = 1ULL
                                              << thread_count_in_word_offset;
  static constexpr auto one_thread_and_one_in_previous =
      one_thread_in_count | 1U;
};

namespace detail {

struct [[nodiscard]] deallocation_request final {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  void *const pointer;

#ifndef NDEBUG
  using debug_callback = std::function<void(const void *)>;

  // Non-const to support move
  debug_callback dealloc_callback;
  qsbr_epoch request_epoch;
#endif

  explicit deallocation_request(void *pointer_ UNODB_DETAIL_LIFETIMEBOUND
#ifndef NDEBUG
                                ,
                                qsbr_epoch request_epoch_,
                                debug_callback dealloc_callback_
#endif
                                ) noexcept
      : pointer{pointer_}
#ifndef NDEBUG
        ,
        dealloc_callback{std::move(dealloc_callback_)},
        request_epoch{request_epoch_}
#endif
  {
#ifndef NDEBUG
    instance_count.fetch_add(1, std::memory_order_relaxed);
#endif
  }

  deallocation_request(deallocation_request &&) noexcept = default;
  ~deallocation_request() = default;

  void deallocate(
#ifndef NDEBUG
      bool orphan, std::optional<qsbr_epoch> dealloc_epoch,
      std::optional<bool> dealloc_epoch_single_thread_mode
#endif
  ) const noexcept;

#ifndef NDEBUG
  static void assert_zero_instances() noexcept {
    UNODB_DETAIL_ASSERT(instance_count.load(std::memory_order_relaxed) == 0);
  }

  deallocation_request(const deallocation_request &) = delete;
  deallocation_request &operator=(const deallocation_request &) = delete;
  deallocation_request &operator=(deallocation_request &&) = delete;

 private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::atomic<std::uint64_t> instance_count;
#endif
};

using dealloc_request_vector = std::vector<deallocation_request>;

class [[nodiscard]] deferred_requests final {
 public:
  UNODB_DETAIL_RELEASE_EXPLICIT deferred_requests(
      dealloc_request_vector &&requests_
#ifndef NDEBUG
      ,
      bool orphaned_requests_, std::optional<qsbr_epoch> request_epoch_,
      std::optional<bool> dealloc_epoch_single_thread_mode_
#endif
      ) noexcept
      : requests{std::move(requests_)}
#ifndef NDEBUG
        ,
        orphaned_requests{orphaned_requests_},
        dealloc_epoch{request_epoch_},
        dealloc_epoch_single_thread_mode{dealloc_epoch_single_thread_mode_}
#endif
  {
  }

  deferred_requests(const deferred_requests &) noexcept = delete;
  deferred_requests(deferred_requests &&) noexcept = delete;
  deferred_requests &operator=(const deferred_requests &) noexcept = delete;
  deferred_requests &operator=(deferred_requests &&) noexcept = delete;

  ~deferred_requests() noexcept {
    for (const auto &dealloc_request : requests) {
      dealloc_request.deallocate(
#ifndef NDEBUG
          orphaned_requests, dealloc_epoch, dealloc_epoch_single_thread_mode
#endif
      );
    }
  }

  deferred_requests() = delete;

 private:
  const dealloc_request_vector requests;

#ifndef NDEBUG
  const bool orphaned_requests;
  const std::optional<qsbr_epoch> dealloc_epoch;
  const std::optional<bool> dealloc_epoch_single_thread_mode;
#endif
};

UNODB_DETAIL_DISABLE_MSVC_WARNING(26495)
struct dealloc_vector_list_node {
  detail::dealloc_request_vector requests;
  dealloc_vector_list_node *next;
};
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

struct set_qsbr_per_thread_in_main_thread;

#ifndef UNODB_DETAIL_MSVC_CLANG

template <typename Func>
void on_thread_exit(Func fn) noexcept {
  struct thread_exiter {
    explicit thread_exiter(Func func_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
        : func{func_} {}
    ~thread_exiter() noexcept { func(); }

    thread_exiter(const thread_exiter &) = delete;
    thread_exiter(thread_exiter &&) = delete;
    thread_exiter &operator=(const thread_exiter &) = delete;
    thread_exiter &operator=(thread_exiter &&) = delete;

   private:
    Func func;
  };

  const thread_local static thread_exiter exiter{fn};
}

#endif  // #ifndef UNODB_DETAIL_MSVC_CLANG

}  // namespace detail

// Thread-local QSBR data structure. It has deallocation request lists for the
// previous and current epoch, as well as the last seen global epoch by any
// operation by this thread and the last seen global epoch by a quiescent state
// of this thread.
class [[nodiscard]] qsbr_per_thread final {
 public:
  [[nodiscard, gnu::pure]] bool is_qsbr_paused() const noexcept {
    return paused;
  }

  qsbr_per_thread();

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  ~qsbr_per_thread() noexcept {
    if (!is_qsbr_paused()) {
#ifdef UNODB_DETAIL_WITH_STATS
      // TODO(laurynas): to avoid try/catch below:
      // - replace std::mutex with noexcept synchronization, realistically only
      // spinlock fits, which might not be good enough;
      // - replace Boost.Accumulator with own noexcept stats.
      try {
        qsbr_pause();
      }
      // The QSBR destructor can only throw std::system_error from the stats
      // mutex lock. Eat this exception, and eat any other unexpected exceptions
      // too, except for the debug build.
      // LCOV_EXCL_START
      catch (const std::system_error &e) {
        std::cerr << "Failed to register QSBR stats for the quitting thread: "
                  << e.what() << '\n';
      } catch (const std::exception &e) {
        std::cerr << "Unknown exception in the QSBR thread destructor: "
                  << e.what() << '\n';
        UNODB_DETAIL_DEBUG_CRASH();
      } catch (...) {
        std::cerr << "Unknown exception in the QSBR thread destructor";
        UNODB_DETAIL_DEBUG_CRASH();
      }
#else
      qsbr_pause();
#endif
      // LCOV_EXCL_STOP
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void on_next_epoch_deallocate(
      void *pointer
#ifdef UNODB_DETAIL_WITH_STATS
      ,
      std::size_t size
#endif
#ifndef NDEBUG
      ,
      detail::deallocation_request::debug_callback dealloc_callback
#endif
  );

  void quiescent()
#ifndef UNODB_DETAIL_WITH_STATS
      noexcept
#endif
      ;

  void qsbr_pause()
#ifndef UNODB_DETAIL_WITH_STATS
      noexcept
#endif
      ;

  void qsbr_resume();

  [[nodiscard]] bool previous_interval_requests_empty() const noexcept {
    return previous_interval_dealloc_requests.empty();
  }

  [[nodiscard]] bool current_interval_requests_empty() const noexcept {
    return current_interval_dealloc_requests.empty();
  }

#ifdef UNODB_DETAIL_WITH_STATS

  [[nodiscard]] std::size_t get_current_interval_total_dealloc_size()
      const noexcept {
    return current_interval_total_dealloc_size;
  }

#endif  // UNODB_DETAIL_WITH_STATS

  qsbr_per_thread(const qsbr_per_thread &) = delete;
  qsbr_per_thread(qsbr_per_thread &&) = delete;
  qsbr_per_thread &operator=(const qsbr_per_thread &) = delete;
  qsbr_per_thread &operator=(qsbr_per_thread &&) = delete;

#ifndef NDEBUG
  void register_active_ptr(const void *ptr);
  void unregister_active_ptr(const void *ptr);
#endif

 private:
  friend class qsbr;
  friend class qsbr_thread;
  friend qsbr_per_thread &this_thread() noexcept;
  friend struct detail::set_qsbr_per_thread_in_main_thread;

  [[nodiscard]] static qsbr_per_thread &get_instance() noexcept {
    return *current_thread_instance;
  }

  static void set_instance(
      std::unique_ptr<qsbr_per_thread> new_instance) noexcept {
    current_thread_instance = std::move(new_instance);
#ifndef UNODB_DETAIL_MSVC_CLANG
    // Force qsbr_per_thread destructor to run on thread exit. It already runs
    // without this kludge on most configurations, except with gcc --coverage on
    // macOS, and it seems it is not guaranteed -
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61991
    detail::on_thread_exit([]() noexcept { current_thread_instance.reset(); });
#endif
  }

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  thread_local static std::unique_ptr<qsbr_per_thread> current_thread_instance;

  // Preallocated list nodes for orphaning the requests to avoid memory
  // allocation on thread exit code path.
  std::unique_ptr<detail::dealloc_vector_list_node>
      previous_interval_orphan_list_node{
          std::make_unique<detail::dealloc_vector_list_node>()};

  std::unique_ptr<detail::dealloc_vector_list_node>
      current_interval_orphan_list_node{
          std::make_unique<detail::dealloc_vector_list_node>()};

  std::uint64_t quiescent_states_since_epoch_change{0};

  // Last seen global epoch by quiescent state for this thread
  detail::qsbr_epoch last_seen_quiescent_state_epoch;

  // Last seen global epoch by any operation for this thread
  detail::qsbr_epoch last_seen_epoch;

  detail::dealloc_request_vector previous_interval_dealloc_requests;
  detail::dealloc_request_vector current_interval_dealloc_requests;

  bool paused{false};

#ifdef UNODB_DETAIL_WITH_STATS
  std::size_t current_interval_total_dealloc_size{0};
#endif  // UNODB_DETAIL_WITH_STATS

  void advance_last_seen_epoch(
      bool single_thread_mode, detail::qsbr_epoch new_seen_epoch,
      detail::dealloc_request_vector new_current_requests = {})
#ifndef UNODB_DETAIL_WITH_STATS
      noexcept
#endif
      ;

  void update_requests(bool single_thread_mode,
                       detail::qsbr_epoch dealloc_epoch,
                       detail::dealloc_request_vector new_current_requests = {})
#ifndef UNODB_DETAIL_WITH_STATS
      noexcept
#endif
      ;

  void orphan_deferred_requests() noexcept;

#ifndef NDEBUG
  std::unordered_multiset<const void *> active_ptrs;
#endif
};

[[nodiscard]] inline qsbr_per_thread &this_thread() noexcept {
  return qsbr_per_thread::get_instance();
}

#ifdef UNODB_DETAIL_WITH_STATS

namespace boost_acc = boost::accumulators;

#endif  // UNODB_DETAIL_WITH_STATS

// If C++ standartisation proposal by A. D. Robison "Policy-based design for
// safe destruction in concurrent containers" ever gets anywhere, consider
// changing to its interface, like Stamp-it paper does.

class qsbr final {
 public:
  [[nodiscard]] static qsbr &instance() noexcept {
    static qsbr instance;
    return instance;
  }

  qsbr(const qsbr &) = delete;
  qsbr(qsbr &&) = delete;
  qsbr &operator=(const qsbr &) = delete;
  qsbr &operator=(qsbr &&) = delete;

 private:
  friend struct detail::deallocation_request;
  friend class qsbr_per_thread;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  static void deallocate(
      void *pointer
#ifndef NDEBUG
      ,
      const detail::deallocation_request::debug_callback &debug_callback
#endif
      ) noexcept {
#ifndef NDEBUG
    if (debug_callback != nullptr) debug_callback(pointer);
#endif
    detail::free_aligned(pointer);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 public:
  [[nodiscard]] detail::qsbr_epoch remove_thread_from_previous_epoch(
      detail::qsbr_epoch current_global_epoch
#ifndef NDEBUG
      ,
      detail::qsbr_epoch thread_epoch
#endif
      ) noexcept;

  [[nodiscard]] detail::qsbr_epoch register_thread() noexcept;

  void unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                         detail::qsbr_epoch thread_epoch,
                         qsbr_per_thread &qsbr_thread)
#ifndef UNODB_DETAIL_WITH_STATS
      noexcept
#endif
      ;

  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &out) const;

#ifdef UNODB_DETAIL_WITH_STATS

  void reset_stats();

  void register_quiescent_states_per_thread_between_epoch_changes(
      std::uint64_t states) {
    const std::lock_guard guard{quiescent_state_stats_lock};
    quiescent_states_per_thread_between_epoch_change_stats(states);
    publish_quiescent_states_per_thread_between_epoch_change_stats();
  }

  void register_dealloc_stats_per_thread_between_epoch_changes(
      std::size_t total_size, std::size_t count) {
    const std::lock_guard guard{dealloc_stats_lock};
    deallocation_size_per_thread_stats(total_size);
    publish_deallocation_size_stats();
    epoch_dealloc_per_thread_count_stats(count);
    publish_epoch_callback_stats();
  }

  [[nodiscard]] std::size_t get_epoch_callback_count_max() const noexcept {
    return epoch_dealloc_per_thread_count_max.load(std::memory_order_acquire);
  }

  [[nodiscard]] double get_epoch_callback_count_variance() const noexcept {
    return epoch_dealloc_per_thread_count_variance.load(
        std::memory_order_acquire);
  }

  [[nodiscard]] double
  get_mean_quiescent_states_per_thread_between_epoch_changes() const noexcept {
    return quiescent_states_per_thread_between_epoch_change_mean.load(
        std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t get_epoch_change_count() const noexcept {
    return epoch_change_count.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t get_max_backlog_bytes() const noexcept {
    return deallocation_size_per_thread_max.load(std::memory_order_acquire);
  }

  [[nodiscard]] double get_mean_backlog_bytes() const noexcept {
    return deallocation_size_per_thread_mean.load(std::memory_order_acquire);
  }

#endif  // UNODB_DETAIL_WITH_STATS

  // Made public for tests and asserts
  [[nodiscard]] qsbr_state::type get_state() const noexcept {
    return state.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool previous_interval_orphaned_requests_empty()
      const noexcept {
    return orphaned_previous_interval_dealloc_requests.load(
               std::memory_order_acquire) == nullptr;
  }

  [[nodiscard]] bool current_interval_orphaned_requests_empty() const noexcept {
    return orphaned_current_interval_dealloc_requests.load(
               std::memory_order_acquire) == nullptr;
  }

  void assert_idle() const noexcept {
#ifndef NDEBUG
    const auto current_state = get_state();
    qsbr_state::assert_invariants(current_state);
    UNODB_DETAIL_ASSERT(qsbr_state::get_thread_count(current_state) <= 1);
    // Quitting threads may race with epoch changes by design, resulting in
    // previous epoch orphaned requests not being executed until epoch
    // changes one more time. If that does not happen, this assert may fire at
    // the process exit time. While it is possible to handle this silently, by
    // executing the requests then, this may also result in some memory being
    // held too long. Thus users are advised to pass through Q state in the
    // last thread a couple more times at the end.
    UNODB_DETAIL_ASSERT(previous_interval_orphaned_requests_empty());
    UNODB_DETAIL_ASSERT(current_interval_orphaned_requests_empty());
    detail::deallocation_request::assert_zero_instances();
#endif
  }

 private:
  qsbr() noexcept = default;

  ~qsbr() noexcept { assert_idle(); }

  static void thread_epoch_change_barrier() noexcept;

  void epoch_change_barrier_and_handle_orphans(
      bool single_thread_mode) noexcept;

  detail::qsbr_epoch change_epoch(detail::qsbr_epoch current_global_epoch,
                                  bool single_thread_mode) noexcept;

#ifdef UNODB_DETAIL_WITH_STATS

  void bump_epoch_change_count() noexcept;

  void publish_deallocation_size_stats() {
    deallocation_size_per_thread_max.store(
        boost_acc::max(deallocation_size_per_thread_stats),
        std::memory_order_relaxed);
    deallocation_size_per_thread_mean.store(
        boost_acc::mean(deallocation_size_per_thread_stats),
        std::memory_order_relaxed);
    deallocation_size_per_thread_variance.store(
        boost_acc::variance(deallocation_size_per_thread_stats),
        std::memory_order_relaxed);
  }

  void publish_epoch_callback_stats() {
    epoch_dealloc_per_thread_count_max.store(
        boost_acc::max(epoch_dealloc_per_thread_count_stats),
        std::memory_order_relaxed);
    epoch_dealloc_per_thread_count_variance.store(
        boost_acc::variance(epoch_dealloc_per_thread_count_stats),
        std::memory_order_relaxed);
  }

  void publish_quiescent_states_per_thread_between_epoch_change_stats() {
    quiescent_states_per_thread_between_epoch_change_mean.store(
        boost_acc::mean(quiescent_states_per_thread_between_epoch_change_stats),
        std::memory_order_relaxed);
  }

#endif  // UNODB_DETAIL_WITH_STATS

  alignas(detail::hardware_destructive_interference_size)
      std::atomic<qsbr_state::type> state;

  std::atomic<detail::dealloc_vector_list_node *>
      orphaned_previous_interval_dealloc_requests;

  std::atomic<detail::dealloc_vector_list_node *>
      orphaned_current_interval_dealloc_requests;

  static_assert(sizeof(state) +
                    sizeof(orphaned_previous_interval_dealloc_requests) +
                    sizeof(orphaned_current_interval_dealloc_requests) <=
                detail::hardware_constructive_interference_size);

#ifdef UNODB_DETAIL_WITH_STATS

  alignas(detail::hardware_destructive_interference_size)
      std::atomic<std::uint64_t> epoch_change_count;

  alignas(detail::hardware_destructive_interference_size) std::mutex
      dealloc_stats_lock;

  // TODO(laurynas): more interesting callback stats?
  boost_acc::accumulator_set<
      std::size_t,
      boost_acc::stats<boost_acc::tag::max, boost_acc::tag::variance>>
      epoch_dealloc_per_thread_count_stats;
  std::atomic<std::size_t> epoch_dealloc_per_thread_count_max;
  std::atomic<double> epoch_dealloc_per_thread_count_variance;

  boost_acc::accumulator_set<
      std::uint64_t,
      boost_acc::stats<boost_acc::tag::max, boost_acc::tag::variance>>
      deallocation_size_per_thread_stats;
  std::atomic<std::uint64_t> deallocation_size_per_thread_max;
  std::atomic<double> deallocation_size_per_thread_mean;
  std::atomic<double> deallocation_size_per_thread_variance;

  alignas(detail::hardware_destructive_interference_size) std::mutex
      quiescent_state_stats_lock;

  boost_acc::accumulator_set<std::uint64_t,
                             boost_acc::stats<boost_acc::tag::mean>>
      quiescent_states_per_thread_between_epoch_change_stats;
  std::atomic<double> quiescent_states_per_thread_between_epoch_change_mean;

#endif  // UNODB_DETAIL_WITH_STATS
};

static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);

UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)
inline qsbr_per_thread::qsbr_per_thread()
    : last_seen_quiescent_state_epoch{qsbr::instance().register_thread()},
      last_seen_epoch{last_seen_quiescent_state_epoch} {}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

inline void qsbr_per_thread::on_next_epoch_deallocate(
    void *pointer
#ifdef UNODB_DETAIL_WITH_STATS
    ,
    std::size_t size
#endif
#ifndef NDEBUG
    ,
    detail::deallocation_request::debug_callback dealloc_callback
#endif
) {
  UNODB_DETAIL_ASSERT(!is_qsbr_paused());

  const auto current_qsbr_state = qsbr::instance().get_state();
  const auto current_global_epoch = qsbr_state::get_epoch(current_qsbr_state);
  const auto single_thread_mode =
      qsbr_state::single_thread_mode(current_qsbr_state);

  if (UNODB_DETAIL_UNLIKELY(single_thread_mode)) {
    advance_last_seen_epoch(single_thread_mode, current_global_epoch);
    qsbr::deallocate(pointer
#ifndef NDEBUG
                     ,
                     dealloc_callback
#endif
    );
    return;
  }

  if (last_seen_epoch != current_global_epoch) {
    detail::dealloc_request_vector new_current_requests;
    new_current_requests.emplace_back(pointer
#ifndef NDEBUG
                                      ,
                                      current_global_epoch,
                                      std::move(dealloc_callback)
#endif
    );
    advance_last_seen_epoch(single_thread_mode, current_global_epoch,
                            std::move(new_current_requests));
    UNODB_DETAIL_ASSERT(current_interval_dealloc_requests.size() == 1);
#ifdef UNODB_DETAIL_WITH_STATS
    UNODB_DETAIL_ASSERT(current_interval_total_dealloc_size == 0);
    current_interval_total_dealloc_size = size;
#endif  // UNODB_DETAIL_WITH_STATS
    return;
  }

  current_interval_dealloc_requests.emplace_back(pointer
#ifndef NDEBUG
                                                 ,
                                                 last_seen_epoch,
                                                 std::move(dealloc_callback)
#endif
  );
#ifdef UNODB_DETAIL_WITH_STATS
  current_interval_total_dealloc_size += size;
#endif  // UNODB_DETAIL_WITH_STATS
}

inline void qsbr_per_thread::advance_last_seen_epoch(
    bool single_thread_mode, detail::qsbr_epoch new_seen_epoch,
    detail::dealloc_request_vector new_current_requests)
#ifndef UNODB_DETAIL_WITH_STATS
    noexcept
#endif
{
  if (new_seen_epoch == last_seen_epoch) return;

  // NOLINTNEXTLINE(readability-simplify-boolean-expr)
  UNODB_DETAIL_ASSERT(
      new_seen_epoch == last_seen_epoch.advance()
      // The current thread is 1) quitting; 2) not having seen the current epoch
      // yet; 3) it quitting will cause an epoch advance
      || (!single_thread_mode && new_seen_epoch == last_seen_epoch.advance(2)));

  update_requests(single_thread_mode, new_seen_epoch,
                  std::move(new_current_requests));
}

inline void qsbr_per_thread::update_requests(
    bool single_thread_mode, detail::qsbr_epoch dealloc_epoch,
    detail::dealloc_request_vector new_current_requests)
#ifndef UNODB_DETAIL_WITH_STATS
    noexcept
#endif
{
  last_seen_epoch = dealloc_epoch;

  const detail::deferred_requests requests_to_deallocate{
      std::move(previous_interval_dealloc_requests)
#ifndef NDEBUG
          ,
      false, dealloc_epoch, single_thread_mode
#endif
  };

#ifdef UNODB_DETAIL_WITH_STATS
  qsbr::instance().register_dealloc_stats_per_thread_between_epoch_changes(
      current_interval_total_dealloc_size,
      current_interval_dealloc_requests.size());
  current_interval_total_dealloc_size = 0;
#endif  // UNODB_DETAIL_WITH_STATS

  if (UNODB_DETAIL_LIKELY(!single_thread_mode)) {
    previous_interval_dealloc_requests =
        std::move(current_interval_dealloc_requests);
  } else {
    previous_interval_dealloc_requests.clear();
    const detail::deferred_requests additional_requests_to_deallocate{
        std::move(current_interval_dealloc_requests)
#ifndef NDEBUG
            ,
        false, dealloc_epoch, single_thread_mode
#endif
    };
  }
  current_interval_dealloc_requests = std::move(new_current_requests);
}

inline void qsbr_per_thread::quiescent()
#ifndef UNODB_DETAIL_WITH_STATS
    noexcept
#endif
{
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());

  const auto state = qsbr::instance().get_state();
  const auto current_global_epoch = qsbr_state::get_epoch(state);
  const auto single_thread_mode = qsbr_state::single_thread_mode(state);

  advance_last_seen_epoch(single_thread_mode, current_global_epoch);

  if (current_global_epoch != last_seen_quiescent_state_epoch) {
    UNODB_DETAIL_ASSERT(current_global_epoch ==
                        last_seen_quiescent_state_epoch.advance());

    last_seen_quiescent_state_epoch = current_global_epoch;
#ifdef UNODB_DETAIL_WITH_STATS
    qsbr::instance().register_quiescent_states_per_thread_between_epoch_changes(
        quiescent_states_since_epoch_change);
#endif  // UNODB_DETAIL_WITH_STATS
    quiescent_states_since_epoch_change = 0;
  }

  UNODB_DETAIL_ASSERT(current_global_epoch == last_seen_quiescent_state_epoch);
  if (quiescent_states_since_epoch_change == 0) {
    const auto new_global_epoch =
        qsbr::instance().remove_thread_from_previous_epoch(
            current_global_epoch
#ifndef NDEBUG
            ,
            last_seen_quiescent_state_epoch
#endif
        );
    UNODB_DETAIL_ASSERT(new_global_epoch == last_seen_quiescent_state_epoch ||
                        new_global_epoch ==
                            last_seen_quiescent_state_epoch.advance());

    if (new_global_epoch != last_seen_quiescent_state_epoch) {
      last_seen_quiescent_state_epoch = new_global_epoch;

      UNODB_DETAIL_ASSERT(last_seen_epoch.advance() == new_global_epoch);
      update_requests(single_thread_mode, new_global_epoch);

#ifdef UNODB_DETAIL_WITH_STATS
      qsbr::instance()
          .register_quiescent_states_per_thread_between_epoch_changes(1);
#endif  // UNODB_DETAIL_WITH_STATS
      return;
    }
  }
  ++quiescent_states_since_epoch_change;
}

namespace detail {

inline void deallocation_request::deallocate(
#ifndef NDEBUG
    bool orphan, std::optional<qsbr_epoch> dealloc_epoch,
    std::optional<bool> dealloc_epoch_single_thread_mode
#endif
) const noexcept {
  // NOLINTNEXTLINE(readability-simplify-boolean-expr)
  UNODB_DETAIL_ASSERT((orphan && !dealloc_epoch.has_value() &&
                       !dealloc_epoch_single_thread_mode.has_value()) ||
                      (!orphan && dealloc_epoch.has_value() &&
                       dealloc_epoch_single_thread_mode.has_value() &&
                       ((*dealloc_epoch_single_thread_mode &&
                         *dealloc_epoch == request_epoch.advance()) ||
                        *dealloc_epoch == request_epoch.advance(2))));

  qsbr::deallocate(pointer
#ifndef NDEBUG
                   ,
                   dealloc_callback
#endif
  );

#ifndef NDEBUG
  instance_count.fetch_sub(1, std::memory_order_relaxed);
#endif
}

}  // namespace detail

inline void qsbr_per_thread::qsbr_pause()
#ifndef UNODB_DETAIL_WITH_STATS
    noexcept
#endif
{
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());

  qsbr::instance().unregister_thread(quiescent_states_since_epoch_change,
                                     last_seen_quiescent_state_epoch, *this);
  paused = true;

  UNODB_DETAIL_ASSERT(previous_interval_requests_empty());
  UNODB_DETAIL_ASSERT(previous_interval_orphan_list_node == nullptr);
  UNODB_DETAIL_ASSERT(current_interval_requests_empty());
  UNODB_DETAIL_ASSERT(current_interval_orphan_list_node == nullptr);
}

inline void qsbr_per_thread::qsbr_resume() {
  UNODB_DETAIL_ASSERT(paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());
  UNODB_DETAIL_ASSERT(previous_interval_requests_empty());
  UNODB_DETAIL_ASSERT(current_interval_requests_empty());

  previous_interval_orphan_list_node =
      std::make_unique<detail::dealloc_vector_list_node>();
  current_interval_orphan_list_node =
      std::make_unique<detail::dealloc_vector_list_node>();

  last_seen_quiescent_state_epoch = qsbr::instance().register_thread();
  last_seen_epoch = last_seen_quiescent_state_epoch;
  quiescent_states_since_epoch_change = 0;
  paused = false;
}

struct quiescent_state_on_scope_exit final {
  quiescent_state_on_scope_exit() = default;

  quiescent_state_on_scope_exit(const quiescent_state_on_scope_exit &) = delete;
  quiescent_state_on_scope_exit(quiescent_state_on_scope_exit &&) = delete;
  quiescent_state_on_scope_exit &operator=(
      const quiescent_state_on_scope_exit &) = delete;
  quiescent_state_on_scope_exit &operator=(quiescent_state_on_scope_exit &&) =
      delete;

  ~quiescent_state_on_scope_exit()
#ifdef UNODB_DETAIL_WITH_STATS
      noexcept(false)
#else
      noexcept
#endif
  {
#ifdef UNODB_DETAIL_WITH_STATS
    // TODO(laurynas): to avoid try/catch, see the TODO at the previous
    // try/catch.
    try {
      this_thread().quiescent();
    }
    // LCOV_EXCL_START
    catch (const std::system_error &e) {
      // cppcheck-suppress exceptThrowInDestructor
      if (exceptions_at_ctor == std::uncaught_exceptions()) throw;
      std::cerr << "QSBR quiescent state failed to register QSBR stats: "
                << e.what() << '\n';
    } catch (const std::exception &e) {
      if (exceptions_at_ctor == std::uncaught_exceptions()) throw;
      std::cerr << "QSBR quiescent state exception: " << e.what() << '\n';
      UNODB_DETAIL_DEBUG_CRASH();
    } catch (...) {
      if (exceptions_at_ctor == std::uncaught_exceptions()) throw;
      std::cerr << "QSBR quiescent state unknown exception\n";
      UNODB_DETAIL_DEBUG_CRASH();
    }
    // LCOV_EXCL_STOP
#else
    this_thread().quiescent();
#endif
  }

#ifdef UNODB_DETAIL_WITH_STATS

 private:
  const int exceptions_at_ctor{std::uncaught_exceptions()};
#endif
};

// Replace with C++20 std::remove_cvref once it's available
template <typename T>
struct remove_cvref final {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

// All the QSBR users must use qsbr_thread instead of std::thread so that
// a thread-local current_thread_reclamator instance gets properly constructed
class [[nodiscard]] qsbr_thread : public std::thread {
 public:
  using thread::thread;

  template <typename Function, typename... Args>
  explicit qsbr_thread(Function &&f, Args &&...args)
      requires(!std::is_same_v<remove_cvref_t<Function>, qsbr_thread>)
      : std::thread{make_qsbr_thread(std::forward<Function>(f),
                                     std::forward<Args>(args)...)} {}

 private:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  template <typename Function, typename... Args>
  [[nodiscard]] static std::thread make_qsbr_thread(Function &&f,
                                                    Args &&...args) {
    auto new_qsbr_thread_reclamator = std::make_unique<qsbr_per_thread>();
    return std::thread{
        [inner_new_qsbr_thread_reclamator =
             std::move(new_qsbr_thread_reclamator)](
            auto &&f2, auto &&...args2) mutable noexcept(noexcept(f2)) {
          qsbr_per_thread::set_instance(
              std::move(inner_new_qsbr_thread_reclamator));
          f2(std::forward<Args>(args2)...);
        },
        std::forward<Function>(f), std::forward<Args>(args)...};
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
};

}  // namespace unodb

#endif  // UNODB_DETAIL_QSBR_HPP
