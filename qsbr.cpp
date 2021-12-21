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

namespace detail {

struct dealloc_vector_list_node {
  std::unique_ptr<detail::dealloc_request_vector> requests;
  dealloc_vector_list_node *next;
};

}  // namespace detail

namespace {

void add_to_orphan_list(
    std::atomic<detail::dealloc_vector_list_node *> &orphan_list,
    std::unique_ptr<detail::dealloc_request_vector> &&requests) noexcept {
  if (requests->empty()) {
    requests.reset();
    return;
  }

  auto list_node = std::make_unique<detail::dealloc_vector_list_node>();
  auto *const list_node_ptr = list_node.release();

  list_node_ptr->requests = std::move(requests);
  list_node_ptr->next = orphan_list.load(std::memory_order_acquire);

  while (true) {
    if (UNODB_DETAIL_LIKELY(orphan_list.compare_exchange_weak(
            list_node_ptr->next, list_node_ptr, std::memory_order_acq_rel,
            std::memory_order_acquire)))
      return;
  }
}

detail::dealloc_vector_list_node *take_orphan_list(
    std::atomic<detail::dealloc_vector_list_node *> &orphan_list) noexcept {
  return orphan_list.exchange(nullptr, std::memory_order_acq_rel);
}

void free_orphan_list(detail::dealloc_vector_list_node *list
#ifndef NDEBUG
                      ,
                      qsbr_epoch dealloc_epoch, bool single_thread_mode
#endif
                      ) noexcept {
  while (list != nullptr) {
    const std::unique_ptr<detail::dealloc_vector_list_node> list_ptr{list};
    detail::deferred_requests requests_to_deallocate{
        std::move(list_ptr->requests)
#ifndef NDEBUG
            ,
        dealloc_epoch, single_thread_mode
#endif
    };
    list = list_ptr->next;
  }
}

}  // namespace

void qsbr_per_thread::orphan_deferred_requests() noexcept {
  add_to_orphan_list(
      qsbr::instance().orphaned_previous_interval_dealloc_requests,
      std::move(previous_interval_dealloc_requests));
  add_to_orphan_list(
      qsbr::instance().orphaned_current_interval_dealloc_requests,
      std::move(current_interval_dealloc_requests));

  UNODB_DETAIL_ASSERT(previous_interval_dealloc_requests == nullptr);
  UNODB_DETAIL_ASSERT(current_interval_dealloc_requests == nullptr);
}

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
                             qsbr_epoch thread_epoch,
                             qsbr_per_thread &qsbr_thread) noexcept {
  bool global_requests_updated_for_epoch_change = false;
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
        qsbr_thread.orphan_deferred_requests();
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
        qsbr_thread.advance_last_seen_epoch(
            qsbr_state::single_thread_mode(old_state), old_epoch.next());
        // Request epoch invariants become fuzzy at this point - the current
        // interval moved to previous but the epoch not advanced yet. Any new
        // requests until CAS will get the old epoch.

        // Call epoch_change_update_requests only once for one epoch change
        if (!global_requests_updated_for_epoch_change) {
          epoch_change_update_requests(
#ifndef NDEBUG
              old_epoch.next(),
#endif
              qsbr_state::single_thread_mode(old_state));
          global_requests_updated_for_epoch_change = true;
        }
      }
    }

    // Use the strong version because we cannot call
    // epoch_change_update_requests the second time due to a spurious failure
    if (UNODB_DETAIL_LIKELY(state.compare_exchange_strong(
            old_state, new_state, std::memory_order_acq_rel,
            std::memory_order_acquire))) {
      qsbr_thread.orphan_deferred_requests();

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

  epoch_dealloc_per_thread_count_stats = {};
  publish_epoch_callback_stats();

  deallocation_size_per_thread_stats = {};
  publish_deallocation_size_stats();

  std::lock_guard guard{stats_lock};

  quiescent_states_per_thread_between_epoch_change_stats = {};
  publish_quiescent_states_per_thread_between_epoch_change_stats();
}

// Some GCC versions suggest cold attribute on already cold-marked functions
UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")

[[gnu::cold, gnu::noinline]] void qsbr::dump(std::ostream &out) const {
  // TODO(laurynas): anyone using it all?
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

  const auto new_epoch =
      change_epoch(current_global_epoch, old_single_thread_mode);

  UNODB_DETAIL_ASSERT(current_global_epoch.next() == new_epoch);

  return new_epoch;
}

void qsbr::epoch_change_update_requests(
#ifndef NDEBUG
    qsbr_epoch dealloc_epoch,
#endif
    bool single_thread_mode) noexcept {
  const auto new_epoch_change_count =
      epoch_change_count.load(std::memory_order_relaxed) + 1;
  epoch_change_count.store(new_epoch_change_count, std::memory_order_relaxed);

#ifdef UNODB_DETAIL_THREAD_SANITIZER
  __tsan_acquire(&instance());
#endif
  // Acquire synchronizes-with atomic_thread_fence(std::memory_order_release)
  // in thread_epoch_change_barrier
  std::atomic_thread_fence(std::memory_order_acquire);

  auto *orphaned_previous_requests =
      take_orphan_list(orphaned_previous_interval_dealloc_requests);
  auto *orphaned_current_requests =
      take_orphan_list(orphaned_current_interval_dealloc_requests);

#ifndef NDEBUG
  if (orphaned_previous_requests != nullptr) {
    const auto request_epoch =
        (*orphaned_previous_requests->requests)[0].request_epoch;
    UNODB_DETAIL_ASSERT(request_epoch.next() == dealloc_epoch ||
                        request_epoch.next().next() == dealloc_epoch ||
                        request_epoch.next().next().next() == dealloc_epoch);
  }
  if (orphaned_current_requests != nullptr) {
    const auto request_epoch =
        (*orphaned_current_requests->requests)[0].request_epoch;
    UNODB_DETAIL_ASSERT(request_epoch.next() == dealloc_epoch ||
                        request_epoch.next().next() == dealloc_epoch);
  }
#endif

  free_orphan_list(orphaned_previous_requests
#ifndef NDEBUG
                   ,
                   dealloc_epoch, single_thread_mode
#endif
  );

  if (UNODB_DETAIL_LIKELY(!single_thread_mode)) {
    detail::dealloc_vector_list_node *new_previous_requests = nullptr;
    if (UNODB_DETAIL_UNLIKELY(
            !orphaned_previous_interval_dealloc_requests
                 .compare_exchange_strong(
                     new_previous_requests, orphaned_current_requests,
                     std::memory_order_acq_rel, std::memory_order_acquire))) {
      // Someone added new previous requests since we took the previous batch
      // above. Append ours at the tail then, only one thread can do this, as
      // everybody else add at the list head.
      while (new_previous_requests->next != nullptr)
        new_previous_requests = new_previous_requests->next;
      new_previous_requests->next = orphaned_current_requests;
    }
  } else {
    free_orphan_list(orphaned_current_requests
#ifndef NDEBUG
                     ,
                     dealloc_epoch, single_thread_mode
#endif
    );
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
qsbr_epoch qsbr::change_epoch(qsbr_epoch current_global_epoch,
                              bool single_thread_mode) noexcept {
  epoch_change_update_requests(
#ifndef NDEBUG
      current_global_epoch.next(),
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

}  // namespace unodb
