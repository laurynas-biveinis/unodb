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
#include <shared_mutex>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>  // IWYU pragma: keep
#include <vector>

#include <boost/accumulators/accumulators.hpp>  // IWYU pragma: keep
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include "heap.hpp"
#ifndef NDEBUG
#include "thread_sync.hpp"
#endif

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

class [[nodiscard]] qsbr_per_thread final {
 public:
  qsbr_per_thread() noexcept;

  ~qsbr_per_thread() {
    if (!is_paused()) pause();
  }

  void quiescent_state() noexcept;

  void pause();

  void resume();

  [[nodiscard, gnu::pure]] bool is_paused() const noexcept { return paused; }

  qsbr_per_thread(const qsbr_per_thread &) = delete;
  qsbr_per_thread(qsbr_per_thread &&) = delete;
  qsbr_per_thread &operator=(const qsbr_per_thread &) = delete;
  qsbr_per_thread &operator=(qsbr_per_thread &&) = delete;

#ifndef NDEBUG
  void register_active_ptr(const void *ptr);
  void unregister_active_ptr(const void *ptr);
#endif

 private:
  std::thread::id thread_id{std::this_thread::get_id()};

  bool paused{true};

  std::uint64_t quiescent_states_since_epoch_change{0};
  std::uint64_t last_seen_epoch;

#ifndef NDEBUG
  std::unordered_multiset<const void *> active_ptrs;
#endif
};

[[nodiscard]] inline qsbr_per_thread &current_thread_reclamator() {
  thread_local static qsbr_per_thread current_thread_reclamator_instance;
  return current_thread_reclamator_instance;
}

inline void construct_current_thread_reclamator() {
  // An ODR-use ensures that the constructor gets called
  (void)current_thread_reclamator();
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
    UNODB_DETAIL_ASSERT(!unodb::current_thread_reclamator().is_paused());

    bool deallocate_immediately = false;
    {
      std::lock_guard guard{qsbr_rwlock};
      if (UNODB_DETAIL_UNLIKELY(single_thread_mode_locked())) {
        deallocate_immediately = true;
      } else {
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
      }
      assert_invariants();
    }
    if (deallocate_immediately)
      deallocate(pointer
#ifndef NDEBUG
                 ,
                 debug_callback
#endif
      );
  }

  inline void register_quiescent_states_per_thread_between_epoch_changes(
      std::uint64_t states) noexcept {
    std::lock_guard guard{stats_rwlock};
    quiescent_states_per_thread_between_epoch_change_stats(states);
  }

  [[nodiscard]] bool remove_thread_from_previous_epoch() noexcept;

  void prepare_new_thread();
  void register_prepared_thread(std::thread::id thread_id) noexcept;

  void register_new_thread(std::thread::id thread_id);

  void unregister_thread(std::thread::id thread_id);

  void reset_stats() noexcept;

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &out) const;

  [[nodiscard]] auto get_epoch_callback_count_max() const noexcept {
    std::shared_lock guard{stats_rwlock};
    return boost_acc::max(epoch_callback_stats);
  }

  [[nodiscard]] auto get_epoch_callback_count_variance() const noexcept {
    std::shared_lock guard{stats_rwlock};
    return boost_acc::variance(epoch_callback_stats);
  }

  [[nodiscard]] auto
  get_mean_quiescent_states_per_thread_between_epoch_changes() const noexcept {
    std::shared_lock guard{stats_rwlock};
    return boost_acc::mean(
        quiescent_states_per_thread_between_epoch_change_stats);
  }

  [[nodiscard]] std::uint64_t get_current_epoch() const noexcept {
    return current_epoch.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto get_max_backlog_bytes() const noexcept {
    std::shared_lock guard{stats_rwlock};
    return boost_acc::max(deallocation_size_stats);
  }

  [[nodiscard]] auto get_mean_backlog_bytes() const noexcept {
    std::shared_lock guard{stats_rwlock};
    return boost_acc::mean(deallocation_size_stats);
  }

  // Made public for tests and asserts
  [[nodiscard]] auto single_thread_mode() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return single_thread_mode_locked();
  }

  [[nodiscard]] auto number_of_threads() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return threads.size();
  }

  [[nodiscard]] auto previous_interval_size() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return previous_interval_deallocation_requests.size();
  }

  [[nodiscard]] auto current_interval_size() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return current_interval_deallocation_requests.size();
  }

  [[nodiscard]] auto get_reserved_thread_capacity() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
    return reserved_thread_capacity;
  }

  [[nodiscard]] auto get_threads_in_previous_epoch() const noexcept {
    std::shared_lock guard{qsbr_rwlock};
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
    assert_invariants();
    // Copy-paste-tweak with expect_idle_qsbr, but not clear how to fix this:
    // here we are asserting over internals, over there we are using Google Test
    // EXPECT macros with the public interface.
    UNODB_DETAIL_ASSERT(previous_interval_deallocation_requests.empty());
    UNODB_DETAIL_ASSERT(previous_interval_total_dealloc_size.load(
                            std::memory_order_relaxed) == 0);
    UNODB_DETAIL_ASSERT(current_interval_deallocation_requests.empty());
    UNODB_DETAIL_ASSERT(current_interval_total_dealloc_size.load(
                            std::memory_order_relaxed) == 0);
    if (threads.empty()) {
      UNODB_DETAIL_ASSERT(reserved_thread_capacity == 0);
      UNODB_DETAIL_ASSERT(threads_in_previous_epoch == 0);
    } else if (threads.size() == 1) {
      UNODB_DETAIL_ASSERT(reserved_thread_capacity == 1);
      UNODB_DETAIL_ASSERT(threads_in_previous_epoch == 1);
    } else {
      UNODB_DETAIL_ASSERT(threads.size() < 2);
    }
#endif
  }

  struct [[nodiscard]] deallocation_request final {
    void *const pointer;

#ifndef NDEBUG
    dealloc_debug_callback dealloc_callback;
    const std::uint64_t request_epoch{qsbr::instance().get_current_epoch()};
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
        std::uint64_t dealloc_epoch, bool dealloc_epoch_single_thread_mode,
        const dealloc_debug_callback &debug_callback
#endif
    ) const noexcept {
      // TODO(laurynas): count deallocation request instances, assert 0 in QSBR
      // dtor
      UNODB_DETAIL_ASSERT(dealloc_epoch == request_epoch + 2 ||
                          (dealloc_epoch_single_thread_mode &&
                           dealloc_epoch == request_epoch + 1));

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
    deferred_requests(std::uint64_t request_epoch_,
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

#ifndef NDEBUG
    // TODO(laurynas): get rid of this wart
    void update_single_thread_mode() noexcept {
      dealloc_epoch_single_thread_mode =
          qsbr::instance().single_thread_mode_locked();
    }
#endif

   private:
#ifndef NDEBUG
    std::uint64_t dealloc_epoch;
    bool dealloc_epoch_single_thread_mode;
#endif
  };

  qsbr() noexcept = default;

  ~qsbr() noexcept { assert_idle(); }

  [[nodiscard]] bool single_thread_mode_locked() const noexcept {
    return threads.size() < 2;
  }

  void prepare_new_thread_locked();
  void register_prepared_thread_locked(std::thread::id thread_id) noexcept;

  [[nodiscard]] bool remove_thread_from_previous_epoch_locked(
      deferred_requests &requests) noexcept;

  [[nodiscard]] deferred_requests change_epoch() noexcept;

  void assert_invariants() const noexcept;

  [[nodiscard]] deferred_requests make_deferred_requests() const noexcept {
    return deferred_requests{
#ifndef NDEBUG
        get_current_epoch(), single_thread_mode_locked()
#endif
    };
  }

  std::atomic<std::uint64_t> current_epoch{0};

  // TODO(laurynas): absolute scalability bottleneck
  mutable std::shared_mutex qsbr_rwlock;

  // Protected by qsbr_rwlock
  std::vector<deallocation_request> previous_interval_deallocation_requests;

  // Protected by qsbr_rwlock
  std::vector<deallocation_request> current_interval_deallocation_requests;

  // Protected by qsbr_rwlock
  std::unordered_set<std::thread::id> threads;

  // Protected by qsbr_rwlock
  std::size_t reserved_thread_capacity{1};

  // Protected by qsbr_rwlock
  std::size_t threads_in_previous_epoch{0};

#ifndef NDEBUG
  // Protected by qsbr_rwlock
  std::uint64_t single_threaded_mode_start_epoch{0};

  // Protected by qsbr_rwlock
  bool thread_count_changed_in_current_epoch{false};

  // Protected by qsbr_rwlock
  bool thread_count_changed_in_previous_epoch{false};
#endif

  // FIXME(laurynas): currently used in qsbr_rwlock critical sections
  std::atomic<std::uint64_t> previous_interval_total_dealloc_size{};
  std::atomic<std::uint64_t> current_interval_total_dealloc_size{};

  mutable std::shared_mutex stats_rwlock;

  // TODO(laurynas): more interesting callback stats?
  // Protected by stats_rwlock
  boost_acc::accumulator_set<
      std::size_t,
      boost_acc::stats<boost_acc::tag::max, boost_acc::tag::variance>>
      epoch_callback_stats;

  // Protected by stats_rwlock
  boost_acc::accumulator_set<
      std::uint64_t,
      boost_acc::stats<boost_acc::tag::max, boost_acc::tag::variance>>
      deallocation_size_stats;

  // Protected by stats_rwlock
  boost_acc::accumulator_set<std::uint64_t,
                             boost_acc::stats<boost_acc::tag::mean>>
      quiescent_states_per_thread_between_epoch_change_stats;
};

inline qsbr_per_thread::qsbr_per_thread() noexcept
    : last_seen_epoch{qsbr::instance().get_current_epoch()} {
  UNODB_DETAIL_ASSERT(paused);
  qsbr::instance().register_prepared_thread(thread_id);
  paused = false;
}

inline void qsbr_per_thread::quiescent_state() noexcept {
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());

  const auto current_global_epoch = qsbr::instance().get_current_epoch();
  UNODB_DETAIL_ASSERT(current_global_epoch >= last_seen_epoch);

  if (current_global_epoch > last_seen_epoch) {
    last_seen_epoch = current_global_epoch;
    qsbr::instance().register_quiescent_states_per_thread_between_epoch_changes(
        quiescent_states_since_epoch_change);
    quiescent_states_since_epoch_change = 0;
  }

  UNODB_DETAIL_ASSERT(current_global_epoch == last_seen_epoch);
  if (quiescent_states_since_epoch_change == 0) {
    if (const auto epoch_changed{
            qsbr::instance().remove_thread_from_previous_epoch()};
        epoch_changed) {
      last_seen_epoch = current_global_epoch + 1;
      qsbr::instance()
          .register_quiescent_states_per_thread_between_epoch_changes(1);
      quiescent_states_since_epoch_change = 0;
      return;
    }
  }
  ++quiescent_states_since_epoch_change;
}

inline void qsbr_per_thread::pause() {
  UNODB_DETAIL_ASSERT(!paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());
  qsbr::instance().unregister_thread(thread_id);
  paused = true;
}

inline void qsbr_per_thread::resume() {
  UNODB_DETAIL_ASSERT(paused);
  UNODB_DETAIL_ASSERT(active_ptrs.empty());
  qsbr::instance().register_new_thread(thread_id);
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

  ~quiescent_state_on_scope_exit() noexcept {
    current_thread_reclamator().quiescent_state();
  }
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
      : std::thread{(qsbr::instance().prepare_new_thread(),
                     [](auto &&f2, auto &&...args2) noexcept {
                       construct_current_thread_reclamator();
                       f2(std::forward<Args>(args2)...);
                     }),
                    std::forward<Function>(f), std::forward<Args>(args)...} {}

#ifndef NDEBUG
  template <typename Function, typename... Args,
            class = std::enable_if_t<
                !std::is_same_v<remove_cvref_t<Function>, qsbr_thread>>>
  qsbr_thread(detail::thread_sync &notify_point,
              detail::thread_sync &wait_point, Function &&f,
              Args &&...args) noexcept
      : std::thread{((void)qsbr::instance().prepare_new_thread(),
                     (void)notify_point.notify(), (void)wait_point.wait(),
                     [](auto &&f2, auto &&...args2) noexcept {
                       construct_current_thread_reclamator();
                       f2(std::forward<Args>(args2)...);
                     }),
                    std::forward<Function>(f), std::forward<Args>(args)...} {}
#endif  // NDEBUG
};

}  // namespace unodb

#endif  // UNODB_DETAIL_QSBR_HPP
