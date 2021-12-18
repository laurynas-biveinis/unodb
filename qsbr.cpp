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

// LCOV_EXCL_START
[[gnu::cold, gnu::noinline]] void qsbr_epoch::dump(std::ostream &os) const {
  os << "epoch = " << static_cast<std::uint64_t>(epoch_val);
  assert_invariant();
}
// LCOV_EXCL_STOP

[[nodiscard]] qsbr_state::type
qsbr_state::atomic_fetch_dec_threads_in_previous_epoch(
    std::atomic<qsbr_state::type> &word) noexcept {
  const auto old_word = word.fetch_sub(1, std::memory_order_acq_rel);

  UNODB_DETAIL_ASSERT(get_threads_in_previous_epoch(old_word) > 0);
  assert_invariants(old_word);

  return old_word;
}

// Some GCC versions suggest cold attribute on already cold-marked functions
UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")

// LCOV_EXCL_START
[[gnu::cold, gnu::noinline]] void qsbr_state::dump(std::ostream &os,
                                                   type word) {
  os << "QSBR state: " << do_get_epoch(word)
     << ", threads = " << do_get_thread_count(word)
     << ", threads in the previous epoch = "
     << do_get_threads_in_previous_epoch(word);
  assert_invariants(word);
}
// LCOV_EXCL_STOP

UNODB_DETAIL_RESTORE_GCC_WARNINGS()

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
  auto old_state = get_state();

  while (true) {
    const auto old_epoch = qsbr_state::get_epoch(old_state);
    const auto old_thread_count = qsbr_state::get_thread_count(old_state);
    const auto old_threads_in_previous_epoch =
        qsbr_state::get_threads_in_previous_epoch(old_state);

    if (UNODB_DETAIL_LIKELY(old_threads_in_previous_epoch > 0 ||
                            old_thread_count == 0)) {
      const auto new_state =
          qsbr_state::inc_thread_count_and_threads_in_previous_epoch(old_state);

      if (UNODB_DETAIL_LIKELY(state.compare_exchange_weak(
              old_state, new_state, std::memory_order_acq_rel,
              std::memory_order_acquire)))
        return old_epoch;

      // LCOV_EXCL_START
      continue;
    }

    // TODO(laurynas): this and the rest of coverage exclusions would require
    // thread schedules that are impossible to get deterministically. Try to
    // device some thread pause-wait-continue facility for debug builds.
    UNODB_DETAIL_ASSERT(old_threads_in_previous_epoch == 0);
    UNODB_DETAIL_ASSERT(old_thread_count > 0);

    // Epoch change in progress - try to bump the thread count only
    const auto new_state = qsbr_state::inc_thread_count(old_state);

    if (UNODB_DETAIL_LIKELY(state.compare_exchange_weak(
            old_state, new_state, std::memory_order_acq_rel,
            std::memory_order_acquire))) {
      // Spin until the epoch change completes. An alternative would be to
      // return the new epoch early, and then handle seeing it in quiescent
      // state as a no-op, but that trades spinning here for more work in a
      // hotter path.
      while (true) {
        old_state = get_state();
        const auto new_epoch = qsbr_state::get_epoch(old_state);
        if (new_epoch != old_epoch) return new_epoch;
      }
    }
  }
  // LCOV_EXCL_STOP
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void qsbr::unregister_thread(std::uint64_t quiescent_states_since_epoch_change,
                             qsbr_epoch thread_epoch) noexcept {
  bool requests_updated_for_epoch_change = false;
  auto old_state = state.load(std::memory_order_acquire);

  while (true) {
    const auto old_epoch = qsbr_state::get_epoch(old_state);
    const auto old_threads_in_previous_epoch =
        qsbr_state::get_threads_in_previous_epoch(old_state);

    if (UNODB_DETAIL_UNLIKELY(old_threads_in_previous_epoch == 0)) {
      // LCOV_EXCL_START
      // Epoch change in progress - try to decrement the thread count only
      const auto new_state = qsbr_state::dec_thread_count(old_state);
      if (UNODB_DETAIL_LIKELY(state.compare_exchange_weak(
              old_state, new_state, std::memory_order_acq_rel,
              std::memory_order_acquire))) {
        return;
      }
      continue;
      // LCOV_EXCL_STOP
    }

    UNODB_DETAIL_ASSERT(old_threads_in_previous_epoch > 0);
    const auto remove_thread_from_previous_epoch =
        (thread_epoch != old_epoch) ||
        (quiescent_states_since_epoch_change == 0);

    const auto new_state =
        remove_thread_from_previous_epoch
            ? (old_threads_in_previous_epoch == 1
                   ? qsbr_state::inc_epoch_dec_thread_count_reset_previous(
                         old_state)
                   : qsbr_state::dec_thread_count_and_threads_in_previous_epoch(
                         old_state))
            : qsbr_state::dec_thread_count(old_state);

    if (remove_thread_from_previous_epoch) {
      thread_epoch_change_barrier();

      if (old_threads_in_previous_epoch == 1) {
        const auto old_single_thread_mode =
            qsbr_state::single_thread_mode(old_state);

        if (!requests_updated_for_epoch_change) {
          const auto requests_to_deallocate = epoch_change_update_requests(
#ifndef NDEBUG
              old_epoch,
#endif
              old_single_thread_mode);
          // If we come here the second time, do not call
          // epoch_change_update_requests as it's already done for this
          // particular epoch change and we cannot change the epoch twice here.
          requests_updated_for_epoch_change = true;
          // Request epoch invariants become fuzzy at this point - the current
          // interval moved to previous but the epoch not advanced yet. Any new
          // requests until CAS will get the old epoch.
        }
      }
    }

    // Use the strong version because we cannot call
    // epoch_change_update_requests the second time due to a spurious failure
    if (UNODB_DETAIL_LIKELY(state.compare_exchange_strong(
            old_state, new_state, std::memory_order_acq_rel,
            std::memory_order_acquire))) {
      if (thread_epoch != old_epoch) {
        register_quiescent_states_per_thread_between_epoch_changes(
            quiescent_states_since_epoch_change);
      }

      return;
    }
  }
}

void qsbr::reset_stats() noexcept {
  // Stats can only be reset on idle QSBR - best-effort check as nothing
  // prevents to leaving idle state at any time
  assert_idle();

  epoch_callback_stats = {};
  publish_epoch_callback_stats();

  deallocation_size_stats = {};
  publish_deallocation_size_stats();

  std::lock_guard guard{quiescent_states_per_thread_between_epoch_change_lock};

  quiescent_states_per_thread_between_epoch_change_stats = {};
  publish_quiescent_states_per_thread_between_epoch_change_stats();
}

// Some GCC versions suggest cold attribute on already cold-marked functions
UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")

[[gnu::cold, gnu::noinline]] void qsbr::dump(std::ostream &out) const {
  // TODO(laurynas): locking? anyone using it all?
  out << "QSBR status:\n";
  out << "Previous interval pending deallocation requests: "
      << previous_interval_deallocation_requests.size() << '\n';
  out << "Current interval pending deallocation requests: "
      << current_interval_deallocation_requests.size() << '\n';
  out << "Current interval pending deallocation bytes: "
      << current_interval_total_dealloc_size.load(std::memory_order_acquire);
  out << state.load(std::memory_order_acquire) << '\n';
}

UNODB_DETAIL_RESTORE_GCC_WARNINGS()

void qsbr::thread_epoch_change_barrier() noexcept {
  // No loads and stores can be reordered past this point, or the quiescent
  // state contract would be violated
  std::atomic_thread_fence(std::memory_order_release);
#ifdef UNODB_DETAIL_THREAD_SANITIZER
  // I have no idea what I am doing
  __tsan_release(&instance());
#endif
}

qsbr_epoch qsbr::remove_thread_from_previous_epoch(
    qsbr_epoch current_global_epoch
#ifndef NDEBUG
    ,
    qsbr_epoch thread_epoch
#endif
    ) noexcept {
  thread_epoch_change_barrier();

  const auto old_state =
      qsbr_state::atomic_fetch_dec_threads_in_previous_epoch(state);

  const auto old_threads_in_previous_epoch =
      qsbr_state::get_threads_in_previous_epoch(old_state);
  const auto old_single_thread_mode = qsbr_state::single_thread_mode(old_state);

  // The global epoch could not have advanced since the passed in value was
  // read because this thread is passing through the quiescent state for the
  // first time in this epoch.
  UNODB_DETAIL_ASSERT(current_global_epoch == qsbr_state::get_epoch(old_state));
  UNODB_DETAIL_ASSERT(thread_epoch == current_global_epoch ||
                      thread_epoch.next() == current_global_epoch);

  if (old_threads_in_previous_epoch > 1) return current_global_epoch;

  detail::deferred_requests to_deallocate;
  const auto new_epoch =
      change_epoch(current_global_epoch, old_single_thread_mode, to_deallocate);

  UNODB_DETAIL_ASSERT(current_global_epoch.next() == new_epoch);

  return new_epoch;
}

[[nodiscard]] detail::deferred_requests qsbr::make_deferred_requests(
#ifndef NDEBUG
    qsbr_epoch dealloc_epoch, bool single_thread_mode
#endif
    ) noexcept {
  return detail::deferred_requests{
#ifndef NDEBUG
      dealloc_epoch, single_thread_mode
#endif
  };
}

template <typename T>
T atomic_fetch_reset(std::atomic<T> &var) noexcept {
  auto old_var = var.load(std::memory_order_acquire);
  while (true) {
    if (UNODB_DETAIL_LIKELY(var.compare_exchange_weak(
            old_var, 0, std::memory_order_acq_rel, std::memory_order_acquire)))
      return old_var;
    // spin
  }
}

detail::deferred_requests qsbr::epoch_change_update_requests(
#ifndef NDEBUG
    qsbr_epoch current_global_epoch,
#endif
    bool single_thread_mode) noexcept {
  const auto new_epoch_change_count =
      epoch_change_count.load(std::memory_order_relaxed) + 1;
  epoch_change_count.store(new_epoch_change_count, std::memory_order_relaxed);

  detail::deferred_requests result = make_deferred_requests(
#ifndef NDEBUG
      current_global_epoch.next(), single_thread_mode
#endif
  );

  const auto old_current_interval_total_dealloc_size =
      atomic_fetch_reset(current_interval_total_dealloc_size);
  deallocation_size_stats(old_current_interval_total_dealloc_size);
  publish_deallocation_size_stats();

  epoch_callback_stats(get_previous_interval_dealloc_count());
  publish_epoch_callback_stats();

  previous_interval_dealloc_count.store(0, std::memory_order_release);

  if (UNODB_DETAIL_LIKELY(!single_thread_mode)) {
    auto old_current_interval_dealloc_count =
        atomic_fetch_reset(current_interval_dealloc_count);
    previous_interval_dealloc_count.store(old_current_interval_dealloc_count);
  } else {
    current_interval_dealloc_count.store(0, std::memory_order_release);
  }

#ifdef UNODB_DETAIL_THREAD_SANITIZER
  __tsan_acquire(&instance());
#endif
  // Acquire synchronizes-with atomic_thread_fence(std::memory_order_release) in
  // thread_epoch_change_barrier
  std::lock_guard guard{qsbr_lock};

#ifndef NDEBUG
  // The asserts cannot be stricter due to epoch change by unregister_thread,
  // which moves requests between intervals not atomically with the epoch
  // change.
  if (!previous_interval_deallocation_requests.empty()) {
    const auto request_epoch =
        previous_interval_deallocation_requests[0].request_epoch;
    UNODB_DETAIL_ASSERT(request_epoch == current_global_epoch ||
                        request_epoch.next() == current_global_epoch ||
                        request_epoch.next().next() == current_global_epoch);
  }
  if (!current_interval_deallocation_requests.empty()) {
    const auto request_epoch =
        current_interval_deallocation_requests[0].request_epoch;
    UNODB_DETAIL_ASSERT(request_epoch == current_global_epoch ||
                        request_epoch.next() == current_global_epoch);
  }
#endif

  result.requests[0] = std::move(previous_interval_deallocation_requests);

  if (UNODB_DETAIL_LIKELY(!single_thread_mode)) {
    previous_interval_deallocation_requests =
        std::move(current_interval_deallocation_requests);
  } else {
    previous_interval_deallocation_requests.clear();
    result.requests[1] = std::move(current_interval_deallocation_requests);
  }
  current_interval_deallocation_requests.clear();

  return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
qsbr_epoch qsbr::change_epoch(qsbr_epoch current_global_epoch,
                              bool single_thread_mode,
                              detail::deferred_requests &requests) noexcept {
  requests = epoch_change_update_requests(
#ifndef NDEBUG
      current_global_epoch,
#endif
      single_thread_mode);

  auto old_state = state.load(std::memory_order_acquire);
  while (true) {
    UNODB_DETAIL_ASSERT(current_global_epoch ==
                        qsbr_state::get_epoch(old_state));

    const auto new_state = qsbr_state::inc_epoch_reset_previous(old_state);
    if (UNODB_DETAIL_LIKELY(state.compare_exchange_weak(
            old_state, new_state, std::memory_order_acq_rel,
            std::memory_order_acquire))) {
      UNODB_DETAIL_ASSERT(current_global_epoch.next() ==
                          qsbr_state::get_epoch(new_state));

      return current_global_epoch.next();
    }

    // Nobody else can change epoch nor threads in the previous epoch, only
    // allowed failures are thread count change and spurious. The next loop
    // iteration will assert this.
  }
}

void qsbr::assert_idle_locked() const noexcept {
#ifndef NDEBUG
  qsbr_state::assert_invariants(get_state());
  // Copy-paste-tweak with expect_idle_qsbr, but not clear how to fix this:
  // here we are asserting over internals, over there we are using Google Test
  // EXPECT macros with the public interface.
  UNODB_DETAIL_ASSERT(previous_interval_deallocation_requests.empty());
  UNODB_DETAIL_ASSERT(current_interval_deallocation_requests.empty());
#endif
}

}  // namespace unodb
