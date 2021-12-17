// Copyright (C) 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_HPP
#define UNODB_DETAIL_QSBR_HPP

#include "global.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#ifndef NDEBUG
#include <functional>  // IWYU pragma: keep
#endif
#include <iostream>
#include <mutex>  // IWYU pragma: keep
#include <shared_mutex>
#include <thread>
#include <type_traits>
#ifndef NDEBUG
#include <unordered_set>
#endif
#include <utility>  // IWYU pragma: keep
#include <vector>

#include <gsl/gsl_util>

#include <boost/accumulators/accumulators.hpp>  // IWYU pragma: keep
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include "heap.hpp"

namespace unodb {

// Quiescent-state based reclamation (QSBR) memory reclamation scheme. Instead
// of freeing memory directly, threads register pending deallocation requests to
// be executed later. Further, each thread notifies when it's not holding any
// pointers to the shared data structure (is quiescent with respect to that
// structure). All the threads having passed through a quiescent state
// constitute a quiescent period, and an epoch change happens at its boundary.
// At that point all the pending deallocation requests queued before the
// start of the just-finished quiescent period can be safely executed.

// The implementation borrows some of the basic ideas from
// https://preshing.com/20160726/using-quiescent-states-to-reclaim-memory/

// If C++ standartisation proposal by A. D. Robison "Policy-based design for
// safe destruction in concurrent containers" ever gets anywhere, consider
// changing to its interface, like Stamp-it paper does.

// Two-bit wrapping-around epoch counter. Two epochs can be compared for
// equality but otherwise are unordered. One bit counter would be enough too,
// but with two bits we can check more invariants.
class [[nodiscard]] qsbr_epoch final {
 public:
  using epoch_type = std::uint8_t;

  static constexpr epoch_type max = 3U;

  qsbr_epoch() noexcept = default;
  qsbr_epoch(const qsbr_epoch &) noexcept = default;
  qsbr_epoch(qsbr_epoch &&) noexcept = default;
  qsbr_epoch &operator=(const qsbr_epoch &) noexcept = default;
  qsbr_epoch &operator=(qsbr_epoch &&) noexcept = default;
  ~qsbr_epoch() noexcept = default;

  constexpr explicit qsbr_epoch(epoch_type epoch_val_) : epoch_val{epoch_val_} {
    assert_invariant();
  }

  [[nodiscard]] constexpr qsbr_epoch next() const noexcept {
    assert_invariant();

    return qsbr_epoch{
        gsl::narrow_cast<epoch_type>((epoch_val + 1) % max_count)};
  }

  [[nodiscard]] constexpr bool operator==(qsbr_epoch other) const noexcept {
    assert_invariant();
    other.assert_invariant();

    return epoch_val == other.epoch_val;
  }

  [[nodiscard]] constexpr bool operator!=(qsbr_epoch other) const noexcept {
    assert_invariant();
    other.assert_invariant();

    return epoch_val != other.epoch_val;
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const;

  friend struct qsbr_state;

 private:
  static constexpr auto max_count = max + 1U;
  static_assert((max_count & (max_count - 1U)) == 0);

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  constexpr void assert_invariant() const noexcept {
    UNODB_DETAIL_ASSERT(epoch_val <= max);
  }

  epoch_type epoch_val;
};

// LCOV_EXCL_START
[[gnu::cold]] inline std::ostream &operator<<(std::ostream &os,
                                              qsbr_epoch value) {
  value.dump(os);
  return os;
}
// LCOV_EXCL_STOP

// The maximum allowed QSBR-managed thread count is 2^29-1, should be enough for
// everybody, let's not even bother checking the limit in the Release
// configuration
using qsbr_thread_count_type = std::uint32_t;

inline constexpr qsbr_thread_count_type max_qsbr_threads = (2UL << 29U) - 1U;

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
struct qsbr_state {
  using type = std::uint64_t;

  [[nodiscard]] static constexpr qsbr_epoch get_epoch(type word) noexcept {
    assert_invariants(word);
    return do_get_epoch(word);
  }

  [[nodiscard]] static constexpr qsbr_thread_count_type get_thread_count(
      type word) noexcept {
    assert_invariants(word);
    return do_get_thread_count(word);
  }

  [[nodiscard]] static constexpr qsbr_thread_count_type
  get_threads_in_previous_epoch(type word) noexcept {
    assert_invariants(word);
    return do_get_threads_in_previous_epoch(word);
  }

  [[nodiscard]] static constexpr bool single_thread_mode(type word) noexcept {
    return get_thread_count(word) < 2;
  }

 private:
  friend class qsbr;

  [[nodiscard]] static constexpr type make_from_epoch(
      qsbr_epoch epoch) noexcept {
    const auto result = static_cast<type>(epoch.epoch_val)
                        << epoch_in_word_offset;
    assert_invariants(result);
    return result;
  }

  [[nodiscard]] static constexpr type inc_thread_count(type word) noexcept {
    assert_invariants(word);

    const auto result = word + one_thread_in_count;

    assert_invariants(result);
    UNODB_DETAIL_ASSERT(get_epoch(word) == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) ==
                        get_threads_in_previous_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) + 1 == get_thread_count(result));

    return result;
  }

  [[nodiscard]] static constexpr type dec_thread_count(type word) noexcept {
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

  [[nodiscard]] static constexpr type
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

  [[nodiscard]] static constexpr type
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

  [[nodiscard]] static constexpr type inc_epoch_reset_previous(
      type word) noexcept {
    assert_invariants(word);
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) == 0);

    const auto old_epoch = get_epoch(word);
    const auto new_epoch_in_word = make_from_epoch(old_epoch.next());
    const auto new_thread_count_in_word = word & thread_count_in_word_mask;
    const auto new_threads_in_previous = (word >> thread_count_in_word_offset) &
                                         threads_in_previous_epoch_in_word_mask;
    const auto result =
        new_epoch_in_word | new_thread_count_in_word | new_threads_in_previous;

    UNODB_DETAIL_ASSERT(get_epoch(result) == old_epoch.next());
    UNODB_DETAIL_ASSERT(get_thread_count(result) == get_thread_count(word));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(result) ==
                        get_thread_count(result));
    assert_invariants(result);

    return result;
  }

  [[nodiscard]] static constexpr type inc_epoch_dec_thread_count_reset_previous(
      type word) noexcept {
    assert_invariants(word);
    const auto old_thread_count = get_thread_count(word);
    UNODB_DETAIL_ASSERT(old_thread_count > 0);
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(word) == 1);

    const auto new_word_with_epoch = make_from_epoch(get_epoch(word).next());
    const auto new_thread_count = old_thread_count - 1;
    const auto new_word_with_thread_count = static_cast<type>(new_thread_count)
                                            << thread_count_in_word_offset;
    const auto new_threads_in_previous = new_thread_count;
    const auto result = new_word_with_epoch | new_word_with_thread_count |
                        new_threads_in_previous;

    UNODB_DETAIL_ASSERT(get_epoch(word).next() == get_epoch(result));
    UNODB_DETAIL_ASSERT(get_thread_count(word) - 1 == get_thread_count(result));
    UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(result) ==
                        get_thread_count(result));
    assert_invariants(result);

    return result;
  }

  [[nodiscard]] static type atomic_fetch_dec_threads_in_previous_epoch(
      std::atomic<type> &word) noexcept;

  [[gnu::cold, gnu::noinline]] static void dump(std::ostream &os, type word);

  static constexpr void assert_invariants(
      type word UNODB_DETAIL_USED_IN_DEBUG) noexcept {
#ifndef NDEBUG
    const auto thread_count = do_get_thread_count(word);
    UNODB_DETAIL_ASSERT(thread_count <= max_qsbr_threads);
    const auto threads_in_previous = do_get_threads_in_previous_epoch(word);
    UNODB_DETAIL_ASSERT(threads_in_previous <= thread_count);
#endif
  }

  static constexpr auto thread_count_mask = max_qsbr_threads;
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

  [[nodiscard]] static constexpr qsbr_epoch do_get_epoch(type word) noexcept {
    return qsbr_epoch{
        gsl::narrow_cast<qsbr_epoch::epoch_type>(word >> epoch_in_word_offset)};
  }

  [[nodiscard]] static constexpr qsbr_thread_count_type do_get_thread_count(
      type word) noexcept {
    const auto result = gsl::narrow_cast<qsbr_thread_count_type>(
        (word & thread_count_in_word_mask) >> thread_count_in_word_offset);
    UNODB_DETAIL_ASSERT(result <= max_qsbr_threads);
    return result;
  }

  [[nodiscard]] static constexpr qsbr_thread_count_type
  do_get_threads_in_previous_epoch(type word) noexcept {
    const auto result = gsl::narrow_cast<qsbr_thread_count_type>(
        word & threads_in_previous_epoch_in_word_mask);
    UNODB_DETAIL_ASSERT(result <= max_qsbr_threads);
    return result;
  }
};

class [[nodiscard]] qsbr_per_thread final {
 public:
  qsbr_per_thread() noexcept;

  ~qsbr_per_thread() {
    if (!is_qsbr_paused()) qsbr_pause();
  }

  void quiescent() noexcept;

  void qsbr_pause();

  void qsbr_resume();

  [[nodiscard, gnu::pure]] bool is_qsbr_paused() const noexcept {
    return paused;
  }

  qsbr_per_thread(const qsbr_per_thread &) = delete;
  qsbr_per_thread(qsbr_per_thread &&) = delete;
  qsbr_per_thread &operator=(const qsbr_per_thread &) = delete;
  qsbr_per_thread &operator=(qsbr_per_thread &&) = delete;

#ifndef NDEBUG
  void register_active_ptr(const void *ptr);
  void unregister_active_ptr(const void *ptr);
#endif

 private:
  std::uint64_t quiescent_states_since_epoch_change{0};
  qsbr_epoch last_seen_epoch;

  bool paused{true};

#ifndef NDEBUG
  std::unordered_multiset<const void *> active_ptrs;
#endif
};

[[nodiscard]] inline qsbr_per_thread &this_thread() {
  thread_local static qsbr_per_thread current_thread_reclamator_instance;
  return current_thread_reclamator_instance;
}

inline void construct_current_thread_reclamator() {
  // An ODR-use ensures that the constructor gets called
  (void)this_thread();
}

namespace boost_acc = boost::accumulators;

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

#ifndef NDEBUG
  using dealloc_debug_callback = std::function<void(const void *)>;
#endif

 private:
  static void deallocate(void *pointer
#ifndef NDEBUG
                         ,
                         const dealloc_debug_callback &debug_callback
#endif
                         ) noexcept {
#ifndef NDEBUG
    if (debug_callback != nullptr) debug_callback(pointer);
#endif
    detail::free_aligned(pointer);
  }

 public:
  void on_next_epoch_deallocate(void *pointer, std::size_t size
#ifndef NDEBUG
                                ,
                                dealloc_debug_callback debug_callback
#endif
  ) {
    UNODB_DETAIL_ASSERT(!unodb::this_thread().is_qsbr_paused());

    bool deallocate_immediately = false;
    {
      if (UNODB_DETAIL_UNLIKELY(qsbr_state::single_thread_mode(get_state()))) {
        deallocate_immediately = true;
      } else {
        std::lock_guard guard{qsbr_rwlock};

        assert_invariants_locked();

        // TODO(laurynas): out of critical section?
        current_interval_total_dealloc_size.fetch_add(
            size, std::memory_order_relaxed);
        current_interval_deallocation_requests.emplace_back(
            pointer
#ifndef NDEBUG
            ,
            std::move(debug_callback)
#endif
        );

        assert_invariants_locked();
      }
    }
    if (deallocate_immediately)
      deallocate(pointer
#ifndef NDEBUG
                 ,
                 debug_callback
#endif
      );
  }

  [[nodiscard]] qsbr_epoch remove_thread_from_previous_epoch(
      qsbr_epoch current_global_epoch
#ifndef NDEBUG
      ,
      qsbr_epoch thread_epoch
#endif
      ) noexcept;

  [[nodiscard]] qsbr_epoch register_thread() noexcept;

  void unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                         qsbr_epoch thread_epoch) noexcept;

  void reset_stats() noexcept;

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &out) const;

  inline void register_quiescent_states_per_thread_between_epoch_changes(
      std::uint64_t states) noexcept {
    std::lock_guard guard{
        quiescent_states_per_thread_between_epoch_change_lock};
    quiescent_states_per_thread_between_epoch_change_stats(states);
    publish_quiescent_states_per_thread_between_epoch_change_stats();
  }

  [[nodiscard]] auto get_epoch_callback_count_max() const noexcept {
    return epoch_callback_max.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto get_epoch_callback_count_variance() const noexcept {
    return epoch_callback_variance.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto
  get_mean_quiescent_states_per_thread_between_epoch_changes() const noexcept {
    return quiescent_states_per_thread_between_epoch_change_mean.load(
        std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t get_epoch_change_count() const noexcept {
    return epoch_change_count.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto get_max_backlog_bytes() const noexcept {
    return deallocation_size_max.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto get_mean_backlog_bytes() const noexcept {
    return deallocation_size_mean.load(std::memory_order_acquire);
  }

  // Made public for tests and asserts
  [[nodiscard]] qsbr_state::type get_state() const noexcept {
    return state.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto previous_interval_size() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return previous_interval_deallocation_requests.size();
  }

  [[nodiscard]] auto current_interval_size() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return current_interval_deallocation_requests.size();
  }

  void assert_idle() const noexcept {
#ifndef NDEBUG
    std::shared_lock guard{qsbr_rwlock};
    assert_idle_locked();
#endif
  }

 private:
  void assert_idle_locked() const noexcept;

  struct [[nodiscard]] deallocation_request final {
    void *const pointer;

#ifndef NDEBUG
    dealloc_debug_callback dealloc_callback;
    const qsbr_epoch request_epoch{
        qsbr_state::get_epoch(qsbr::instance().get_state())};
#endif

    explicit deallocation_request(void *pointer_
#ifndef NDEBUG
                                  ,
                                  dealloc_debug_callback dealloc_callback_
#endif
                                  ) noexcept
        : pointer {
      pointer_
    }
#ifndef NDEBUG
    , dealloc_callback { std::move(dealloc_callback_) }
#endif
    {}

    void deallocate(
#ifndef NDEBUG
        qsbr_epoch dealloc_epoch, bool dealloc_epoch_single_thread_mode,
        const dealloc_debug_callback &debug_callback
#endif
    ) const noexcept {
      // TODO(laurynas): count deallocation request instances, assert 0 in QSBR
      // dtor
      // The assert cannot be stricter due to epoch changes by
      // unregister_thread, which moves requests between intervals not
      // atomically with the epoch change.
      UNODB_DETAIL_ASSERT(
          dealloc_epoch == request_epoch.next() ||
          dealloc_epoch == request_epoch.next().next() ||
          (dealloc_epoch == request_epoch.next().next().next()) ||
          (dealloc_epoch_single_thread_mode &&
           dealloc_epoch == request_epoch.next()));

      qsbr::deallocate(pointer
#ifndef NDEBUG
                       ,
                       debug_callback
#endif
      );
    }
  };

  class [[nodiscard]] deferred_requests final {
   public:
    std::array<std::vector<deallocation_request>, 2> requests;

    deferred_requests() noexcept = default;

#ifndef NDEBUG
    deferred_requests(qsbr_epoch request_epoch_,
                      bool dealloc_epoch_single_thread_mode_) noexcept
        : dealloc_epoch{request_epoch_},
          dealloc_epoch_single_thread_mode{dealloc_epoch_single_thread_mode_} {}
#endif

    deferred_requests(const deferred_requests &) noexcept = default;
    deferred_requests(deferred_requests &&) noexcept = default;
    deferred_requests &operator=(const deferred_requests &) noexcept = default;
    deferred_requests &operator=(deferred_requests &&) noexcept = default;

    ~deferred_requests() {
      for (const auto &reqs : requests) {
        for (const auto &dealloc_request : reqs) {
          dealloc_request.deallocate(
#ifndef NDEBUG
              dealloc_epoch, dealloc_epoch_single_thread_mode,
              dealloc_request.dealloc_callback
#endif
          );
        }
      }
    }

   private:
#ifndef NDEBUG
    qsbr_epoch dealloc_epoch;
    bool dealloc_epoch_single_thread_mode;
#endif
  };

  qsbr() noexcept = default;

  ~qsbr() noexcept { assert_idle(); }

  static void thread_epoch_change_barrier() noexcept;

  deferred_requests epoch_change_update_requests(
#ifndef NDEBUG
      qsbr_epoch current_global_epoch,
#endif
      bool single_thread_mode) noexcept;

  qsbr_epoch change_epoch(qsbr_epoch current_global_epoch,
                          bool single_thread_mode,
                          deferred_requests &requests) noexcept;

  void assert_invariants_locked() const noexcept {
#ifndef NDEBUG
    qsbr_state::assert_invariants(get_state());
    if (previous_interval_deallocation_requests.empty()) {
      UNODB_DETAIL_ASSERT(previous_interval_total_dealloc_size.load(
                              std::memory_order_relaxed) == 0);
    }

    if (current_interval_deallocation_requests.empty()) {
      UNODB_DETAIL_ASSERT(current_interval_total_dealloc_size.load(
                              std::memory_order_relaxed) == 0);
    }
#endif
  }

  [[nodiscard]] static deferred_requests make_deferred_requests(
#ifndef NDEBUG
      qsbr_epoch dealloc_epoch, bool single_thread_mode
#endif
      ) noexcept;

  void publish_deallocation_size_stats() noexcept {
    deallocation_size_max.store(boost_acc::max(deallocation_size_stats),
                                std::memory_order_relaxed);
    deallocation_size_mean.store(boost_acc::mean(deallocation_size_stats),
                                 std::memory_order_relaxed);
    deallocation_size_variance.store(
        boost_acc::variance(deallocation_size_stats),
        std::memory_order_relaxed);
  }

  void publish_epoch_callback_stats() noexcept {
    epoch_callback_max.store(boost_acc::max(epoch_callback_stats),
                             std::memory_order_relaxed);
    epoch_callback_variance.store(boost_acc::variance(epoch_callback_stats),
                                  std::memory_order_relaxed);
  }

  void
  publish_quiescent_states_per_thread_between_epoch_change_stats() noexcept {
    quiescent_states_per_thread_between_epoch_change_mean.store(
        boost_acc::mean(quiescent_states_per_thread_between_epoch_change_stats),
        std::memory_order_relaxed);
  }

  std::atomic<qsbr_state::type> state;

  // TODO(laurynas): absolute scalability bottleneck
  mutable std::shared_mutex qsbr_rwlock;

  // Protected by qsbr_rwlock
  std::vector<deallocation_request> previous_interval_deallocation_requests;

  // Protected by qsbr_rwlock
  std::vector<deallocation_request> current_interval_deallocation_requests;

  // TODO(laurynas): atomic but mostly manipulated in qsbr_rwlock critical
  // sections. See if can move it out.
  std::atomic<std::uint64_t> previous_interval_total_dealloc_size{};
  std::atomic<std::uint64_t> current_interval_total_dealloc_size{};

  std::atomic<std::uint64_t> epoch_change_count;

  // TODO(laurynas): more interesting callback stats?
  boost_acc::accumulator_set<
      std::size_t,
      boost_acc::stats<boost_acc::tag::max, boost_acc::tag::variance>>
      epoch_callback_stats;
  std::atomic<std::size_t> epoch_callback_max;
  std::atomic<double> epoch_callback_variance;

  boost_acc::accumulator_set<
      std::uint64_t,
      boost_acc::stats<boost_acc::tag::max, boost_acc::tag::variance>>
      deallocation_size_stats;
  std::atomic<std::uint64_t> deallocation_size_max;
  std::atomic<double> deallocation_size_mean;
  std::atomic<double> deallocation_size_variance;

  std::mutex quiescent_states_per_thread_between_epoch_change_lock;
  boost_acc::accumulator_set<std::uint64_t,
                             boost_acc::stats<boost_acc::tag::mean>>
      quiescent_states_per_thread_between_epoch_change_stats;
  std::atomic<double> quiescent_states_per_thread_between_epoch_change_mean;
};

static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);

inline qsbr_per_thread::qsbr_per_thread() noexcept
    : last_seen_epoch{qsbr::instance().register_thread()} {
  UNODB_DETAIL_ASSERT(paused);
  paused = false;
}

inline void qsbr_per_thread::quiescent() noexcept {
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());

  const auto current_global_epoch =
      qsbr_state::get_epoch(qsbr::instance().get_state());

  if (current_global_epoch != last_seen_epoch) {
    UNODB_DETAIL_ASSERT(current_global_epoch == last_seen_epoch.next());

    last_seen_epoch = current_global_epoch;
    qsbr::instance().register_quiescent_states_per_thread_between_epoch_changes(
        quiescent_states_since_epoch_change);
    quiescent_states_since_epoch_change = 0;
  }

  UNODB_DETAIL_ASSERT(current_global_epoch == last_seen_epoch);
  if (quiescent_states_since_epoch_change == 0) {
    const auto new_global_epoch =
        qsbr::instance().remove_thread_from_previous_epoch(current_global_epoch
#ifndef NDEBUG
                                                           ,
                                                           last_seen_epoch
#endif
        );
    UNODB_DETAIL_ASSERT(new_global_epoch == last_seen_epoch ||
                        new_global_epoch == last_seen_epoch.next());

    if (new_global_epoch != last_seen_epoch) {
      last_seen_epoch = new_global_epoch;
      qsbr::instance()
          .register_quiescent_states_per_thread_between_epoch_changes(1);
      quiescent_states_since_epoch_change = 0;
      return;
    }
  }
  ++quiescent_states_since_epoch_change;
}

inline void qsbr_per_thread::qsbr_pause() {
#ifndef NDEBUG
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());
#endif
  qsbr::instance().unregister_thread(quiescent_states_since_epoch_change,
                                     last_seen_epoch);
  paused = true;
}

inline void qsbr_per_thread::qsbr_resume() {
  UNODB_DETAIL_ASSERT(paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());
  last_seen_epoch = qsbr::instance().register_thread();
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

  ~quiescent_state_on_scope_exit() noexcept { this_thread().quiescent(); }
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

  qsbr_thread(qsbr_thread &&other) noexcept
      : std::thread{std::move(static_cast<std::thread &&>(other))} {}
  qsbr_thread(const qsbr_thread &) = delete;

  qsbr_thread &operator=(qsbr_thread &&other) noexcept {
    thread::operator=(std::move(static_cast<std::thread &&>(other)));
    return *this;
  }

  ~qsbr_thread() = default;

  qsbr_thread &operator=(const qsbr_thread &) = delete;

  template <typename Function, typename... Args,
            class = std::enable_if_t<
                !std::is_same_v<remove_cvref_t<Function>, qsbr_thread>>>
  explicit qsbr_thread(Function &&f, Args &&...args) noexcept
      : std::thread{[](auto &&f2, auto &&...args2) noexcept {
                      construct_current_thread_reclamator();
                      f2(std::forward<Args>(args2)...);
                    },
                    std::forward<Function>(f), std::forward<Args>(args)...} {}
};

}  // namespace unodb

#endif  // UNODB_DETAIL_QSBR_HPP
