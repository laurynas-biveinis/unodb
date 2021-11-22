// Copyright (C) 2019-2021 Laurynas Biveinis

#include "global.hpp"

#include <atomic>
#ifdef NDEBUG
#include <cstdlib>
#endif
#include <thread>

#include "qsbr.hpp"

namespace {

[[gnu::constructor]] void run_tls_ctor_in_main_thread() {
  unodb::construct_current_thread_reclamator();
}

}  // namespace

namespace unodb {

#ifndef NDEBUG

void qsbr_per_thread::register_active_ptr(const void *ptr) {
  UNODB_DETAIL_ASSERT(ptr != nullptr);
  UNODB_DETAIL_ASSERT(!is_qsbr_paused());

  active_ptrs.insert(ptr);
}

void qsbr_per_thread::unregister_active_ptr(const void *ptr) {
  UNODB_DETAIL_ASSERT(ptr != nullptr);
  UNODB_DETAIL_ASSERT(!is_qsbr_paused());

  const auto itr = active_ptrs.find(ptr);
  UNODB_DETAIL_ASSERT(itr != active_ptrs.end());
  active_ptrs.erase(itr);
}

#endif  // !NDEBUG

qsbr_epoch qsbr::register_thread() noexcept {
  std::lock_guard guard{qsbr_rwlock};
#ifndef NDEBUG
  thread_count_changed_in_current_epoch = true;
  assert_invariants();
#endif

  ++thread_count;
  ++threads_in_previous_epoch;
  return current_epoch;
}

void qsbr::unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                             qsbr_epoch thread_epoch) noexcept {
  register_quiescent_states_per_thread_between_epoch_changes(
      quiescent_states_since_epoch_change);
  deferred_requests requests_to_deallocate;
  {
    std::lock_guard guard{qsbr_rwlock};

#ifndef NDEBUG
    UNODB_DETAIL_ASSERT(thread_epoch == current_epoch ||
                        thread_epoch + 1 == current_epoch);
    thread_count_changed_in_current_epoch = true;
#endif

    const auto epoch_changed =
        ((thread_epoch + 1 == current_epoch) ||
         (quiescent_states_since_epoch_change == 0))
            ? remove_thread_from_previous_epoch_locked(requests_to_deallocate
#ifndef NDEBUG
                                                       ,
                                                       thread_epoch
#endif
                                                       )
            : false;

    --thread_count;
    if (epoch_changed) {
      // The epoch change marked this thread as not-quiescent again, and
      // included it in threads_in_previous_epoch
      --threads_in_previous_epoch;
    }
#ifndef NDEBUG
    requests_to_deallocate.update_single_thread_mode();
    single_threaded_mode_start_epoch = get_current_epoch_locked();
#endif

    // If we became single-threaded, we still cannot deallocate neither previous
    // nor current interval requests immediately. We could track the
    // deallocating thread in the request structure and deallocate the ones
    // coming from the sole live thread, but not sure whether that would be a
    // good trade-off.
    // Any new deallocation requests from this point on can be executed
    // immediately.

    assert_invariants();
  }
}

void qsbr::reset_stats() noexcept {
#ifndef NDEBUG
  {
    // Stats can only be reset on idle QSBR - best-effort check due to different
    // locks required
    std::lock_guard guard{qsbr_rwlock};
    assert_idle_locked();
  }
#endif
  std::lock_guard guard{stats_rwlock};

  deallocation_size_stats = {};
  epoch_callback_stats = {};
  quiescent_states_per_thread_between_epoch_change_stats = {};
}

void qsbr::dump(std::ostream &out) const {
  // TODO(laurynas): locking? anyone using it all?
  out << "QSBR status:\n";
  out << "Previous interval pending deallocation requests: "
      << previous_interval_deallocation_requests.size() << '\n';
  out << "Previous interval pending deallocation bytes: "
      << previous_interval_total_dealloc_size.load(std::memory_order_relaxed);
  out << "Current interval pending deallocation requests: "
      << current_interval_deallocation_requests.size() << '\n';
  out << "Current interval pending deallocation bytes: "
      << current_interval_total_dealloc_size.load(std::memory_order_relaxed);
  out << "Number of tracked threads: " << thread_count << '\n';
  out << "Number of threads in the previous epoch = "
      << threads_in_previous_epoch << '\n';
}

bool qsbr::remove_thread_from_previous_epoch(
#ifndef NDEBUG
    qsbr_epoch thread_epoch
#endif
    ) noexcept {
  deferred_requests to_deallocate;
  bool epoch_changed = false;
  {
    std::lock_guard guard{qsbr_rwlock};

    UNODB_DETAIL_ASSERT(thread_epoch == current_epoch ||
                        thread_epoch + 1 == current_epoch);

    epoch_changed = remove_thread_from_previous_epoch_locked(to_deallocate
#ifndef NDEBUG
                                                             ,
                                                             thread_epoch
#endif
    );
  }
  return epoch_changed;
}

bool qsbr::remove_thread_from_previous_epoch_locked(
    qsbr::deferred_requests &requests
#ifndef NDEBUG
    ,
    qsbr_epoch thread_epoch
#endif
    ) noexcept {
  UNODB_DETAIL_ASSERT(thread_epoch == current_epoch ||
                      thread_epoch + 1 == current_epoch);
  assert_invariants();

  UNODB_DETAIL_ASSERT(threads_in_previous_epoch > 0);
  --threads_in_previous_epoch;

  if (threads_in_previous_epoch > 0) return false;

  requests = change_epoch();

  UNODB_DETAIL_ASSERT(thread_epoch + 1 == current_epoch ||
                      thread_epoch + 2 == current_epoch);
  return true;
}

qsbr::deferred_requests qsbr::change_epoch() noexcept {
  ++current_epoch;

  {
    // TODO(laurynas): consider saving the update values and actually updating
    // the stats after qsbr_rwlock is released
    std::lock_guard guard{stats_rwlock};
    deallocation_size_stats(
        current_interval_total_dealloc_size.load(std::memory_order_relaxed));
    epoch_callback_stats(previous_interval_deallocation_requests.size());
  }

  deferred_requests result{make_deferred_requests()};
  result.requests[0] = std::move(previous_interval_deallocation_requests);

  if (UNODB_DETAIL_LIKELY(!single_thread_mode_locked())) {
    previous_interval_deallocation_requests =
        std::move(current_interval_deallocation_requests);
    previous_interval_total_dealloc_size.store(
        current_interval_total_dealloc_size.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
  } else {
    previous_interval_deallocation_requests.clear();
    previous_interval_total_dealloc_size.store(0, std::memory_order_relaxed);
    result.requests[1] = std::move(current_interval_deallocation_requests);
  }
  current_interval_deallocation_requests.clear();
  current_interval_total_dealloc_size.store(0, std::memory_order_relaxed);

#ifndef NDEBUG
  thread_count_changed_in_previous_epoch =
      thread_count_changed_in_current_epoch;
  thread_count_changed_in_current_epoch = false;
#endif

  threads_in_previous_epoch = thread_count;
  return result;
}

void qsbr::assert_invariants() const noexcept {
#ifndef NDEBUG
  UNODB_DETAIL_ASSERT(threads_in_previous_epoch <= thread_count);
  if (previous_interval_deallocation_requests.empty()) {
    UNODB_DETAIL_ASSERT(previous_interval_total_dealloc_size.load(
                            std::memory_order_relaxed) == 0);
  }

  if (current_interval_deallocation_requests.empty()) {
    UNODB_DETAIL_ASSERT(current_interval_total_dealloc_size.load(
                            std::memory_order_relaxed) == 0);
  }

  if (single_thread_mode_locked() &&
      get_current_epoch_locked() > single_threaded_mode_start_epoch)
    UNODB_DETAIL_ASSERT(previous_interval_deallocation_requests.empty());
#endif
}

}  // namespace unodb
