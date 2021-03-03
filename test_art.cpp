// Copyright 2019-2021 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>

#include <gtest/gtest.h>  // IWYU pragma: keep

#include "art.hpp"
#include "db_test_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace {

template <class Db>
class ARTCorrectnessTest : public ::testing::Test {
 public:
  using Test::Test;
};

using ARTTypes = ::testing::Types<unodb::db, unodb::mutex_db, unodb::olc_db>;

// Because Google thinks
// error: must specify at least one argument for '...' parameter of variadic
// macro
//       [-Werror,-Wgnu-zero-variadic-macro-arguments]
// TYPED_TEST_CASE(ARTCorrectnessTest, ARTTypes);
//                                             ^
// is not a bug: https://github.com/google/googletest/issues/2271
DISABLE_CLANG_WARNING("-Wgnu-zero-variadic-macro-arguments")

TYPED_TEST_CASE(ARTCorrectnessTest, ARTTypes);

RESTORE_CLANG_WARNINGS()

// Because Google thinks
// error: 'void {anonymous}::ARTCorrectnessTest_single_node_tree_empty_value_
// Test<gtest_TypeParam_>::TestBody() [with gtest_TypeParam_ = unodb::db]'
// can be marked override [-Werror=suggest-override] is not a bug:
// https://github.com/google/googletest/issues/1063

DISABLE_GCC_WARNING("-Wsuggest-override")

TYPED_TEST(ARTCorrectnessTest, SingleNodeTreeEmptyValue) {
  tree_verifier<TypeParam> verifier;
  verifier.check_absent_keys({1});
  verifier.insert(1, {});
  verifier.assert_node_counts(1, 0, 0, 0, 0);
  verifier.assert_increasing_nodes(0, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0});
}

TYPED_TEST(ARTCorrectnessTest, SingleNodeTreeNonemptyValue) {
  tree_verifier<TypeParam> verifier;
  verifier.insert(1, test_values[2]);
  verifier.assert_node_counts(1, 0, 0, 0, 0);
  verifier.assert_increasing_nodes(0, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2});
}

TYPED_TEST(ARTCorrectnessTest, TooLongValue) {
  std::byte fake_val{0x00};
  unodb::value_view too_long{
      &fake_val,
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
          1U};

  tree_verifier<TypeParam> verifier;

  ASSERT_THROW((void)verifier.get_db().insert(1, too_long), std::length_error);

  verifier.check_absent_keys({1});
  verifier.assert_empty();
  verifier.assert_increasing_nodes(0, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, ExpandLeafToNode4) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(0, test_values[1]);
  verifier.assert_node_counts(1, 0, 0, 0, 0);
  verifier.assert_increasing_nodes(0, 0, 0, 0);

  verifier.insert(1, test_values[2]);
  verifier.assert_node_counts(2, 1, 0, 0, 0);
  verifier.assert_increasing_nodes(1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({2});
}

TYPED_TEST(ARTCorrectnessTest, DuplicateKey) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(0, test_values[0]);
  verifier.assert_node_counts(1, 0, 0, 0, 0);

  const auto mem_use_before = verifier.get_db().get_current_memory_use();
  ASSERT_FALSE(verifier.get_db().insert(0, test_values[3]));
  ASSERT_EQ(mem_use_before, verifier.get_db().get_current_memory_use());

  verifier.assert_node_counts(1, 0, 0, 0, 0);
  verifier.assert_increasing_nodes(0, 0, 0, 0);
  verifier.check_present_values();
}

TYPED_TEST(ARTCorrectnessTest, InsertToFullNode4) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 4);
  verifier.assert_node_counts(4, 1, 0, 0, 0);
  verifier.assert_increasing_nodes(1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({5, 4});
}

TYPED_TEST(ARTCorrectnessTest, TwoNode4) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);
  verifier.assert_increasing_nodes(1, 0, 0, 0);

  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF01, test_values[3]);
  verifier.assert_node_counts(3, 2, 0, 0, 0);
  verifier.assert_increasing_nodes(2, 0, 0, 0);
  verifier.assert_key_prefix_splits(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF00, 2});
}

TYPED_TEST(ARTCorrectnessTest, DbInsertNodeRecursion) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF0001, test_values[3]);
  verifier.assert_increasing_nodes(2, 0, 0, 0);
  verifier.assert_key_prefix_splits(1);

  // Then insert a value that shares full prefix with the above node and will
  // ask for a recursive insertion there
  verifier.insert(0xFF0101, test_values[1]);
  verifier.assert_node_counts(4, 3, 0, 0, 0);
  verifier.assert_increasing_nodes(3, 0, 0, 0);
  verifier.assert_key_prefix_splits(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF0100, 0xFF0000, 2});
}

TYPED_TEST(ARTCorrectnessTest, Node16) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 4);
  verifier.check_present_values();
  verifier.insert(5, test_values[0]);
  verifier.assert_node_counts(5, 0, 1, 0, 0);
  verifier.assert_increasing_nodes(1, 1, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({6, 0x0100, 0xFFFFFFFFFFFFFFFFULL});
}

TYPED_TEST(ARTCorrectnessTest, FullNode16) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 16);
  verifier.assert_node_counts(16, 0, 1, 0, 0);
  verifier.assert_increasing_nodes(1, 1, 0, 0);

  verifier.check_absent_keys({16});
  verifier.check_present_values();
}

TYPED_TEST(ARTCorrectnessTest, Node16KeyPrefixSplit) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 5);

  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);
  verifier.assert_node_counts(6, 1, 1, 0, 0);
  verifier.assert_increasing_nodes(2, 1, 0, 0);
  verifier.assert_key_prefix_splits(1);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x10FF});
}

TYPED_TEST(ARTCorrectnessTest, Node16KeyInsertOrderDescending) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(5, test_values[0]);
  verifier.insert(4, test_values[1]);
  verifier.insert(3, test_values[2]);
  verifier.insert(2, test_values[3]);
  verifier.insert(1, test_values[4]);
  verifier.insert(0, test_values[0]);
  verifier.assert_node_counts(6, 0, 1, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({6});
}

TYPED_TEST(ARTCorrectnessTest, Node48) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 17);
  verifier.assert_node_counts(17, 0, 0, 1, 0);
  verifier.assert_increasing_nodes(1, 1, 1, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({17});
}

TYPED_TEST(ARTCorrectnessTest, FullNode48) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 48);
  verifier.assert_node_counts(48, 0, 0, 1, 0);
  verifier.assert_increasing_nodes(1, 1, 1, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({49});
}

TYPED_TEST(ARTCorrectnessTest, Node48KeyPrefixSplit) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 17);
  verifier.assert_node_counts(17, 0, 0, 1, 0);
  verifier.assert_increasing_nodes(1, 1, 1, 0);
  verifier.assert_key_prefix_splits(0);

  // Insert a value that does share full prefix with the current Node48
  verifier.insert(0x100020, test_values[0]);
  verifier.assert_node_counts(18, 1, 0, 1, 0);
  verifier.assert_increasing_nodes(2, 1, 1, 0);
  verifier.assert_key_prefix_splits(1);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 27, 0x100019, 0x100100, 0x110000});
}

TYPED_TEST(ARTCorrectnessTest, Node256) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
  verifier.assert_node_counts(49, 0, 0, 0, 1);
  verifier.assert_increasing_nodes(1, 1, 1, 1);

  verifier.check_present_values();
  verifier.check_absent_keys({50});
}

TYPED_TEST(ARTCorrectnessTest, FullNode256) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 256);
  verifier.assert_node_counts(256, 0, 0, 0, 1);
  verifier.assert_increasing_nodes(1, 1, 1, 1);

  verifier.check_present_values();
  verifier.check_absent_keys({256});
}

TYPED_TEST(ARTCorrectnessTest, Node256KeyPrefixSplit) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(20, 49);
  verifier.assert_node_counts(49, 0, 0, 0, 1);
  verifier.assert_increasing_nodes(1, 1, 1, 1);
  verifier.assert_key_prefix_splits(0);

  // Insert a value that does share full prefix with the current Node256
  verifier.insert(0x100020, test_values[0]);
  verifier.assert_node_counts(50, 1, 0, 0, 1);
  verifier.assert_increasing_nodes(2, 1, 1, 1);
  verifier.assert_key_prefix_splits(1);

  verifier.check_present_values();
  verifier.check_absent_keys({19, 69, 0x100019, 0x100100, 0x110000});
}

TYPED_TEST(ARTCorrectnessTest, TryDeleteFromEmpty) {
  tree_verifier<TypeParam> verifier;

  verifier.attempt_remove_missing_keys({1});
  verifier.assert_empty();
  verifier.check_absent_keys({1});
}

TYPED_TEST(ARTCorrectnessTest, SingleNodeTreeDelete) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(1, test_values[0]);
  verifier.remove(1);
  verifier.assert_empty();
  verifier.check_absent_keys({1});
  verifier.attempt_remove_missing_keys({1});
  verifier.check_absent_keys({1});
}

TYPED_TEST(ARTCorrectnessTest, SingleNodeTreeAttemptDeleteAbsent) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(2, test_values[1]);
  verifier.attempt_remove_missing_keys({1, 3, 0xFF02});
  verifier.check_present_values();
  verifier.assert_node_counts(1, 0, 0, 0, 0);
  verifier.check_absent_keys({1, 3, 0xFF02});
}

TYPED_TEST(ARTCorrectnessTest, Node4AttemptDeleteAbsent) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);
  verifier.attempt_remove_missing_keys({0, 6, 0xFF000001});
  verifier.assert_node_counts(4, 1, 0, 0, 0);

  verifier.check_absent_keys({0, 6, 0xFF00000});
}

TYPED_TEST(ARTCorrectnessTest, Node4FullDeleteMiddleAndBeginning) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);
  // Delete from Node4 middle
  verifier.remove(2);
  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 5});
  // Delete from Node4 beginning
  verifier.remove(1);
  verifier.check_present_values();
  verifier.check_absent_keys({1, 0, 2, 5});

  verifier.assert_node_counts(2, 1, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node4FullDeleteEndAndMiddle) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);
  // Delete from Node4 end
  verifier.remove(4);
  verifier.check_present_values();
  verifier.check_absent_keys({4, 0, 5});
  // Delete from Node4 middle
  verifier.remove(2);
  verifier.check_present_values();
  verifier.check_absent_keys({2, 4, 0, 5});

  verifier.assert_node_counts(2, 1, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node4ShrinkToSingleLeaf) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 2);
  verifier.assert_shrinking_nodes(0, 0, 0, 0);

  verifier.remove(1);
  verifier.assert_shrinking_nodes(1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({1});
  verifier.assert_node_counts(1, 0, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node4DeleteLowerNode) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF00, test_values[3]);
  verifier.assert_shrinking_nodes(0, 0, 0, 0);
  verifier.assert_key_prefix_splits(1);

  // Make the lower Node4 shrink to a single value leaf
  verifier.remove(0);
  verifier.assert_shrinking_nodes(1, 0, 0, 0);
  verifier.assert_key_prefix_splits(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 0xFF01});
  verifier.assert_node_counts(2, 1, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node4DeleteKeyPrefixMerge) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0x8001, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0x90AA, test_values[3]);
  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts(3, 2, 0, 0, 0);

  // And delete it
  verifier.remove(0x90AA);
  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts(2, 1, 0, 0, 0);
  verifier.assert_shrinking_nodes(1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0x90AA, 0x8003});

  verifier.assert_node_counts(2, 1, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node16DeleteBeginningMiddleEnd) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 16);
  verifier.remove(5);
  verifier.remove(1);
  verifier.remove(16);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 5, 16, 17});

  verifier.assert_node_counts(13, 0, 1, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node16ShrinkToNode4DeleteMiddle) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 5);
  verifier.assert_node_counts(5, 0, 1, 0, 0);

  verifier.remove(2);
  verifier.assert_shrinking_nodes(0, 1, 0, 0);
  verifier.assert_node_counts(4, 1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 6});
}

TYPED_TEST(ARTCorrectnessTest, Node16ShrinkToNode4DeleteBeginning) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 5);
  verifier.assert_node_counts(5, 0, 1, 0, 0);

  verifier.remove(1);
  verifier.assert_shrinking_nodes(0, 1, 0, 0);
  verifier.assert_node_counts(4, 1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 6});
}

TYPED_TEST(ARTCorrectnessTest, Node16ShrinkToNode4DeleteEnd) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 5);
  verifier.assert_node_counts(5, 0, 1, 0, 0);

  verifier.remove(5);
  verifier.assert_shrinking_nodes(0, 1, 0, 0);
  verifier.assert_node_counts(4, 1, 0, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 5, 6});
}

TYPED_TEST(ARTCorrectnessTest, Node16KeyPrefixMerge) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 5);
  // Insert a value that does not share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);
  verifier.assert_node_counts(6, 1, 1, 0, 0);
  verifier.assert_key_prefix_splits(1);

  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node16 one
  verifier.remove(0x1020);
  verifier.assert_shrinking_nodes(1, 0, 0, 0);
  verifier.assert_node_counts(5, 0, 1, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 16, 0x1020});
}

TYPED_TEST(ARTCorrectnessTest, Node48DeleteBeginningMiddleEnd) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 48);
  verifier.remove(30);
  verifier.remove(48);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 30, 48, 49});

  verifier.assert_node_counts(45, 0, 0, 1, 0);
}

TYPED_TEST(ARTCorrectnessTest, Node48ShrinkToNode16DeleteMiddle) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0x80, 17);
  verifier.assert_node_counts(17, 0, 0, 1, 0);

  verifier.remove(0x85);
  verifier.assert_shrinking_nodes(0, 0, 1, 0);
  verifier.assert_node_counts(16, 0, 1, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0x7F, 0x85, 0x91});
}

TYPED_TEST(ARTCorrectnessTest, Node48ShrinkToNode16DeleteBeginning) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 17);
  verifier.assert_node_counts(17, 0, 0, 1, 0);

  verifier.remove(1);
  verifier.assert_shrinking_nodes(0, 0, 1, 0);
  verifier.assert_node_counts(16, 0, 1, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 18});
}

TYPED_TEST(ARTCorrectnessTest, Node48ShrinkToNode16DeleteEnd) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 17);
  verifier.assert_node_counts(17, 0, 0, 1, 0);

  verifier.remove(17);
  verifier.assert_shrinking_nodes(0, 0, 1, 0);
  verifier.assert_node_counts(16, 0, 1, 0, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 17, 18});
}

TYPED_TEST(ARTCorrectnessTest, Node48KeyPrefixMerge) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 17);
  // Insert a value that does not share full prefix with the current Node48
  verifier.insert(0x2010, test_values[1]);
  verifier.assert_node_counts(18, 1, 0, 1, 0);

  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node48 one
  verifier.remove(0x2010);
  verifier.assert_shrinking_nodes(1, 0, 0, 0);
  verifier.assert_node_counts(17, 0, 0, 1, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 28});
}

TYPED_TEST(ARTCorrectnessTest, Node256DeleteBeginningMiddleEnd) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 256);
  verifier.remove(180);
  verifier.remove(1);
  verifier.remove(256);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 180, 256});
  verifier.assert_node_counts(253, 0, 0, 0, 1);
}

TYPED_TEST(ARTCorrectnessTest, Node256ShrinkToNode48DeleteMiddle) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
  verifier.assert_node_counts(49, 0, 0, 0, 1);

  verifier.remove(25);
  verifier.assert_shrinking_nodes(0, 0, 0, 1);
  verifier.assert_node_counts(48, 0, 0, 1, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 25, 50});
}

TYPED_TEST(ARTCorrectnessTest, Node256ShrinkToNode48DeleteBeginning) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
  verifier.assert_node_counts(49, 0, 0, 0, 1);

  verifier.remove(1);
  verifier.assert_shrinking_nodes(0, 0, 0, 1);
  verifier.assert_node_counts(48, 0, 0, 1, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 50});
}

TYPED_TEST(ARTCorrectnessTest, Node256ShrinkToNode48DeleteEnd) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
  verifier.assert_node_counts(49, 0, 0, 0, 1);

  verifier.remove(49);
  verifier.assert_shrinking_nodes(0, 0, 0, 1);
  verifier.assert_node_counts(48, 0, 0, 1, 0);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 49, 50});
}

TYPED_TEST(ARTCorrectnessTest, Node256KeyPrefixMerge) {
  tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 49);
  // Insert a value that does not share full prefix with the current Node256
  verifier.insert(0x2010, test_values[1]);
  verifier.assert_node_counts(50, 1, 0, 0, 1);

  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node256 one
  verifier.remove(0x2010);
  verifier.assert_shrinking_nodes(1, 0, 0, 0);
  verifier.assert_node_counts(49, 0, 0, 0, 1);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 60});
}

TYPED_TEST(ARTCorrectnessTest, MissingKeyWithPresentPrefix) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(0x010000, test_values[0]);
  verifier.insert(0x000001, test_values[1]);
  verifier.insert(0x010001, test_values[2]);

  verifier.attempt_remove_missing_keys({0x000002, 0x010100, 0x010002});
}

TYPED_TEST(ARTCorrectnessTest, MissingKeyMatchingInodePath) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(0x0100, test_values[0]);
  verifier.insert(0x0200, test_values[1]);
  verifier.attempt_remove_missing_keys({0x0101, 0x0202});
}

TYPED_TEST(ARTCorrectnessTest, MemoryAccountingDuplicateKeyInsert) {
  tree_verifier<TypeParam> verifier;
  verifier.insert(0, test_values[0]);
  ASSERT_FALSE(verifier.get_db().insert(0, test_values[1]));
  verifier.remove(0);
  ASSERT_EQ(verifier.get_db().get_current_memory_use(), 0);
}

TYPED_TEST(ARTCorrectnessTest, Node48InsertIntoDeletedSlot) {
  tree_verifier<TypeParam> verifier;
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
  verifier.assert_node_counts(18, 0, 0, 1, 0);
}

TYPED_TEST(ARTCorrectnessTest, ClearOnEmpty) {
  tree_verifier<TypeParam> verifier;

  verifier.clear();
  verifier.assert_node_counts(0, 0, 0, 0, 0);
}

TYPED_TEST(ARTCorrectnessTest, Clear) {
  tree_verifier<TypeParam> verifier;

  verifier.insert(1, test_values[0]);
  verifier.assert_node_counts(1, 0, 0, 0, 0);

  verifier.clear();

  verifier.check_absent_keys({1});
  verifier.assert_node_counts(0, 0, 0, 0, 0);
}

RESTORE_GCC_WARNINGS()

}  // namespace
