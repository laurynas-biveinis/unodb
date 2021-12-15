// Copyright (C) 2019-2021 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_HPP
#define UNODB_DETAIL_QSBR_HPP

#include "global.hpp"

#include <array>
#include <atomic>
#include <cstddef>  // IWYU pragma: keep
#include <cstdint>
#ifndef NDEBUG
#include <functional>
#endif
#include <iostream>
#include <mutex>
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
class qsbr_epoch final {
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

  friend std::ostream &operator<<(std::ostream &os, qsbr_epoch value);

 private:
  static constexpr auto max_count = max + 1U;
  static_assert((max_count & (max_count - 1U)) == 0);

  constexpr void assert_invariant() const noexcept {
    UNODB_DETAIL_ASSERT(epoch_val <= max);
  }

  epoch_type epoch_val;
};

// LCOV_EXCL_START
[[gnu::cold, gnu::noinline]] inline std::ostream &operator<<(std::ostream &os,
                                                             qsbr_epoch value) {
  os << "epoch = " << static_cast<std::uint64_t>(value.epoch_val);
  value.assert_invariant();
  return os;
}
// LCOV_EXCL_STOP

// The maximum allowed QSBR-managed thread count is 2^30-1, should be enough for
// everybody, let's not even bother checking the limit in the Release
// configuration
using qsbr_thread_count_type = std::uint32_t;

inline constexpr qsbr_thread_count_type max_qsbr_threads = (2UL << 30U) - 1U;

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
      if (UNODB_DETAIL_UNLIKELY(single_thread_mode())) {
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

  [[nodiscard]] qsbr_epoch get_current_epoch() const noexcept {
    return current_epoch.load(std::memory_order_acquire);
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
  [[nodiscard]] bool single_thread_mode() const noexcept {
    return single_thread_mode(number_of_threads());
  }

  [[nodiscard]] qsbr_thread_count_type number_of_threads() const noexcept {
    const auto result = thread_count.load(std::memory_order_acquire);
    UNODB_DETAIL_ASSERT(result <= max_qsbr_threads);
    return result;
  }

  [[nodiscard]] auto previous_interval_size() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return previous_interval_deallocation_requests.size();
  }

  [[nodiscard]] auto current_interval_size() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return current_interval_deallocation_requests.size();
  }

  [[nodiscard]] auto get_threads_in_previous_epoch() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    // Not correct but will go away in the lock-free QSBR
    UNODB_DETAIL_ASSERT(threads_in_previous_epoch <= max_qsbr_threads + 1);
    return threads_in_previous_epoch;
  }

  void assert_idle() const noexcept {
#ifndef NDEBUG
    std::shared_lock guard{qsbr_rwlock};
    assert_idle_locked();
#endif
  }

 private:
  void assert_idle_locked() const noexcept {
#ifndef NDEBUG
    assert_invariants_locked();
    // Copy-paste-tweak with expect_idle_qsbr, but not clear how to fix this:
    // here we are asserting over internals, over there we are using Google Test
    // EXPECT macros with the public interface.
    UNODB_DETAIL_ASSERT(previous_interval_deallocation_requests.empty());
    UNODB_DETAIL_ASSERT(previous_interval_total_dealloc_size.load(
                            std::memory_order_relaxed) == 0);
    UNODB_DETAIL_ASSERT(current_interval_deallocation_requests.empty());
    UNODB_DETAIL_ASSERT(current_interval_total_dealloc_size.load(
                            std::memory_order_relaxed) == 0);
#endif
  }

  struct [[nodiscard]] deallocation_request final {
    void *const pointer;

#ifndef NDEBUG
    dealloc_debug_callback dealloc_callback;
    const qsbr_epoch request_epoch{qsbr::instance().get_current_epoch_locked()};
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
      UNODB_DETAIL_ASSERT(dealloc_epoch == request_epoch.next().next() ||
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

  // May be used to block epoch advance by ensuring threads_in_previous_epoch
  // cannot reach zero until a balanced decrement
  void inc_threads_in_previous_epoch() noexcept {
    std::lock_guard guard{qsbr_rwlock};

    assert_invariants_locked();

    ++threads_in_previous_epoch;
    // Not correct but will go away in the lock-free QSBR
    UNODB_DETAIL_ASSERT(threads_in_previous_epoch <= max_qsbr_threads + 1);

    assert_invariants_locked();
  }

  [[nodiscard]] static bool single_thread_mode(
      qsbr_thread_count_type thread_count) noexcept {
    UNODB_DETAIL_ASSERT(thread_count <= max_qsbr_threads);
    return thread_count < 2;
  }

  [[nodiscard]] qsbr_epoch get_current_epoch_locked() const noexcept {
    return current_epoch.load(std::memory_order_relaxed);
  }

  [[nodiscard]] qsbr_epoch remove_thread_from_previous_epoch_locked(
      qsbr_epoch current_global_epoch, deferred_requests &requests) noexcept;

  qsbr_epoch change_epoch(qsbr_epoch current_global_epoch,
                          qsbr_thread_count_type old_thread_count,
                          deferred_requests &requests) noexcept;

  void assert_invariants_locked() const noexcept {
#ifndef NDEBUG
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
      ) noexcept {
    return deferred_requests{
#ifndef NDEBUG
        dealloc_epoch, single_thread_mode
#endif
    };
  }

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

  std::atomic<qsbr_thread_count_type> thread_count;

  // TODO(laurynas): absolute scalability bottleneck
  mutable std::shared_mutex qsbr_rwlock;

  // Updated in qsbr_rwlock critical section with the rest of QSBR data
  // structures
  std::atomic<qsbr_epoch> current_epoch{qsbr_epoch{0}};

  // Protected by qsbr_rwlock
  std::vector<deallocation_request> previous_interval_deallocation_requests;

  // Protected by qsbr_rwlock
  std::vector<deallocation_request> current_interval_deallocation_requests;

  // Protected by qsbr_rwlock
  qsbr_thread_count_type threads_in_previous_epoch{0};

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

  const auto current_global_epoch = qsbr::instance().get_current_epoch();

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
