// Copyright 2019-2025 UnoDB contributors
#ifndef UNODB_DETAIL_DB_TEST_UTILS_HPP
#define UNODB_DETAIL_DB_TEST_UTILS_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__ostream/basic_ostream.h>
// IWYU pragma: no_include <iomanip>
// IWYU pragma: no_include <string>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <tuple>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gtest_utils.hpp"

#include "art.hpp"
#include "art_common.hpp"
#include "art_internal.hpp"
#include "assert.hpp"
#include "mutex_art.hpp"
#include "node_type.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"
#ifndef NDEBUG
#include "test_heap.hpp"
#endif

extern template class unodb::db<std::uint64_t>;
extern template class unodb::mutex_db<std::uint64_t>;
extern template class unodb::olc_db<std::uint64_t>;

namespace unodb::test {

template <class TestDb>
using thread = typename std::conditional_t<
    std::is_same_v<TestDb, unodb::olc_db<typename TestDb::key_type>>,
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
    unodb::value_view{test_value_1},     // [0] { 00              }
    unodb::value_view{test_value_2},     // [1] { 00 02           }
    unodb::value_view{test_value_3},     // [2] { 03 00 01        }
    unodb::value_view{test_value_4},     // [3] { 04 01 00 02     }
    unodb::value_view{test_value_5},     // [4] { 05 F4 FF 00 01  }
    unodb::value_view{empty_test_value}  // [5] {                 }
};

namespace detail {

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wused-but-marked-unused")

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
template <class Db>
void assert_value_eq(const typename Db::get_result &result,
                     unodb::value_view expected) noexcept {
  if constexpr (std::is_same_v<Db, unodb::mutex_db<typename Db::key_type>>) {
    UNODB_DETAIL_ASSERT(result.second.owns_lock());
    UNODB_DETAIL_ASSERT(result.first.has_value());
    UNODB_ASSERT_TRUE(std::ranges::equal(*result.first, expected));
  } else {
    UNODB_DETAIL_ASSERT(result.has_value());
    UNODB_ASSERT_TRUE(std::ranges::equal(*result, expected));
  }
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <class Db>
void do_assert_result_eq(const Db &db, typename Db::key_type key,
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
void assert_result_eq(const Db &db, typename Db::key_type key,
                      unodb::value_view expected, const char *file, int line) {
  if constexpr (std::is_same_v<Db, unodb::olc_db<typename Db::key_type>>) {
    const quiescent_state_on_scope_exit qsbr_after_get{};
    do_assert_result_eq<Db>(db, key, expected, file, line);
  } else {
    do_assert_result_eq<Db>(db, key, expected, file, line);
  }
}

}  // namespace detail

#define ASSERT_VALUE_FOR_KEY(DbType, test_db, key, expected) \
  detail::assert_result_eq<DbType>(test_db, key, expected, __FILE__, __LINE__)

template <class Db>
class [[nodiscard]] tree_verifier final {
 public:
  using key_type = typename Db::key_type;

 private:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26440)
  // Replace std::enable_if_t in the next two methods with
  // requires(std::is_same_v) when LLVM 15 is the oldest supported LLVM version.
  // Earlier versions give errors of different declarations having identical
  // mangled names.
  // NOLINTBEGIN(modernize-use-constraints)
  template <class Db2 = Db>
  std::enable_if_t<!std::is_same_v<Db2, unodb::olc_db<key_type>>, void>
  do_insert(key_type k, unodb::value_view v) {
    UNODB_ASSERT_TRUE(test_db.insert(k, v));
  }

  template <class Db2 = Db>
  std::enable_if_t<std::is_same_v<Db2, unodb::olc_db<key_type>>, void>
  do_insert(key_type k, unodb::value_view v) {
    const quiescent_state_on_scope_exit qsbr_after_get{};
    UNODB_ASSERT_TRUE(test_db.insert(k, v));
  }
  // NOLINTEND(modernize-use-constraints)
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void do_remove(key_type k, bool bypass_verifier) {
    if (!bypass_verifier) {
      const auto remove_result = values.erase(k);
      UNODB_ASSERT_EQ(remove_result, 1);
    }

#ifdef UNODB_DETAIL_WITH_STATS
    const auto node_counts_before = test_db.get_node_counts();
    const auto mem_use_before = test_db.get_current_memory_use();
    UNODB_ASSERT_GT(node_counts_before[as_i<unodb::node_type::LEAF>], 0);
    UNODB_ASSERT_GT(mem_use_before, 0);
    const auto growing_inodes_before = test_db.get_growing_inode_counts();
    const auto shrinking_inodes_before = test_db.get_shrinking_inode_counts();
    const auto key_prefix_splits_before = test_db.get_key_prefix_splits();
#endif  // UNODB_DETAIL_WITH_STATS

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
#ifdef UNODB_DETAIL_WITH_STATS
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
#endif  // UNODB_DETAIL_WITH_STATS
      throw;
    }

#ifdef UNODB_DETAIL_WITH_STATS
    if (!parallel_test) {
      const auto mem_use_after = test_db.get_current_memory_use();
      UNODB_ASSERT_LT(mem_use_after, mem_use_before);

      const auto leaf_count_after =
          test_db.template get_node_count<::unodb::node_type::LEAF>();
      UNODB_ASSERT_EQ(leaf_count_after,
                      node_counts_before[as_i<unodb::node_type::LEAF>] - 1);
    }
#endif  // UNODB_DETAIL_WITH_STATS
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26440)
  // Replace std::enable_if_t in the next two methods with
  // requires(std::is_same_v) when LLVM 15 is the oldest supported LLVM version.
  // Earlier versions give errors of different declarations having identical
  // mangled names.
  // NOLINTBEGIN(modernize-use-constraints)
  template <class Db2 = Db>
  std::enable_if_t<!std::is_same_v<Db2, unodb::olc_db<key_type>>, void>
  do_try_remove_missing_key(key_type absent_key) {
    UNODB_ASSERT_FALSE(test_db.remove(absent_key));
  }

  template <class Db2 = Db>
  std::enable_if_t<std::is_same_v<Db2, unodb::olc_db<key_type>>, void>
  do_try_remove_missing_key(key_type absent_key) {
    const quiescent_state_on_scope_exit qsbr_after_get{};
    UNODB_ASSERT_FALSE(test_db.remove(absent_key));
  }
  // NOLINTEND(modernize-use-constraints)
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 public:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26455)
  explicit tree_verifier(bool parallel_test_ = false)
      : parallel_test{parallel_test_} {
    assert_empty();
#ifdef UNODB_DETAIL_WITH_STATS
    assert_growing_inodes({0, 0, 0, 0});
    assert_shrinking_inodes({0, 0, 0, 0});
    assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void insert(key_type k, unodb::value_view v, bool bypass_verifier = false) {
    const auto empty_before = test_db.empty();
#ifdef UNODB_DETAIL_WITH_STATS
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();
    const auto node_counts_before = test_db.get_node_counts();
    const auto growing_inodes_before = test_db.get_growing_inode_counts();
    const auto shrinking_inodes_before = test_db.get_shrinking_inode_counts();
    const auto key_prefix_splits_before = test_db.get_key_prefix_splits();
#endif  // UNODB_DETAIL_WITH_STATS

    try {
      do_insert(k, v);
    } catch (...) {
      if (!parallel_test) {
        UNODB_ASSERT_EQ(empty_before, test_db.empty());
#ifdef UNODB_DETAIL_WITH_STATS
        UNODB_ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
        UNODB_ASSERT_THAT(test_db.get_node_counts(),
                          ::testing::ElementsAreArray(node_counts_before));
        UNODB_ASSERT_THAT(test_db.get_growing_inode_counts(),
                          ::testing::ElementsAreArray(growing_inodes_before));
        UNODB_ASSERT_THAT(test_db.get_shrinking_inode_counts(),
                          ::testing::ElementsAreArray(shrinking_inodes_before));
        UNODB_ASSERT_EQ(test_db.get_key_prefix_splits(),
                        key_prefix_splits_before);
#endif  // UNODB_DETAIL_WITH_STATS
      }
      throw;
    }

    UNODB_ASSERT_FALSE(test_db.empty());

#ifdef UNODB_DETAIL_WITH_STATS
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
#endif  // UNODB_DETAIL_WITH_STATS

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

  void insert_key_range(key_type start_key, std::size_t count,
                        bool bypass_verifier = false) {
    for (auto key = start_key; key < start_key + count; ++key) {
      insert(key, test_values[key % test_values.size()], bypass_verifier);
    }
  }

  void try_insert(key_type k, unodb::value_view v) {
    std::ignore = test_db.insert(k, v);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void preinsert_key_range_to_verifier_only(key_type start_key,
                                            std::size_t count) {
    for (auto key = start_key; key < start_key + count; ++key) {
      const auto [pos, insert_succeeded] =
          values.try_emplace(key, test_values[key % test_values.size()]);
      (void)pos;
      UNODB_ASSERT_TRUE(insert_succeeded);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void insert_preinserted_key_range(key_type start_key, std::size_t count) {
    for (auto key = start_key; key < start_key + count; ++key) {
      do_insert(key, test_values[key % test_values.size()]);
    }
  }

  // Replace std::enable_if_t in the next four methods with
  // requires(std::is_same_v) when LLVM 15 is the oldest supported LLVM version.
  // Earlier versions give errors of different declarations having identical
  // mangled names.
  // NOLINTBEGIN(modernize-use-constraints)
  template <class Db2 = Db>
  std::enable_if_t<!std::is_same_v<Db2, unodb::olc_db<key_type>>, void> remove(
      key_type k, bool bypass_verifier = false) {
    do_remove(k, bypass_verifier);
  }

  template <class Db2 = Db>
  std::enable_if_t<std::is_same_v<Db2, unodb::olc_db<key_type>>, void> remove(
      key_type k, bool bypass_verifier = false) {
    const quiescent_state_on_scope_exit qsbr_after_get{};
    do_remove(k, bypass_verifier);
  }

  template <class Db2 = Db>
  std::enable_if_t<!std::is_same_v<Db2, unodb::olc_db<key_type>>, void>
  try_remove(key_type k) {
    std::ignore = test_db.remove(k);
  }

  template <class Db2 = Db>
  std::enable_if_t<std::is_same_v<Db2, unodb::olc_db<key_type>>, void>
  try_remove(key_type k) {
    const quiescent_state_on_scope_exit qsbr_after_get{};
    std::ignore = test_db.remove(k);
  }
  // NOLINTEND(modernize-use-constraints)

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void attempt_remove_missing_keys(
      std::initializer_list<key_type> absent_keys) {
#ifdef UNODB_DETAIL_WITH_STATS
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS

    for (const auto absent_key : absent_keys) {
      const auto remove_result = values.erase(absent_key);
      UNODB_ASSERT_EQ(remove_result, 0);
      do_try_remove_missing_key(absent_key);
#ifdef UNODB_DETAIL_WITH_STATS
      if (!parallel_test) {
        UNODB_ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
      }
#endif  // UNODB_DETAIL_WITH_STATS
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  // Replace std::enable_if_t in the next two methods with
  // requires(std::is_same_v) when LLVM 15 is the oldest supported LLVM version.
  // Earlier versions give errors of different declarations having identical
  // mangled names.
  // NOLINTBEGIN(modernize-use-constraints)
  template <class Db2 = Db>
  std::enable_if_t<!std::is_same_v<Db2, unodb::olc_db<key_type>>, void> try_get(
      key_type k) const noexcept(noexcept(this->test_db.get(k))) {
    std::ignore = test_db.get(k);
  }

  template <class Db2 = Db>
  std::enable_if_t<std::is_same_v<Db2, unodb::olc_db<key_type>>, void> try_get(
      key_type k) const noexcept(noexcept(this->test_db.get(k))) {
    const quiescent_state_on_scope_exit qsbr_after_get{};
    std::ignore = test_db.get(k);
  }
  // NOLINTEND(modernize-use-constraints)

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26445)
  void check_present_values() const {
    for (const auto &[key, value] : values) {
      ASSERT_VALUE_FOR_KEY(Db, test_db, key, value);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
  void check_absent_keys(std::initializer_list<key_type> absent_keys) const
      noexcept(noexcept(this->try_get(unused_key))) {
    for (const auto absent_key : absent_keys) {
      UNODB_ASSERT_EQ(values.find(absent_key), values.cend());
      try_get(absent_key);
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  void assert_empty() const {
    UNODB_ASSERT_TRUE(test_db.empty());

#ifdef UNODB_DETAIL_WITH_STATS
    UNODB_ASSERT_EQ(test_db.get_current_memory_use(), 0);

    assert_node_counts({0, 0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
  }

#ifdef UNODB_DETAIL_WITH_STATS

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

#endif  // UNODB_DETAIL_WITH_STATS

  void clear() {
    test_db.clear();
#ifndef NDEBUG
    allocation_failure_injector::reset();
#endif
    assert_empty();

    values.clear();
  }

  [[nodiscard, gnu::pure]] constexpr Db &get_db() noexcept { return test_db; }

  tree_verifier(const tree_verifier &) = delete;
  tree_verifier &operator=(const tree_verifier &) = delete;

 private:
  // Custom comparator is required for key_view.
  struct comparator {
    bool operator()(const key_type &lhs, const key_type &rhs) const {
      if constexpr (std::is_same_v<key_type, unodb::key_view>) {
        return unodb::detail::compare(lhs, rhs) < 0;
      } else {
        return lhs < rhs;
      }
    }
  };

  Db test_db{};

  // Note: The hash map does not support key_view keys in the map.  So
  // we need to switch over to the slower red/black tree for the
  // ground truth map.
  std::map<key_type, unodb::value_view, comparator> values;

  // replaces the use of try_get(0) with a parameterized key type.
  key_type unused_key{};

  const bool parallel_test;
};

// TODO(thompsonbry) variable length keys. declare key_view variants
// here.

using u64_db = unodb::db<std::uint64_t>;
using u64_mutex_db = unodb::mutex_db<std::uint64_t>;
using u64_olc_db = unodb::olc_db<std::uint64_t>;

extern template class tree_verifier<u64_db>;
extern template class tree_verifier<u64_mutex_db>;
extern template class tree_verifier<u64_olc_db>;

}  // namespace unodb::test

#endif  // UNODB_DETAIL_DB_TEST_UTILS_HPP
