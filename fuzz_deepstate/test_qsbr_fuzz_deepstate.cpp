// Copyright 2021 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>  // IWYU pragma: keep
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>  // IWYU pragma: keep
#include <sstream>
#include <unordered_set>
#include <utility>  // IWYU pragma: keep
#include <vector>

#include <deepstate/DeepState.hpp>

#include "deepstate_utils.hpp"
#include "heap.hpp"
#include "qsbr.hpp"
#include "qsbr_ptr.hpp"
#include "thread_sync.hpp"

namespace {

constexpr auto max_threads{1024};
static_assert(max_threads <= unodb::max_qsbr_threads);
constexpr auto max_thread_id{102400};

constexpr std::uint64_t object_mem = 0xAABBCCDD22446688ULL;

enum class [[nodiscard]] thread_operation{
    ALLOCATE_POINTER,       DEALLOCATE_POINTER, TAKE_ACTIVE_POINTER,
    RELEASE_ACTIVE_POINTER, QUIESCENT_STATE,    QUIT_THREAD,
    PAUSE_THREAD,           RESUME_THREAD,      RESET_STATS};

thread_operation thread_op;
std::size_t op_thread_i;

std::unordered_set<std::uint64_t *> allocated_pointers;

using active_pointers = std::vector<unodb::qsbr_ptr<std::uint64_t>>;

struct [[nodiscard]] thread_info final {
  unodb::qsbr_thread thread;
  std::size_t id{SIZE_MAX};
  bool is_paused{false};
  active_pointers active_ptrs;

  explicit thread_info(std::size_t id_) noexcept : id{id_} {}

  template <typename Function, typename... Args>
  explicit thread_info(std::size_t id_, Function &&f, Args &&...args)
      : thread{std::forward<Function>(f), std::forward<Args>(args)...},
        id{id_} {}

  thread_info(thread_info &&other) noexcept = default;

  thread_info(const thread_info &) = delete;

  ~thread_info() = default;

  thread_info &operator=(thread_info &&other) noexcept = default;

  thread_info &operator=(const thread_info &) = delete;
};

constexpr std::size_t main_thread_i{0};

std::vector<thread_info> threads;

constexpr std::size_t main_thread_id{0};

std::array<unodb::detail::thread_sync, max_thread_id> thread_sync;

std::size_t new_thread_id{1};

template <class T>
[[nodiscard]] std::pair<typename T::difference_type, typename T::iterator>
randomly_advanced_pos_and_iterator(T &container) {
  auto itr{container.begin()};
  auto i{static_cast<typename T::difference_type>(
      DeepState_ContainerIndex(container))};
  std::advance(itr, i);
  return std::make_pair(i, std::move(itr));
}

[[nodiscard]] auto choose_thread() { return DeepState_ContainerIndex(threads); }

[[nodiscard]] auto choose_non_main_thread() {
  ASSERT(threads.size() >= 2);
  return DeepState_SizeTInRange(1, threads.size() - 1);
}

void resume_thread(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());
  ASSERT(threads[thread_i].is_paused);

  LOG(TRACE) << "Resuming thread";
  unodb::this_thread().qsbr_resume();
  threads[thread_i].is_paused = false;
}

void release_active_pointer(std::size_t thread_i);

void quiescent_state(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());

  if (threads[thread_i].is_paused) {
    LOG(TRACE) << "Thread is paused, resuming it instead of quiescent state";
    resume_thread(thread_i);
    return;
  }
  if (!threads[thread_i].active_ptrs.empty()) {
    LOG(TRACE) << "Thread has active pointers, releasing one instead of "
                  "quiescent state";
    release_active_pointer(thread_i);
    return;
  }
  LOG(TRACE) << "Quiescent state";
  unodb::this_thread().quiescent();
}

void allocate_pointer(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());

  if (threads[thread_i].is_paused) {
    LOG(TRACE)
        << "Thread is paused, resuming it instead of allocating a pointer";
    resume_thread(thread_i);
    return;
  }

  LOG(TRACE) << "Allocating pointer";
  auto *const new_ptr{static_cast<std::uint64_t *>(
      unodb::detail::allocate_aligned(sizeof(object_mem)))};
  *new_ptr = object_mem;
  allocated_pointers.insert(new_ptr);
}

#ifndef NDEBUG
void check_qsbr_pointer_on_dealloc(const void *ptr) noexcept {
  ASSERT(*static_cast<const std::uint64_t *>(ptr) == object_mem);
}
#endif

void deallocate_pointer(std::uint64_t *ptr) {
  ASSERT(!unodb::this_thread().is_qsbr_paused());
  ASSERT(*ptr == object_mem);

  unodb::qsbr::instance().on_next_epoch_deallocate(ptr, sizeof(object_mem)
#ifndef NDEBUG
                                                            ,
                                                   check_qsbr_pointer_on_dealloc
#endif
  );
}

void deallocate_pointer(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());

  if (threads[thread_i].is_paused) {
    LOG(TRACE) << "Current thread paused, resuming it instead of deallocating";
    resume_thread(thread_i);
    return;
  }
  if (allocated_pointers.empty()) {
    LOG(TRACE) << "No pointers allocated, doing quiescent state instead of "
                  "deallocating";
    quiescent_state(thread_i);
    return;
  }
  if (!threads[thread_i].active_ptrs.empty()) {
    LOG(TRACE) << "Active pointers exist, releasing one instead of QSBR free";
    release_active_pointer(thread_i);
    return;
  }

  auto [ptr_i, itr] = randomly_advanced_pos_and_iterator(allocated_pointers);
  LOG(TRACE) << "Deallocating pointer index " << ptr_i;
  auto *const ptr{*itr};
  deallocate_pointer(ptr);
  allocated_pointers.erase(itr);
}

void new_active_pointer_from_allocated_pointer(active_pointers &active_ptrs) {
  auto [allocated_ptr_i, allocated_ptr_itr] =
      randomly_advanced_pos_and_iterator(allocated_pointers);
  LOG(TRACE) << "Taking allocated pointer " << allocated_ptr_i;
  ASSERT(**allocated_ptr_itr == object_mem);
  active_ptrs.emplace_back(*allocated_ptr_itr);
}

void new_copy_constructed_active_pointer(active_pointers &active_ptrs) {
  auto [active_ptr_i, active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  LOG(TRACE) << "Copy-constructing active pointer from " << active_ptr_i;
  ASSERT(**active_ptr_itr == object_mem);
  active_ptrs.emplace_back(*active_ptr_itr);
}

void new_move_constructed_active_pointer(active_pointers &active_ptrs) {
  auto [active_ptr_i, active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  LOG(TRACE) << "Move-constructing active pointer from " << active_ptr_i;
  ASSERT(**active_ptr_itr == object_mem);
  active_ptrs.emplace_back(std::move(*active_ptr_itr));
  active_ptrs.erase(active_ptrs.begin() + active_ptr_i);
}

void copy_assign_active_pointer(active_pointers &active_ptrs) {
  auto [source_active_ptr_i, source_active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  auto [dest_active_ptr_i, dest_active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  LOG(TRACE) << "Copy-assigning active pointer from " << source_active_ptr_i
             << " to " << dest_active_ptr_i;
  ASSERT(**dest_active_ptr_itr == object_mem);
  ASSERT(**source_active_ptr_itr == object_mem);
  *dest_active_ptr_itr = *source_active_ptr_itr;
  ASSERT(**dest_active_ptr_itr == object_mem);
  ASSERT(**source_active_ptr_itr == object_mem);
}

void move_assign_active_pointer(active_pointers &active_ptrs) {
  auto [source_active_ptr_i, source_active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  auto [dest_active_ptr_i, dest_active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  if (source_active_ptr_i != dest_active_ptr_i) {
    LOG(TRACE) << "Move-assigning active pointer from " << source_active_ptr_i
               << " to " << dest_active_ptr_i;
    ASSERT(**dest_active_ptr_itr == object_mem);
    ASSERT(**source_active_ptr_itr == object_mem);
    *dest_active_ptr_itr = std::move(*source_active_ptr_itr);
    ASSERT(**dest_active_ptr_itr == object_mem);
    active_ptrs.erase(source_active_ptr_itr);
  } else {
    LOG(TRACE) << "Copy-self-assigning active pointer " << source_active_ptr_i;
    ASSERT(**dest_active_ptr_itr == object_mem);
    ASSERT(**source_active_ptr_itr == object_mem);
    *dest_active_ptr_itr = *source_active_ptr_itr;
    ASSERT(**dest_active_ptr_itr == object_mem);
    ASSERT(**source_active_ptr_itr == object_mem);
  }
}

void take_active_pointer(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());

  if (allocated_pointers.empty()) {
    LOG(TRACE) << "No allocated pointers, doing quiescent state instead of "
                  "taking active pointer";
    quiescent_state(thread_i);
    return;
  }
  if (threads[thread_i].is_paused) {
    LOG(TRACE) << "Current thread paused, resuming it instead of taking active "
                  "pointer";
    resume_thread(thread_i);
    return;
  }

  auto &active_ptrs = threads[thread_i].active_ptrs;

  if (active_ptrs.empty()) {
    LOG(TRACE) << "No active pointers, creating new one from allocated pointer";
    new_active_pointer_from_allocated_pointer(active_ptrs);
  } else if (active_ptrs.size() == 1) {
    switch (DeepState_CharInRange(0, 3)) {
      case 0:
        new_active_pointer_from_allocated_pointer(active_ptrs);
        return;
      case 1:
        new_copy_constructed_active_pointer(active_ptrs);
        return;
      case 2:
        new_move_constructed_active_pointer(active_ptrs);
        return;
      case 3:
        copy_assign_active_pointer(active_ptrs);
        return;
      default:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
  } else {
    switch (DeepState_CharInRange(0, 4)) {
      case 0:
        new_active_pointer_from_allocated_pointer(active_ptrs);
        return;
      case 1:
        new_copy_constructed_active_pointer(active_ptrs);
        return;
      case 2:
        new_move_constructed_active_pointer(active_ptrs);
        return;
      case 3:
        copy_assign_active_pointer(active_ptrs);
        return;
      case 4:
        move_assign_active_pointer(active_ptrs);
        return;
      default:
        UNODB_DETAIL_CANNOT_HAPPEN();
    }
  }
}

void release_active_pointer(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());
  if (threads[thread_i].is_paused) {
    LOG(TRACE) << "Current thread paused, resuming it instead of releasing "
                  " active pointer";
    resume_thread(thread_i);
    return;
  }

  auto &active_ptrs = threads[thread_i].active_ptrs;

  if (active_ptrs.empty()) {
    LOG(TRACE) << "No active pointers, going through quiescent state instead";
    quiescent_state(thread_i);
    return;
  }

  auto [active_ptr_i, active_ptr_itr] =
      randomly_advanced_pos_and_iterator(active_ptrs);
  LOG(TRACE) << "Releasing active pointer " << active_ptr_i;
  active_ptrs.erase(active_ptr_itr);
}

UNODB_DETAIL_RESTORE_GCC_WARNINGS()

void pause_thread(std::size_t thread_i) {
  ASSERT(threads[thread_i].is_paused == unodb::this_thread().is_qsbr_paused());

  if (!threads[thread_i].active_ptrs.empty()) {
    LOG(TRACE)
        << "Thread has active pointers, releasing one instead of pausing";
    release_active_pointer(thread_i);
    return;
  }
  LOG(TRACE) << "Pausing thread";
  unodb::this_thread().qsbr_pause();
  threads[thread_i].is_paused = true;
}

void do_op_in_thread(std::size_t thread_i, thread_operation op) {
  ASSERT(thread_i > main_thread_i);
  ASSERT(thread_i < threads.size());
  ASSERT(op != thread_operation::QUIT_THREAD);
  ASSERT(op != thread_operation::RESET_STATS);

  thread_op = op;
  op_thread_i = thread_i;

  const auto thread_id = threads[thread_i].id;
  ASSERT(thread_id > main_thread_i);
  ASSERT(thread_id < new_thread_id);

  thread_sync[thread_id].notify();
  thread_sync[main_thread_id].wait();
}

void quit_thread(std::size_t thread_i) {
  LOG(TRACE) << "Trying to quit thread " << thread_i;
  ASSERT(thread_i > main_thread_i);

  const auto thread_itr =
      threads.begin() +
      static_cast<decltype(threads)::difference_type>(thread_i);
  thread_info &tinfo{*thread_itr};

  if (!tinfo.active_ptrs.empty()) {
    ASSERT(!tinfo.is_paused);

    LOG(TRACE) << "Selected thread has active pointers, releasing one instead "
                  "of quitting";
    do_op_in_thread(thread_i, thread_operation::RELEASE_ACTIVE_POINTER);
    return;
  }

  const auto thread_id{tinfo.id};
  ASSERT(thread_id > main_thread_id);
  ASSERT(thread_id < new_thread_id);
  LOG(TRACE) << "Stopping the thread with ID " << thread_id;
  thread_op = thread_operation::QUIT_THREAD;
  thread_sync[thread_id].notify();
  tinfo.thread.join();
  threads.erase(thread_itr);
}

void reset_stats() {
  ASSERT(threads.size() == 1);

  if (!threads[main_thread_i].active_ptrs.empty()) {
    LOG(TRACE) << "Thread has active pointers, releasing one instead of "
                  "resetting stats";
    release_active_pointer(main_thread_i);
    return;
  }
  if (!allocated_pointers.empty()) {
    LOG(TRACE) << "Allocated pointers exist, deallocating one instead of "
                  "resetting stats";
    deallocate_pointer(main_thread_i);
    return;
  }
  if (unodb::qsbr::instance().get_previous_interval_dealloc_count() > 0) {
    LOG(TRACE) << "Previous interval non-empty, going through qstate instead "
                  "of resetting stats";
    quiescent_state(main_thread_i);
    return;
  }
  if (unodb::qsbr::instance().get_current_interval_dealloc_count() > 0) {
    LOG(TRACE) << "Current interval non-empty, going through qstate instead of "
                  "resetting stats";
    quiescent_state(main_thread_i);
    return;
  }
  LOG(TRACE) << "Resetting QSBR stats";
  unodb::qsbr::instance().reset_stats();
}

void do_op(std::size_t thread_i, thread_operation op) {
  ASSERT(op != thread_operation::QUIT_THREAD);
  ASSERT(op != thread_operation::RESET_STATS);

  switch (op) {
    case thread_operation::ALLOCATE_POINTER:
      allocate_pointer(thread_i);
      break;
    case thread_operation::DEALLOCATE_POINTER:
      deallocate_pointer(thread_i);
      break;
    case thread_operation::TAKE_ACTIVE_POINTER:
      take_active_pointer(thread_i);
      break;
    case thread_operation::RELEASE_ACTIVE_POINTER:
      release_active_pointer(thread_i);
      break;
    case thread_operation::QUIESCENT_STATE:
      quiescent_state(thread_i);
      break;
    case thread_operation::PAUSE_THREAD:
      pause_thread(thread_i);
      break;
    case thread_operation::RESUME_THREAD:
      resume_thread(thread_i);
      break;
    case thread_operation::RESET_STATS:
    case thread_operation::QUIT_THREAD:
      UNODB_DETAIL_CANNOT_HAPPEN();
  }
}

void test_thread(std::size_t thread_id) {
  ASSERT(thread_id > main_thread_id);
  thread_sync[main_thread_id].notify();

  while (true) {
    thread_sync[thread_id].wait();

    ASSERT(thread_op != thread_operation::RESET_STATS);

    if (thread_op == thread_operation::QUIT_THREAD) return;
    do_op(op_thread_i, thread_op);

    thread_sync[main_thread_id].notify();
  }
}

void do_or_dispatch_op(std::size_t thread_i, thread_operation op) {
  ASSERT(op != thread_operation::QUIT_THREAD);
  ASSERT(op != thread_operation::RESET_STATS);

  LOG(TRACE) << "Next operation in thread " << thread_i;
  if (thread_i == main_thread_i)
    do_op(thread_i, op);
  else
    do_op_in_thread(thread_i, op);
}

UNODB_START_DEEPSTATE_TESTS()

TEST(QSBR, DeepStateFuzz) {
  const auto test_length = DeepState_ShortInRange(0, 2000);
  LOG(TRACE) << "Test length " << test_length;

  threads.emplace_back(main_thread_i);

  for (auto i = 0; i < test_length; ++i) {
    LOG(TRACE) << "Iteration " << i;
    deepstate::OneOf(
        // Allocate a new pointer in a random thread
        [&] {
          const auto thread_i{choose_thread()};
          do_or_dispatch_op(thread_i, thread_operation::ALLOCATE_POINTER);
        },
        // Deallocate a random old pointer in a random thread
        [&] {
          const auto thread_i{choose_thread()};
          do_or_dispatch_op(thread_i, thread_operation::DEALLOCATE_POINTER);
        },
        // Take an active pointer in a random thread
        [&] {
          const auto thread_i{choose_thread()};
          do_or_dispatch_op(thread_i, thread_operation::TAKE_ACTIVE_POINTER);
        },
        // Release an active pointer in a random thread
        [&] {
          const auto thread_i{choose_thread()};
          do_or_dispatch_op(thread_i, thread_operation::RELEASE_ACTIVE_POINTER);
        },
        // Start a new thread
        [&] {
          if (threads.size() == max_threads) {
            LOG(TRACE) << "Thread limit reached, quitting a thread instead";
            quit_thread(choose_non_main_thread());
            return;
          }
          const auto tid = new_thread_id++;
          LOG(TRACE) << "Creating a new thread with ID " << tid;
          threads.emplace_back(tid, test_thread, tid);
          thread_sync[main_thread_id].wait();
        },
        // A random thread passes through a quiescent state
        [&] {
          const auto thread_i{choose_thread()};
          do_or_dispatch_op(thread_i, thread_operation::QUIESCENT_STATE);
        },
        // Stop a random thread
        [&] {
          if (threads.size() == 1) return;
          quit_thread(choose_non_main_thread());
        },
        // Pause or resume a random thread
        [&] {
          const auto thread_i{choose_thread()};
          const auto op = threads[thread_i].is_paused
                              ? thread_operation::RESUME_THREAD
                              : thread_operation::PAUSE_THREAD;
          do_or_dispatch_op(thread_i, op);
        },
        // Reset stats
        [&] {
          if (threads.size() > 1) {
            LOG(TRACE)
                << "More than one thread running, stopping one instead of "
                   "resetting stats";
            quit_thread(choose_non_main_thread());
            return;
          }
          reset_stats();
        });
    const auto unpaused_threads = static_cast<unodb::qsbr_thread_count_type>(
        std::count_if(threads.cbegin(), threads.cend(),
                      [](const thread_info &info) { return !info.is_paused; }));
    const auto current_qsbr_state = unodb::qsbr::instance().get_state();
    ASSERT(unodb::qsbr_state::single_thread_mode(current_qsbr_state) ==
           (unpaused_threads < 2));
    ASSERT(unodb::qsbr_state::get_thread_count(current_qsbr_state) ==
           unpaused_threads);
    ASSERT(unodb::qsbr_state::get_threads_in_previous_epoch(
               current_qsbr_state) <= unpaused_threads);

    for (const auto &tinfo : threads)
      for (const auto &active_ptr : tinfo.active_ptrs)
        ASSERT(*active_ptr == object_mem);
    for (const auto *const ptr : allocated_pointers) ASSERT(*ptr == object_mem);

    // Check that dump does not crash
    std::stringstream dump_sink;
    unodb::qsbr::instance().dump(dump_sink);
    // Check that getters do not crash
    dump_sink << unodb::qsbr::instance().get_epoch_callback_count_max();
    dump_sink << unodb::qsbr::instance().get_epoch_callback_count_variance();
    dump_sink
        << unodb::qsbr::instance()
               .get_mean_quiescent_states_per_thread_between_epoch_changes();
    dump_sink << unodb::qsbr::instance().get_state();
    dump_sink << unodb::qsbr::instance().get_epoch_change_count();
    dump_sink << unodb::qsbr::instance().get_max_backlog_bytes();
    dump_sink << unodb::qsbr::instance().get_mean_backlog_bytes();
    dump_sink << unodb::qsbr::instance().get_previous_interval_dealloc_count();
    dump_sink << unodb::qsbr::instance().get_current_interval_dealloc_count();
  }

  for (std::size_t i = 0; i < threads.size(); ++i) {
    LOG(TRACE) << "Cleaning up thread " << i;
    if (threads[i].is_paused) {
      LOG(TRACE) << "Thread is stopped, resuming";
      ASSERT(threads[i].active_ptrs.empty());
      do_or_dispatch_op(i, thread_operation::RESUME_THREAD);
      continue;
    }
    while (!threads[i].active_ptrs.empty()) {
      LOG(TRACE) << "Releasing active pointer in thread " << i;
      do_or_dispatch_op(i, thread_operation::RELEASE_ACTIVE_POINTER);
    }
  }

  for (const auto &ptr : allocated_pointers) {
    LOG(TRACE) << "Deallocating pointer at the test end";
    deallocate_pointer(ptr);
  }
  allocated_pointers.clear();

  while (threads.size() > 1) quit_thread(threads.size() - 1);

  threads.clear();
  new_thread_id = 1;

  unodb::this_thread().quiescent();

  unodb::qsbr::instance().assert_idle();
}

}  // namespace
