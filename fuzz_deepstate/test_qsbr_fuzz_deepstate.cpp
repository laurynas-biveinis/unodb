// Copyright 2021 Laurynas Biveinis

#include "global.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <deepstate/DeepState.hpp>

#include "debug_thread_sync.h"
#include "deepstate_utils.hpp"
#include "heap.hpp"
#include "qsbr.hpp"

namespace {

constexpr auto max_threads{1024};
constexpr auto max_thread_id{102400};

constexpr std::uint64_t object_mem = 0xAABBCCDD22446688ULL;

enum class thread_operation {
  ALLOCATE_POINTER,
  DEALLOCATE_POINTER,
  QUIESCENT_STATE,
  QUIT_THREAD,
  PAUSE_THREAD,
  RESUME_THREAD
};

thread_operation thread_op;

std::unordered_set<std::uint64_t *> allocated_pointers;

struct thread_info {
  unodb::qsbr_thread thread;
  std::size_t id{SIZE_MAX};
  bool is_paused{false};

  explicit thread_info(std::size_t id_) noexcept : id{id_} {}

  template <typename Function, typename... Args>
  explicit thread_info(std::size_t id_, Function &&f, Args &&...args)
      : thread{std::forward<Function>(f), std::forward<Args>(args)...},
        id{id_} {}

  thread_info(thread_info &&other) noexcept
      : thread{std::move(other.thread)},
        id{other.id},
        is_paused{other.is_paused} {}

  thread_info(const thread_info &) = delete;

  ~thread_info() = default;

  thread_info &operator=(thread_info &&other) noexcept {
    id = other.id;
    thread = std::move(other.thread);
    is_paused = other.is_paused;
    return *this;
  }

  thread_info &operator=(const thread_info &) = delete;
};

std::vector<thread_info> threads;

std::array<unodb::debug::thread_wait, max_thread_id> thread_sync;

std::size_t new_thread_id{1};

DISABLE_GCC_WARNING("-Wuseless-cast")

auto choose_thread() {
  ASSERT(!threads.empty());
  return DeepState_SizeTInRange(0, threads.size() - 1);
}

auto choose_non_main_thread() {
  ASSERT(threads.size() >= 2);
  return DeepState_SizeTInRange(1, threads.size() - 1);
}

RESTORE_GCC_WARNINGS()

void allocate_pointer() {
  LOG(TRACE) << "Allocating pointer";
  auto *const new_ptr{static_cast<std::uint64_t *>(
      unodb::detail::pmr_new_delete_resource()->allocate(sizeof(object_mem)))};
  *new_ptr = object_mem;
  allocated_pointers.insert(new_ptr);
}

void deallocate_pointer(std::uint64_t *ptr) {
  ASSERT(*ptr == object_mem);
  unodb::qsbr::instance().on_next_epoch_pool_deallocate(
      *unodb::detail::pmr_new_delete_resource(), ptr, sizeof(object_mem));
}

void deallocate_pointer() {
  if (allocated_pointers.empty()) {
    LOG(TRACE) << "No pointers allocated, skipping";
    return;
  }
  if (unodb::current_thread_reclamator().is_paused()) {
    LOG(TRACE) << "Current thread paused, skipping";
    return;
  }

  const auto ptr_i{static_cast<decltype(allocated_pointers)::difference_type>(
      DeepState_SizeTInRange(0, allocated_pointers.size() - 1))};
  LOG(TRACE) << "Deallocating pointer index "
             << static_cast<std::uint64_t>(ptr_i);
  const auto itr{std::next(allocated_pointers.begin(), ptr_i)};
  auto *const ptr{*itr};
  deallocate_pointer(ptr);
  allocated_pointers.erase(itr);
}

void quiescent_state() {
  if (unodb::current_thread_reclamator().is_paused()) {
    LOG(TRACE) << "Skipping quiescent state for a paused thread";
    return;
  }
  LOG(TRACE) << "Quiescent state";
  unodb::current_thread_reclamator().quiescent_state();
}

void pause_thread() {
  LOG(TRACE) << "Pausing thread";
  unodb::current_thread_reclamator().pause();
}

void resume_thread() {
  LOG(TRACE) << "Resuming thread";
  unodb::current_thread_reclamator().resume();
}

void quit_thread(thread_info &tinfo) {
  const auto thread_id{tinfo.id};
  ASSERT(thread_id > 0);
  ASSERT(thread_id < new_thread_id);

  LOG(TRACE) << "Stopping the thread with ID "
             << static_cast<std::uint64_t>(thread_id);

  thread_op = thread_operation::QUIT_THREAD;
  thread_sync[thread_id].notify();
  tinfo.thread.join();
}

void do_op_in_thread(std::size_t thread_i, thread_operation op) {
  ASSERT(thread_i > 0);
  ASSERT(thread_i < threads.size());

  thread_op = op;

  const auto thread_id = threads[thread_i].id;
  ASSERT(thread_id > 0);
  ASSERT(thread_id < new_thread_id);

  thread_sync[thread_id].notify();
  thread_sync[0].wait();
}

void do_op(thread_operation op) {
  ASSERT(op != thread_operation::QUIT_THREAD);

  switch (op) {
    case thread_operation::ALLOCATE_POINTER:
      allocate_pointer();
      break;
    case thread_operation::DEALLOCATE_POINTER:
      deallocate_pointer();
      break;
    case thread_operation::QUIESCENT_STATE:
      quiescent_state();
      break;
    case thread_operation::PAUSE_THREAD:
      pause_thread();
      break;
    case thread_operation::RESUME_THREAD:
      resume_thread();
      break;
    case thread_operation::QUIT_THREAD:
      CANNOT_HAPPEN();
  }
}

void test_thread(std::size_t thread_id) {
  ASSERT(thread_id > 0);
  thread_sync[0].notify();

  while (true) {
    thread_sync[thread_id].wait();
    if (thread_op == thread_operation::QUIT_THREAD) return;
    do_op(thread_op);
    thread_sync[0].notify();
  }
}

void do_op(std::size_t thread_i, thread_operation op) {
  LOG(TRACE) << "Next operation in thread "
             << static_cast<std::uint64_t>(thread_i);
  if (thread_i == 0)
    do_op(op);
  else
    do_op_in_thread(thread_i, op);
}

UNODB_START_DEEPSTATE_TESTS()

TEST(QSBR, DeepStateFuzz) {
  const auto test_length = DeepState_ShortInRange(0, 2000);
  LOG(TRACE) << "Test length " << test_length;

  threads.emplace_back(0);

  for (auto i = 0; i < test_length; ++i) {
    LOG(TRACE) << "Iteration " << i;
    deepstate::OneOf(
        // Allocate a new pointer in a random thread
        [&] {
          const auto thread_i{choose_thread()};
          do_op(thread_i, thread_operation::ALLOCATE_POINTER);
        },
        // Deallocate a random old pointer in a random thread
        [&] {
          const auto thread_i{choose_thread()};
          do_op(thread_i, thread_operation::DEALLOCATE_POINTER);
        },
        // Start a new thread
        [&] {
          if (threads.size() == max_threads) return;
          const auto tid = new_thread_id++;
          LOG(TRACE) << "Creating a new thread with ID "
                     << static_cast<std::uint64_t>(tid);
          threads.emplace_back(tid, test_thread, tid);
          thread_sync[0].wait();
        },
        // A random thread passes through a quiescent state
        [&] {
          const auto thread_i{choose_thread()};
          do_op(thread_i, thread_operation::QUIESCENT_STATE);
        },
        // Stop a random thread
        [&] {
          if (threads.size() == 1) return;

          const auto thread_i{choose_non_main_thread()};

          const auto thread_itr =
              threads.begin() +
              static_cast<decltype(threads)::difference_type>(thread_i);
          quit_thread(*thread_itr);
          threads.erase(thread_itr);
        },
        // Pause or resume a random thread
        [&] {
          const auto thread_i{choose_thread()};
          const auto op = threads[thread_i].is_paused
                              ? thread_operation::RESUME_THREAD
                              : thread_operation::PAUSE_THREAD;
          do_op(thread_i, op);
          threads[thread_i].is_paused = !threads[thread_i].is_paused;
        });
    const auto unpaused_threads = static_cast<std::size_t>(
        std::count_if(threads.cbegin(), threads.cend(),
                      [](const thread_info &info) { return !info.is_paused; }));
    ASSERT(unodb::qsbr::instance().single_thread_mode() ==
           (unpaused_threads < 2));
    ASSERT(unodb::qsbr::instance().number_of_threads() == unpaused_threads);
    ASSERT(unodb::qsbr::instance().get_threads_in_previous_epoch() <=
           unpaused_threads);
    for (auto *ptr : allocated_pointers) {
      ASSERT(*ptr == object_mem);
    }
    // Check that dump does not crash
    std::stringstream dump_sink;
    unodb::qsbr::instance().dump(dump_sink);
    // Check that getters do not crash
    dump_sink << unodb::qsbr::instance().get_epoch_callback_count_max();
    dump_sink << unodb::qsbr::instance().get_epoch_callback_count_variance();
    dump_sink
        << unodb::qsbr::instance()
               .get_mean_quiescent_states_per_thread_between_epoch_changes();
    dump_sink << unodb::qsbr::instance().get_current_epoch();
    dump_sink << unodb::qsbr::instance().get_max_backlog_bytes();
    dump_sink << unodb::qsbr::instance().get_mean_backlog_bytes();
    dump_sink << unodb::qsbr::instance().previous_interval_size();
    dump_sink << unodb::qsbr::instance().current_interval_size();
    dump_sink << unodb::qsbr::instance().get_reserved_thread_capacity();
  }

  if (threads[0].is_paused) {
    LOG(TRACE) << "Resuming main thread";
    resume_thread();
  }

  for (const auto &ptr : allocated_pointers) {
    LOG(TRACE) << "Deallocating pointer at the test end";
    deallocate_pointer(ptr);
  }
  allocated_pointers.clear();

  for (auto &tinfo : threads) {
    if (tinfo.id == 0) continue;
    quit_thread(tinfo);
  }

  unodb::current_thread_reclamator().quiescent_state();

  threads.clear();
  new_thread_id = 1;

  unodb::qsbr::instance().assert_idle();
}

}  // namespace
