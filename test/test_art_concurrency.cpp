// Copyright 2021-2023 Laurynas Biveinis

// IWYU pragma: no_include <__random/geometric_distribution.h>
// IWYU pragma: no_include <__random/mersenne_twister_engine.h>
// IWYU pragma: no_include <__random/random_device.h>
// IWYU pragma: no_include <memory>
// IWYU pragma: no_include "gtest/gtest.h"

#include "global.hpp"

#include <array>
#include <cstddef>
#include <random>  // IWYU pragma: keep

#include <gtest/gtest.h>

#include "art_common.hpp"
#include "assert.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "mutex_art.hpp"  // IWYU pragma: keep
#include "olc_art.hpp"    // IWYU pragma: keep
#include "qsbr.hpp"
#include "qsbr_test_utils.hpp"

namespace {

template <class Db>
class ARTConcurrencyTest : public ::testing::Test {
 public:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  ~ARTConcurrencyTest() noexcept override {
    if constexpr (std::is_same_v<Db, unodb::olc_db>) {
      unodb::this_thread().quiescent();
      unodb::test::expect_idle_qsbr();
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 protected:
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ARTConcurrencyTest() noexcept {
    if constexpr (std::is_same_v<Db, unodb::olc_db>)
      unodb::test::expect_idle_qsbr();
  }

  template <std::size_t ThreadCount, std::size_t OpsPerThread, typename TestFn>
  void parallel_test(TestFn test_function) {
    if constexpr (std::is_same_v<Db, unodb::olc_db>)
      unodb::this_thread().qsbr_pause();

    std::array<unodb::test::thread<Db>, ThreadCount> threads;
    for (decltype(ThreadCount) i = 0; i < ThreadCount; ++i) {
      threads[i] =
          unodb::test::thread<Db>{test_function, &verifier, i, OpsPerThread};
    }
    for (auto &t : threads) {
      t.join();
    }

    if constexpr (std::is_same_v<Db, unodb::olc_db>)
      unodb::this_thread().qsbr_resume();
  }

  template <unsigned PreinsertLimit, std::size_t ThreadCount,
            std::size_t OpsPerThread>
  void key_range_op_test() {
    verifier.insert_key_range(0, PreinsertLimit, true);

    parallel_test<ThreadCount, OpsPerThread>(key_range_op_thread);
  }

  static void parallel_insert_thread(unodb::test::tree_verifier<Db> *verifier,
                                     std::size_t thread_i,
                                     std::size_t ops_per_thread) {
    verifier->insert_preinserted_key_range(thread_i * ops_per_thread,
                                           ops_per_thread);
  }

  static void parallel_remove_thread(unodb::test::tree_verifier<Db> *verifier,
                                     std::size_t thread_i,
                                     std::size_t ops_per_thread) {
    const auto start_key = thread_i * ops_per_thread;
    for (decltype(ops_per_thread) i = 0; i < ops_per_thread; ++i) {
      verifier->remove(start_key + i, true);
    }
  }

  static void key_range_op_thread(unodb::test::tree_verifier<Db> *verifier,
                                  std::size_t thread_i,
                                  std::size_t ops_per_thread) {
    unodb::key key = thread_i / 3 * 3;
    for (decltype(ops_per_thread) i = 0; i < ops_per_thread; ++i) {
      switch (thread_i % 3) {
        case 0: /* insert */
          verifier->try_insert(key, unodb::test::test_value_1);
          break;
        case 1: /* remove */
          verifier->try_remove(key);
          break;
        case 2: /* get */
          verifier->try_get(key);
          break;
        default:
          UNODB_DETAIL_CANNOT_HAPPEN();
      }
      key++;
    }
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  static void random_op_thread(unodb::test::tree_verifier<Db> *verifier,
                               std::size_t thread_i,
                               std::size_t ops_per_thread) {
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::geometric_distribution<unodb::key> key_generator{0.5};
    for (decltype(ops_per_thread) i = 0; i < ops_per_thread; ++i) {
      const auto key{key_generator(gen)};
      switch (thread_i % 3) {
        case 0: /* insert */
          verifier->try_insert(key, unodb::test::test_value_2);
          break;
        case 1: /* remove */
          verifier->try_remove(key);
          break;
        case 2: /* get */
          verifier->try_get(key);
          break;
        default:
          UNODB_DETAIL_CANNOT_HAPPEN();
      }
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  unodb::test::tree_verifier<Db> verifier{true};

 public:
  ARTConcurrencyTest(const ARTConcurrencyTest<Db> &) = delete;
  ARTConcurrencyTest(ARTConcurrencyTest<Db> &&) = delete;
  ARTConcurrencyTest<Db> &operator=(const ARTConcurrencyTest<Db> &) = delete;
  ARTConcurrencyTest<Db> &operator=(ARTConcurrencyTest<Db> &&) = delete;
};

using ConcurrentARTTypes = ::testing::Types<unodb::mutex_db, unodb::olc_db>;

UNODB_TYPED_TEST_SUITE(ARTConcurrencyTest, ConcurrentARTTypes)

UNODB_START_TYPED_TESTS()

TYPED_TEST(ARTConcurrencyTest, ParallelInsertOneTree) {
  constexpr auto thread_count = 4;
  constexpr auto total_keys = 1024;
  constexpr auto ops_per_thread = total_keys / thread_count;

  this->verifier.preinsert_key_range_to_verifier_only(0, total_keys);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::parallel_insert_thread);
  this->verifier.check_present_values();
}

TYPED_TEST(ARTConcurrencyTest, ParallelTearDownOneTree) {
  constexpr auto thread_count = 8;
  constexpr auto total_keys = 2048;
  constexpr auto ops_per_thread = total_keys / thread_count;

  this->verifier.insert_key_range(0, total_keys);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::parallel_remove_thread);
  this->verifier.assert_empty();
}

TYPED_TEST(ARTConcurrencyTest, Node4ParallelOps) {
  this->template key_range_op_test<3, 9, 6>();
}

TYPED_TEST(ARTConcurrencyTest, Node16ParallelOps) {
  this->template key_range_op_test<10, 9, 12>();
}

TYPED_TEST(ARTConcurrencyTest, Node48ParallelOps) {
  this->template key_range_op_test<32, 9, 32>();
}

TYPED_TEST(ARTConcurrencyTest, Node256ParallelOps) {
  this->template key_range_op_test<152, 9, 208>();
}

TYPED_TEST(ARTConcurrencyTest, ParallelRandomInsertDeleteGet) {
  constexpr auto thread_count = 4 * 3;
  constexpr auto initial_keys = 2048;
  constexpr auto ops_per_thread = 10000;

  this->verifier.insert_key_range(0, initial_keys, true);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::random_op_thread);
}

UNODB_END_TESTS()

}  // namespace
