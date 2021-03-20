// Copyright (C) 2019-2021 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <atomic>
#ifdef NDEBUG
#include <cstdlib>
#endif
#include <exception>
#include <iostream>
#include <iterator>
#include <thread>
#include <utility>

#include "qsbr.hpp"

namespace {

[[gnu::constructor]] void run_tls_ctor_in_main_thread() {
  unodb::construct_current_thread_reclamator();
}

}  // namespace

namespace unodb {

void qsbr::prepare_new_thread() {
  std::lock_guard<std::mutex> guard{qsbr_mutex};
  prepare_new_thread_locked();
}

void qsbr::register_prepared_thread(std::thread::id thread_id) noexcept {
  std::lock_guard<std::mutex> guard{qsbr_mutex};
  register_prepared_thread_locked(thread_id);
}

void qsbr::register_new_thread(std::thread::id thread_id) {
  std::lock_guard<std::mutex> guard{qsbr_mutex};
  // TODO(laurynas): both of these calls share the critical section, simpler
  // implementation possible
  prepare_new_thread_locked();
  register_prepared_thread_locked(thread_id);
}

void qsbr::unregister_thread(std::thread::id thread_id) {
  deferred_requests_type requests_to_deallocate;
  {
    std::lock_guard<std::mutex> guard{qsbr_mutex};

#ifndef NDEBUG
    thread_count_changed_in_current_epoch = true;
#endif

    // TODO(laurynas): return iterator to save the find call below
    // A thread being unregistered must be quiescent
    requests_to_deallocate = quiescent_state_locked(thread_id);

    const auto thread_state = threads.find(thread_id);
    assert(thread_state != threads.end());
    if (unlikely(thread_state->second == 0)) {
      // The thread being unregistered become quiescent and that allowed an
      // epoch change, which marked this thread as not-quiescent again, and
      // included it in threads_in_previous_epoch
      --threads_in_previous_epoch;
    }
    threads.erase(thread_state);
    --reserved_thread_capacity;

    if (unlikely(threads.empty())) {
      threads_in_previous_epoch = 0;
    } else if (unlikely(single_thread_mode_locked())) {
      // Deallocate previous epoch requests immediately as the sole current
      // thread cannot be holding any live pointers to them. We cannot do the
      // same for the current epoch requests as they might come from a
      // just-stopped thread with the current thread holding live pointers.
      // However, any new deallocation requests from this point on can be
      // executed immediately.
      epoch_callback_stats(previous_interval_deallocation_requests.size());
      deallocation_size_stats(
          previous_interval_total_dealloc_size.load(std::memory_order_relaxed));
      previous_interval_total_dealloc_size.store(0, std::memory_order_relaxed);
      // FIXME(laurynas): replace with a third member of deferred_requests_type
      std::move(std::begin(previous_interval_deallocation_requests),
                std::end(previous_interval_deallocation_requests),
                std::back_inserter(requests_to_deallocate[0]));
      previous_interval_deallocation_requests.clear();
    }

    assert_invariants();
  }

  deallocate_requests(requests_to_deallocate[0]);
  deallocate_requests(requests_to_deallocate[1]);
}

void qsbr::reset() noexcept {
  std::lock_guard<std::mutex> guard{qsbr_mutex};

  assert_invariants();

  deallocation_size_stats = {};
  epoch_callback_stats = {};
  quiescent_states_per_thread_between_epoch_change_stats = {};
  epoch_change_count = 0;
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
  out << "Number of tracked threads: " << threads.size() << '\n';
  for (const auto &thread_info : threads) {
    out << "Thread key: " << thread_info.first
        << ", quiescent: " << thread_info.second << '\n';
  }
  out << "Number of threads in the previous epoch = "
      << threads_in_previous_epoch << '\n';
}

void qsbr::prepare_new_thread_locked() {
  assert_invariants();

  ++reserved_thread_capacity;
  assert(reserved_thread_capacity > threads.size());

  threads.reserve(reserved_thread_capacity);
}

void qsbr::register_prepared_thread_locked(std::thread::id thread_id) noexcept {
  // No need to reserve space for the first (or the last, depending on the
  // workload) thread
  if (unlikely(reserved_thread_capacity == 0)) reserved_thread_capacity = 1;

#ifndef NDEBUG
  thread_count_changed_in_current_epoch = true;
  assert_invariants();
  assert(reserved_thread_capacity > threads.size());
#endif

  try {
    const auto USED_IN_DEBUG[itr, insert_ok] =
        threads.insert({thread_id, false});
    assert(insert_ok);
    // LCOV_EXCL_START
  } catch (std::exception &e) {
    std::cerr
        << "Impossible happened: QSBR thread vector insert threw exception: "
        << e.what() << ", aborting!\n";
#ifdef NDEBUG
    std::abort();
#else
    CANNOT_HAPPEN();
#endif
  } catch (...) {
    std::cerr << "Impossible happened: QSBR thread vector insert threw unknown "
                 "exception, aborting!\n";
#ifdef NDEBUG
    std::abort();
#else
    CANNOT_HAPPEN();
#endif
    // LCOV_EXCL_STOP
  }

  ++threads_in_previous_epoch;
}

qsbr::deferred_requests_type qsbr::quiescent_state_locked(
    std::thread::id thread_id) noexcept {
  assert_invariants();

  auto thread_state = threads.find(thread_id);
  assert(thread_state != threads.end());

  ++thread_state->second;
  if (thread_state->second > 1) {
    assert_invariants();
    return deferred_requests_type{};
  }

  --threads_in_previous_epoch;

  deferred_requests_type result = (threads_in_previous_epoch == 0)
                                      ? change_epoch()
                                      : deferred_requests_type{};

  assert_invariants();

  return result;
}

qsbr::deferred_requests_type qsbr::change_epoch() noexcept {
  ++epoch_change_count;

  deallocation_size_stats(
      current_interval_total_dealloc_size.load(std::memory_order_relaxed));
  epoch_callback_stats(previous_interval_deallocation_requests.size());

  deferred_requests_type result;
  result[0] = std::move(previous_interval_deallocation_requests);

  if (likely(!single_thread_mode_locked())) {
    previous_interval_deallocation_requests =
        std::move(current_interval_deallocation_requests);
    previous_interval_total_dealloc_size.store(
        current_interval_total_dealloc_size.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
  } else {
    previous_interval_deallocation_requests.clear();
    result[1] = std::move(current_interval_deallocation_requests);
  }
  current_interval_deallocation_requests.clear();
  current_interval_total_dealloc_size.store(0, std::memory_order_relaxed);

#ifndef NDEBUG
  thread_count_changed_in_previous_epoch =
      thread_count_changed_in_current_epoch;
  thread_count_changed_in_current_epoch = false;
#endif

  for (auto &itr : threads) {
    quiescent_states_per_thread_between_epoch_change_stats(itr.second);
    itr.second = 0;
  }

  threads_in_previous_epoch = threads.size();
  return result;
}

void qsbr::assert_invariants() const noexcept {
#ifndef NDEBUG
  assert(reserved_thread_capacity >= threads.size());

  if (single_thread_mode_locked()) {
    assert(previous_interval_deallocation_requests.empty());
    assert(previous_interval_total_dealloc_size.load(
               std::memory_order_relaxed) == 0);
  }
  // TODO(laurynas): can this be simplified after the thread registration
  // quiescent state fix?
  if (!thread_count_changed_in_current_epoch &&
      !thread_count_changed_in_previous_epoch) {
    const auto actual_threads_in_previous_epoch = std::count_if(
        threads.cbegin(), threads.cend(),
        [](decltype(threads)::value_type value) { return value.second == 0; });
    if (static_cast<std::size_t>(actual_threads_in_previous_epoch) !=
        threads_in_previous_epoch) {
      assert(static_cast<std::size_t>(actual_threads_in_previous_epoch) ==
             threads_in_previous_epoch);
    }
  }
#endif
}

}  // namespace unodb
