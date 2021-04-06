// Copyright 2019-2021 Laurynas Biveinis
#ifndef UNODB_DB_TEST_UTILS_HPP_
#define UNODB_DB_TEST_UTILS_HPP_

#include "global.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <new>
#include <optional>
#include <type_traits>  // IWYU pragma: keep
#include <unordered_map>

#include <gtest/gtest.h>  // IWYU pragma: keep

#include "art.hpp"
#include "art_common.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"

namespace unodb::test {

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

// warning: 'ScopedTrace' was marked unused but was used
// [-Wused-but-marked-unused]
DISABLE_CLANG_WARNING("-Wused-but-marked-unused")

template <class Db>
void do_assert_result_eq(const Db &db, unodb::key key,
                         unodb::value_view expected, const char *file,
                         int line) noexcept {
  std::ostringstream msg;
  unodb::detail::dump_key(msg, key);
  testing::ScopedTrace trace(file, line, msg.str());
  const auto result = db.get(key);
  if (!result) {
    // LCOV_EXCL_START
    std::cerr << "db.get did not find ";
    unodb::detail::dump_key(std::cerr, key);
    std::cerr << '\n';
    db.dump(std::cerr);
    FAIL();
    // LCOV_EXCL_STOP
  }
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

RESTORE_CLANG_WARNINGS()

template <class Db>
void assert_result_eq(const Db &db, unodb::key key, unodb::value_view expected,
                      const char *file, int line) noexcept {
  do_assert_result_eq(db, key, expected, file, line);
}

template <>
inline void assert_result_eq(const unodb::olc_db &db, unodb::key key,
                             unodb::value_view expected, const char *file,
                             int line) noexcept {
  quiescent_state_on_scope_exit qsbr_after_get{};
  do_assert_result_eq(db, key, expected, file, line);
}

}  // namespace detail

#define ASSERT_VALUE_FOR_KEY(test_db, key, expected) \
  detail::assert_result_eq(test_db, key, expected, __FILE__, __LINE__)

template <class Db>
class tree_verifier final {
 private:
  void do_insert(unodb::key k, unodb::value_view v) {
    ASSERT_TRUE(test_db.insert(k, v));
  }

  void do_remove(unodb::key k, bool bypass_verifier) {
    if (!bypass_verifier) {
      const auto remove_result = values.erase(k);
      ASSERT_EQ(remove_result, 1);
    }
    const auto leaf_count_before = test_db.get_leaf_count();
    const auto mem_use_before = test_db.get_current_memory_use();
    ASSERT_TRUE(leaf_count_before > 0);
    ASSERT_TRUE(mem_use_before > 0);

    if (!test_db.remove(k)) {
      // LCOV_EXCL_START
      std::cerr << "test_db.remove failed for ";
      unodb::detail::dump_key(std::cerr, k);
      std::cerr << '\n';
      test_db.dump(std::cerr);
      FAIL();
      // LCOV_EXCL_STOP
    }

    if (!parallel_test) {
      const auto mem_use_after = test_db.get_current_memory_use();
      ASSERT_TRUE(mem_use_after < mem_use_before);

      const auto leaf_count_after = test_db.get_leaf_count();
      ASSERT_EQ(leaf_count_before - 1, leaf_count_after);
    }
  }

  void do_try_remove_missing_key(unodb::key absent_key) {
    ASSERT_FALSE(test_db.remove(absent_key));
  }

 public:
  explicit constexpr tree_verifier(bool parallel_test_ = false) noexcept
      : parallel_test{parallel_test_} {
    assert_empty();
    assert_increasing_nodes(0, 0, 0, 0);
    assert_shrinking_nodes(0, 0, 0, 0);
    assert_key_prefix_splits(0);
  }

  void insert(unodb::key k, unodb::value_view v, bool bypass_verifier = false) {
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();
    const auto leaf_count_before = parallel_test ? 0 : test_db.get_leaf_count();

    try {
      do_insert(k, v);
    } catch (const std::bad_alloc &) {
      if (!parallel_test) {
        const auto mem_use_after = test_db.get_current_memory_use();
        ASSERT_EQ(mem_use_before, mem_use_after);

        const auto leaf_count_after = test_db.get_leaf_count();
        ASSERT_EQ(leaf_count_before, leaf_count_after);
      }
      throw;
    }

    ASSERT_FALSE(test_db.empty());

    const auto mem_use_after = test_db.get_current_memory_use();
    if (parallel_test)
      ASSERT_TRUE(mem_use_after > 0);
    else
      ASSERT_TRUE(mem_use_before < mem_use_after);

    const auto leaf_count_after = test_db.get_leaf_count();
    if (parallel_test)
      ASSERT_TRUE(leaf_count_after > 0);
    else
      ASSERT_EQ(leaf_count_after, leaf_count_before + 1);

    if (!bypass_verifier) {
      const auto insert_result = values.emplace(k, v);
      ASSERT_TRUE(insert_result.second);
    }
  }

  void insert_key_range(unodb::key start_key, std::size_t count,
                        bool bypass_verifier = false) {
    for (auto key = start_key; key < start_key + count; ++key) {
      insert(key, test_values[key % test_values.size()], bypass_verifier);
    }
  }

  void try_insert(unodb::key k, unodb::value_view v) {
    (void)test_db.insert(k, v);
  }

  void preinsert_key_range_to_verifier_only(unodb::key start_key,
                                            std::size_t count) {
    for (auto key = start_key; key < start_key + count; ++key) {
      const auto insert_result =
          values.emplace(key, test_values[key % test_values.size()]);
      ASSERT_TRUE(insert_result.second);
    }
  }

  void insert_preinserted_key_range(unodb::key start_key, std::size_t count) {
    for (auto key = start_key; key < start_key + count; ++key) {
      do_insert(key, test_values[key % test_values.size()]);
    }
  }

  void remove(unodb::key k, bool bypass_verifier = false) {
    do_remove(k, bypass_verifier);
  }

  void try_remove(unodb::key k) { (void)test_db.remove(k); }

  void attempt_remove_missing_keys(
      std::initializer_list<unodb::key> absent_keys) noexcept {
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();

    for (const auto &absent_key : absent_keys) {
      const auto remove_result = values.erase(absent_key);
      ASSERT_EQ(remove_result, 0);
      do_try_remove_missing_key(absent_key);
      if (!parallel_test) {
        ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
      }
    }
  }

  void try_get(unodb::key k) const noexcept { (void)test_db.get(k); }

  void check_present_values() const noexcept {
    for (const auto &[key, value] : values) {
      ASSERT_VALUE_FOR_KEY(test_db, key, value);
    }
  }

  void check_absent_keys(
      std::initializer_list<unodb::key> absent_keys) const noexcept {
    for (const auto &absent_key : absent_keys) {
      ASSERT_TRUE(values.find(absent_key) == values.cend());
      try_get(absent_key);
    }
  }

  constexpr void assert_empty() const noexcept {
    ASSERT_TRUE(test_db.empty());

    ASSERT_EQ(test_db.get_current_memory_use(), 0);

    assert_node_counts(0, 0, 0, 0, 0);
  }

  void assert_node_counts(
      std::optional<std::uint64_t> leaf_count,
      std::optional<std::uint64_t> inode4_count,
      std::optional<std::uint64_t> inode16_count,
      std::optional<std::uint64_t> inode48_count,
      std::optional<std::uint64_t> inode256_count) const noexcept {
    // Dump the tree to a string. Do not attempt to check the dump format, only
    // that dumping does not crash
    std::stringstream dump_sink;
    test_db.dump(dump_sink);

    if (leaf_count.has_value()) {
      ASSERT_EQ(test_db.get_leaf_count(), *leaf_count);
    }
    if (inode4_count.has_value() &&
        test_db.get_inode4_count() != *inode4_count) {
      // LCOV_EXCL_START
      std::cerr << "inode4 count mismatch! Expected: " << *inode4_count
                << ", actual: " << test_db.get_inode4_count() << '\n';
      test_db.dump(std::cerr);
      FAIL();
      // LCOV_EXCL_STOP
    }
    if (inode16_count.has_value()) {
      ASSERT_EQ(test_db.get_inode16_count(), *inode16_count);
    }
    if (inode48_count.has_value()) {
      ASSERT_EQ(test_db.get_inode48_count(), *inode48_count);
    }
    if (inode256_count.has_value()) {
      ASSERT_EQ(test_db.get_inode256_count(), *inode256_count);
    }
  }

  constexpr void assert_increasing_nodes(
      std::uint64_t created_inode4_count, std::uint64_t inode4_to_inode16_count,
      std::uint64_t inode16_to_inode48_count,
      std::uint64_t inode48_to_inode256_count) const noexcept {
    ASSERT_EQ(test_db.get_created_inode4_count(), created_inode4_count);
    ASSERT_EQ(test_db.get_inode4_to_inode16_count(), inode4_to_inode16_count);
    ASSERT_EQ(test_db.get_inode16_to_inode48_count(), inode16_to_inode48_count);
    ASSERT_EQ(test_db.get_inode48_to_inode256_count(),
              inode48_to_inode256_count);
  }

  constexpr void assert_shrinking_nodes(
      std::uint64_t deleted_inode4_count, std::uint64_t inode16_to_inode4_count,
      std::uint64_t inode48_to_inode16_count,
      std::uint64_t inode256_to_inode48_count) {
    ASSERT_EQ(test_db.get_deleted_inode4_count(), deleted_inode4_count);
    ASSERT_EQ(test_db.get_inode16_to_inode4_count(), inode16_to_inode4_count);
    ASSERT_EQ(test_db.get_inode48_to_inode16_count(), inode48_to_inode16_count);
    ASSERT_EQ(test_db.get_inode256_to_inode48_count(),
              inode256_to_inode48_count);
  }

  constexpr void assert_key_prefix_splits(std::uint64_t splits) const noexcept {
    ASSERT_EQ(test_db.get_key_prefix_splits(), splits);
  }

  void clear() noexcept {
    test_db.clear();
    assert_empty();

    values.clear();
  }

  [[nodiscard]] constexpr Db &get_db() noexcept { return test_db; }

 private:
  Db test_db{};

  std::unordered_map<unodb::key, unodb::value_view> values;

  const bool parallel_test;
};

template <>
inline void tree_verifier<unodb::olc_db>::do_insert(unodb::key k,
                                                    unodb::value_view v) {
  quiescent_state_on_scope_exit qsbr_after_get{};
  ASSERT_TRUE(test_db.insert(k, v));
}

template <>
inline void tree_verifier<unodb::olc_db>::remove(unodb::key k,
                                                 bool bypass_verifier) {
  quiescent_state_on_scope_exit qsbr_after_get{};
  do_remove(k, bypass_verifier);
}

template <>
inline void tree_verifier<unodb::olc_db>::try_remove(unodb::key k) {
  quiescent_state_on_scope_exit qsbr_after_get{};
  (void)test_db.remove(k);
}

template <>
inline void tree_verifier<unodb::olc_db>::do_try_remove_missing_key(
    unodb::key absent_key) {
  quiescent_state_on_scope_exit qsbr_after_get{};
  ASSERT_FALSE(test_db.remove(absent_key));
}

template <>
inline void tree_verifier<unodb::olc_db>::try_get(unodb::key k) const noexcept {
  quiescent_state_on_scope_exit qsbr_after_get{};
  (void)test_db.get(k);
}

extern template class tree_verifier<unodb::db>;
extern template class tree_verifier<unodb::mutex_db>;
extern template class tree_verifier<unodb::olc_db>;

using olc_tree_verifier = tree_verifier<unodb::olc_db>;

}  // namespace unodb::test

#endif  // UNODB_DB_TEST_UTILS_HPP_
