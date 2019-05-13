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
DISABLE_CLANG_WARNING("-Wused-but-marked-unused")

auto assert_result_eq(unodb::key_type key, unodb::db::get_result result,
                      unodb::value_view expected, int caller_line) noexcept {
  std::ostringstream msg;
  msg << "key = " << static_cast<unsigned>(key);
  testing::ScopedTrace trace(__FILE__, caller_line, msg.str());
  ASSERT_TRUE(result);
  ASSERT_TRUE(std::equal(result->cbegin(), result->cend(), expected.cbegin(),
                         expected.cend()));
}

RESTORE_CLANG_WARNINGS()

#define ASSERT_VALUE_FOR_KEY(key, expected) \
  assert_result_eq(key, test_db.get(key), expected, __LINE__)

class tree_verifier final {
 public:
  explicit tree_verifier(unodb::db &test_db_) : test_db(test_db_) {}

  void insert(unodb::key_type k, unodb::value_view v);

  void insert_key_range(unodb::key_type start_key, size_t count);

  void remove(unodb::key_type k);

  void test_insert_until_memory_limit();

  void attempt_remove_missing_keys(
      std::initializer_list<unodb::key_type> absent_keys) noexcept;

  void check_present_values() const noexcept;

  void check_absent_keys(
      std::initializer_list<unodb::key_type> absent_keys) const noexcept;

 private:
  unodb::db &test_db;

  std::unordered_map<unodb::key_type, unodb::value_view> values;
};

void tree_verifier::insert(unodb::key_type k, unodb::value_view v) {
  const auto mem_use_before = test_db.get_current_memory_use();
  try {
    ASSERT_TRUE(test_db.insert(k, v));
  } catch (const std::bad_alloc &) {
    const auto mem_use_after = test_db.get_current_memory_use();
    ASSERT_EQ(mem_use_before, mem_use_after);
    throw;
  }
  const auto mem_use_after = test_db.get_current_memory_use();
  ASSERT_TRUE(mem_use_before < mem_use_after);
  const auto insert_result = values.emplace(k, v);
  ASSERT_TRUE(insert_result.second);
}

void tree_verifier::insert_key_range(unodb::key_type start_key, size_t count) {
  for (unodb::key_type key = start_key; key < start_key + count; key++) {
    insert(key, test_values[key % test_values.size()]);
  }
}

void tree_verifier::test_insert_until_memory_limit() {
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

void tree_verifier::remove(unodb::key_type k) {
  const auto remove_result = values.erase(k);
  ASSERT_EQ(remove_result, 1);
  const auto mem_use_before = test_db.get_current_memory_use();
  ASSERT_TRUE(test_db.remove(k));
  ASSERT_TRUE(test_db.get_current_memory_use() < mem_use_before);
}

void tree_verifier::attempt_remove_missing_keys(
    std::initializer_list<unodb::key_type> absent_keys) noexcept {
  const auto mem_use_before = test_db.get_current_memory_use();
  for (const auto &absent_key : absent_keys) {
    const auto remove_result = values.erase(absent_key);
    ASSERT_EQ(remove_result, 0);
    ASSERT_FALSE(test_db.remove(absent_key));
    ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
  }
}

void tree_verifier::check_present_values() const noexcept {
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

void tree_verifier::check_absent_keys(
    std::initializer_list<unodb::key_type> absent_keys) const noexcept {
  for (const auto &absent_key : absent_keys) {
    ASSERT_TRUE(values.find(absent_key) == values.cend());
    ASSERT_FALSE(test_db.get(absent_key));
  }
}

TEST(ART, single_node_tree_empty_value) {
  unodb::db test_db{1024};
  tree_verifier verifier{test_db};
  verifier.check_absent_keys({1});
  verifier.insert(1, {});

  verifier.check_present_values();
  verifier.check_absent_keys({0});
}

TEST(ART, single_node_tree_nonempty_value) {
  unodb::db test_db{1024};
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
  unodb::db test_db{1024};
  tree_verifier verifier{test_db};

  verifier.insert(0, test_values[1]);
  verifier.insert(1, test_values[2]);

  verifier.check_present_values();
  verifier.check_absent_keys({2});
}

TEST(ART, duplicate_key) {
  unodb::db test_db{1024};
  tree_verifier verifier{test_db};

  verifier.insert(0, test_values[0]);
  const auto mem_use_before = test_db.get_current_memory_use();
  ASSERT_FALSE(test_db.insert(0, test_values[3]));
  ASSERT_EQ(mem_use_before, test_db.get_current_memory_use());
  verifier.check_present_values();
}

TEST(ART, insert_to_full_node4) {
  unodb::db test_db{1024};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 4);

  verifier.check_present_values();
  verifier.check_absent_keys({5, 4});
}

TEST(ART, two_node4) {
  unodb::db test_db{2048};
  tree_verifier verifier{test_db};

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF01, test_values[3]);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF00, 2});
}

TEST(ART, db_insert_node_recursion) {
  unodb::db test_db{2048};
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
  unodb::db test_db{2048};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 5);

  verifier.check_present_values();
  verifier.check_absent_keys({6, 0x0100, 0xFFFFFFFFFFFFFFFFULL});
}

TEST(ART, full_node16) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 16);

  verifier.check_absent_keys({16});
  verifier.check_present_values();
}

TEST(ART, node16_key_prefix_split) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 5);

  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x10FF});
}

TEST(ART, node16_key_insert_order_descending) {
  unodb::db test_db{4096};
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
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 17);

  verifier.check_present_values();
  verifier.check_absent_keys({17});
}

TEST(ART, full_node48) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 48);

  verifier.check_present_values();
  verifier.check_absent_keys({49});
}

TEST(ART, node48_key_prefix_split) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(10, 17);

  // Insert a value that does share full prefix with the current Node48
  verifier.insert(0x100020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 27, 0x100019, 0x100100, 0x110000});
}

TEST(ART, node256) {
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);

  verifier.check_present_values();
  verifier.check_absent_keys({50});
}

TEST(ART, full_node256) {
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0, 256);

  verifier.check_present_values();
  verifier.check_absent_keys({256});
}

TEST(ART, node256_key_prefix_split) {
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(20, 49);

  // Insert a value that does share full prefix with the current Node256
  verifier.insert(0x100020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({19, 69, 0x100019, 0x100100, 0x110000});
}

TEST(ART, try_delete_from_empty) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.attempt_remove_missing_keys({1});

  verifier.check_absent_keys({1});
}

TEST(ART, single_node_tree_delete) {
  unodb::db test_db{1024};
  tree_verifier verifier{test_db};

  verifier.insert(1, test_values[0]);
  verifier.remove(1);
  verifier.check_absent_keys({1});
  verifier.attempt_remove_missing_keys({1});
  verifier.check_absent_keys({1});
}

TEST(ART, node4_attempt_delete_absent) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 4);
  verifier.attempt_remove_missing_keys({0, 6, 0xFF000001});
  verifier.check_absent_keys({0, 6, 0xFF00000});
}

TEST(ART, node4_full_delete_middle_n_beginning) {
  unodb::db test_db{2048};
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
  unodb::db test_db{2048};
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
  unodb::db test_db{2048};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 2);
  verifier.remove(1);
  verifier.check_present_values();
  verifier.check_absent_keys({1});
}

TEST(ART, node4_delete_lower_node) {
  unodb::db test_db{2048};
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
  unodb::db test_db{2048};
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
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 16);
  verifier.remove(5);
  verifier.remove(1);
  verifier.remove(16);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 5, 16, 17});
}

TEST(ART, node16_shrink_to_node4_delete_middle) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 5);
  verifier.remove(2);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 6});
}

TEST(ART, node16_shrink_to_node4_delete_beginning) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 5);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 6});
}

TEST(ART, node16_shrink_to_node4_delete_end) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 5);
  verifier.remove(5);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 5, 6});
}

TEST(ART, node16_key_prefix_merge) {
  unodb::db test_db{4096};
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
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 48);
  verifier.remove(30);
  verifier.remove(48);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 30, 48, 49});
}

TEST(ART, node48_shrink_to_node16_delete_middle) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(0x80, 17);
  verifier.remove(0x85);

  verifier.check_present_values();
  verifier.check_absent_keys({0x7F, 0x85, 0x91});
}

TEST(ART, node48_shrink_to_node16_delete_beginning) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 17);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 18});
}

TEST(ART, node48_shrink_to_node16_delete_end) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 17);
  verifier.remove(17);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 17, 18});
}

TEST(ART, node48_key_prefix_merge) {
  unodb::db test_db{10240};
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
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 256);
  verifier.remove(180);
  verifier.remove(1);
  verifier.remove(256);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 180, 256});
}

TEST(ART, node256_shrink_to_node48_delete_middle) {
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);
  verifier.remove(25);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 25, 50});
}

TEST(ART, node256_shrink_to_node48_delete_beginning) {
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 50});
}

TEST(ART, node256_shrink_to_node48_delete_end) {
  unodb::db test_db{20480};
  tree_verifier verifier{test_db};

  verifier.insert_key_range(1, 49);
  verifier.remove(49);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 49, 50});
}

TEST(ART, node256_key_prefix_merge) {
  unodb::db test_db{20480};
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

TEST(ART, missing_key_with_present_prefix) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert(0x010000, test_values[0]);
  verifier.insert(0x000001, test_values[1]);
  verifier.insert(0x010001, test_values[2]);

  verifier.attempt_remove_missing_keys({0x000002, 0x010100, 0x010002});
}

TEST(ART, missing_key_matching_internal_node_path) {
  unodb::db test_db{10240};
  tree_verifier verifier{test_db};

  verifier.insert(0x0100, test_values[0]);
  verifier.insert(0x0200, test_values[1]);
  verifier.attempt_remove_missing_keys({0x0101, 0x0202});
}

TEST(ART, memory_limit_below_minimum) {
  unodb::db test_db{1};
  tree_verifier verifier{test_db};
  verifier.test_insert_until_memory_limit();
}

// It was one leaf at the time of writing the test, this is not
// guaranteed later
TEST(ART, memory_limit_one_leaf) {
  unodb::db test_db{20};
  tree_verifier verifier{test_db};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, memory_limit_one_node4) {
  unodb::db test_db{80};
  tree_verifier verifier{test_db};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, memory_limit_one_node16) {
  unodb::db test_db{320};
  tree_verifier verifier{test_db};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, memory_limit_one_node48) {
  unodb::db test_db{1024};
  tree_verifier verifier{test_db};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, memory_limit_one_node256) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, memory_accounting_duplicate_key_insert) {
  unodb::db test_db{2048};
  tree_verifier verifier{test_db};
  verifier.insert(0, test_values[0]);
  ASSERT_FALSE(test_db.insert(0, test_values[1]));
  verifier.remove(0);
  ASSERT_EQ(test_db.get_current_memory_use(), 0);
}

TEST(ART, node48_insert_into_deleted_slot) {
  unodb::db test_db{4096};
  tree_verifier verifier{test_db};
  verifier.insert(16865361447928765957ULL, test_values[0]);
  verifier.insert(7551546784238320931ULL, test_values[1]);
  verifier.insert(10913915230368519832ULL, test_values[2]);
  verifier.insert(3754602112003529886ULL, test_values[3]);
  verifier.insert(15202487832924025715ULL, test_values[4]);
  verifier.insert(501264303707694295ULL, test_values[0]);
  verifier.insert(9228847637821057196ULL, test_values[1]);
  verifier.insert(4772373217231458680ULL, test_values[2]);
  verifier.insert(10396278540561456315ULL, test_values[3]);
  verifier.insert(16646085826334346534ULL, test_values[4]);
  verifier.insert(3854084731240466350ULL, test_values[0]);
  verifier.insert(12957550352669724359ULL, test_values[1]);
  verifier.insert(6583227679421302512ULL, test_values[2]);
  verifier.insert(6829398721825682578ULL, test_values[3]);
  verifier.insert(11455392605080430684ULL, test_values[4]);
  verifier.insert(10176313584012002900ULL, test_values[0]);
  verifier.insert(13700634388772836888ULL, test_values[1]);
  verifier.insert(17872125209760305988ULL, test_values[2]);
  verifier.remove(6583227679421302512ULL);
  verifier.insert(0, test_values[0]);
  verifier.check_present_values();
}

}  // namespace
