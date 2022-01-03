// Copyright (C) 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_HPP
#define UNODB_DETAIL_QSBR_HPP

#include "global.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#ifndef NDEBUG
#include <functional>  // IWYU pragma: keep
#endif
#include <iostream>
#include <mutex>  // IWYU pragma: keep
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

namespace detail {

struct [[nodiscard]] deallocation_request final {
  void *const pointer;

#ifndef NDEBUG
  using debug_callback = std::function<void(const void *)>;

  // Non-const to support move
  debug_callback dealloc_callback;
  qsbr_epoch request_epoch;
#endif

  explicit deallocation_request(void *pointer_
#ifndef NDEBUG
                                ,
                                qsbr_epoch request_epoch_,
                                debug_callback dealloc_callback_
#endif
                                ) noexcept
      : pointer {
    pointer_
  }
#ifndef NDEBUG
  , dealloc_callback{std::move(dealloc_callback_)}, request_epoch {
    request_epoch_
  }
#endif
  {}

  void deallocate(
#ifndef NDEBUG
      qsbr_epoch dealloc_epoch, bool dealloc_epoch_single_thread_mode
#endif
  ) const noexcept;
};

using dealloc_request_vector = std::vector<deallocation_request>;

class [[nodiscard]] deferred_requests final {
 public:
  UNODB_DETAIL_RELEASE_EXPLICIT deferred_requests(
      std::unique_ptr<dealloc_request_vector> &&requests_
#ifndef NDEBUG
      ,
      qsbr_epoch request_epoch_, bool dealloc_epoch_single_thread_mode_
#endif
      ) noexcept
      : requests {
    std::move(requests_)
  }
#ifndef NDEBUG
  , dealloc_epoch{request_epoch_}, dealloc_epoch_single_thread_mode {
    dealloc_epoch_single_thread_mode_
  }
#endif
  {}

  deferred_requests(const deferred_requests &) noexcept = delete;
  deferred_requests(deferred_requests &&) noexcept = delete;
  deferred_requests &operator=(const deferred_requests &) noexcept = delete;
  deferred_requests &operator=(deferred_requests &&) noexcept = delete;

  ~deferred_requests() {
    if (requests == nullptr) return;
    for (const auto &dealloc_request : *requests) {
      dealloc_request.deallocate(
#ifndef NDEBUG
          dealloc_epoch, dealloc_epoch_single_thread_mode
#endif
      );
    }
  }

 private:
  std::unique_ptr<dealloc_request_vector> requests;

#ifndef NDEBUG
  qsbr_epoch dealloc_epoch;
  bool dealloc_epoch_single_thread_mode;
#endif
};

}  // namespace detail

class [[nodiscard]] qsbr_per_thread final {
 public:
  qsbr_per_thread() noexcept;

  ~qsbr_per_thread() {
    if (!is_qsbr_paused()) qsbr_pause();
  }

  void on_next_epoch_deallocate(
      void *pointer, std::size_t size
#ifndef NDEBUG
      ,
      detail::deallocation_request::debug_callback dealloc_callback
#endif
  );

  void quiescent() noexcept;

  void qsbr_pause();

  void qsbr_resume();

  [[nodiscard, gnu::pure]] bool is_qsbr_paused() const noexcept {
    return paused;
  }

  [[nodiscard]] bool previous_interval_requests_empty() const noexcept {
    return previous_interval_dealloc_requests->empty();
  }

  [[nodiscard]] bool current_interval_requests_empty() const noexcept {
    return current_interval_dealloc_requests->empty();
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
  friend class qsbr;

  std::uint64_t quiescent_states_since_epoch_change{0};
  qsbr_epoch last_seen_quiescent_state_epoch;

  qsbr_epoch last_seen_epoch;

  std::unique_ptr<detail::dealloc_request_vector>
      previous_interval_dealloc_requests;

  std::unique_ptr<detail::dealloc_request_vector>
      current_interval_dealloc_requests;
  std::size_t current_interval_total_dealloc_size{0};

  bool paused{true};

  void advance_last_seen_epoch(bool single_thread_mode,
                               qsbr_epoch new_seen_epoch) noexcept;

  void update_requests(bool single_thread_mode,
                       qsbr_epoch dealloc_epoch) noexcept;

  void orphan_deferred_requests() noexcept;

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

namespace detail {

struct dealloc_vector_list_node;

}  // namespace detail

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

 public:
  [[nodiscard]] qsbr_epoch remove_thread_from_previous_epoch(
      qsbr_epoch current_global_epoch
#ifndef NDEBUG
      ,
      qsbr_epoch thread_epoch
#endif
      ) noexcept;

  [[nodiscard]] qsbr_epoch register_thread() noexcept;

  void unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                         qsbr_epoch thread_epoch,
                         qsbr_per_thread &qsbr_thread) noexcept;

  void reset_stats() noexcept;

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &out) const;

  void register_quiescent_states_per_thread_between_epoch_changes(
      std::uint64_t states) noexcept {
    std::lock_guard guard{quiescent_state_stats_lock};
    quiescent_states_per_thread_between_epoch_change_stats(states);
    publish_quiescent_states_per_thread_between_epoch_change_stats();
  }

  void register_dealloc_stats_per_thread_between_epoch_changes(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      std::size_t total_size, std::size_t count) noexcept {
    std::lock_guard guard{dealloc_stats_lock};
    deallocation_size_per_thread_stats(total_size);
    publish_deallocation_size_stats();
    epoch_dealloc_per_thread_count_stats(count);
    publish_epoch_callback_stats();
  }

  [[nodiscard]] auto get_epoch_callback_count_max() const noexcept {
    return epoch_dealloc_per_thread_count_max.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto get_epoch_callback_count_variance() const noexcept {
    return epoch_dealloc_per_thread_count_variance.load(
        std::memory_order_acquire);
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
    return deallocation_size_per_thread_max.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto get_mean_backlog_bytes() const noexcept {
    return deallocation_size_per_thread_mean.load(std::memory_order_acquire);
  }

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
    UNODB_DETAIL_ASSERT(previous_interval_orphaned_requests_empty());
    UNODB_DETAIL_ASSERT(current_interval_orphaned_requests_empty());
#endif
  }

 private:
  qsbr() noexcept = default;

  ~qsbr() noexcept { assert_idle(); }

  static void thread_epoch_change_barrier() noexcept;

  void epoch_change_update_requests(bool single_thread_mode
#ifndef NDEBUG
                                    ,
                                    qsbr_epoch dealloc_epoch
#endif
                                    ) noexcept;

  qsbr_epoch change_epoch(qsbr_epoch current_global_epoch,
                          bool single_thread_mode) noexcept;

  void publish_deallocation_size_stats() noexcept {
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

  void publish_epoch_callback_stats() noexcept {
    epoch_dealloc_per_thread_count_max.store(
        boost_acc::max(epoch_dealloc_per_thread_count_stats),
        std::memory_order_relaxed);
    epoch_dealloc_per_thread_count_variance.store(
        boost_acc::variance(epoch_dealloc_per_thread_count_stats),
        std::memory_order_relaxed);
  }

  void
  publish_quiescent_states_per_thread_between_epoch_change_stats() noexcept {
    quiescent_states_per_thread_between_epoch_change_mean.store(
        boost_acc::mean(quiescent_states_per_thread_between_epoch_change_stats),
        std::memory_order_relaxed);
  }

  alignas(detail::hardware_destructive_interference_size)
      std::atomic<qsbr_state::type> state;

  std::atomic<std::uint64_t> epoch_change_count;

  std::atomic<detail::dealloc_vector_list_node *>
      orphaned_previous_interval_dealloc_requests;

  std::atomic<detail::dealloc_vector_list_node *>
      orphaned_current_interval_dealloc_requests;

  static_assert(sizeof(state) + sizeof(epoch_change_count) +
                    sizeof(orphaned_previous_interval_dealloc_requests) +
                    sizeof(orphaned_current_interval_dealloc_requests) <=
                detail::hardware_constructive_interference_size);

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
};

static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);

// cppcheck-suppress uninitMemberVar
inline qsbr_per_thread::qsbr_per_thread() noexcept
    : last_seen_quiescent_state_epoch{qsbr::instance().register_thread()},
      last_seen_epoch{last_seen_quiescent_state_epoch},
      previous_interval_dealloc_requests{
          std::make_unique<detail::dealloc_request_vector>()},
      current_interval_dealloc_requests{
          std::make_unique<detail::dealloc_request_vector>()} {
  UNODB_DETAIL_ASSERT(paused);
  paused = false;
}

inline void qsbr_per_thread::on_next_epoch_deallocate(
    void *pointer, std::size_t size
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

  advance_last_seen_epoch(single_thread_mode, current_global_epoch);

  if (UNODB_DETAIL_UNLIKELY(single_thread_mode)) {
    qsbr::deallocate(pointer
#ifndef NDEBUG
                     ,
                     dealloc_callback
#endif
    );
    return;
  }

  current_interval_total_dealloc_size += size;

  current_interval_dealloc_requests->emplace_back(pointer
#ifndef NDEBUG
                                                  ,
                                                  last_seen_epoch,
                                                  std::move(dealloc_callback)
#endif
  );
}

inline void qsbr_per_thread::advance_last_seen_epoch(
    bool single_thread_mode, qsbr_epoch new_seen_epoch) noexcept {
  if (new_seen_epoch == last_seen_epoch) return;

  UNODB_DETAIL_ASSERT(new_seen_epoch == last_seen_epoch.next()
                      // The current thread is 1) quitting; 2) not having seen
                      // the current epoch yet; 3) it quitting will cause an
                      // epoch advance
                      || new_seen_epoch == last_seen_epoch.next().next());
  update_requests(single_thread_mode, new_seen_epoch);
}

inline void qsbr_per_thread::update_requests(
    bool single_thread_mode, qsbr_epoch dealloc_epoch) noexcept {
  last_seen_epoch = dealloc_epoch;

  detail::deferred_requests requests_to_deallocate{
      std::move(previous_interval_dealloc_requests)
#ifndef NDEBUG
          ,
      dealloc_epoch, single_thread_mode
#endif
  };

  qsbr::instance().register_dealloc_stats_per_thread_between_epoch_changes(
      current_interval_total_dealloc_size,
      current_interval_dealloc_requests->size());

  current_interval_total_dealloc_size = 0;

  if (UNODB_DETAIL_LIKELY(!single_thread_mode)) {
    previous_interval_dealloc_requests =
        std::move(current_interval_dealloc_requests);
  } else {
    previous_interval_dealloc_requests =
        std::make_unique<detail::dealloc_request_vector>();
    detail::deferred_requests additional_requests_to_deallocate{
        std::move(current_interval_dealloc_requests)
#ifndef NDEBUG
            ,
        dealloc_epoch, single_thread_mode
#endif
    };
  }
  current_interval_dealloc_requests =
      std::make_unique<detail::dealloc_request_vector>();
}

inline void qsbr_per_thread::quiescent() noexcept {
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());

  const auto state = qsbr::instance().get_state();
  const auto current_global_epoch = qsbr_state::get_epoch(state);
  const auto single_thread_mode = qsbr_state::single_thread_mode(state);

  advance_last_seen_epoch(single_thread_mode, current_global_epoch);

  if (current_global_epoch != last_seen_quiescent_state_epoch) {
    UNODB_DETAIL_ASSERT(current_global_epoch ==
                        last_seen_quiescent_state_epoch.next());

    last_seen_quiescent_state_epoch = current_global_epoch;
    qsbr::instance().register_quiescent_states_per_thread_between_epoch_changes(
        quiescent_states_since_epoch_change);
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
                            last_seen_quiescent_state_epoch.next());

    if (new_global_epoch != last_seen_quiescent_state_epoch) {
      last_seen_quiescent_state_epoch = new_global_epoch;

      UNODB_DETAIL_ASSERT(last_seen_epoch.next() == new_global_epoch);
      update_requests(qsbr_state::single_thread_mode(state), new_global_epoch);

      qsbr::instance()
          .register_quiescent_states_per_thread_between_epoch_changes(1);
      quiescent_states_since_epoch_change = 0;
      return;
    }
  }
  ++quiescent_states_since_epoch_change;
}

namespace detail {

inline void deallocation_request::deallocate(
#ifndef NDEBUG
    qsbr_epoch dealloc_epoch, bool dealloc_epoch_single_thread_mode
#endif
) const noexcept {
  // TODO(laurynas): count deallocation request instances, assert 0 in QSBR dtor
  // The assert cannot be stricter due to epoch changes by unregister_thread,
  // which moves requests between intervals not atomically with the epoch
  // change.
  UNODB_DETAIL_ASSERT(dealloc_epoch == request_epoch.next() ||
                      dealloc_epoch == request_epoch.next().next() ||
                      (dealloc_epoch == request_epoch.next().next().next()) ||
                      (dealloc_epoch_single_thread_mode &&
                       dealloc_epoch == request_epoch.next()));

  qsbr::deallocate(pointer
#ifndef NDEBUG
                   ,
                   dealloc_callback
#endif
  );
}

}  // namespace detail

inline void qsbr_per_thread::qsbr_pause() {
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());

  qsbr::instance().unregister_thread(quiescent_states_since_epoch_change,
                                     last_seen_quiescent_state_epoch, *this);
  paused = true;

  UNODB_DETAIL_ASSERT(previous_interval_dealloc_requests == nullptr);
  UNODB_DETAIL_ASSERT(current_interval_dealloc_requests == nullptr);
}

inline void qsbr_per_thread::qsbr_resume() {
  UNODB_DETAIL_ASSERT(paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());
  UNODB_DETAIL_ASSERT(previous_interval_dealloc_requests == nullptr);
  UNODB_DETAIL_ASSERT(current_interval_dealloc_requests == nullptr);

  last_seen_quiescent_state_epoch = qsbr::instance().register_thread();
  last_seen_epoch = last_seen_quiescent_state_epoch;

  previous_interval_dealloc_requests =
      std::make_unique<detail::dealloc_request_vector>();
  current_interval_dealloc_requests =
      std::make_unique<detail::dealloc_request_vector>();
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
