// Copyright 2019-2024 Laurynas Biveinis

// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>

#include <gtest/gtest.h>

#include "art.hpp"
#include "art_common.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"
#include "test_utils.hpp"
#include "thread_sync.hpp"

extern template class unodb::db<unodb::key_view>;

namespace unodb::test {

constexpr std::array<unodb::value_view, 6> test_keys = {
    unodb::key_view{test_value_1}, unodb::key_view{test_value_2},
    unodb::key_view{test_value_3}, unodb::key_view{test_value_4},
    unodb::key_view{test_value_5}, unodb::key_view{empty_test_value}};

using span_db = unodb::db<unodb::key_view>;

extern template class tree_verifier<u64_db>;

}  // namespace unodb::test

namespace {
using unodb::detail::thread_syncs;
using unodb::test::test_values;

template <class Db>
class ARTSpanCorrectnessTest : public ::testing::Test {
 public:
  using Test::Test;
};

using ARTTypes = ::testing::Types<unodb::test::span_db>;
// ::testing::Types<unodb::test::u64_db, unodb::test::u64_mutex_db,
//                  unodb::test::u64_olc_db>;

UNODB_TYPED_TEST_SUITE(ARTSpanCorrectnessTest, ARTTypes)

UNODB_START_TYPED_TESTS()

/// Unit test bootstraps variable length key support by testing the
/// full public API for a single key/value pair in an otherwise empty
/// tree.
TYPED_TEST(ARTSpanCorrectnessTest, SingleKeyOperationsOnEmptyTree) {
  const auto key = unodb::test::test_keys[1];  // 0x 00 02
  const auto val = unodb::test::test_keys[2];  // 0x 03 00 01
  TypeParam db;
  EXPECT_FALSE(db.get(key));
  {
    uint64_t n = 0;
    auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
      n++;           // LCOV_EXCL_LINE
      return false;  // LCOV_EXCL_LINE
    };
    db.scan(fn);
    UNODB_EXPECT_EQ(0, n);
  }
  EXPECT_TRUE(db.insert(key, val));
  auto tmp = db.get(key);
  EXPECT_TRUE(tmp.has_value());
  EXPECT_TRUE(std::ranges::equal(tmp.value(), val));
  {
    uint64_t n = 0;
    std::vector<std::pair<unodb::key_view, unodb::value_view>> expected;
    expected.emplace_back(key, val);  // key is already encoded.
    auto fn = [&n, &expected](
                  const unodb::visitor<typename TypeParam::iterator>& visitor) {
      const auto& k = visitor.get_key();
      const auto& v = visitor.get_value();
      EXPECT_TRUE(std::ranges::equal(k, expected[n].first));
      EXPECT_TRUE(std::ranges::equal(v, expected[n].second));
      n++;           // LCOV_EXCL_LINE
      return false;  // LCOV_EXCL_LINE
    };
    db.scan(fn);
    UNODB_EXPECT_EQ(1, n);  // FIXME CHECK VISITED KEY/VAL
  }
  EXPECT_TRUE(db.remove(key));
  EXPECT_FALSE(db.get(key));
  EXPECT_FALSE(db.remove(key));
  {
    uint64_t n = 0;
    auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
      n++;           // LCOV_EXCL_LINE
      return false;  // LCOV_EXCL_LINE
    };
    db.scan(fn);
    UNODB_EXPECT_EQ(0, n);
  }
}

TYPED_TEST(ARTSpanCorrectnessTest, SingleNodeTreeEmptyValue) {
  unodb::test::tree_verifier<TypeParam> verifier;
  verifier.check_absent_keys({unodb::test::test_keys[1]});
  verifier.insert(unodb::test::test_keys[1], {});

  verifier.check_present_values();
  verifier.check_absent_keys({unodb::test::test_keys[0]});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({1, 0, 0, 0, 0});
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, SingleNodeTreeNonemptyValue) {
  unodb::test::tree_verifier<TypeParam> verifier;
  verifier.insert(unodb::test::test_values[1], unodb::test::test_values[2]);

  verifier.check_present_values();
  verifier.check_absent_keys(
      {unodb::test::test_values[0], unodb::test::test_values[2]});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({1, 0, 0, 0, 0});
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
TYPED_TEST(ARTSpanCorrectnessTest, TooLongValue) {
  constexpr std::byte fake_val{0x00};
  const unodb::value_view too_long{
      &fake_val,
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
          1U};

  unodb::test::tree_verifier<TypeParam> verifier;

  const auto& key = unodb::test::test_values[1];

  UNODB_ASSERT_THROW(std::ignore = verifier.get_db().insert(key, too_long),
                     std::length_error);

  verifier.check_absent_keys({key});
  verifier.assert_empty();

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
TYPED_TEST(ARTSpanCorrectnessTest, TooLongKey) {
  constexpr std::byte fake_val{0x00};
  const unodb::key_view too_long{
      &fake_val,
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
          1U};

  unodb::test::tree_verifier<TypeParam> verifier;

  UNODB_ASSERT_THROW(std::ignore = verifier.get_db().insert(too_long, {}),
                     std::length_error);

  verifier.assert_empty();

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

TYPED_TEST(ARTSpanCorrectnessTest, ExpandLeafToNode4) {
  unodb::test::tree_verifier<TypeParam> verifier;

  const auto& k0 = unodb::test::test_values[0];  // 00
  const auto& k1 = unodb::test::test_values[1];  // 00 02
  const auto& k2 = unodb::test::test_values[2];  // 03 00 01

  verifier.insert(k0, unodb::test::test_values[1]);
  verifier.get_db().dump();  // FIXME REMOVE

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({1, 0, 0, 0, 0});
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.insert(k1, unodb::test::test_values[2]);
  verifier.get_db().dump();  // FIXME REMOVE

  verifier.check_present_values();  // FIXME FAILS HERE
  verifier.check_absent_keys({k2});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({2, 1, 0, 0, 0});
  verifier.assert_growing_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
TYPED_TEST(ARTSpanCorrectnessTest, DuplicateKey) {
  unodb::test::tree_verifier<TypeParam> verifier;

  const auto& k0 = unodb::test::test_values[0];

  verifier.insert(k0, unodb::test::test_values[0]);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({1, 0, 0, 0, 0});

  const auto mem_use_before = verifier.get_db().get_current_memory_use();
#endif  // UNODB_DETAIL_WITH_STATS

  unodb::test::must_not_allocate([&verifier] {
    UNODB_ASSERT_FALSE(
        verifier.get_db().insert(k0, unodb::test::test_values[3]));
  });

  verifier.check_present_values();

#ifdef UNODB_DETAIL_WITH_STATS
  UNODB_ASSERT_EQ(mem_use_before, verifier.get_db().get_current_memory_use());

  verifier.assert_node_counts({1, 0, 0, 0, 0});
  verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#if 0
TYPED_TEST(ARTSpanCorrectnessTest, InsertToFullNode4) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 4);

  verifier.check_present_values();
  verifier.check_absent_keys({5, 4});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({4, 1, 0, 0, 0});
  verifier.assert_growing_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4InsertFFByte) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0xFC, 4);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 0xFB});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({4, 1, 0, 0, 0});
  verifier.assert_growing_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, TwoNode4) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(3, unodb::test::test_values[2]);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_growing_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF01, unodb::test::test_values[3]);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF00, 2});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({3, 2, 0, 0, 0});
  verifier.assert_growing_inodes({2, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, DbInsertNodeRecursion) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(3, unodb::test::test_values[2]);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF0001, unodb::test::test_values[3]);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_growing_inodes({2, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS

  // Then insert a value that shares full prefix with the above node and will
  // ask for a recursive insertion there
  verifier.insert(0xFF0101, unodb::test::test_values[1]);

  verifier.check_present_values();
  verifier.check_absent_keys({0xFF0100, 0xFF0000, 2});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({4, 3, 0, 0, 0});
  verifier.assert_growing_inodes({3, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 4);
  verifier.check_present_values();
  verifier.insert(5, unodb::test::test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({6, 0x0100, 0xFFFFFFFFFFFFFFFFULL});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({5, 0, 1, 0, 0});
  verifier.assert_growing_inodes({1, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, FullNode16) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 16);

  verifier.check_absent_keys({16});
  verifier.check_present_values();

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({16, 0, 1, 0, 0});
  verifier.assert_growing_inodes({1, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16KeyPrefixSplit) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 5);

  // Insert a value that does share full prefix with the current Node16
  verifier.insert(0x1020, unodb::test::test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x10FF});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({6, 1, 1, 0, 0});
  verifier.assert_growing_inodes({2, 1, 0, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16KeyInsertOrderDescending) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(5, unodb::test::test_values[0]);
  verifier.insert(4, unodb::test::test_values[1]);
  verifier.insert(3, unodb::test::test_values[2]);
  verifier.insert(2, unodb::test::test_values[3]);
  verifier.insert(1, unodb::test::test_values[4]);
  verifier.insert(0, unodb::test::test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({6});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({6, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16ConstructWithFFKeyByte) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0xFB, 4);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({4, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.insert(0xFF, unodb::test::test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({0, 0xFA});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({5, 0, 1, 0, 0});
  verifier.assert_growing_inodes({1, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 17);

  verifier.check_present_values();
  verifier.check_absent_keys({17});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({17, 0, 0, 1, 0});
  verifier.assert_growing_inodes({1, 1, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, FullNode48) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 48);

  verifier.check_present_values();
  verifier.check_absent_keys({49});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({48, 0, 0, 1, 0});
  verifier.assert_growing_inodes({1, 1, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48KeyPrefixSplit) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 17);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({17, 0, 0, 1, 0});
  verifier.assert_growing_inodes({1, 1, 1, 0});
  verifier.assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS

  // Insert a value that does share full prefix with the current Node48
  verifier.insert(0x100020, unodb::test::test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({9, 27, 0x100019, 0x100100, 0x110000});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({18, 1, 0, 1, 0});
  verifier.assert_growing_inodes({2, 1, 1, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);

  verifier.check_present_values();
  verifier.check_absent_keys({50});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({49, 0, 0, 0, 1});
  verifier.assert_growing_inodes({1, 1, 1, 1});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, FullNode256) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 256);

  verifier.check_present_values();
  verifier.check_absent_keys({256});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({256, 0, 0, 0, 1});
  verifier.assert_growing_inodes({1, 1, 1, 1});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256KeyPrefixSplit) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(20, 49);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({49, 0, 0, 0, 1});
  verifier.assert_growing_inodes({1, 1, 1, 1});
  verifier.assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS

  // Insert a value that does share full prefix with the current Node256
  verifier.insert(0x100020, unodb::test::test_values[0]);

  verifier.check_present_values();
  verifier.check_absent_keys({19, 69, 0x100019, 0x100100, 0x110000});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({50, 1, 0, 0, 1});
  verifier.assert_growing_inodes({2, 1, 1, 1});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, TryDeleteFromEmpty) {
  unodb::test::tree_verifier<TypeParam> verifier;

  unodb::test::must_not_allocate(
      [&verifier] { verifier.attempt_remove_missing_keys({1}); });

  verifier.assert_empty();
  verifier.check_absent_keys({1});
}

TYPED_TEST(ARTSpanCorrectnessTest, SingleNodeTreeDelete) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(1, unodb::test::test_values[0]);

  unodb::test::must_not_allocate([&verifier] { verifier.remove(1); });

  verifier.assert_empty();
  verifier.check_absent_keys({1});
  verifier.attempt_remove_missing_keys({1});
  verifier.check_absent_keys({1});
}

TYPED_TEST(ARTSpanCorrectnessTest, SingleNodeTreeAttemptDeleteAbsent) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(2, unodb::test::test_values[1]);

  unodb::test::must_not_allocate([&verifier] {
    verifier.attempt_remove_missing_keys({1, 3, 0xFF02});
  });

  verifier.check_present_values();
  verifier.check_absent_keys({1, 3, 0xFF02});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({1, 0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4AttemptDeleteAbsent) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);

  unodb::test::must_not_allocate([&verifier] {
    verifier.attempt_remove_missing_keys({0, 6, 0xFF000001});
  });

  verifier.check_absent_keys({0, 6, 0xFF00000});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({4, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4FullDeleteMiddleAndBeginning) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);

  // Delete from Node4 middle
  unodb::test::must_not_allocate([&verifier] { verifier.remove(2); });

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 5});

  // Delete from Node4 beginning
  unodb::test::must_not_allocate([&verifier] { verifier.remove(1); });

  verifier.check_present_values();
  verifier.check_absent_keys({1, 0, 2, 5});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({2, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4FullDeleteEndAndMiddle) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);

  // Delete from Node4 end
  unodb::test::must_not_allocate([&verifier] { verifier.remove(4); });

  verifier.check_present_values();
  verifier.check_absent_keys({4, 0, 5});

  // Delete from Node4 middle
  unodb::test::must_not_allocate([&verifier] { verifier.remove(2); });

  verifier.check_present_values();
  verifier.check_absent_keys({2, 4, 0, 5});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({2, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4ShrinkToSingleLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 2);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  unodb::test::must_not_allocate([&verifier] { verifier.remove(1); });

  verifier.check_present_values();
  verifier.check_absent_keys({1});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_node_counts({1, 0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4DeleteLowerNode) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF00, unodb::test::test_values[3]);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS

  // Make the lower Node4 shrink to a single value leaf
  unodb::test::must_not_allocate([&verifier] { verifier.remove(0); });

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 0xFF01});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts({2, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4DeleteKeyPrefixMerge) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0x8001, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0x90AA, unodb::test::test_values[3]);

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts({3, 2, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  // And delete it
  unodb::test::must_not_allocate([&verifier] { verifier.remove(0x90AA); });

  verifier.check_present_values();
  verifier.check_absent_keys({0x90AA, 0x8003});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts({2, 1, 0, 0, 0});
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node4DeleteKeyPrefixMerge2) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(0x0000000003020102, unodb::test::test_values[0]);
  verifier.insert(0x0000000003030302, unodb::test::test_values[1]);
  verifier.insert(0x0000000100010102, unodb::test::test_values[2]);

  unodb::test::must_not_allocate([&verifier] {
    verifier.remove(0x0000000100010102);
    verifier.remove(0x0000000003020102);
  });

  verifier.check_present_values();
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16DeleteBeginningMiddleEnd) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 16);

  unodb::test::must_not_allocate([&verifier] {
    verifier.remove(5);
    verifier.remove(1);
    verifier.remove(16);
  });

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 5, 16, 17});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({13, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16ShrinkToNode4DeleteMiddle) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 5);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({5, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(2);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 1, 0, 0});
  verifier.assert_node_counts({4, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 6});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16ShrinkToNode4DeleteBeginning) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 5);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({5, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(1);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 1, 0, 0});
  verifier.assert_node_counts({4, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 6});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16ShrinkToNode4DeleteEnd) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 5);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({5, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(5);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 1, 0, 0});
  verifier.assert_node_counts({4, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 5, 6});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node16KeyPrefixMerge) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 5);
  // Insert a value that does not share full prefix with the current Node16
  verifier.insert(0x1020, unodb::test::test_values[0]);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({6, 1, 1, 0, 0});
  verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS

  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node16 one
  unodb::test::must_not_allocate([&verifier] { verifier.remove(0x1020); });

  verifier.check_present_values();
  verifier.check_absent_keys({9, 16, 0x1020});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_node_counts({5, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48DeleteBeginningMiddleEnd) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 48);

  unodb::test::must_not_allocate([&verifier] {
    verifier.remove(30);
    verifier.remove(48);
    verifier.remove(1);
  });

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 30, 48, 49});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({45, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48ShrinkToNode16DeleteMiddle) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0x80, 17);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({17, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(0x85);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 1, 0});
  verifier.assert_node_counts({16, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0x7F, 0x85, 0x91});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48ShrinkToNode16DeleteBeginning) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 17);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({17, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(1);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 1, 0});
  verifier.assert_node_counts({16, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.check_absent_keys({0, 1, 18});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48ShrinkToNode16DeleteEnd) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 17);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({17, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(17);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 1, 0});
  verifier.assert_node_counts({16, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 17, 18});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node48KeyPrefixMerge) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 17);
  // Insert a value that does not share full prefix with the current Node48
  verifier.insert(0x2010, unodb::test::test_values[1]);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({18, 1, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node48 one
  unodb::test::must_not_allocate([&verifier] { verifier.remove(0x2010); });

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 28});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_node_counts({17, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256DeleteBeginningMiddleEnd) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 256);

  unodb::test::must_not_allocate([&verifier] {
    verifier.remove(180);
    verifier.remove(1);
    verifier.remove(256);
  });

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 180, 256});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({253, 0, 0, 0, 1});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256ShrinkToNode48DeleteMiddle) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({49, 0, 0, 0, 1});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(25);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 0, 1});
  verifier.assert_node_counts({48, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 25, 50});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256ShrinkToNode48DeleteBeginning) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({49, 0, 0, 0, 1});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(1);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 0, 1});
  verifier.assert_node_counts({48, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 1, 50});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256ShrinkToNode48DeleteEnd) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 49);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({49, 0, 0, 0, 1});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.remove(49);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({0, 0, 0, 1});
  verifier.assert_node_counts({48, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS

  verifier.check_present_values();
  verifier.check_absent_keys({0, 49, 50});
}

TYPED_TEST(ARTSpanCorrectnessTest, Node256KeyPrefixMerge) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 49);
  // Insert a value that does not share full prefix with the current Node256
  verifier.insert(0x2010, unodb::test::test_values[1]);
#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({50, 1, 0, 0, 1});
#endif  // UNODB_DETAIL_WITH_STATS

  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node256 one
  unodb::test::must_not_allocate([&verifier] { verifier.remove(0x2010); });

  verifier.check_present_values();
  verifier.check_absent_keys({9, 0x2010, 60});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_node_counts({49, 0, 0, 0, 1});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, MissingKeyWithPresentPrefix) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(0x010000, unodb::test::test_values[0]);
  verifier.insert(0x000001, unodb::test::test_values[1]);
  verifier.insert(0x010001, unodb::test::test_values[2]);

  unodb::test::must_not_allocate([&verifier] {
    verifier.attempt_remove_missing_keys({0x000002, 0x010100, 0x010002});
  });
}

TYPED_TEST(ARTSpanCorrectnessTest, MissingKeyMatchingInodePath) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(0x0100, unodb::test::test_values[0]);
  verifier.insert(0x0200, unodb::test::test_values[1]);

  unodb::test::must_not_allocate([&verifier] {
    verifier.attempt_remove_missing_keys({0x0101, 0x0202});
  });
}

#ifdef UNODB_DETAIL_WITH_STATS

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)
TYPED_TEST(ARTSpanCorrectnessTest, MemoryAccountingDuplicateKeyInsert) {
  unodb::test::tree_verifier<TypeParam> verifier;
  verifier.insert(0, unodb::test::test_values[0]);
  unodb::test::must_not_allocate([&verifier] {
    UNODB_ASSERT_FALSE(
        verifier.get_db().insert(0, unodb::test::test_values[1]));
  });
  verifier.remove(0);
  UNODB_ASSERT_EQ(verifier.get_db().get_current_memory_use(), 0);
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

#endif  // UNODB_DETAIL_WITH_STATS

TYPED_TEST(ARTSpanCorrectnessTest, Node48InsertIntoDeletedSlot) {
  unodb::test::tree_verifier<TypeParam> verifier;
  verifier.insert(16865361447928765957ULL, unodb::test::test_values[0]);
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

  unodb::test::must_not_allocate(
      [&verifier] { verifier.remove(6583227679421302512ULL); });
  verifier.insert(0, test_values[0]);

  verifier.check_present_values();

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({18, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, ClearOnEmpty) {
  unodb::test::tree_verifier<TypeParam> verifier;

  unodb::test::must_not_allocate([&verifier] { verifier.clear(); });

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({0, 0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, Clear) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(1, test_values[0]);

  unodb::test::must_not_allocate([&verifier] { verifier.clear(); });

  verifier.check_absent_keys({1});

#ifdef UNODB_DETAIL_WITH_STATS
  verifier.assert_node_counts({0, 0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
}

TYPED_TEST(ARTSpanCorrectnessTest, TwoInstances) {
  unodb::test::tree_verifier<TypeParam> v1;
  unodb::test::tree_verifier<TypeParam> v2;

  unodb::test::thread<TypeParam> second_thread([&v2] {
    thread_syncs[0].notify();
    thread_syncs[1].wait();

    v2.insert(0, unodb::test::test_values[0]);
    v2.check_present_values();
  });

  thread_syncs[0].wait();
  thread_syncs[1].notify();

  v1.insert(0, unodb::test::test_values[1]);
  v1.check_present_values();

  second_thread.join();
}
#endif

UNODB_END_TESTS()

}  // namespace