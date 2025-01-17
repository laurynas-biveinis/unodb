// Copyright 2019-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__ostream/basic_ostream.h>
// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "art_internal_impl.hpp"

#include <algorithm>
#include <iostream>

#include <gtest/gtest.h>

#include "art.hpp"
#include "art_common.hpp"
#include "art_internal.hpp"
#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "mutex_art.hpp"
#include "olc_art.hpp"

namespace unodb {

// Test suite for an ART iterator.
//
// TODO(thompsonbry) variable length keys :: unit tests for
// std::span<std::byte>
template <class Db>
class ARTIteratorTest : public ::testing::Test {
 public:
  using Test::Test;
};

using ARTTypes = ::testing::Types<unodb::db, unodb::mutex_db, unodb::olc_db>;

UNODB_TYPED_TEST_SUITE(ARTIteratorTest, ARTTypes)

UNODB_START_TYPED_TESTS()

// unit test with an empty tree.
TYPED_TEST(ARTIteratorTest, emptyTreeForwardScan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  auto b = db.test_only_iterator();
  b.first();  // obtain iterator.
  UNODB_EXPECT_TRUE(!b.valid());
  UNODB_EXPECT_TRUE(!b.get_key());
  UNODB_EXPECT_TRUE(!b.get_val());
}

// unit test with an empty tree.
TYPED_TEST(ARTIteratorTest, emptyTreeReverseScan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  auto b = db.test_only_iterator();
  b.last();  // obtain iterator.
  UNODB_EXPECT_FALSE(b.valid());
  UNODB_EXPECT_FALSE(b.get_key());
  UNODB_EXPECT_FALSE(b.get_val());
}

// unit test where the root is a single leaf.
TYPED_TEST(ARTIteratorTest, singleLeafIteratorOneValue) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  auto b = db.test_only_iterator();
  b.first();  // obtain iterator.
  UNODB_EXPECT_TRUE(b.valid());
  const auto k = b.get_key();
  const auto v = b.get_val();
  UNODB_EXPECT_TRUE(k && k.value() == 0);
  UNODB_EXPECT_TRUE(v.has_value() &&
                    std::ranges::equal(v.value(), unodb::test::test_values[0]));
  b.next();                       // advance.
  UNODB_EXPECT_FALSE(b.valid());  // nothing more in the iterator.
}

// unit test where the root is an I4 with two leafs under it.
TYPED_TEST(ARTIteratorTest, I4AndTwoLeavesForwardScan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  auto b = db.test_only_iterator();
  b.first();  // obtain iterator.
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[0]));
  }
  b.next();  // advance
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 1);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
  }
  b.next();                       // advance.
  UNODB_EXPECT_FALSE(b.valid());  // nothing more in the iterator.
}

// unit test where the root is an I4 with two leafs under it.
TYPED_TEST(ARTIteratorTest, I4AndTwoLeavesReverseScan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  auto b = db.test_only_iterator();
  b.last();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 1);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
  }
  b.prior();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[0]));
  }
  b.prior();
  UNODB_EXPECT_FALSE(b.valid());  // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how
// we choose the keys.
//
//       I4
//   I4     L2
// L0 L1
TYPED_TEST(ARTIteratorTest, C0001) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0xaa00, unodb::test::test_values[0]);
  verifier.insert(0xaa01, unodb::test::test_values[1]);
  verifier.insert(0xab00, unodb::test::test_values[2]);
  auto b = db.test_only_iterator();
  b.first();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xaa00);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[0]));
  }
  b.next();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xaa01);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
  }
  b.next();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xab00);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[2]));
  }
  b.next();
  UNODB_EXPECT_FALSE(b.valid());  // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how
// we choose the keys.
//
//       I4
//   I4     L2
// L0 L1
TYPED_TEST(ARTIteratorTest, C0002) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0xaa00, unodb::test::test_values[0]);
  verifier.insert(0xaa01, unodb::test::test_values[1]);
  verifier.insert(0xab00, unodb::test::test_values[2]);
  auto b = db.test_only_iterator();
  b.last();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xab00);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[2]));
  }
  b.prior();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xaa01);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
  }
  b.prior();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xaa00);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[0]));
  }
  b.prior();
  UNODB_EXPECT_FALSE(b.valid());  // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how
// we choose the keys.
//
//       I4
//   L0     I4
//        L1 L2
TYPED_TEST(ARTIteratorTest, C0003) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0xaa00, unodb::test::test_values[0]);
  verifier.insert(0xab0c, unodb::test::test_values[1]);
  verifier.insert(0xab0d, unodb::test::test_values[2]);
  auto b = db.test_only_iterator();
  b.first();  // obtain iterators.
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xaa00);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[0]));
  }
  b.next();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xab0c);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
  }
  b.next();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xab0d);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[2]));
  }
  b.next();
  UNODB_EXPECT_FALSE(b.valid());  // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how we choose
// the keys.
//
//       I4
//   L0     I4
//        L1 L2
TYPED_TEST(ARTIteratorTest, C0004) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0xaa00, unodb::test::test_values[0]);
  verifier.insert(0xab0c, unodb::test::test_values[1]);
  verifier.insert(0xab0d, unodb::test::test_values[2]);
  auto b = db.test_only_iterator();
  b.last();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xab0d);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[2]));
  }
  b.prior();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xab0c);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
  }
  b.prior();
  UNODB_EXPECT_TRUE(b.valid());
  {
    const auto k = b.get_key();
    const auto v = b.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 0xaa00);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[0]));
  }
  b.prior();
  UNODB_EXPECT_FALSE(b.valid());
}

//
// seek()
//

// unit test with an empty tree.
TYPED_TEST(ARTIteratorTest, emptyTreeSeek) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  {
    auto e = db.test_only_iterator();
    bool match = false;
    e.seek(unodb::detail::art_key{0}, match, true /*fwd*/);
    UNODB_EXPECT_FALSE(e.valid());
    UNODB_EXPECT_EQ(match, false);
  }
  {
    auto e = db.test_only_iterator();
    bool match = false;
    e.seek(unodb::detail::art_key{0}, match, false /*fwd*/);
    UNODB_EXPECT_FALSE(e.valid());
    UNODB_EXPECT_EQ(match, false);
  }
}

// unit test where the root is a single leaf.
TYPED_TEST(ARTIteratorTest, singleLeafSeek) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[1]);
  {  // exact match, forward traversal (GTE)
    auto e = db.test_only_iterator();
    bool match = false;
    auto& it = e.seek(unodb::detail::art_key{1}, match, true /*fwd*/);
    UNODB_EXPECT_TRUE(it.valid());
    UNODB_EXPECT_EQ(match, true);
    const auto k = it.get_key();
    const auto v = it.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 1);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
    it.next();
    UNODB_EXPECT_FALSE(it.valid());
  }
  {  // exact match, reverse traversal (LTE)
    auto e = db.test_only_iterator();
    bool match = false;
    auto& it = e.seek(unodb::detail::art_key{1}, match, false /*fwd*/);
    UNODB_EXPECT_TRUE(it.valid());
    UNODB_EXPECT_EQ(match, true);
    const auto k = it.get_key();
    const auto v = it.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 1);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
    it.next();
    UNODB_EXPECT_FALSE(it.valid());  // nothing more in the iterator.
  }
  {  // forward traversal, before the first key in the data.
    // match=false and iterator is positioned on the first key in the
    // data.
    bool match = true;
    auto it = db.test_only_iterator();
    it.seek(unodb::detail::art_key{0}, match, true /*fwd*/);
    UNODB_EXPECT_TRUE(it.valid());
    UNODB_EXPECT_EQ(match, false);
    const auto k = it.get_key();
    const auto v = it.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 1);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
    it.next();
    UNODB_EXPECT_FALSE(it.valid());
  }
  {  // forward traversal, after the last key in the data.  match=false
    // and iterator is invalidated.
    bool match = true;
    auto it = db.test_only_iterator();
    it.seek(unodb::detail::art_key{2}, match, true /*fwd*/);
    UNODB_EXPECT_FALSE(it.valid());
    UNODB_EXPECT_EQ(match, false);
  }
  {  // reverse traversal, before the first key in the data.
    // match=false and iterator is invalidated.
    bool match = true;
    auto it = db.test_only_iterator();
    it.seek(unodb::detail::art_key{0}, match, false /*fwd*/);
    UNODB_EXPECT_FALSE(it.valid());
    UNODB_EXPECT_EQ(match, false);
  }
  {  // reverse traversal, after the last key in the data.  match=false
    // and iterator is positioned at the last key.
    bool match = true;
    auto it = db.test_only_iterator();
    it.seek(unodb::detail::art_key{2}, match, false /*fwd*/);
    UNODB_EXPECT_TRUE(it.valid());
    UNODB_EXPECT_EQ(match, false);
    const auto k = it.get_key();
    const auto v = it.get_val();
    UNODB_EXPECT_TRUE(k && k.value() == 1);
    UNODB_EXPECT_TRUE(
        v.has_value() &&
        std::ranges::equal(v.value(), unodb::test::test_values[1]));
    it.next();
    UNODB_EXPECT_FALSE(it.valid());
  }
}

// unit test for the following tree structure, which is setup by how
// we choose the keys.
//
//       I4
//   I4     L2
// L0 L1
TYPED_TEST(ARTIteratorTest, C101) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  const unodb::key k0 = 0xaa00;
  const unodb::key k1 = 0xaa10;
  const unodb::key k2 = 0xab10;
  verifier.insert(k0, unodb::test::test_values[0]);
  verifier.insert(k1, unodb::test::test_values[1]);
  verifier.insert(k2, unodb::test::test_values[2]);
  {    // exact match, forward traversal
    {  // exact match, forward traversal (GTE), first key.
      auto it = db.test_only_iterator();
      bool match = false;
      it.seek(unodb::detail::art_key{k0}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k0);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[0]));
    }
    {  // exact match, forward traversal (GTE), middle key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k1}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k1);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[1]));
    }
    {  // exact match, forward traversal (GTE), last key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k2}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k2);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[2]));
    }
  }
  {    // exact match, reverse traversal
    {  // exact match, reverse traversal (LTE), first key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k0}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k0);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[0]));
    }
    {  // exact match, reverse traversal (LTE), middle key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k1}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k1);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[1]));
    }
    {  // exact match, reverse traversal (LTE), last key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k2}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k2);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[2]));
    }
  }
  {    // before and after the first and last key
    {  // forward traversal, before the first key in the data.
      // match=false and iterator is positioned on the first key in the
      // data.
      bool match = true;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{0}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, false);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k0);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[0]));
    }
    {  // forward traversal, after the last key in the data.  match=false
      // and iterator is invalidated.
      bool match = true;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{0xffff}, match, true /*fwd*/);
      UNODB_EXPECT_FALSE(it.valid());
      UNODB_EXPECT_EQ(match, false);
    }
    {  // reverse traversal, before the first key in the data.
      // match=false and iterator is invalidated.
      bool match = true;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{0}, match, false /*fwd*/);
      UNODB_EXPECT_FALSE(it.valid());
      UNODB_EXPECT_EQ(match, false);
    }
    {  // reverse traversal, after the last key in the data.  match=false
      // and iterator is positioned at the last key.
      bool match = true;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{0xffff}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, false);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k2);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[2]));
      it.next();
      UNODB_EXPECT_FALSE(it.valid());  // nothing more in the iterator.
    }
  }
}

//    I4
// L0 L1 L2
//
TYPED_TEST(ARTIteratorTest, seekThreeLeavesUnderTheRoot) {
  constexpr bool debug = false;
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();
  const unodb::key k0 = 0xaa10;
  const unodb::key k1 = 0xaa20;
  const unodb::key k2 = 0xaa30;
  verifier.insert(k0, unodb::test::test_values[0]);
  verifier.insert(k1, unodb::test::test_values[1]);
  verifier.insert(k2, unodb::test::test_values[2]);
  if (debug) {
    std::cerr << "db state::\n";
    db.dump(std::cerr);
  }
  {    // exact match, forward traversal
    {  // exact match, forward traversal (GTE), first key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k0}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k0);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[0]));
    }
    {  // exact match, forward traversal (GTE), middle key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k1}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k1);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[1]));
    }
    {  // exact match, forward traversal (GTE), last key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k2}, match, true /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k2);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[2]));
    }
  }
  {    // exact match, reverse traversal
    {  // exact match, reverse traversal (LTE), first key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k0}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k0);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[0]));
    }
    {  // exact match, reverse traversal (LTE), middle key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k1}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k1);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[1]));
    }
    {  // exact match, reverse traversal (LTE), last key.
      bool match = false;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{k2}, match, false /*fwd*/);
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, true);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k2);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[2]));
    }
  }
  {    // before and after the first and last key
    {  // forward traversal, before the first key in the data.
      // match=false and iterator is positioned on the first key in the
      // data.
      bool match = true;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{0}, match, true /*fwd*/);
      if (debug) {
        std::cerr << "it.seek(0,&match,true)::\n";
        it.dump(std::cerr);
      }
      UNODB_EXPECT_TRUE(it.valid());
      UNODB_EXPECT_EQ(match, false);
      const auto k = it.get_key();
      const auto v = it.get_val();
      UNODB_EXPECT_TRUE(k && k.value() == k0);
      UNODB_EXPECT_TRUE(
          v.has_value() &&
          std::ranges::equal(v.value(), unodb::test::test_values[0]));
    }
    {  // forward traversal, after the last key in the data.
       // match=false and iterator is invalidated.
      bool match = true;
      auto it = db.test_only_iterator();
      it.seek(unodb::detail::art_key{0xffff}, match, true /*fwd*/);
      if (debug) {
        std::cerr << "it.seek(0xffff,&match,true)::\n";
        it.dump(std::cerr);
      }
      UNODB_EXPECT_FALSE(it.valid());
      UNODB_EXPECT_EQ(match, false);
    }
    {
      {  // reverse traversal, before the first key in the data.
        // match=false and iterator is invalidated.
        bool match = true;
        auto it = db.test_only_iterator();
        it.seek(unodb::detail::art_key{0}, match, false /*fwd*/);
        if (debug) {
          std::cerr << "it.seek(0,&match,true)::\n";
          it.dump(std::cerr);
        }
        UNODB_EXPECT_FALSE(it.valid());
        UNODB_EXPECT_EQ(match, false);
      }
      {  // reverse traversal, after the last key in the data.
         // match=false and iterator is positioned at the last key.
        bool match = true;
        auto it = db.test_only_iterator();
        it.seek(unodb::detail::art_key{0xffff}, match, false /*fwd*/);
        if (debug) {
          std::cerr << "it.seek(0xffff,&match,false)::\n";
          it.dump(std::cerr);
        }
        UNODB_EXPECT_TRUE(it.valid());
        UNODB_EXPECT_EQ(match, false);
        const auto k = it.get_key();
        const auto v = it.get_val();
        UNODB_EXPECT_TRUE(k && k.value() == k2);
        UNODB_EXPECT_TRUE(
            v.has_value() &&
            std::ranges::equal(v.value(), unodb::test::test_values[2]));
        it.next();
        UNODB_EXPECT_FALSE(it.valid());
      }
    }
  }
}

UNODB_END_TESTS()

}  // namespace unodb
