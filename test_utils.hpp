// Copyright 2019-2020 Laurynas Biveinis
#ifndef UNODB_TEST_UTILS_HPP_
#define UNODB_TEST_UTILS_HPP_

#include "global.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <optional>
#include <unordered_map>

#include <gtest/gtest.h>

#include "art.hpp"
#include "mutex_art.hpp"

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

// warning: 'ScopedTrace' was marked unused but was used
// [-Wused-but-marked-unused]
DISABLE_CLANG_WARNING("-Wused-but-marked-unused")

template <class Db>
void assert_result_eq(Db &db, unodb::key key, unodb::value_view expected,
                      int caller_line) noexcept {
  std::ostringstream msg;
  msg << "key = " << static_cast<unsigned>(key);
  testing::ScopedTrace trace(__FILE__, caller_line, msg.str());
  const auto result = db.get(key);
  if (!result) {
    std::cerr << "db.get did not find key: " << key << '\n';
    db.dump(std::cerr);
    FAIL();
  }
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

RESTORE_CLANG_WARNINGS()

#define ASSERT_VALUE_FOR_KEY(test_db, key, expected) \
  assert_result_eq(test_db, key, expected, __LINE__)

template <class Db>
class tree_verifier final {
 public:
  explicit tree_verifier(std::size_t memory_limit = 0,
                         bool parallel_test_ = false) noexcept
      : test_db{memory_limit},
        memory_size_tracked{memory_limit != 0},
        parallel_test{parallel_test_} {
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
      ASSERT_TRUE(test_db.insert(k, v));
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

    const auto mem_use_after =
        memory_size_tracked ? test_db.get_current_memory_use() : 1;
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
      ASSERT_TRUE(test_db.insert(key, test_values[key % test_values.size()]));
    }
  }

  void remove(unodb::key k, bool bypass_verifier = false) {
    if (!bypass_verifier) {
      const auto remove_result = values.erase(k);
      ASSERT_EQ(remove_result, 1);
    }
    const auto leaf_count_before = test_db.get_leaf_count();
    const auto mem_use_before =
        memory_size_tracked ? test_db.get_current_memory_use() : 1;
    ASSERT_TRUE(leaf_count_before > 0);
    ASSERT_TRUE(mem_use_before > 0);

    if (!test_db.remove(k)) {
      std::cerr << "test_db.remove failed for key " << k << '\n';
      test_db.dump(std::cerr);
      FAIL();
    }

    if (!parallel_test) {
      const auto mem_use_after = test_db.get_current_memory_use();
      ASSERT_TRUE(mem_use_after < mem_use_before);

      const auto leaf_count_after = test_db.get_leaf_count();
      ASSERT_EQ(leaf_count_before - 1, leaf_count_after);
    }
  }

  void try_remove(unodb::key k) { (void)test_db.remove(k); }

  void test_insert_until_memory_limit(
      std::optional<std::uint64_t> leaf_count,
      std::optional<std::uint64_t> inode4_count,
      std::optional<std::uint64_t> inode16_count,
      std::optional<std::uint64_t> inode48_count,
      std::optional<std::uint64_t> inode256_count) {
    ASSERT_THROW(insert_key_range(1, 100000), std::bad_alloc);
    check_present_values();
    check_absent_keys({0, values.size() + 1});
    assert_node_counts(leaf_count, inode4_count, inode16_count, inode48_count,
                       inode256_count);

    while (!values.empty()) {
      const auto [key, value] = *values.cbegin();
      remove(key);
      check_absent_keys({key});
      check_present_values();
    }
    ASSERT_EQ(test_db.get_current_memory_use(), 0);
  }

  void attempt_remove_missing_keys(
      std::initializer_list<unodb::key> absent_keys) noexcept {
    const auto mem_use_before =
        parallel_test ? 0 : test_db.get_current_memory_use();

    for (const auto &absent_key : absent_keys) {
      const auto remove_result = values.erase(absent_key);
      ASSERT_EQ(remove_result, 0);
      ASSERT_FALSE(test_db.remove(absent_key));
      if (!parallel_test) {
        ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
      }
    }
  }

  void try_get(unodb::key k) noexcept { (void)test_db.get(k); }

  void check_present_values() const noexcept {
    for (const auto &[key, value] : values) {
      ASSERT_VALUE_FOR_KEY(test_db, key, value);
    }
    // Dump the tree to a string. Do not attempt to check the dump format, only
    // that dumping does not crash
    std::stringstream dump_sink;
    test_db.dump(dump_sink);
  }

  void check_absent_keys(
      std::initializer_list<unodb::key> absent_keys) const noexcept {
    for (const auto &absent_key : absent_keys) {
      ASSERT_TRUE(values.find(absent_key) == values.cend());
      ASSERT_FALSE(test_db.get(absent_key));
    }
  }

  void assert_empty() const noexcept {
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
    if (leaf_count.has_value()) {
      ASSERT_EQ(test_db.get_leaf_count(), *leaf_count);
    }
    if (inode4_count.has_value() &&
        test_db.get_inode4_count() != *inode4_count) {
      std::cerr << "inode4 count mismatch! Expected: " << *inode4_count
                << ", actual: " << test_db.get_inode4_count() << '\n';
      test_db.dump(std::cerr);
      FAIL();
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

  void assert_increasing_nodes(
      std::uint64_t created_inode4_count, std::uint64_t inode4_to_inode16_count,
      std::uint64_t inode16_to_inode48_count,
      std::uint64_t inode48_to_inode256_count) const noexcept {
    ASSERT_EQ(test_db.get_created_inode4_count(), created_inode4_count);
    ASSERT_EQ(test_db.get_inode4_to_inode16_count(), inode4_to_inode16_count);
    ASSERT_EQ(test_db.get_inode16_to_inode48_count(), inode16_to_inode48_count);
    ASSERT_EQ(test_db.get_inode48_to_inode256_count(),
              inode48_to_inode256_count);
  }

  void assert_shrinking_nodes(std::uint64_t deleted_inode4_count,
                              std::uint64_t inode16_to_inode4_count,
                              std::uint64_t inode48_to_inode16_count,
                              std::uint64_t inode256_to_inode48_count) {
    ASSERT_EQ(test_db.get_deleted_inode4_count(), deleted_inode4_count);
    ASSERT_EQ(test_db.get_inode16_to_inode4_count(), inode16_to_inode4_count);
    ASSERT_EQ(test_db.get_inode48_to_inode16_count(), inode48_to_inode16_count);
    ASSERT_EQ(test_db.get_inode256_to_inode48_count(),
              inode256_to_inode48_count);
  }

  void assert_key_prefix_splits(std::uint64_t splits) const noexcept {
    ASSERT_EQ(test_db.get_key_prefix_splits(), splits);
  }

  void clear() noexcept {
    test_db.clear();
    assert_empty();

    values.clear();
  }

  Db &get_db() noexcept { return test_db; }

 private:
  Db test_db;

  std::unordered_map<unodb::key, unodb::value_view> values;

  const bool memory_size_tracked;
  const bool parallel_test;
};

extern template class tree_verifier<unodb::db>;
extern template class tree_verifier<unodb::mutex_db>;

#endif  // UNODB_TEST_UTILS_HPP_
