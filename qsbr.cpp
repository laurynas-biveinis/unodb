// Copyright (C) 2019-2021 Laurynas Biveinis

#include "global.hpp"

#include "qsbr.hpp"

#ifdef UNODB_DETAIL_THREAD_SANITIZER
#include <sanitizer/tsan_interface.h>
#endif

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

  assert_invariants_locked();

  ++thread_count;
  ++threads_in_previous_epoch;
  return get_current_epoch_locked();
}

void qsbr::unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                             qsbr_epoch thread_epoch) noexcept {
  register_quiescent_states_per_thread_between_epoch_changes(
      quiescent_states_since_epoch_change);
  deferred_requests requests_to_deallocate;
  {
    std::lock_guard guard{qsbr_rwlock};

    assert_invariants_locked();

    const auto current_global_epoch = get_current_epoch_locked();
    UNODB_DETAIL_ASSERT(thread_epoch == current_global_epoch ||
                        thread_epoch + 1 == current_global_epoch);

    const auto new_global_epoch =
        ((thread_epoch + 1 == current_global_epoch) ||
         (quiescent_states_since_epoch_change == 0))
            ? remove_thread_from_previous_epoch_locked(current_global_epoch,
                                                       requests_to_deallocate)
            : current_global_epoch;
    UNODB_DETAIL_ASSERT(current_global_epoch == new_global_epoch ||
                        current_global_epoch + 1 == new_global_epoch);

    --thread_count;

    if (current_global_epoch < new_global_epoch) {
      // The epoch change marked this thread as not-quiescent again, and
      // included it in threads_in_previous_epoch
      --threads_in_previous_epoch;
    }
#ifndef NDEBUG
    requests_to_deallocate.update_single_thread_mode();
#endif

    // If we became single-threaded, we still cannot deallocate neither previous
    // nor current interval requests immediately. We could track the
    // deallocating thread in the request structure and deallocate the ones
    // coming from the sole live thread, but not sure whether that would be a
    // good trade-off.
    // Any new deallocation requests from this point on can be executed
    // immediately.

    assert_invariants_locked();
  }
}

void qsbr::reset_stats() noexcept {
  {
    std::lock_guard guard{qsbr_rwlock};
    // Stats can only be reset on idle QSBR - best-effort check due to lock
    // release later
    assert_idle_locked();

    epoch_callback_stats = {};
    publish_epoch_callback_stats();

    deallocation_size_stats = {};
    publish_deallocation_size_stats();
  }

  std::lock_guard guard{quiescent_states_per_thread_between_epoch_change_lock};

  quiescent_states_per_thread_between_epoch_change_stats = {};
  publish_quiescent_states_per_thread_between_epoch_change_stats();
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

qsbr_epoch qsbr::remove_thread_from_previous_epoch(
    qsbr_epoch current_global_epoch
#ifndef NDEBUG
    ,
    qsbr_epoch thread_epoch
#endif
    ) noexcept {
  deferred_requests to_deallocate;
  qsbr_epoch result;

  // No loads and stores can be reordered past this point, or the quiescent
  // state contract would be violated
  std::atomic_thread_fence(std::memory_order_release);
#ifdef UNODB_DETAIL_THREAD_SANITIZER
  // I have no idea what I am doing
  __tsan_release(&qsbr_rwlock);
#endif

  {
#ifdef UNODB_DETAIL_THREAD_SANITIZER
    __tsan_acquire(&qsbr_rwlock);
#endif
    // Acquire synchronizes-with the release fence, OK to release the old
    // pointers.
    std::lock_guard guard{qsbr_rwlock};

    assert_invariants_locked();

    // The global epoch could not have advanced since the passed in value was
    // read because this thread is passing through the quiescent state for the
    // first time in this epoch.
    UNODB_DETAIL_ASSERT(current_global_epoch == get_current_epoch_locked());
    UNODB_DETAIL_ASSERT(thread_epoch == current_global_epoch ||
                        thread_epoch + 1 == current_global_epoch);

    const auto new_global_epoch = remove_thread_from_previous_epoch_locked(
        current_global_epoch, to_deallocate);
    UNODB_DETAIL_ASSERT(new_global_epoch == current_global_epoch ||
                        new_global_epoch == current_global_epoch + 1);

    result = new_global_epoch;

    assert_invariants_locked();
  }
  return result;
}

qsbr_epoch qsbr::remove_thread_from_previous_epoch_locked(
    qsbr_epoch current_global_epoch,
    qsbr::deferred_requests &requests) noexcept {
  UNODB_DETAIL_ASSERT(threads_in_previous_epoch > 0);
  --threads_in_previous_epoch;

  if (threads_in_previous_epoch > 0) return current_global_epoch;

  const auto new_epoch = change_epoch(current_global_epoch, requests);

  UNODB_DETAIL_ASSERT(current_global_epoch + 1 == new_epoch);

  return new_epoch;
}

qsbr_epoch qsbr::change_epoch(qsbr_epoch current_global_epoch,
                              qsbr::deferred_requests &requests) noexcept {
  const auto result = current_global_epoch + 1;
  UNODB_DETAIL_ASSERT(get_current_epoch_locked() + 1 == result);
  current_epoch.store(result, std::memory_order_relaxed);

  deallocation_size_stats(
      current_interval_total_dealloc_size.load(std::memory_order_relaxed));
  publish_deallocation_size_stats();

  epoch_callback_stats(previous_interval_deallocation_requests.size());
  publish_epoch_callback_stats();

  requests = make_deferred_requests();
  requests.requests[0] = std::move(previous_interval_deallocation_requests);

  if (UNODB_DETAIL_LIKELY(!single_thread_mode_locked())) {
    previous_interval_deallocation_requests =
        std::move(current_interval_deallocation_requests);
    previous_interval_total_dealloc_size.store(
        current_interval_total_dealloc_size.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
  } else {
    previous_interval_deallocation_requests.clear();
    previous_interval_total_dealloc_size.store(0, std::memory_order_relaxed);
    requests.requests[1] = std::move(current_interval_deallocation_requests);
  }
  current_interval_deallocation_requests.clear();
  current_interval_total_dealloc_size.store(0, std::memory_order_relaxed);

  threads_in_previous_epoch = thread_count;
  return result;
}

}  // namespace unodb
