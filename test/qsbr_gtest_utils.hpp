// Copyright 2022-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_GTEST_UTILS_HPP
#define UNODB_DETAIL_QSBR_GTEST_UTILS_HPP

// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <gtest/gtest.h>

#include "gtest_utils.hpp"
#include "heap.hpp"
#include "qsbr.hpp"
#include "test_utils.hpp"

namespace unodb::test {

class QSBRTestBase : public ::testing::Test {
 public:
  ~QSBRTestBase() noexcept override;

 protected:
  QSBRTestBase();

  // Thread operation wrappers

  static void join(unodb::qsbr_thread &thread) {
    unodb::test::must_not_allocate([&thread]() { thread.join(); });
  }

  // QSBR operation wrappers
 private:
  [[nodiscard]] static auto get_qsbr_state() noexcept {
    return must_not_allocate(
        []() noexcept { return unodb::qsbr::instance().get_state(); });
  }

 protected:
  [[nodiscard]] static auto get_qsbr_thread_count() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr_state::get_thread_count(get_qsbr_state());
    });
  }

  [[nodiscard]] static auto get_qsbr_threads_in_previous_epoch() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr_state::get_threads_in_previous_epoch(get_qsbr_state());
    });
  }

  [[nodiscard]] static bool is_qsbr_paused() noexcept {
    return must_not_allocate(
        []() noexcept { return unodb::this_thread().is_qsbr_paused(); });
  }

  static void qsbr_pause() {
    unodb::test::must_not_allocate([]()
#ifndef UNODB_DETAIL_WITH_STATS
                                       noexcept
#endif
                                   { unodb::this_thread().qsbr_pause(); });
  }

#ifdef UNODB_DETAIL_WITH_STATS

  static void qsbr_reset_stats() {
    unodb::test::must_not_allocate(
        [] { unodb::qsbr::instance().reset_stats(); });
  }

  [[nodiscard]] static auto qsbr_get_max_backlog_bytes() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_max_backlog_bytes();
    });
  }

  [[nodiscard]] static auto qsbr_get_mean_backlog_bytes() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_mean_backlog_bytes();
    });
  }

  [[nodiscard]] static auto qsbr_get_epoch_callback_count_max() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_epoch_callback_count_max();
    });
  }

  [[nodiscard]] static auto qsbr_get_epoch_callback_count_variance() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_epoch_callback_count_variance();
    });
  }

  [[nodiscard]] static auto
  qsbr_get_mean_quiescent_states_per_thread_between_epoch_changes() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance()
          .get_mean_quiescent_states_per_thread_between_epoch_changes();
    });
  }

  [[nodiscard]] static auto qsbr_get_epoch_change_count() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_epoch_change_count();
    });
  }

#endif  // UNODB_DETAIL_WITH_STATS

  [[nodiscard]] static auto
  qsbr_previous_interval_orphaned_requests_empty() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance()
          .previous_interval_orphaned_requests_empty();
    });
  }

  [[nodiscard]] static auto
  qsbr_current_interval_orphaned_requests_empty() noexcept {
    return must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().current_interval_orphaned_requests_empty();
    });
  }

  // Epochs

  void mark_epoch() noexcept {
    last_epoch =
        unodb::qsbr_state::get_epoch(unodb::qsbr::instance().get_state());
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

  void check_epoch_advanced() noexcept {
    const auto current_epoch =
        unodb::qsbr_state::get_epoch(unodb::qsbr::instance().get_state());
    UNODB_EXPECT_EQ(last_epoch.advance(), current_epoch);
    last_epoch = current_epoch;
  }

  void check_epoch_same() const noexcept {
    const auto current_epoch =
        unodb::qsbr_state::get_epoch(unodb::qsbr::instance().get_state());
    UNODB_EXPECT_EQ(last_epoch, current_epoch);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // Allocation and deallocation

  [[nodiscard]] static void *allocate() {
    return unodb::detail::allocate_aligned(1);
  }

#ifndef NDEBUG
  static void check_ptr_on_qsbr_dealloc(const void *ptr) noexcept {
    // The pointer must be readable
    static const volatile char sink UNODB_DETAIL_UNUSED =
        *static_cast<const char *>(ptr);
  }
#endif

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

  static void qsbr_deallocate(void *ptr) {
#ifdef UNODB_DETAIL_WITH_STATS
    const auto current_interval_total_dealloc_size_before =
        unodb::this_thread().get_current_interval_total_dealloc_size();
#endif  // UNODB_DETAIL_WITH_STATS
    const auto previous_interval_empty_before =
        unodb::this_thread().previous_interval_requests_empty();
    const auto current_interval_empty_before =
        unodb::this_thread().current_interval_requests_empty();

    try {
      unodb::this_thread().on_next_epoch_deallocate(ptr
#ifdef UNODB_DETAIL_WITH_STATS
                                                    ,
                                                    1
#endif
#ifndef NDEBUG
                                                    ,
                                                    check_ptr_on_qsbr_dealloc
#endif
      );
    } catch (...) {
      const auto previous_interval_empty_after =
          unodb::this_thread().previous_interval_requests_empty();
      const auto current_interval_empty_after =
          unodb::this_thread().current_interval_requests_empty();
#ifdef UNODB_DETAIL_WITH_STATS
      const auto current_interval_total_dealloc_size_after =
          unodb::this_thread().get_current_interval_total_dealloc_size();
      UNODB_EXPECT_EQ(current_interval_total_dealloc_size_before,
                      current_interval_total_dealloc_size_after);
#endif  // UNODB_DETAIL_WITH_STATS
      UNODB_EXPECT_EQ(previous_interval_empty_before,
                      previous_interval_empty_after);
      UNODB_EXPECT_EQ(current_interval_empty_before,
                      current_interval_empty_after);
      throw;
    }

#ifdef UNODB_DETAIL_WITH_STATS
    const auto current_interval_total_dealloc_size_after =
        unodb::this_thread().get_current_interval_total_dealloc_size();
    const auto current_interval_empty_after =
        unodb::this_thread().current_interval_requests_empty();
    const auto single_thread_mode =
        qsbr_state::single_thread_mode(qsbr::instance().get_state());
    UNODB_EXPECT_EQ(current_interval_empty_after,
                    (current_interval_total_dealloc_size_after == 0));
    if (single_thread_mode) {
      UNODB_EXPECT_TRUE(current_interval_total_dealloc_size_before ==
                            current_interval_total_dealloc_size_after ||
                        current_interval_total_dealloc_size_after == 0);
    } else {
      UNODB_EXPECT_GT(current_interval_total_dealloc_size_after, 0);
      UNODB_EXPECT_TRUE(current_interval_total_dealloc_size_after == 1 ||
                        (current_interval_total_dealloc_size_after ==
                         current_interval_total_dealloc_size_before + 1));
    }
#endif  // UNODB_DETAIL_WITH_STATS
  }

  static void quiescent() {
#ifdef UNODB_DETAIL_WITH_STATS
    const auto current_interval_total_dealloc_size_before =
        unodb::this_thread().get_current_interval_total_dealloc_size();
#endif  // UNODB_DETAIL_WITH_STATS

    unodb::this_thread().quiescent();

#ifdef UNODB_DETAIL_WITH_STATS
    const auto current_interval_total_dealloc_size_after =
        unodb::this_thread().get_current_interval_total_dealloc_size();
    UNODB_EXPECT_TRUE(current_interval_total_dealloc_size_before ==
                          current_interval_total_dealloc_size_after ||
                      current_interval_total_dealloc_size_after == 0);
#endif  // UNODB_DETAIL_WITH_STATS
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  static void touch_memory(char *ptr, char opt_val = '\0') noexcept {
    if (opt_val != '\0') {
      *ptr = opt_val;
    } else {
      static char value = 'A';
      *ptr = value;
      ++value;
    }
  }

 public:
  QSBRTestBase(const QSBRTestBase &) = delete;
  QSBRTestBase(QSBRTestBase &&) = delete;
  QSBRTestBase &operator=(const QSBRTestBase &) = delete;
  QSBRTestBase &operator=(QSBRTestBase &&) = delete;

 private:
  unodb::qsbr_epoch last_epoch{0};
};

}  // namespace unodb::test

#endif  // #ifndef UNODB_DETAIL_QSBR_GTEST_UTILS_HPP
