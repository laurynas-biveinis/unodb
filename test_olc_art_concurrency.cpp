// Copyright 2019-2021 Laurynas Biveinis

#include "global.hpp"

#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "art.hpp"
#include "db_test_utils.hpp"
#include "qsbr.hpp"
#include "qsbr_test_utils.hpp"

namespace {

class ARTOptimisticLockCoupling : public ::testing::Test {
 protected:
  ARTOptimisticLockCoupling() noexcept { expect_idle_qsbr(); }

  ~ARTOptimisticLockCoupling() noexcept override { expect_idle_qsbr(); }

  olc_tree_verifier verifier{true};
};

template <unsigned ThreadCount, unsigned OpsPerThread, typename TestFn>
void parallel_test(TestFn test_function, olc_tree_verifier &verifier) {
  unodb::current_thread_reclamator().pause();
  std::array<unodb::qsbr_thread, ThreadCount> threads;
  for (std::size_t i = 0; i < ThreadCount; ++i) {
    threads[i] = unodb::qsbr_thread{test_function, &verifier, i, OpsPerThread};
  }
  for (auto &t : threads) {
    t.join();
  }
  unodb::current_thread_reclamator().resume();
}

void key_range_op_thread(olc_tree_verifier *verifier, std::size_t thread_i,
                         unsigned op_count) {
  unodb::key key = thread_i / 3 * 3;
  for (unsigned i = 0; i < op_count; ++i) {
    switch (thread_i % 3) {
      case 0: /* insert */
        verifier->try_insert(key, test_value_1);
        break;
      case 1: /* remove */
        verifier->try_remove(key);
        break;
      case 2: /* get */
        verifier->try_get(key);
        break;
    }
    key++;
  }
}

template <unsigned ThreadCount, unsigned OpCount>
void key_range_op_test(olc_tree_verifier &verifier) {
  static_assert(ThreadCount >= 3);

  unodb::current_thread_reclamator().pause();
  std::array<unodb::qsbr_thread, ThreadCount> threads;
  for (std::size_t i = 0; i < ThreadCount; ++i) {
    threads[i] = unodb::qsbr_thread{key_range_op_thread, &verifier, i, OpCount};
  }
  for (auto &t : threads) {
    t.join();
  }
  unodb::current_thread_reclamator().resume();
}

void parallel_insert_thread(olc_tree_verifier *verifier, std::size_t thread_i,
                            unsigned count) {
  verifier->insert_preinserted_key_range(thread_i * count, count);
}

TEST_F(ARTOptimisticLockCoupling, ParallelInsertOneTree) {
  constexpr auto num_of_threads = 4;
  constexpr auto total_keys = 1024;
  verifier.preinsert_key_range_to_verifier_only(0, total_keys);
  parallel_test<num_of_threads, total_keys / num_of_threads>(
      parallel_insert_thread, verifier);
  // FIXME(laurynas): move to the fixture class
  verifier.check_present_values();
}

void parallel_remove_thread(olc_tree_verifier *verifier, std::size_t thread_i,
                            unsigned count) {
  const auto start_key = thread_i * count;
  for (std::size_t i = 0; i < count; ++i) {
    verifier->remove(start_key + i, true);
  }
}

TEST_F(ARTOptimisticLockCoupling, ParallelTearDownOneTree) {
  constexpr auto num_of_threads = 8;
  constexpr auto total_keys = 2048;
  verifier.insert_key_range(0, total_keys, true);
  parallel_test<num_of_threads, total_keys / num_of_threads>(
      parallel_remove_thread, verifier);
  verifier.assert_empty();
}

TEST_F(ARTOptimisticLockCoupling, Node4ParallelOps) {
  verifier.insert_key_range(0, 3, true);
  key_range_op_test<9, 6>(verifier);
}

TEST_F(ARTOptimisticLockCoupling, Node16ParallelOps) {
  verifier.insert_key_range(0, 10, true);
  key_range_op_test<9, 12>(verifier);
}

TEST_F(ARTOptimisticLockCoupling, Node48ParallelOps) {
  verifier.insert_key_range(0, 32, true);
  key_range_op_test<9, 32>(verifier);
}

TEST_F(ARTOptimisticLockCoupling, Node256ParallelOps) {
  verifier.insert_key_range(0, 152, true);
  key_range_op_test<9, 208>(verifier);
}

void random_op_thread(olc_tree_verifier *verifier, std::size_t thread_i,
                      unsigned op_count) {
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::geometric_distribution<unodb::key> key_generator{0.5};
  for (unsigned i = 0; i < op_count; ++i) {
    const auto key{key_generator(gen)};
    switch (thread_i % 3) {
      case 0: /* insert */
        verifier->try_insert(key, test_value_2);
        break;
      case 1: /* remove */
        verifier->try_remove(key);
        break;
      case 2: /* get */
        verifier->try_get(key);
        break;
    }
  }
}

TEST_F(ARTOptimisticLockCoupling, ParallelRandomInsertDeleteGet) {
  verifier.insert_key_range(0, 2048, true);
  parallel_test<4 * 3, 10000>(random_op_thread, verifier);
}

}  // namespace
