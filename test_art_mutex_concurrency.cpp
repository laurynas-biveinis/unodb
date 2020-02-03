// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include <random>
#include <thread>

#include "gtest/gtest.h"

#include "mutex_art.hpp"
#include "test_utils.hpp"

namespace {

void parallel_insert_thread(tree_verifier<unodb::mutex_db> *verifier,
                            unodb::key start_key, std::size_t count) {
  verifier->insert_preinserted_key_range(start_key, count);
}

TEST(ARTMutexConcurrency, ParallelInsertOneTree) {
  constexpr auto num_of_threads = 4;
  constexpr auto total_keys = 1024;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.preinsert_key_range_to_verifier_only(0, total_keys);
  std::array<std::thread, num_of_threads> threads;
  unodb::key i = 0;
  for (std::size_t j = 0; j < num_of_threads; ++j) {
    threads[j] = std::thread{parallel_insert_thread, &verifier, i,
                             total_keys / num_of_threads /*, (j == 0)*/};
    i += total_keys / num_of_threads;
  }
  for (auto &t : threads) {
    t.join();
  }
  verifier.check_present_values();
}

void parallel_remove_thread(tree_verifier<unodb::mutex_db> *verifier,
                            unodb::key start_key, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    verifier->remove(start_key + i, true);
  }
}

TEST(ARTMutexConcurrency, ParallelTearDownOneTree) {
  constexpr auto num_of_threads = 8;
  constexpr auto total_keys = 2048;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, total_keys);
  unodb::key i = 0;
  std::array<std::thread, num_of_threads> threads;
  for (std::size_t j = 0; j < num_of_threads; ++j) {
    threads[j] = std::thread{parallel_remove_thread, &verifier, i,
                             total_keys / num_of_threads};
    i += total_keys / num_of_threads;
  }
  for (auto &t : threads) {
    t.join();
  }
  verifier.assert_empty();
}

void key_range_op_thread(tree_verifier<unodb::mutex_db> *verifier,
                         std::size_t thread_i, unsigned op_count) {
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

TEST(ARTMutexConcurrency, Node4ParallelOps) {
  constexpr auto num_of_threads = 9;
  constexpr auto op_count = 6;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, 3, true);
  std::array<std::thread, num_of_threads> threads;
  for (std::size_t i = 0; i < num_of_threads; ++i) {
    threads[i] = std::thread{key_range_op_thread, &verifier, i, op_count};
  }
  for (auto &t : threads) {
    t.join();
  }
}

TEST(ARTMutexConcurrency, Node16ParallelOps) {
  constexpr auto num_of_threads = 9;
  constexpr auto op_count = 12;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, 10, true);
  std::array<std::thread, num_of_threads> threads;
  for (std::size_t i = 0; i < num_of_threads; ++i) {
    threads[i] = std::thread{key_range_op_thread, &verifier, i, op_count};
  }
  for (auto &t : threads) {
    t.join();
  }
}

TEST(ARTMutexConcurrency, Node48ParallelOps) {
  constexpr auto num_of_threads = 9;
  constexpr auto op_count = 32;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, 32, true);
  std::array<std::thread, num_of_threads> threads;
  for (std::size_t i = 0; i < num_of_threads; ++i) {
    threads[i] = std::thread{key_range_op_thread, &verifier, i, op_count};
  }
  for (auto &t : threads) {
    t.join();
  }
}

TEST(ARTMutexConcurrency, Node256ParallelOps) {
  constexpr auto num_of_threads = 9;
  constexpr auto op_count = 208;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, 152, true);
  std::array<std::thread, num_of_threads> threads;
  for (std::size_t i = 0; i < num_of_threads; ++i) {
    threads[i] = std::thread{key_range_op_thread, &verifier, i, op_count};
  }
  for (auto &t : threads) {
    t.join();
  }
}

void random_op_thread(tree_verifier<unodb::mutex_db> *verifier,
                      std::size_t thread_i, unsigned op_count) {
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

TEST(ARTMutexConcurrency, ParallelRandomInsertDeleteGet) {
  constexpr auto num_of_threads = 4 * 3;
  constexpr auto initial_keys = 2048;
  constexpr auto ops_per_thread = 10000;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, initial_keys, true);
  std::array<std::thread, num_of_threads> threads;
  for (std::size_t i = 0; i < num_of_threads; ++i) {
    threads[i] = std::thread{random_op_thread, &verifier, i, ops_per_thread};
  }
  for (auto &t : threads) {
    t.join();
  }
}

}  // namespace
