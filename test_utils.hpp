// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_TEST_UTILS_HPP_
#define UNODB_TEST_UTILS_HPP_

#include "global.hpp"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <new>
#include <unordered_map>

#include "gtest/gtest.h"

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

constexpr std::array<unodb::value_view, 5> test_values = {
    unodb::value_view{test_value_1}, unodb::value_view{test_value_2},
    unodb::value_view{test_value_3}, unodb::value_view{test_value_4},
    unodb::value_view{test_value_5}};

void assert_result_eq(unodb::key key, unodb::get_result result,
                      unodb::value_view expected, int caller_line) noexcept;

#define ASSERT_VALUE_FOR_KEY(key, expected) \
  assert_result_eq(key, test_db.get(key), expected, __LINE__)

template <class Db>
class tree_verifier final {
 public:
  explicit tree_verifier(std::size_t memory_limit = 0) noexcept
      : test_db{memory_limit}, memory_size_tracked(memory_limit != 0) {
    assert_empty();
  }

  void insert(unodb::key k, unodb::value_view v, bool bypass_verifier = false) {
    const auto mem_use_before = test_db.get_current_memory_use();
    try {
      ASSERT_TRUE(test_db.insert(k, v));
    } catch (const std::bad_alloc &) {
      const auto mem_use_after = test_db.get_current_memory_use();
      ASSERT_EQ(mem_use_before, mem_use_after);
      throw;
    }
    ASSERT_FALSE(test_db.empty());
    const auto mem_use_after =
        memory_size_tracked ? test_db.get_current_memory_use() : 1;
    ASSERT_TRUE(mem_use_before < mem_use_after);
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
    const auto mem_use_before =
        memory_size_tracked ? test_db.get_current_memory_use() : 1;
    ASSERT_TRUE(test_db.remove(k));
    const auto mem_use_after = test_db.get_current_memory_use();
    ASSERT_TRUE(mem_use_after < mem_use_before);
  }

  void try_remove(unodb::key k) { (void)test_db.remove(k); }

  void test_insert_until_memory_limit() {
    ASSERT_THROW(insert_key_range(1, 100000), std::bad_alloc);
    check_present_values();
    check_absent_keys({0, values.size() + 1});
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
    const auto mem_use_before = test_db.get_current_memory_use();
    for (const auto &absent_key : absent_keys) {
      const auto remove_result = values.erase(absent_key);
      ASSERT_EQ(remove_result, 0);
      ASSERT_FALSE(test_db.remove(absent_key));
      ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
    }
  }

  void try_get(unodb::key k) noexcept { (void)test_db.get(k); }

  void check_present_values() const noexcept {
    for (const auto &[key, value] : values) {
      ASSERT_VALUE_FOR_KEY(key, value);
    }
#ifndef NDEBUG
    // Dump the tree to a string. Do not attempt to check the dump format, only
    // that dumping does not crash
    std::stringstream dump_sink;
    test_db.dump(dump_sink);
#endif
  }

  void check_absent_keys(std::initializer_list<unodb::key> absent_keys) const
      noexcept {
    for (const auto &absent_key : absent_keys) {
      ASSERT_TRUE(values.find(absent_key) == values.cend());
      ASSERT_FALSE(test_db.get(absent_key));
    }
  }

  void assert_empty() const noexcept {
    ASSERT_TRUE(test_db.empty());
    ASSERT_EQ(test_db.get_current_memory_use(), 0);
  }

  Db &get_db() noexcept { return test_db; }

 private:
  Db test_db;

  std::unordered_map<unodb::key, unodb::value_view> values;

  const bool memory_size_tracked;
};

extern template class tree_verifier<unodb::db>;
extern template class tree_verifier<unodb::mutex_db>;

#endif  // UNODB_TEST_UTILS_HPP_
