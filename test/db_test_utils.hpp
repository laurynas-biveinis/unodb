// Copyright 2019-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_DB_TEST_UTILS_HPP
#define UNODB_DETAIL_DB_TEST_UTILS_HPP

#include "global.hpp"

// IWYU pragma: no_include <__fwd/sstream.h>
// IWYU pragma: no_include <iterator>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gmock/gmock.h"
// IWYU pragma: no_include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>

#include <gmock/gmock.h>  // IWYU pragma: keep
#include <gtest/gtest.h>

#include "gtest_utils.hpp"

#include "art.hpp"
#include "art_common.hpp"
#include "assert.hpp"
#include "mutex_art.hpp"
#include "node_type.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"
#ifndef NDEBUG
#include "test_heap.hpp"
#endif

namespace unodb::test {

template <class Db>
using thread = typename std::conditional_t<std::is_same_v<Db, unodb::olc_db>,
                                           unodb::qsbr_thread, std::thread>;

constexpr auto test_value_1 = std::array<std::byte, 1>{std::byte{0x00}};
constexpr auto test_value_2 =
    std::array<std::byte, 2>{std::byte{0x00}, std::byte{0x02}};
constexpr auto test_value_3 =
    std::array<std::byte, 3>{std::byte{0x03}, std::byte{0x00}, std::byte{0x01}};
constexpr auto test_value_4 = std::array<std::byte, 4>{
    std::byte{0x04}, std::byte{0x01}, std::byte{0x00}, std::byte{0x02}};
constexpr auto test_value_5 =
    std::array<std::byte, 5>{std::byte{0x05}, std::byte{0xF4}, std::byte{0xFF},
                             std::byte{0x00}, std::byte{0x01}};
constexpr auto empty_test_value = std::array<std::byte, 0>{};

constexpr std::array<unodb::value_view, 6> test_values = {
    unodb::value_view{test_value_1}, unodb::value_view{test_value_2},
    unodb::value_view{test_value_3}, unodb::value_view{test_value_4},
    unodb::value_view{test_value_5}, unodb::value_view{empty_test_value}};

namespace detail {

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wused-but-marked-unused")

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
template <class Db>
void assert_value_eq(const typename Db::get_result &result,
                     unodb::value_view expected) noexcept {
  if constexpr (std::is_same_v<Db, unodb::mutex_db>) {
    UNODB_DETAIL_ASSERT(result.second.owns_lock());
    UNODB_DETAIL_ASSERT(result.first.has_value());
    UNODB_ASSERT_TRUE(std::equal(std::cbegin(*result.first),
                                 std::cend(*result.first),
                                 std::cbegin(expected), std::cend(expected)));
  } else {
    UNODB_DETAIL_ASSERT(result.has_value());
    UNODB_ASSERT_TRUE(std::equal(std::cbegin(*result), std::cend(*result),
                                 std::cbegin(expected), std::cend(expected)));
  }
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <class Db>
void do_assert_result_eq(const Db &db, unodb::key key,
                         unodb::value_view expected, const char *file,
                         int line) {
  std::ostringstream msg;
  unodb::detail::dump_key(msg, key);
  const testing::ScopedTrace trace(file, line, msg.str());
  const auto result = db.get(key);
  if (!Db::key_found(result)) {
    // LCOV_EXCL_START
    std::cerr << "db.get did not find ";
    unodb::detail::dump_key(std::cerr, key);
    std::cerr << '\n';
    db.dump(std::cerr);
    FAIL();
    // LCOV_EXCL_STOP
  }
  assert_value_eq<Db>(result, expected);
}

UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

template <class Db>
void assert_result_eq(const Db &db, unodb::key key, unodb::value_view expected,
                      const char *file, int line) {
  do_assert_result_eq(db, key, expected, file, line);
}

template <>
inline void assert_result_eq(const unodb::olc_db &db, unodb::key key,
                             unodb::value_view expected, const char *file,
                             int line) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  do_assert_result_eq(db, key, expected, file, line);
}

}  // namespace detail

#define ASSERT_VALUE_FOR_KEY(test_db, key, expected) \
  detail::assert_result_eq(test_db, key, expected, __FILE__, __LINE__)

template <class Db>
class [[nodiscard]] tree_verifier final {
 private:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26440)
  void do_insert(unodb::key k, unodb::value_view v) {
    UNODB_ASSERT_TRUE(test_db.insert(k, v));
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void do_remove(unodb::key k, bool bypass_verifier) {
    if (!bypass_verifier) {
      const auto remove_result = values.erase(k);
      UNODB_ASSERT_EQ(remove_result, 1);
    }
    const auto node_counts_before = test_db.get_node_counts();
    const auto mem_use_before = test_db.get_current_memory_use();
    UNODB_ASSERT_GT(node_counts_before[as_i<unodb::node_type::LEAF>], 0);
    UNODB_ASSERT_GT(mem_use_before, 0);
    const auto growing_inodes_before = test_db.get_growing_inode_counts();
    const auto shrinking_inodes_before = test_db.get_shrinking_inode_counts();
    const auto key_prefix_splits_before = test_db.get_key_prefix_splits();

    try {
      if (!test_db.remove(k)) {
        // LCOV_EXCL_START
        std::cerr << "test_db.remove failed for ";
        unodb::detail::dump_key(std::cerr, k);
        std::cerr << '\n';
        test_db.dump(std::cerr);
        FAIL();
        // LCOV_EXCL_STOP
      }
    } catch (...) {
      if (!parallel_test) {
        UNODB_ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
        UNODB_ASSERT_THAT(test_db.get_node_counts(),
                          ::testing::ElementsAreArray(node_counts_before));
        UNODB_ASSERT_THAT(test_db.get_growing_inode_counts(),
                          ::testing::ElementsAreArray(growing_inodes_before));
        UNODB_ASSERT_THAT(test_db.get_shrinking_inode_counts(),
                          ::testing::ElementsAreArray(shrinking_inodes_before));
        UNODB_ASSERT_EQ(test_db.get_key_prefix_splits(),
                        key_prefix_splits_before);
      }
      throw;
    }

    if (!parallel_test) {
      const auto mem_use_after = test_db.get_current_memory_use();
      UNODB_ASSERT_LT(mem_use_after, mem_use_before);

      const auto leaf_count_after =
          test_db.template get_node_count<::unodb::node_type::LEAF>();
      UNODB_ASSERT_EQ(leaf_count_after,
                      node_counts_before[as_i<unodb::node_type::LEAF>] - 1);
    }
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26440)
  void do_try_remove_missing_key(unodb::key absent_key) {
    UNODB_ASSERT_FALSE(test_db.remove(absent_key));
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 public:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)
  explicit tree_verifier(bool parallel_test_ = false)
      : parallel_test{parallel_test_} {
    assert_empty();
    assert_growing_inodes({0, 0, 0, 0});
    assert_shrinking_inodes({0, 0, 0, 0});
    assert_key_prefix_splits(0);
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void insert(unodb::key k, unodb::value_view v, bool bypass_verifier = false) {
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();
    const auto node_counts_before = test_db.get_node_counts();
    const auto empty_before = test_db.empty();
    const auto growing_inodes_before = test_db.get_growing_inode_counts();
    const auto shrinking_inodes_before = test_db.get_shrinking_inode_counts();
    const auto key_prefix_splits_before = test_db.get_key_prefix_splits();

    try {
      do_insert(k, v);
    } catch (...) {
      if (!parallel_test) {
        UNODB_ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
        UNODB_ASSERT_THAT(test_db.get_node_counts(),
                          ::testing::ElementsAreArray(node_counts_before));
        UNODB_ASSERT_EQ(empty_before, test_db.empty());
        UNODB_ASSERT_THAT(test_db.get_growing_inode_counts(),
                          ::testing::ElementsAreArray(growing_inodes_before));
        UNODB_ASSERT_THAT(test_db.get_shrinking_inode_counts(),
                          ::testing::ElementsAreArray(shrinking_inodes_before));
        UNODB_ASSERT_EQ(test_db.get_key_prefix_splits(),
                        key_prefix_splits_before);
      }
      throw;
    }

    UNODB_ASSERT_FALSE(test_db.empty());

    const auto mem_use_after = test_db.get_current_memory_use();
    if (parallel_test)
      UNODB_ASSERT_GT(mem_use_after, 0);
    else
      UNODB_ASSERT_LT(mem_use_before, mem_use_after);

    const auto leaf_count_after =
        test_db.template get_node_count<unodb::node_type::LEAF>();
    if (parallel_test)
      UNODB_ASSERT_GT(leaf_count_after, 0);
    else
      UNODB_ASSERT_EQ(leaf_count_after,
                      node_counts_before[as_i<unodb::node_type::LEAF>] + 1);

    if (!bypass_verifier) {
#ifndef NDEBUG
      allocation_failure_injector::reset();
#endif
      const auto [pos, insert_succeeded] = values.try_emplace(k, v);
      (void)pos;
      UNODB_ASSERT_TRUE(insert_succeeded);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void insert_key_range(unodb::key start_key, std::size_t count,
                        bool bypass_verifier = false) {
    for (auto key = start_key; key < start_key + count; ++key) {
      insert(key, test_values[key % test_values.size()], bypass_verifier);
    }
  }

  void try_insert(unodb::key k, unodb::value_view v) {
    std::ignore = test_db.insert(k, v);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void preinsert_key_range_to_verifier_only(unodb::key start_key,
                                            std::size_t count) {
    for (auto key = start_key; key < start_key + count; ++key) {
      const auto [pos, insert_succeeded] =
          values.try_emplace(key, test_values[key % test_values.size()]);
      (void)pos;
      UNODB_ASSERT_TRUE(insert_succeeded);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void insert_preinserted_key_range(unodb::key start_key, std::size_t count) {
    for (auto key = start_key; key < start_key + count; ++key) {
      do_insert(key, test_values[key % test_values.size()]);
    }
  }

  void remove(unodb::key k, bool bypass_verifier = false) {
    do_remove(k, bypass_verifier);
  }

  void try_remove(unodb::key k) { std::ignore = test_db.remove(k); }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void attempt_remove_missing_keys(
      std::initializer_list<unodb::key> absent_keys) {
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();

    for (const auto absent_key : absent_keys) {
      const auto remove_result = values.erase(absent_key);
      UNODB_ASSERT_EQ(remove_result, 0);
      do_try_remove_missing_key(absent_key);
      if (!parallel_test) {
        UNODB_ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
      }
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void try_get(unodb::key k) const noexcept(noexcept(test_db.get(k))) {
    std::ignore = test_db.get(k);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26445)
  void check_present_values() const {
    for (const auto &[key, value] : values) {
      ASSERT_VALUE_FOR_KEY(test_db, key, value);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void check_absent_keys(std::initializer_list<unodb::key> absent_keys) const
      noexcept(noexcept(try_get(0))) {
    for (const auto absent_key : absent_keys) {
      UNODB_ASSERT_EQ(values.find(absent_key), values.cend());
      try_get(absent_key);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void assert_empty() const {
    UNODB_ASSERT_TRUE(test_db.empty());

    UNODB_ASSERT_EQ(test_db.get_current_memory_use(), 0);

    assert_node_counts({0, 0, 0, 0, 0});
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void assert_node_counts(
      const node_type_counter_array &expected_node_counts) const {
    // Dump the tree to a string. Do not attempt to check the dump format, only
    // that dumping does not crash
    std::stringstream dump_sink;
    test_db.dump(dump_sink);

    const auto actual_node_counts = test_db.get_node_counts();
    UNODB_ASSERT_THAT(actual_node_counts,
                      ::testing::ElementsAreArray(expected_node_counts));
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  constexpr void assert_growing_inodes(
      const inode_type_counter_array &expected_growing_inode_counts)
      const noexcept {
    const auto actual_growing_inode_counts = test_db.get_growing_inode_counts();
    UNODB_ASSERT_THAT(
        actual_growing_inode_counts,
        ::testing::ElementsAreArray(expected_growing_inode_counts));
  }

  constexpr void assert_shrinking_inodes(
      const inode_type_counter_array &expected_shrinking_inode_counts) {
    const auto actual_shrinking_inode_counts =
        test_db.get_shrinking_inode_counts();
    UNODB_ASSERT_THAT(
        actual_shrinking_inode_counts,
        ::testing::ElementsAreArray(expected_shrinking_inode_counts));
  }

  constexpr void assert_key_prefix_splits(std::uint64_t splits) const noexcept {
    UNODB_ASSERT_EQ(test_db.get_key_prefix_splits(), splits);
  }

  void clear() {
    test_db.clear();
#ifndef NDEBUG
    allocation_failure_injector::reset();
#endif
    assert_empty();

    values.clear();
  }

  [[nodiscard, gnu::pure]] constexpr Db &get_db() noexcept { return test_db; }

 private:
  Db test_db{};

  std::unordered_map<unodb::key, unodb::value_view> values;

  const bool parallel_test;
};

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
template <>
inline void tree_verifier<unodb::olc_db>::do_insert(unodb::key k,
                                                    unodb::value_view v) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  UNODB_ASSERT_TRUE(test_db.insert(k, v));
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <>
inline void tree_verifier<unodb::olc_db>::remove(unodb::key k,
                                                 bool bypass_verifier) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  do_remove(k, bypass_verifier);
}

template <>
inline void tree_verifier<unodb::olc_db>::try_remove(unodb::key k) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  std::ignore = test_db.remove(k);
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
template <>
inline void tree_verifier<unodb::olc_db>::do_try_remove_missing_key(
    unodb::key absent_key) {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  UNODB_ASSERT_FALSE(test_db.remove(absent_key));
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <>
inline void tree_verifier<unodb::olc_db>::try_get(unodb::key k) const noexcept {
  const quiescent_state_on_scope_exit qsbr_after_get{};
  std::ignore = test_db.get(k);
}

extern template class tree_verifier<unodb::db>;
extern template class tree_verifier<unodb::mutex_db>;
extern template class tree_verifier<unodb::olc_db>;

using olc_tree_verifier = tree_verifier<unodb::olc_db>;

}  // namespace unodb::test

#endif  // UNODB_DETAIL_DB_TEST_UTILS_HPP
