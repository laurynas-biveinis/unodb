// Copyright 2022 Laurynas Biveinis

#ifndef NDEBUG

#include "global.hpp"

#include <gtest/gtest.h>

#include "art.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "heap.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

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
void oom_insert_test(unsigned fail_limit, Init init, unodb::key k,
                     unodb::value_view v,
                     CheckAfterSuccess check_after_success) {
  oom_test<TypeParam>(
      fail_limit, init,
      [k, v](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(k, v, true);
      },
      [k](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_absent_keys({k});
      },
      check_after_success);
}

template <class TypeParam, typename Init, typename CheckAfterSuccess>
void oom_remove_test(unsigned fail_limit, Init init, unodb::key k,
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

using ARTTypes = ::testing::Types<unodb::db, unodb::mutex_db, unodb::olc_db>;

UNODB_TYPED_TEST_SUITE(ARTOOMTest, ARTTypes)

UNODB_START_TYPED_TESTS()

TYPED_TEST(ARTOOMTest, CtorDoesNotAllocate) {
  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  TypeParam tree;
  unodb::test::allocation_failure_injector::reset();
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeEmptyValue) {
  oom_insert_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({0, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      1, {},
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeNonemptyValue) {
  oom_insert_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({0, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      1, unodb::test::test_values[2],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, ExpandLeafToNode4) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(0, unodb::test::test_values[1]);
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      1, unodb::test::test_values[2],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({2, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, TwoNode4) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[0]);
        verifier.insert(3, unodb::test::test_values[2]);
        verifier.assert_growing_inodes({1, 0, 0, 0});
        verifier.assert_node_counts({2, 1, 0, 0, 0});
        verifier.assert_key_prefix_splits(0);
      },
      // Insert a value that does not share full prefix with the current Node4
      0xFF01, unodb::test::test_values[3],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({3, 2, 0, 0, 0});
        verifier.assert_growing_inodes({2, 0, 0, 0});
        verifier.assert_key_prefix_splits(1);
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
        verifier.assert_node_counts({3, 2, 0, 0, 0});
        verifier.assert_growing_inodes({2, 0, 0, 0});
        verifier.assert_key_prefix_splits(1);
      },
      // Then insert a value that shares full prefix with the above node and
      // will ask for a recursive insertion there
      0xFF0101, unodb::test::test_values[1],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({4, 3, 0, 0, 0});
        verifier.assert_growing_inodes({3, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node16) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 4);
        verifier.assert_node_counts({4, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
      },
      5, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node16KeyPrefixSplit) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(10, 5);
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
        verifier.assert_key_prefix_splits(0);
      },
      // Insert a value that does share full prefix with the current Node16
      0x1020, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({6, 1, 1, 0, 0});
        verifier.assert_growing_inodes({2, 1, 0, 0});
        verifier.assert_key_prefix_splits(1);
      });
}

TYPED_TEST(ARTOOMTest, Node48) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 16);
        verifier.assert_node_counts({16, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
      },
      16, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node48KeyPrefixSplit) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(10, 17);
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
        verifier.assert_key_prefix_splits(0);
      },
      // Insert a value that does share full prefix with the current Node48
      0x100020, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({18, 1, 0, 1, 0});
        verifier.assert_growing_inodes({2, 1, 1, 0});
        verifier.assert_key_prefix_splits(1);
      });
}

TYPED_TEST(ARTOOMTest, Node256) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 48);
        verifier.assert_node_counts({48, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
      },
      49, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_growing_inodes({1, 1, 1, 1});
      });
}

TYPED_TEST(ARTOOMTest, Node256KeyPrefixSplit) {
  oom_insert_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(20, 49);
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_growing_inodes({1, 1, 1, 1});
        verifier.assert_key_prefix_splits(0);
      },
      // Insert a value that does share full prefix with the current Node48
      0x100020, unodb::test::test_values[0],
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({50, 1, 0, 0, 1});
        verifier.assert_growing_inodes({2, 1, 1, 1});
        verifier.assert_key_prefix_splits(1);
      });
}

TYPED_TEST(ARTOOMTest, Node16ShrinkToNode4) {
  oom_remove_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(1, 5);
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
      },
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_shrinking_inodes({0, 1, 0, 0});
        verifier.assert_node_counts({4, 1, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node48ShrinkToNode16) {
  oom_remove_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0x80, 17);
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
      },
      0x85,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_shrinking_inodes({0, 0, 1, 0});
        verifier.assert_node_counts({16, 0, 1, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node256ShrinkToNode48) {
  oom_remove_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(1, 49);
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
      },
      25,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_shrinking_inodes({0, 0, 0, 1});
        verifier.assert_node_counts({48, 0, 0, 1, 0});
      });
}

}  // namespace

#endif  // #ifndef NDEBUG
