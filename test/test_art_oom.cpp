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

}  // namespace

#endif  // #ifndef NDEBUG
