// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>
#include <unordered_map>

#include "gtest/gtest.h"  // IWYU pragma: keep

#include "art.hpp"
#include "test_utils.hpp"

namespace {

TEST(ART, SingleNodeTreeEmptyValue) {
  tree_verifier<unodb::db> verifier{1024};
  verifier.check_absent_keys({1});
  verifier.insert(1, {});

  verifier.check_present_values();
  verifier.check_absent_keys({0});
}

TEST(ART, SingleNodeTreeNonemptyValue) {
  tree_verifier<unodb::db> verifier{1024};
  verifier.insert(1, test_values[2]);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2});
}

TEST(ART, TooLongValue) {
  std::byte fake_val{0x00};
  unodb::value_view too_long{
      &fake_val,
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
          1U};

  tree_verifier<unodb::db> verifier;

  ASSERT_THROW((void)verifier.get_db().insert(1, too_long), std::length_error);

  verifier.check_absent_keys({1});
}

TEST(ART, ExpandLeafToNode4) {
  tree_verifier<unodb::db> verifier{1024};

  verifier.insert(0, test_values[1]);
  verifier.insert(1, test_values[2]);

  verifier.check_present_values();
  verifier.check_absent_keys({2});
}

TEST(ART, DuplicateKey) {
  tree_verifier<unodb::db> verifier{1024};

  verifier.insert(0, test_values[0]);
  const auto mem_use_before = verifier.get_db().get_current_memory_use();
  ASSERT_FALSE(verifier.get_db().insert(0, test_values[3]));
  ASSERT_EQ(mem_use_before, verifier.get_db().get_current_memory_use());
  verifier.check_present_values();
}

TEST(ART, InsertToFullNode4) {
  tree_verifier<unodb::db> verifier{1024};

  verifier.insert_key_range(0, 4);

  verifier.check_present_values();
  verifier.check_absent_keys({5, 4});
}

TEST(ART, TwoNode4) {
  tree_verifier<unodb::db> verifier{2048};

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF01, test_values[3]);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF00, 2});
}

TEST(ART, DbInsertNodeRecursion) {
  tree_verifier<unodb::db> verifier{2048};

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

TEST(ART, Node16) {
  tree_verifier<unodb::db> verifier{2048};

  verifier.insert_key_range(0, 5);

  verifier.check_present_values();
  verifier.check_absent_keys({6, 0x0100, 0xFFFFFFFFFFFFFFFFULL});
}

TEST(ART, FullNode16) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(0, 16);

  verifier.check_absent_keys({16});
  verifier.check_present_values();
}

TEST(ART, Node16KeyPrefixSplit) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(10, 5);

  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x10FF});
}

TEST(ART, Node16KeyInsertOrderDescending) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert(5, test_values[0]);
  verifier.insert(4, test_values[1]);
  verifier.insert(3, test_values[2]);
  verifier.insert(2, test_values[3]);
  verifier.insert(1, test_values[4]);
  verifier.insert(0, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({6});
}

TEST(ART, Node48) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(0, 17);

  verifier.check_present_values();
  verifier.check_absent_keys({17});
}

TEST(ART, FullNode48) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(0, 48);

  verifier.check_present_values();
  verifier.check_absent_keys({49});
}

TEST(ART, Node48KeyPrefixSplit) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(10, 17);

  // Insert a value that does share full prefix with the current Node48
  verifier.insert(0x100020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 27, 0x100019, 0x100100, 0x110000});
}

TEST(ART, Node256) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(1, 49);

  verifier.check_present_values();
  verifier.check_absent_keys({50});
}

TEST(ART, FullNode256) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(0, 256);

  verifier.check_present_values();
  verifier.check_absent_keys({256});
}

TEST(ART, Node256KeyPrefixSplit) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(20, 49);

  // Insert a value that does share full prefix with the current Node256
  verifier.insert(0x100020, test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({19, 69, 0x100019, 0x100100, 0x110000});
}

TEST(ART, TryDeleteFromEmpty) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.attempt_remove_missing_keys({1});
  verifier.assert_empty();
  verifier.check_absent_keys({1});
}

TEST(ART, SingleNodeTreeDelete) {
  tree_verifier<unodb::db> verifier{1024};

  verifier.insert(1, test_values[0]);
  verifier.remove(1);
  verifier.assert_empty();
  verifier.check_absent_keys({1});
  verifier.attempt_remove_missing_keys({1});
  verifier.check_absent_keys({1});
}

TEST(ART, Node4AttemptDeleteAbsent) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(1, 4);
  verifier.attempt_remove_missing_keys({0, 6, 0xFF000001});
  verifier.check_absent_keys({0, 6, 0xFF00000});
}

TEST(ART, Node4FullDeleteMiddleAndBeginning) {
  tree_verifier<unodb::db> verifier{2048};

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

TEST(ART, Node4FullDeleteEndAndMiddle) {
  tree_verifier<unodb::db> verifier{2048};

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

TEST(ART, Node4ShrinkToSingleLeaf) {
  tree_verifier<unodb::db> verifier{2048};

  verifier.insert_key_range(1, 2);
  verifier.remove(1);
  verifier.check_present_values();
  verifier.check_absent_keys({1});
}

TEST(ART, Node4DeleteLowerNode) {
  tree_verifier<unodb::db> verifier{2048};

  verifier.insert_key_range(0, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF00, test_values[3]);
  // Make the lower Node4 shrink to a single value leaf
  verifier.remove(0);
  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 0xFF01});
}

TEST(ART, Node4DeleteKeyPrefixMerge) {
  tree_verifier<unodb::db> verifier{2048};

  verifier.insert_key_range(0x8001, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0x90AA, test_values[3]);
  // And delete it
  verifier.remove(0x90AA);
  verifier.check_present_values();
  verifier.check_absent_keys({0x90AA, 0x8003});
}

TEST(ART, Node16DeleteBeginningMiddleEnd) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(1, 16);
  verifier.remove(5);
  verifier.remove(1);
  verifier.remove(16);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 5, 16, 17});
}

TEST(ART, Node16ShrinkToNode4DeleteMiddle) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(1, 5);
  verifier.remove(2);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 6});
}

TEST(ART, Node16ShrinkToNode4DeleteBeginning) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(1, 5);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 6});
}

TEST(ART, Node16ShrinkToNode4DeleteEnd) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(1, 5);
  verifier.remove(5);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 5, 6});
}

TEST(ART, Node16KeyPrefixMerge) {
  tree_verifier<unodb::db> verifier{4096};

  verifier.insert_key_range(10, 5);
  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, test_values[0]);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node16 one
  verifier.remove(0x1020);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 16, 0x1020});
}

TEST(ART, Node48DeleteBeginningMiddleEnd) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(1, 48);
  verifier.remove(30);
  verifier.remove(48);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 30, 48, 49});
}

TEST(ART, Node48ShrinkToNode16DeleteMiddle) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(0x80, 17);
  verifier.remove(0x85);

  verifier.check_present_values();
  verifier.check_absent_keys({0x7F, 0x85, 0x91});
}

TEST(ART, Node48ShrinkToNode16DeleteBeginning) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(1, 17);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 18});
}

TEST(ART, Node48ShrinkToNode16DeleteEnd) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(1, 17);
  verifier.remove(17);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 17, 18});
}

TEST(ART, Node48KeyPrefixMerge) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert_key_range(10, 17);
  // Insert a value that does not share full prefix with the current Node48
  verifier.insert(0x2010, test_values[1]);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node48 one
  verifier.remove(0x2010);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 28});
}

TEST(ART, Node256DeleteBeginningMiddleEnd) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(1, 256);
  verifier.remove(180);
  verifier.remove(1);
  verifier.remove(256);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 180, 256});
}

TEST(ART, Node256ShrinkToNode48DeleteMiddle) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(1, 49);
  verifier.remove(25);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 25, 50});
}

TEST(ART, Node256ShrinkToNode48DeleteBeginning) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(1, 49);
  verifier.remove(1);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 50});
}

TEST(ART, Node256ShrinkToNode48DeleteEnd) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(1, 49);
  verifier.remove(49);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 49, 50});
}

TEST(ART, Node256KeyPrefixMerge) {
  tree_verifier<unodb::db> verifier{20480};

  verifier.insert_key_range(10, 49);
  // Insert a value that does not share full prefix with the current Node256
  verifier.insert(0x2010, test_values[1]);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node256 one
  verifier.remove(0x2010);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 60});
}

TEST(ART, MissingKeyWithPresentPrefix) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert(0x010000, test_values[0]);
  verifier.insert(0x000001, test_values[1]);
  verifier.insert(0x010001, test_values[2]);

  verifier.attempt_remove_missing_keys({0x000002, 0x010100, 0x010002});
}

TEST(ART, MissingKeyMatchingInodePath) {
  tree_verifier<unodb::db> verifier{10240};

  verifier.insert(0x0100, test_values[0]);
  verifier.insert(0x0200, test_values[1]);
  verifier.attempt_remove_missing_keys({0x0101, 0x0202});
}

TEST(ART, MemoryLimitBelowMinimum) {
  tree_verifier<unodb::db> verifier{1};
  verifier.test_insert_until_memory_limit();
}

// It was one leaf at the time of writing the test, this is not
// guaranteed later
TEST(ART, MemoryLimitOneLeaf) {
  tree_verifier<unodb::db> verifier{20};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, MemoryLimitOneNode4) {
  tree_verifier<unodb::db> verifier{80};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, MemoryLimitOneNode16) {
  tree_verifier<unodb::db> verifier{320};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, MemoryLimitOneNode48) {
  tree_verifier<unodb::db> verifier{1024};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, MemoryLimitOneNode256) {
  tree_verifier<unodb::db> verifier{4096};
  verifier.test_insert_until_memory_limit();
}

TEST(ART, MemoryAccountingDuplicateKeyInsert) {
  tree_verifier<unodb::db> verifier{2048};
  verifier.insert(0, test_values[0]);
  ASSERT_FALSE(verifier.get_db().insert(0, test_values[1]));
  verifier.remove(0);
  ASSERT_EQ(verifier.get_db().get_current_memory_use(), 0);
}

TEST(ART, Node48InsertIntoDeletedSlot) {
  tree_verifier<unodb::db> verifier{4096};
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

TEST(ART, MemoryAccountingGrowingNodeException) {
  tree_verifier<unodb::db> verifier{1024};

  verifier.insert_key_range(0, 4);

  std::array<std::byte, 900> large_value;
  const unodb::value_view large_value_view{large_value};

  // The leaf node will be created first and will take memory use almost to the
  // limit, then Node16 allocation will go over the limit
  ASSERT_THROW(verifier.insert(10, large_value_view), std::bad_alloc);

  verifier.check_present_values();
  verifier.check_absent_keys({10});
}

TEST(ART, MemoryAccountingLeafToNode4Exception) {
  tree_verifier<unodb::db> verifier{50};

  verifier.insert(0, test_values[0]);

  ASSERT_THROW(verifier.insert(1, test_values[1]), std::bad_alloc);

  verifier.check_present_values();
  verifier.check_absent_keys({1});
}

TEST(ART, MemoryAccountingPrefixSplitException) {
  tree_verifier<unodb::db> verifier{140};

  verifier.insert(1, test_values[0]);
  verifier.insert(3, test_values[2]);

  // Insert a value that does not share full prefix with the current Node4
  ASSERT_THROW(verifier.insert(0xFF01, test_values[3]), std::bad_alloc);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF01});
}

}  // namespace
