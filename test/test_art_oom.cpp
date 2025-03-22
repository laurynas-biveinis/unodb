// Copyright 2022-2025 UnoDB contributors

#ifndef NDEBUG

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <new>

#include "art_common.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "test_heap.hpp"

// The OOM tests are dependent on the number of heap allocations in the test,
// that's brittle and hardcoded. Suppose some op takes 5 heap allocations. The
// tests is written in that it knows that the test should fail on OOMs injected
// on the 1st-5th allocation and pass on the 6th one. The allocations done by
// libstdc++ are included.
//
// Changing the data structure in the main code or the test suite might perturb
// this, causing tests to fail. If this happens you need to decide whether the
// change in behavior was for a valid reason or not. If tests fail in that
// "expected exception was not thrown", try incrementing the allocation counter
// in the test. If they fail in that "exception was thrown but we weren't
// expecting it", try decrementing it.
//
// TODO(laurynas) OOM tests for the scan API.
namespace {

template <class TypeParam, typename Init, typename Test, typename CheckAfterOOM,
          typename CheckAfterSuccess>
void oom_test(unsigned fail_limit, Init init, Test test,
              CheckAfterOOM check_after_oom,
              CheckAfterSuccess check_after_success) {
  unsigned fail_n;
  for (fail_n = 1; fail_n < fail_limit; ++fail_n) {
    unodb::test::tree_verifier<TypeParam> verifier;
    init(verifier);

    unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
    UNODB_ASSERT_THROW(test(verifier), std::bad_alloc);
    unodb::test::allocation_failure_injector::reset();

    verifier.check_present_values();
    check_after_oom(verifier);
  }

  unodb::test::tree_verifier<TypeParam> verifier;
  init(verifier);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
  test(verifier);
  unodb::test::allocation_failure_injector::reset();

  verifier.check_present_values();
  check_after_success(verifier);
}

template <class TypeParam, typename Init, typename CheckAfterSuccess>
void oom_insert_test(unsigned fail_limit, Init init, std::uint64_t k,
                     unodb::value_view v,
                     CheckAfterSuccess check_after_success) {
  oom_test<TypeParam>(
      fail_limit, init,
      [k, v](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(k, v);
      },
      [k](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_absent_keys({k});
      },
      check_after_success);
}

template <class TypeParam, typename Init, typename CheckAfterSuccess>
void oom_remove_test(unsigned fail_limit, Init init, std::uint64_t k,
                     CheckAfterSuccess check_after_success) {
  oom_test<TypeParam>(
      fail_limit, init,
      [k](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.remove(k);
      },
      [](unodb::test::tree_verifier<TypeParam>&) {},
      [k,
       check_after_success](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_absent_keys({k});
        check_after_success(verifier);
      });
}

template <class Db>
class ARTOOMTest : public ::testing::Test {
 public:
  using Test::Test;
};

using ARTTypes =
    ::testing::Types<unodb::test::u64_db, unodb::test::u64_mutex_db,
                     unodb::test::u64_olc_db>;

UNODB_TYPED_TEST_SUITE(ARTOOMTest, ARTTypes)

UNODB_START_TESTS()

TYPED_TEST(ARTOOMTest, CtorDoesNotAllocate) {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  const TypeParam tree;
  unodb::test::allocation_failure_injector::reset();
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeEmptyValue) {
  oom_insert_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({0, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      1, {},
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
#endif
      });
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeNonemptyValue) {
  oom_insert_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({0, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      1, unodb::test::test_values[2],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, ExpandLeafToNode4) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(0, unodb::test::test_values[1]);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      1, unodb::test::test_values[2],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({2, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, TwoNode4) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[0]);
        verifier.insert(3, unodb::test::test_values[2]);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_growing_inodes({1, 0, 0, 0});
        verifier.assert_node_counts({2, 1, 0, 0, 0});
        verifier.assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS
      },
      // Insert a value that does not share full prefix with the current Node4
      0xFF01, unodb::test::test_values[3],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({3, 2, 0, 0, 0});
        verifier.assert_growing_inodes({2, 0, 0, 0});
        verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, DbInsertNodeRecursion) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[0]);
        verifier.insert(3, unodb::test::test_values[2]);
        // Insert a value that does not share full prefix with the current Node4
        verifier.insert(0xFF0001, unodb::test::test_values[3]);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({3, 2, 0, 0, 0});
        verifier.assert_growing_inodes({2, 0, 0, 0});
        verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
      },
      // Then insert a value that shares full prefix with the above node and
      // will ask for a recursive insertion there
      0xFF0101, unodb::test::test_values[1],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({4, 3, 0, 0, 0});
        verifier.assert_growing_inodes({3, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node16) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 4);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({4, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      5, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node16KeyPrefixSplit) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(10, 5);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
        verifier.assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS
      },
      // Insert a value that does share full prefix with the current Node16
      0x1020, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({6, 1, 1, 0, 0});
        verifier.assert_growing_inodes({2, 1, 0, 0});
        verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node48) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 16);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({16, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      16, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node48KeyPrefixSplit) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(10, 17);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
        verifier.assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS
      },
      // Insert a value that does share full prefix with the current Node48
      0x100020, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({18, 1, 0, 1, 0});
        verifier.assert_growing_inodes({2, 1, 1, 0});
        verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node256) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 48);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({48, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      49, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_growing_inodes({1, 1, 1, 1});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node256KeyPrefixSplit) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(20, 49);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_growing_inodes({1, 1, 1, 1});
        verifier.assert_key_prefix_splits(0);
#endif  // UNODB_DETAIL_WITH_STATS
      },
      // Insert a value that does share full prefix with the current Node48
      0x100020, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({50, 1, 0, 0, 1});
        verifier.assert_growing_inodes({2, 1, 1, 1});
        verifier.assert_key_prefix_splits(1);
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node16ShrinkToNode4) {
  oom_remove_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(1, 5);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      2,
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_shrinking_inodes({0, 1, 0, 0});
        verifier.assert_node_counts({4, 1, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node48ShrinkToNode16) {
  oom_remove_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0x80, 17);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      0x85,
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_shrinking_inodes({0, 0, 1, 0});
        verifier.assert_node_counts({16, 0, 1, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

TYPED_TEST(ARTOOMTest, Node256ShrinkToNode48) {
  oom_remove_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(1, 49);
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      },
      25,
      [](unodb::test::tree_verifier<TypeParam>&
#ifdef UNODB_DETAIL_WITH_STATS
             verifier
#endif  // UNODB_DETAIL_WITH_STATS
      ) {
#ifdef UNODB_DETAIL_WITH_STATS
        verifier.assert_shrinking_inodes({0, 0, 0, 1});
        verifier.assert_node_counts({48, 0, 0, 1, 0});
#endif  // UNODB_DETAIL_WITH_STATS
      });
}

}  // namespace

#endif  // #ifndef NDEBUG
