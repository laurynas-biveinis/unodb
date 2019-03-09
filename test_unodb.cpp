// Copyright 2019 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <unordered_map>

#include "gtest/gtest.h"  // IWYU pragma: keep

#include "art.hpp"

namespace {

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

// warning: 'ScopedTrace' was marked unused but was used
// [-Wused-but-marked-unused]
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wused-but-marked-unused"
#endif

auto assert_result_eq(unodb::db::get_result result, unodb::value_view expected,
                      int caller_line) noexcept {
  testing::ScopedTrace trace(__FILE__, caller_line, "");
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

#define ASSERT_RESULT_EQ(result, expected) \
  assert_result_eq(result, expected, __LINE__)

class tree_verifier final {
 public:
  explicit tree_verifier(unodb::db &test_db_) : test_db(test_db_) {}

  void insert(unodb::key_type k, unodb::value_view v);

  void check_present_values() const;

  void check_absent_keys(
      std::initializer_list<unodb::key_type> absent_keys) const;

 private:
  unodb::db &test_db;

  std::unordered_map<unodb::key_type, unodb::value_view> values;
};

void tree_verifier::insert(unodb::key_type k, unodb::value_view v) {
  const auto insert_result = values.emplace(k, v);
  assert(insert_result.second);
  ASSERT_TRUE(test_db.insert(k, v));
}

void tree_verifier::check_present_values() const {
  for (const auto &[key, value] : values) {
    ASSERT_RESULT_EQ(test_db.get(key), value);
  }
}

void tree_verifier::check_absent_keys(
    std::initializer_list<unodb::key_type> absent_keys) const {
  for (const auto &absent_key : absent_keys) {
    assert(values.find(absent_key) == values.cend());
    ASSERT_FALSE(test_db.get(absent_key));
  }
}

TEST(UnoDB, single_node_tree_empty_value) {
  unodb::db test_db;
  tree_verifier verifier(test_db);
  verifier.check_absent_keys({1});
  verifier.insert(1, {});

  verifier.check_present_values();
  verifier.check_absent_keys({0});
}

TEST(UnoDB, single_node_tree_nonempty_value) {
  unodb::db test_db;
  tree_verifier verifier(test_db);
  verifier.insert(1, unodb::value_view{test_value_3});

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2});
}

TEST(UnoDB, too_long_value) {
  std::byte fake_val{0x00};
  unodb::value_view too_long{
      &fake_val,
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U};

  unodb::db test_db;
  tree_verifier verifier(test_db);

  ASSERT_THROW((void)test_db.insert(1, too_long), std::length_error);

  verifier.check_absent_keys({1});
}

TEST(UnoDB, expand_leaf_to_node4) {
  unodb::db test_db;
  tree_verifier verifier(test_db);

  verifier.insert(0, unodb::value_view{test_value_2});
  verifier.insert(1, unodb::value_view{test_value_3});

  verifier.check_present_values();
  verifier.check_absent_keys({2});
}

TEST(UnoDB, duplicate_key) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(0, unodb::value_view{test_value_1});
  ASSERT_FALSE(test_db.insert(0, unodb::value_view{test_value_4}));
  verifier.check_present_values();
}

TEST(UnoDB, insert_to_full_node4) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(2, unodb::value_view{test_value_2});
  verifier.insert(4, unodb::value_view{test_value_4});
  verifier.insert(0, unodb::value_view{test_value_1});
  verifier.insert(3, unodb::value_view{test_value_3});

  verifier.check_present_values();
  verifier.check_absent_keys({1, 5});
}

TEST(UnoDB, two_node4) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(1, unodb::value_view{test_value_1});
  verifier.insert(3, unodb::value_view{test_value_3});
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF01, unodb::value_view{test_value_4});

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF00, 2});
}

TEST(UnoDB, db_insert_node_recursion) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(1, unodb::value_view{test_value_1});
  verifier.insert(3, unodb::value_view{test_value_3});
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF0001, unodb::value_view{test_value_4});
  // Then insert a value that shares full prefix with the above node and will
  // ask for a recursive insertion there
  verifier.insert(0xFF0101, unodb::value_view{test_value_2});

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF0100, 0xFF0000, 2});
}

TEST(UnoDB, node16) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(5, unodb::value_view{test_value_5});
  verifier.insert(3, unodb::value_view{test_value_3});
  verifier.insert(4, unodb::value_view{test_value_4});
  verifier.insert(1, unodb::value_view{test_value_1});
  verifier.insert(2, unodb::value_view{test_value_2});

  verifier.check_present_values();
  verifier.check_absent_keys({6, 0x0100, 0xFFFFFFFFFFFFFFFFULL});
}

TEST(UnoDB, full_node16) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(7, unodb::value_view{test_value_1});
  verifier.insert(6, unodb::value_view{test_value_2});
  verifier.insert(5, unodb::value_view{test_value_3});
  verifier.insert(4, unodb::value_view{test_value_4});
  verifier.insert(3, unodb::value_view{test_value_5});
  verifier.insert(2, unodb::value_view{test_value_1});
  verifier.insert(1, unodb::value_view{test_value_2});
  verifier.insert(0, unodb::value_view{test_value_3});
  verifier.insert(8, unodb::value_view{test_value_4});
  verifier.insert(9, unodb::value_view{test_value_5});
  verifier.insert(10, unodb::value_view{test_value_1});
  verifier.insert(11, unodb::value_view{test_value_2});
  verifier.insert(12, unodb::value_view{test_value_3});
  verifier.insert(13, unodb::value_view{test_value_4});
  verifier.insert(14, unodb::value_view{test_value_5});
  verifier.insert(15, unodb::value_view{test_value_1});

  verifier.check_absent_keys({16});
  verifier.check_present_values();
}

TEST(UnoDB, node16_key_prefix_split) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(20, unodb::value_view{test_value_2});
  verifier.insert(10, unodb::value_view{test_value_1});
  verifier.insert(30, unodb::value_view{test_value_3});
  verifier.insert(40, unodb::value_view{test_value_4});
  verifier.insert(50, unodb::value_view{test_value_5});

  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, unodb::value_view{test_value_1});

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x10FF});
}

}  // namespace
