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

[[gnu::cold, gnu::noinline]] void qsbr_epoch::dump(std::ostream &os) const {
  os << "epoch = " << static_cast<std::uint64_t>(epoch_val);
  assert_invariant();
}

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
  // Start by incrementing threads_in_previous_epoch so that the epoch cannot
  // advance before we incremented thread_count too.
  inc_threads_in_previous_epoch();

  const auto old_thread_count UNODB_DETAIL_USED_IN_DEBUG =
      thread_count.fetch_add(1, std::memory_order_acq_rel);
  UNODB_DETAIL_ASSERT(old_thread_count < max_qsbr_threads);

  return get_current_epoch();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void qsbr::unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                             qsbr_epoch thread_epoch) noexcept {
  // Block the epoch change
  inc_threads_in_previous_epoch();

  const auto old_thread_count2 UNODB_DETAIL_USED_IN_DEBUG =
      thread_count.fetch_sub(1, std::memory_order_acq_rel);
  UNODB_DETAIL_ASSERT(old_thread_count2 <= max_qsbr_threads);

  bool remove_from_previous_epoch = (quiescent_states_since_epoch_change == 0);
  const auto current_global_epoch = get_current_epoch();

  if (current_global_epoch != thread_epoch) {
    UNODB_DETAIL_ASSERT(current_global_epoch == thread_epoch.next());
    register_quiescent_states_per_thread_between_epoch_changes(
        quiescent_states_since_epoch_change);
    remove_from_previous_epoch = true;
  }

  if (remove_from_previous_epoch) {
    std::lock_guard guard{qsbr_rwlock};
    --threads_in_previous_epoch;
  }

  qsbr::deferred_requests requests;
  {
    std::lock_guard guard{qsbr_rwlock};
    assert_invariants_locked();

    if (threads_in_previous_epoch == 1) {
      // TODO(laurynas): suspicious! Can we miss going to single-threaded mode
      // this way?
      const auto old_thread_count =
          thread_count.load(std::memory_order_acquire) + 1;
      UNODB_DETAIL_ASSERT(old_thread_count < max_qsbr_threads);
      // Only our bump to block the epoch change prevented that, now is the time
      change_epoch(current_global_epoch, old_thread_count, requests);
    } else {
      // Undo our bump to block the epoch change
      UNODB_DETAIL_ASSERT(threads_in_previous_epoch > 0);
      --threads_in_previous_epoch;
    }
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
  out << "Number of tracked threads: " << number_of_threads() << '\n';
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
                        thread_epoch.next() == current_global_epoch);

    const auto new_global_epoch = remove_thread_from_previous_epoch_locked(
        current_global_epoch, to_deallocate);
    UNODB_DETAIL_ASSERT(new_global_epoch == current_global_epoch ||
                        new_global_epoch == current_global_epoch.next());

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

  const auto new_epoch =
      change_epoch(current_global_epoch, number_of_threads(), requests);

  UNODB_DETAIL_ASSERT(current_global_epoch.next() == new_epoch);

  return new_epoch;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
qsbr_epoch qsbr::change_epoch(qsbr_epoch current_global_epoch,
                              qsbr_thread_count_type old_thread_count,
                              qsbr::deferred_requests &requests) noexcept {
  UNODB_DETAIL_ASSERT(old_thread_count <= max_qsbr_threads);

  const auto result = current_global_epoch.next();
  UNODB_DETAIL_ASSERT(get_current_epoch_locked().next() == result);
  current_epoch.store(result, std::memory_order_relaxed);

  const auto new_epoch_change_count =
      epoch_change_count.load(std::memory_order_relaxed) + 1;
  epoch_change_count.store(new_epoch_change_count, std::memory_order_relaxed);

  deallocation_size_stats(
      current_interval_total_dealloc_size.load(std::memory_order_relaxed));
  publish_deallocation_size_stats();

  epoch_callback_stats(previous_interval_deallocation_requests.size());
  publish_epoch_callback_stats();

  requests = make_deferred_requests(
#ifndef NDEBUG
      result, single_thread_mode()
#endif
  );

  UNODB_DETAIL_ASSERT(
      previous_interval_deallocation_requests.empty() ||
      previous_interval_deallocation_requests[0].request_epoch.next() ==
          current_global_epoch);
  UNODB_DETAIL_ASSERT(current_interval_deallocation_requests.empty() ||
                      current_interval_deallocation_requests[0].request_epoch ==
                          current_global_epoch);

  requests.requests[0] = std::move(previous_interval_deallocation_requests);

  if (UNODB_DETAIL_LIKELY(!single_thread_mode(old_thread_count))) {
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

  threads_in_previous_epoch = number_of_threads();

  return result;
}

}  // namespace unodb
