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

constexpr std::array<unodb::value_view, 5> test_values = {
    unodb::value_view{test_value_1}, unodb::value_view{test_value_2},
    unodb::value_view{test_value_3}, unodb::value_view{test_value_4},
    unodb::value_view{test_value_5}};

// warning: 'ScopedTrace' was marked unused but was used
// [-Wused-but-marked-unused]
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wused-but-marked-unused"
#endif

auto assert_result_eq(unodb::key_type key, unodb::db::get_result result,
                      unodb::value_view expected, int caller_line) noexcept {
  std::ostringstream msg;
  msg << "key = " << static_cast<unsigned>(key);
  testing::ScopedTrace trace(__FILE__, caller_line, msg.str());
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

#define ASSERT_VALUE_FOR_KEY(key, expected) \
  assert_result_eq(key, test_db.get(key), expected, __LINE__)

class tree_verifier final {
 public:
  explicit tree_verifier(unodb::db &test_db_) : test_db(test_db_) {}

  void insert(unodb::key_type k, unodb::value_view v);

  void insert_key_range(unodb::key_type start_key, size_t count);

  void remove(unodb::key_type k);

  void attempt_remove_missing(unodb::key_type k);

  void check_present_values() const noexcept;

  void check_absent_keys(
      std::initializer_list<unodb::key_type> absent_keys) const noexcept;

 private:
  unodb::db &test_db;

  std::unordered_map<unodb::key_type, unodb::value_view> values;
};

void tree_verifier::insert(unodb::key_type k, unodb::value_view v) {
  const auto insert_result = values.emplace(k, v);
  ASSERT_TRUE(insert_result.second);
  ASSERT_TRUE(test_db.insert(k, v));
}

void tree_verifier::insert_key_range(unodb::key_type start_key, size_t count) {
  for (unodb::key_type key = start_key; key < start_key + count; key++) {
    insert(key, test_values[key % test_values.size()]);
  }
}

void tree_verifier::remove(unodb::key_type k) {
  const auto remove_result = values.erase(k);
  ASSERT_EQ(remove_result, 1);
  ASSERT_TRUE(test_db.remove(k));
}

void tree_verifier::attempt_remove_missing(unodb::key_type k) {
  const auto remove_result = values.erase(k);
  ASSERT_EQ(remove_result, 0);
  ASSERT_FALSE(test_db.remove(k));
}

void tree_verifier::check_present_values() const noexcept {
  for (const auto &[key, value] : values) {
    ASSERT_VALUE_FOR_KEY(key, value);
  }
}

void tree_verifier::check_absent_keys(
    std::initializer_list<unodb::key_type> absent_keys) const noexcept {
  for (const auto &absent_key : absent_keys) {
    ASSERT_TRUE(values.find(absent_key) == values.cend());
    ASSERT_FALSE(test_db.get(absent_key));
  }
}

TEST(ART, single_node_tree_empty_value) {
  unodb::db test_db;
  tree_verifier verifier{test_db};
  verifier.check_absent_keys({1});
  verifier.insert(1, {});

  verifier.check_present_values();
  verifier.check_absent_keys({0});
}

TEST(ART, single_node_tree_nonempty_value) {
  unodb::db test_db;
  tree_verifier verifier{test_db};
  verifier.insert(1, test_values[2]);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2});
}

TEST(ART, too_long_value) {
  std::byte fake_val{0x00};
  unodb::value_view too_long{
      &fake_val,
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U};

  unodb::db test_db;
  tree_verifier verifier{test_db};

  ASSERT_THROW((void)test_db.insert(1, too_long), std::length_error);

  verifier.check_absent_keys({1});
}

TEST(ART, expand_leaf_to_node4) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(0, test_values[1]);
  verifier.insert(1, test_values[2]);

  verifier.check_present_values();
  verifier.check_absent_keys({2});
}

TEST(ART, duplicate_key) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(0, test_values[0]);
  ASSERT_FALSE(test_db.insert(0, test_values[3]));
  verifier.check_present_values();
}

TEST(ART, insert_to_full_node4) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 4);

  verifier.check_present_values();
  verifier.check_absent_keys({5, 4});
}

TEST(ART, two_node4) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF01, test_values[3]);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF00, 2});
}

TEST(ART, db_insert_node_recursion) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF0001, test_values[3]);
  // Then insert a value that shares full prefix with the above node and will
  // ask for a recursive insertion there
  verifier.insert(0xFF0101, test_values[1]);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF0100, 0xFF0000, 2});
}

TEST(ART, node16) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 5);

  verifier.check_present_values();
  verifier.check_absent_keys({6, 0x0100, 0xFFFFFFFFFFFFFFFFULL});
}

TEST(ART, full_node16) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 16);

  verifier.check_absent_keys({16});
  verifier.check_present_values();
}

TEST(ART, node16_key_prefix_split) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 5);

  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x10FF});
}

TEST(ART, node16_key_insert_order_descending) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(5, test_values[0]);
  verifier.insert(4, test_values[1]);
  verifier.insert(3, test_values[2]);
  verifier.insert(2, test_values[3]);
  verifier.insert(1, test_values[4]);
  verifier.insert(0, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({6});
}

TEST(ART, node48) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 17);

  verifier.check_present_values();
  verifier.check_absent_keys({17});
}

TEST(ART, full_node48) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 48);

  verifier.check_present_values();
  verifier.check_absent_keys({49});
}

TEST(ART, node48_key_prefix_split) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 17);

  // Insert a value that does share full prefix with the current Node48
  verifier.insert(0x100020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 27, 0x100019, 0x100100, 0x110000});
}

TEST(ART, node256) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);

  verifier.check_present_values();
  verifier.check_absent_keys({50});
}

TEST(ART, full_node256) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 256);

  verifier.check_present_values();
  verifier.check_absent_keys({256});
}

TEST(ART, node256_key_prefix_split) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(20, 49);

  // Insert a value that does share full prefix with the current Node256
  verifier.insert(0x100020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({19, 69, 0x100019, 0x100100, 0x110000});
}

TEST(ART, try_delete_from_empty) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.attempt_remove_missing(1);

  verifier.check_absent_keys({1});
}

TEST(ART, single_node_tree_delete) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert(1, test_values[0]);
  verifier.remove(1);
  verifier.check_absent_keys({1});
  verifier.attempt_remove_missing(1);
  verifier.check_absent_keys({1});
}

TEST(ART, node4_attempt_delete_absent) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 4);
  verifier.attempt_remove_missing(0);
  verifier.attempt_remove_missing(6);
  verifier.attempt_remove_missing(0xFF000001);
  verifier.check_absent_keys({0, 6, 0xFF00000});
}

TEST(ART, node4_full_delete_middle_n_beginning) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 4);
  // Delete from Node4 middle
  verifier.remove(2);
  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 5});
  // Delete from Node4 beginning
  verifier.remove(1);
  verifier.check_present_values();
  verifier.check_absent_keys({1, 0, 2, 5});
}

TEST(ART, node4_full_delete_end_n_middle) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 4);
  // Delete from Node4 end
  verifier.remove(4);
  verifier.check_present_values();
  verifier.check_absent_keys({4, 0, 5});
  // Delete from Node4 middle
  verifier.remove(2);
  verifier.check_present_values();
  verifier.check_absent_keys({2, 4, 0, 5});
}

TEST(ART, node4_shrink_to_single_leaf) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 2);
  verifier.remove(1);
  verifier.check_present_values();
  verifier.check_absent_keys({1});
}

TEST(ART, node4_delete_lower_node) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF00, test_values[3]);
  // Make the lower Node4 shrink to a single value leaf
  verifier.remove(0);
  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 0xFF01});
}

TEST(ART, node4_delete_key_prefix_merge) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0x8001, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0x90AA, test_values[3]);
  // And delete it
  verifier.remove(0x90AA);
  verifier.check_present_values();
  verifier.check_absent_keys({0x90AA, 0x8003});
}

TEST(ART, node16_delete_beginning_middle_end) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 16);
  verifier.remove(5);
  verifier.remove(1);
  verifier.remove(16);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 5, 16, 17});
}

TEST(ART, node16_shrink_to_node4_delete_middle) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 5);
  verifier.remove(2);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 6});
}

TEST(ART, node16_shrink_to_node4_delete_beginning) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 5);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 6});
}

TEST(ART, node16_shrink_to_node4_delete_end) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 5);
  verifier.remove(5);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 5, 6});
}

TEST(ART, node16_key_prefix_merge) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 5);
  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node16 one
  verifier.remove(0x1020);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 16, 0x1020});
}

TEST(ART, node48_delete_beginning_middle_end) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 48);
  verifier.remove(30);
  verifier.remove(48);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 30, 48, 49});
}

TEST(ART, node48_shrink_to_node16_delete_middle) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0x80, 17);
  verifier.remove(0x85);

  verifier.check_present_values();
  verifier.check_absent_keys({0x7F, 0x85, 0x91});
}

TEST(ART, node48_shrink_to_node16_delete_beginning) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 17);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 18});
}

TEST(ART, node48_shrink_to_node16_delete_end) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 17);
  verifier.remove(17);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 17, 18});
}

TEST(ART, node48_key_prefix_merge) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 17);
  // Insert a value that does not share full prefix with the current Node48
  verifier.insert(0x2010, test_values[1]);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node48 one
  verifier.remove(0x2010);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 28});
}

TEST(ART, node256_delete_beginning_middle_end) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 256);
  verifier.remove(180);
  verifier.remove(1);
  verifier.remove(256);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 180, 256});
}

TEST(ART, node256_shrink_to_node48_delete_middle) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);
  verifier.remove(25);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 25, 50});
}

TEST(ART, node256_shrink_to_node48_delete_beginning) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 50});
}

TEST(ART, node256_shrink_to_node48_delete_end) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);
  verifier.remove(49);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 49, 50});
}

TEST(ART, node256_key_prefix_merge) {
  unodb::db test_db;
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 49);
  // Insert a value that does not share full prefix with the current Node256
  verifier.insert(0x2010, test_values[1]);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node256 one
  verifier.remove(0x2010);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 60});
}

}  // namespace
