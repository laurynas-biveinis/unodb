// Copyright 2024-2025 Laurynas Biveinis

// A simple example showing unodb::olc_db parallelism. For simplicity &
// self-containedness does not concern with exception handling and refactoring
// the duplicated code with other examples.

// IWYU pragma: no_include <__ostream/basic_ostream.h>
// IWYU pragma: no_include <allocator>
// IWYU pragma: no_include <memory>

#include "global.hpp"  // IWYU pragma: keep

// std::cerr should be safe to access from different threads if it is
// synchronized with the C streams, yet TSan under XCode gives diagnostics.
#if defined(__APPLE__) && defined(UNODB_DETAIL_THREAD_SANITIZER)
#define UNODB_SYNC_CERR
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#ifdef UNODB_SYNC_CERR
#include <mutex>
#endif
#include <random>
#include <sstream>
#include <string_view>

#include "art_common.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,fuchsia-statically-constructed-objects)
unodb::olc_db<std::uint64_t> tree;

constexpr std::string_view value = "A value";

[[nodiscard, gnu::pure]] unodb::value_view from_string_view(
    std::string_view sv) {
  return {reinterpret_cast<const std::byte *>(sv.data()), sv.length()};
}

#ifdef UNODB_SYNC_CERR
std::mutex cerr_mutex;
#endif

void insert_thread() {
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<> rnd(0, 9);
  for (int i = 0; i < 10; ++i) {
    unodb::quiescent_state_on_scope_exit qstate_on_exit{};
    const auto key = static_cast<std ::uint64_t>(rnd(gen));
    const auto insert_result = tree.insert(key, from_string_view(value));
    std::ostringstream buf;
    buf << "Insert thread "
        << " inserting key " << key << ", result = " << insert_result << '\n';
    {
#ifdef UNODB_SYNC_CERR
      std::lock_guard g{cerr_mutex};
#endif
      std::cerr << buf.str();
    }
  }
}

void remove_thread() {
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<> rnd(0, 9);
  for (int i = 0; i < 10; ++i) {
    const auto key = static_cast<std ::uint64_t>(rnd(gen));
    const auto remove_result = tree.remove(key);
    std::ostringstream buf;
    buf << "Remove thread "
        << " removing key " << key << ", result = " << remove_result << '\n';
    {
#ifdef UNODB_SYNC_CERR
      std::lock_guard g{cerr_mutex};
#endif
      std::cerr << buf.str();
    }  // An alternative to quiescent states on scope exit is the direct q state
    // call:
    unodb::this_thread().quiescent();
  }
}

void get_thread() {
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<> rnd(0, 9);
  for (int i = 0; i < 10; ++i) {
    unodb::quiescent_state_on_scope_exit qstate_on_exit{};
    const auto key = static_cast<std ::uint64_t>(rnd(gen));
    const auto get_result = tree.get(key);
    std::ostringstream buf;
    buf << "Get thread "
        << " getting key " << key << ", key found = " << get_result.has_value()
        << '\n';
    {
#ifdef UNODB_SYNC_CERR
      std::lock_guard g{cerr_mutex};
#endif
      std::cerr << buf.str();
    }
  }
}

}  // namespace

int main() {
  // So that writing to std::cerr from concurrent threads is safe
  std::ios::sync_with_stdio(true);
  // The main thread does not participate in QSBR
  unodb::this_thread().qsbr_pause();

  std::array<unodb::qsbr_thread, 3> threads;
  threads[0] = unodb::qsbr_thread(insert_thread);
  threads[1] = unodb::qsbr_thread(remove_thread);
  threads[2] = unodb::qsbr_thread(get_thread);

  threads[0].join();
  threads[1].join();
  threads[2].join();

  // Quitting threads may race with epoch changes by design, resulting in
  // previous epoch orphaned requests not being executed until epoch
  // changes one more time. If that does not happen, some memory might be
  // held too long. Thus users are advised to pass through Q state in the
  // last thread a couple more times at the end.
  unodb::this_thread().qsbr_resume();
  unodb::this_thread().quiescent();
  unodb::this_thread().quiescent();

#ifdef UNODB_DETAIL_WITH_STATS
  std::cerr << "Final tree memory use: " << tree.get_current_memory_use()
            << '\n';
  std::cerr << "QSBR epochs changed: "
            << unodb::qsbr::instance().get_epoch_change_count()
            << ", max bytes in the deallocation backlog: "
            << unodb::qsbr::instance().get_max_backlog_bytes() << '\n';
#endif  // UNODB_DETAIL_WITH_STATS
}
