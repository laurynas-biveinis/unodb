// Copyright 2022 Laurynas Biveinis
#ifndef UNODB_DETAIL_QSBR_GTEST_UTILS_HPP
#define UNODB_DETAIL_QSBR_GTEST_UTILS_HPP

#include "global.hpp"

#include <gtest/gtest.h>

#include "gtest_utils.hpp"
#include "qsbr.hpp"
#include "qsbr_test_utils.hpp"

namespace unodb::test {

class QSBRTestBase : public ::testing::Test {
 public:
  ~QSBRTestBase() override;

 protected:
  QSBRTestBase();

  // Thread operation wrappers

  static void join(unodb::qsbr_thread &thread) {
    unodb::test::must_not_allocate([&thread]() { thread.join(); });
  }

  // QSBR operation wrappers
 private:
  template <typename F>
  [[nodiscard]] static auto getter_must_not_allocate(F f) noexcept(
      noexcept(f())) {
    std::invoke_result_t<F> result;
    unodb::test::must_not_allocate(
        [&]() noexcept(noexcept(f())) { result = f(); });
    return result;
  }

  [[nodiscard]] static auto get_qsbr_state() noexcept {
    return getter_must_not_allocate(
        []() noexcept { return unodb::qsbr::instance().get_state(); });
  }

 protected:
  [[nodiscard]] static auto get_qsbr_thread_count() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr_state::get_thread_count(get_qsbr_state());
    });
  }

  [[nodiscard]] static auto get_qsbr_threads_in_previous_epoch() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr_state::get_threads_in_previous_epoch(get_qsbr_state());
    });
  }

  [[nodiscard]] static bool is_qsbr_paused() noexcept {
    return getter_must_not_allocate(
        []() noexcept { return unodb::this_thread().is_qsbr_paused(); });
  }

  static void qsbr_pause() {
    unodb::test::must_not_allocate([]() { unodb::this_thread().qsbr_pause(); });
  }

  static void qsbr_reset_stats() {
    unodb::test::must_not_allocate(
        [] { unodb::qsbr::instance().reset_stats(); });
  }

  static auto qsbr_get_max_backlog_bytes() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_max_backlog_bytes();
    });
  }

  static auto qsbr_get_mean_backlog_bytes() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_mean_backlog_bytes();
    });
  }

  static auto qsbr_get_epoch_callback_count_max() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_epoch_callback_count_max();
    });
  }

  static auto qsbr_get_epoch_callback_count_variance() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_epoch_callback_count_variance();
    });
  }

  static auto
  qsbr_get_mean_quiescent_states_per_thread_between_epoch_changes() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance()
          .get_mean_quiescent_states_per_thread_between_epoch_changes();
    });
  }

  static auto qsbr_previous_interval_orphaned_requests_empty() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance()
          .previous_interval_orphaned_requests_empty();
    });
  }

  static auto qsbr_current_interval_orphaned_requests_empty() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().current_interval_orphaned_requests_empty();
    });
  }

  static auto qsbr_get_epoch_change_count() noexcept {
    return getter_must_not_allocate([]() noexcept {
      return unodb::qsbr::instance().get_epoch_change_count();
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
    static volatile char sink UNODB_DETAIL_UNUSED =
        *static_cast<const char *>(ptr);
  }
#endif

  static void qsbr_deallocate(void *ptr) {
    unodb::this_thread().on_next_epoch_deallocate(ptr, 1
#ifndef NDEBUG
                                                  ,
                                                  check_ptr_on_qsbr_dealloc
#endif
    );
  }

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
  unodb::qsbr_epoch last_epoch{};
};

}  // namespace unodb::test

#endif  // #ifndef UNODB_DETAIL_QSBR_GTEST_UTILS_HPP
