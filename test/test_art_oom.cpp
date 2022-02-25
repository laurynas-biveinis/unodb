// Copyright 2022 Laurynas Biveinis

#ifndef NDEBUG

#include "global.hpp"

#include <cstdlib>
#include <new>

#include <gtest/gtest.h>

#include "art.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "heap.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace {

template <typename Alloc, typename... Args>
void* do_new(Alloc alloc, std::size_t count, Args... args) {
  unodb::test::allocation_failure_injector::maybe_fail();
  while (true) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    void* const result = alloc(count, args...);
    if (UNODB_DETAIL_LIKELY(result != nullptr)) return result;
    // LCOV_EXCL_START
    auto* new_handler = std::get_new_handler();
    if (new_handler == nullptr) throw std::bad_alloc{};
    (*new_handler)();
    // LCOV_EXCL_STOP
  }
}

}  // namespace

void* operator new(std::size_t count) { return do_new(&malloc, count); }

void* operator new(std::size_t count, std::align_val_t al) {
  return do_new(&unodb::detail::allocate_aligned_nothrow, count,
                static_cast<std::size_t>(al));
}

void operator delete(void* ptr) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
  free(ptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wmissing-prototypes")
void operator delete(void* ptr, std::size_t) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
  free(ptr);
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

void operator delete(void* ptr, std::align_val_t) noexcept {
  unodb::detail::free_aligned(ptr);
}

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

    check_after_oom(verifier);
  }

  unodb::test::tree_verifier<TypeParam> verifier;
  init(verifier);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(fail_n);
  test(verifier);
  unodb::test::allocation_failure_injector::reset();

  check_after_success(verifier);
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
  oom_test<TypeParam>(
      2, [](unodb::test::tree_verifier<TypeParam>&) {},
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, {}, true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_absent_keys({1});
        verifier.assert_node_counts({0, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeNonemptyValue) {
  oom_test<TypeParam>(
      2, [](unodb::test::tree_verifier<TypeParam>&) {},
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[2], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_absent_keys({1});
        verifier.assert_node_counts({0, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, ExpandLeafToNode4) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(0, unodb::test::test_values[1]);
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[2], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.check_absent_keys({1});
        verifier.assert_node_counts({1, 0, 0, 0, 0});
        verifier.assert_growing_inodes({0, 0, 0, 0});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({2, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, NoAllocationOnDuplicateKey) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(0, unodb::test::test_values[0]);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  UNODB_ASSERT_FALSE(verifier.get_db().insert(0, unodb::test::test_values[3]));
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_node_counts({1, 0, 0, 0, 0});
  verifier.assert_growing_inodes({0, 0, 0, 0});
  verifier.check_present_values();
}

TYPED_TEST(ARTOOMTest, TwoNode4) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[0]);
        verifier.insert(3, unodb::test::test_values[2]);
        verifier.assert_growing_inodes({1, 0, 0, 0});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        // Insert a value that does not share full prefix with the current Node4
        verifier.insert(0xFF01, unodb::test::test_values[3], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.check_absent_keys({0xFF01});
        verifier.assert_node_counts({2, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
        verifier.assert_key_prefix_splits(0);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({3, 2, 0, 0, 0});
        verifier.assert_growing_inodes({2, 0, 0, 0});
        verifier.assert_key_prefix_splits(1);
      });
}

TYPED_TEST(ARTOOMTest, DbInsertNodeRecursion) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(1, unodb::test::test_values[0]);
        verifier.insert(3, unodb::test::test_values[2]);
        // Insert a value that does not share full prefix with the current Node4
        verifier.insert(0xFF0001, unodb::test::test_values[3]);
        verifier.assert_growing_inodes({2, 0, 0, 0});
        verifier.assert_key_prefix_splits(1);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        // Then insert a value that shares full prefix with the above node and
        // will ask for a recursive insertion there
        verifier.insert(0xFF0101, unodb::test::test_values[1], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({3, 2, 0, 0, 0});
        verifier.assert_growing_inodes({2, 0, 0, 0});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({4, 3, 0, 0, 0});
        verifier.assert_growing_inodes({3, 0, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node16) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 4);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(5, unodb::test::test_values[0], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({4, 1, 0, 0, 0});
        verifier.assert_growing_inodes({1, 0, 0, 0});
        verifier.check_absent_keys({5});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
      });
}

TYPED_TEST(ARTOOMTest, Node16KeyPrefixSplit) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(10, 5);
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
        verifier.assert_key_prefix_splits(0);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        // Insert a value that does share full prefix with the current Node16
        verifier.insert(0x1020, unodb::test::test_values[0], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
        verifier.check_absent_keys({0x1020});
        verifier.assert_key_prefix_splits(0);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.check_present_values();
        verifier.assert_node_counts({6, 1, 1, 0, 0});
        verifier.assert_growing_inodes({2, 1, 0, 0});
        verifier.assert_key_prefix_splits(1);
      });
}

TYPED_TEST(ARTOOMTest, Node48) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 16);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(16, unodb::test::test_values[0], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({16, 0, 1, 0, 0});
        verifier.assert_growing_inodes({1, 1, 0, 0});
        verifier.check_present_values();
        verifier.check_absent_keys({16});
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
        verifier.check_present_values();
      });
}

TYPED_TEST(ARTOOMTest, Node48KeyPrefixSplit) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(10, 17);
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
        verifier.assert_key_prefix_splits(0);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        // Insert a value that does share full prefix with the current Node48
        verifier.insert(0x100020, unodb::test::test_values[0], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
        verifier.assert_key_prefix_splits(0);
        verifier.check_present_values();
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({18, 1, 0, 1, 0});
        verifier.assert_growing_inodes({2, 1, 1, 0});
        verifier.assert_key_prefix_splits(1);
        verifier.check_present_values();
      });
}

TYPED_TEST(ARTOOMTest, Node256) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0, 48);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert(49, unodb::test::test_values[0], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({48, 0, 0, 1, 0});
        verifier.assert_growing_inodes({1, 1, 1, 0});
        verifier.check_present_values();
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_growing_inodes({1, 1, 1, 1});
        verifier.check_present_values();
      });
}

TYPED_TEST(ARTOOMTest, Node256KeyPrefixSplit) {
  oom_test<TypeParam>(
      3,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(20, 49);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        // Insert a value that does share full prefix with the current Node48
        verifier.insert(0x100020, unodb::test::test_values[0], true);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_growing_inodes({1, 1, 1, 1});
        verifier.assert_key_prefix_splits(0);
        verifier.check_present_values();
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({50, 1, 0, 0, 1});
        verifier.assert_growing_inodes({2, 1, 1, 1});
        verifier.assert_key_prefix_splits(1);
        verifier.check_present_values();
      });
}

TYPED_TEST(ARTOOMTest, DeleteFromEmptyDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.attempt_remove_missing_keys({1});
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_empty();
  verifier.check_absent_keys({1});
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeDeleteDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(1, unodb::test::test_values[0]);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.remove(1);
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_empty();
  verifier.check_absent_keys({1});
  verifier.attempt_remove_missing_keys({1});
  verifier.check_absent_keys({1});
}

TYPED_TEST(ARTOOMTest, SingleNodeTreeAttemptDeleteAbsentDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert(2, unodb::test::test_values[1]);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.attempt_remove_missing_keys({1, 3, 0xFF02});
  unodb::test::allocation_failure_injector::reset();

  verifier.check_present_values();
  verifier.assert_node_counts({1, 0, 0, 0, 0});
  verifier.check_absent_keys({1, 3, 0xFF02});
}

TYPED_TEST(ARTOOMTest, Node4AttemptDeleteAbsentDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.attempt_remove_missing_keys({0, 6, 0xFF000001});
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_node_counts({4, 1, 0, 0, 0});
  verifier.check_absent_keys({0, 6, 0xFF00000});
}

TYPED_TEST(ARTOOMTest, Node4DeleteDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 4);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.remove(2);
  unodb::test::allocation_failure_injector::reset();

  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 5});
  verifier.assert_node_counts({3, 1, 0, 0, 0});
}

TYPED_TEST(ARTOOMTest, Node4ShrinkToSingleLeafDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 2);
  verifier.assert_shrinking_inodes({0, 0, 0, 0});

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.remove(1);
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.check_present_values();
  verifier.check_absent_keys({1});
  verifier.assert_node_counts({1, 0, 0, 0, 0});
}

TYPED_TEST(ARTOOMTest, Node4DeleteLowerNodeDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0xFF00, unodb::test::test_values[3]);
  verifier.assert_shrinking_inodes({0, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  // Make the lower Node4 shrink to a single value leaf
  verifier.remove(0);
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_key_prefix_splits(1);
  verifier.check_present_values();
  verifier.check_absent_keys({0, 2, 0xFF01});
  verifier.assert_node_counts({2, 1, 0, 0, 0});
}

TYPED_TEST(ARTOOMTest, Node4DeleteKeyPrefixMergeDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(0x8001, 2);
  // Insert a value that does not share full prefix with the current Node4
  verifier.insert(0x90AA, unodb::test::test_values[3]);
  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts({3, 2, 0, 0, 0});

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  // And delete it
  verifier.remove(0x90AA);
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_key_prefix_splits(1);
  verifier.assert_node_counts({2, 1, 0, 0, 0});
  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.check_present_values();
  verifier.check_absent_keys({0x90AA, 0x8003});
  verifier.assert_node_counts({2, 1, 0, 0, 0});
}

TYPED_TEST(ARTOOMTest, Node16DeleteDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 16);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.remove(5);
  unodb::test::allocation_failure_injector::reset();

  verifier.check_present_values();
  verifier.check_absent_keys({5});
  verifier.assert_node_counts({15, 0, 1, 0, 0});
}

TYPED_TEST(ARTOOMTest, Node16ShrinkToNode4) {
  oom_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(1, 5);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.remove(2);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({5, 0, 1, 0, 0});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
        verifier.check_present_values();
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_shrinking_inodes({0, 1, 0, 0});
        verifier.assert_node_counts({4, 1, 0, 0, 0});
        verifier.check_present_values();
        verifier.check_absent_keys({2});
      });
}

TYPED_TEST(ARTOOMTest, Node16KeyPrefixMergeDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(10, 5);
  // Insert a value that does not share full prefix with the current Node16
  verifier.insert(0x1020, unodb::test::test_values[0]);
  verifier.assert_node_counts({6, 1, 1, 0, 0});
  verifier.assert_key_prefix_splits(1);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  // And delete it, so that upper level Node4 key prefix gets merged with
  // Node16 one
  verifier.remove(0x1020);
  unodb::test::allocation_failure_injector::reset();

  verifier.assert_shrinking_inodes({1, 0, 0, 0});
  verifier.assert_node_counts({5, 0, 1, 0, 0});
  verifier.check_present_values();
  verifier.check_absent_keys({9, 16, 0x1020});
}

TYPED_TEST(ARTOOMTest, Node48DeleteDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 48);

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.remove(30);
  unodb::test::allocation_failure_injector::reset();

  verifier.check_present_values();
  verifier.check_absent_keys({30});
  verifier.assert_node_counts({47, 0, 0, 1, 0});
}

TYPED_TEST(ARTOOMTest, Node48ShrinkToNode16) {
  oom_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(0x80, 17);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.remove(0x85);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({17, 0, 0, 1, 0});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
        verifier.check_present_values();
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_shrinking_inodes({0, 0, 1, 0});
        verifier.assert_node_counts({16, 0, 1, 0, 0});
        verifier.check_present_values();
        verifier.check_absent_keys({0x85});
      });
}

TYPED_TEST(ARTOOMTest, Node256DeleteDoesNotAllocate) {
  unodb::test::tree_verifier<TypeParam> verifier;

  verifier.insert_key_range(1, 255);
  verifier.assert_node_counts({255, 0, 0, 0, 1});

  unodb::test::allocation_failure_injector::fail_on_nth_allocation(1);
  verifier.remove(180);
  unodb::test::allocation_failure_injector::reset();

  verifier.check_present_values();
  verifier.check_absent_keys({180});
  verifier.assert_node_counts({254, 0, 0, 0, 1});
}

TYPED_TEST(ARTOOMTest, Node256ShrinkToNode48) {
  oom_test<TypeParam>(
      2,
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.insert_key_range(1, 49);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.remove(25);
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_node_counts({49, 0, 0, 0, 1});
        verifier.assert_shrinking_inodes({0, 0, 0, 0});
        verifier.check_present_values();
      },
      [](unodb::test::tree_verifier<TypeParam>& verifier) {
        verifier.assert_shrinking_inodes({0, 0, 0, 1});
        verifier.assert_node_counts({48, 0, 0, 1, 0});
        verifier.check_present_values();
        verifier.check_absent_keys({25});
      });
}

}  // namespace

#endif  // #ifndef NDEBUG
