// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include <random>
#include <thread>

#include "gtest/gtest.h"

#include "mutex_art.hpp"
#include "test_utils.hpp"

namespace {

void parallel_insert_thread(tree_verifier<unodb::mutex_db> *verifier,
                            unodb::key_type start_key, std::size_t count) {
  verifier->insert_preinserted_key_range(start_key, count);
}

TEST(ART_mutex_concurrency, parallel_insert_one_tree) {
  constexpr auto num_of_threads = 4;
  constexpr auto total_keys = 1024;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.preinsert_key_range_to_verifier_only(0, total_keys);
  std::array<std::thread, num_of_threads> threads;
  unodb::key_type i = 0;
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
                            unodb::key_type start_key, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    verifier->remove(start_key + i, true);
  }
}

TEST(ART_mutex_concurrency, parallel_tear_down_one_tree) {
  constexpr auto num_of_threads = 8;
  constexpr auto total_keys = 2048;

  tree_verifier<unodb::mutex_db> verifier;
  verifier.insert_key_range(0, total_keys);
  unodb::key_type i = 0;
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
  unodb::key_type key = thread_i / 3 * 3;
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

TEST(ART_mutex_concurrency, node4_parallel_ops) {
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

TEST(ART_mutex_concurrency, node16_parallel_ops) {
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

TEST(ART_mutex_concurrency, node48_parallel_ops) {
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

TEST(ART_mutex_concurrency, node256_parallel_ops) {
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
  std::geometric_distribution<unodb::key_type> key_generator{0.5};
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

TEST(ART_mutex_concurrency, parallel_random_insert_delete_get) {
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
