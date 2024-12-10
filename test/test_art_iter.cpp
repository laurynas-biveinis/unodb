// Copyright 2019-2024 Laurynas Biveinis

// IWYU pragma: no_include <array>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

#include "global.hpp"

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

namespace unodb {
using unodb::detail::thread_syncs;
using unodb::test::test_values;

// Test suite for an ART iterator.
template <class Db>
class ARTIteratorTest : public ::testing::Test {
 public:
  using Test::Test;
};

//
// aliases for db begin() and end() methods that would otherwise not be accessible to iterator friend tests.
//
#define begin(db) db.__test_only_iterator__().first()
#define last(db) db.__test_only_iterator__().last()
#define end(db) db.__test_only_iterator__()

using ARTTypes = ::testing::Types<unodb::db, unodb::mutex_db, unodb::olc_db>;

UNODB_TYPED_TEST_SUITE(ARTIteratorTest, ARTTypes)

UNODB_START_TYPED_TESTS()

// FIXME variable length keys :: unit tests for gsl::span<std::byte>

// unit test with an empty tree.
TYPED_TEST(ARTIteratorTest, empty_tree__forward_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  auto b = begin(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b == e );
  UNODB_EXPECT_TRUE( ! b.get_key() );
  UNODB_EXPECT_TRUE( ! b.get_val() );
}

// unit test with an empty tree.
TYPED_TEST(ARTIteratorTest, empty_tree__reverse_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  auto b = last(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b == e );
  UNODB_EXPECT_TRUE( ! b.get_key() );
  UNODB_EXPECT_TRUE( ! b.get_val() );
}

// unit test where the root is a single leaf.
TYPED_TEST(ARTIteratorTest, single_leaf_iterator_one_value) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  auto b = begin(db); // obtain iterators.
  const auto e = end(db);
  // std::cerr<<"db state::\n"; db.dump(std::cerr);
  // std::cerr<<"begin()::\n"; b.dump(std::cerr);
  UNODB_EXPECT_TRUE( b != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.next() == e ); // nothing more in the iterator.
}

// unit test where the root is an I4 with two leafs under it.
TYPED_TEST(ARTIteratorTest, I4_and_two_leaves__forward_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  //std::cerr<<"db state::\n"; db.dump(std::cerr);
  auto b = begin(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b != e );
  //std::cerr<<"begin()::\n"; b.dump(std::cerr);
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.next() != e );
  //std::cerr<<"b.next()::\n"; b.dump(std::cerr);
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 1 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[1] );
  UNODB_EXPECT_TRUE( b.next() == e ); // nothing more in the iterator.
  //std::cerr<<"b.next()::\n"; b.dump(std::cerr);
}

// unit test where the root is an I4 with two leafs under it.
TYPED_TEST(ARTIteratorTest, I4_and_two_leaves__reverse_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  //std::cerr<<"db state::\n"; db.dump(std::cerr);
  auto b = last(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b != e );
  //std::cerr<<"begin()::\n"; b.dump(std::cerr);
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 1 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[1] );
  UNODB_EXPECT_TRUE( b.prior() != e );
  //std::cerr<<"b.next()::\n"; b.dump(std::cerr);
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.prior() == e ); // nothing more in the iterator.
  //std::cerr<<"b.next()::\n"; b.dump(std::cerr);
}

// unit test for the following tree structure, which is setup by how we choose the keys.
//
//       I4
//   I4     L2
// L0 L1
TYPED_TEST(ARTIteratorTest, iterator_three_values_left_axis_two_deep_right_axis_one_deep__forward_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0xaa00, unodb::test::test_values[0] );
  verifier.insert( 0xaa01, unodb::test::test_values[1] );
  verifier.insert( 0xab00, unodb::test::test_values[2] );
  auto b = begin(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xaa00 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.next() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xaa01 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[1] );
  UNODB_EXPECT_TRUE( b.next() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xab00 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[2] );
  UNODB_EXPECT_TRUE( b.next() == e ); // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how we choose the keys.
//
//       I4
//   I4     L2
// L0 L1
TYPED_TEST(ARTIteratorTest, iterator_three_values_left_axis_two_deep_right_axis_one_deep__reverse_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0xaa00, unodb::test::test_values[0] );
  verifier.insert( 0xaa01, unodb::test::test_values[1] );
  verifier.insert( 0xab00, unodb::test::test_values[2] );
  auto b = last(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xab00 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[2] );
  UNODB_EXPECT_TRUE( b.prior() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xaa01 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[1] );
  UNODB_EXPECT_TRUE( b.prior() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xaa00 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.prior() == e ); // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how we choose the keys.
//
//       I4
//   L0     I4
//        L1 L2
TYPED_TEST(ARTIteratorTest, single_node_iterators_three_values_left_axis_one_deep_right_axis_two_deep__forward_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0xaa00, unodb::test::test_values[0] );
  verifier.insert( 0xab0c, unodb::test::test_values[1] );
  verifier.insert( 0xab0d, unodb::test::test_values[2] );
  auto b = begin(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xaa00 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.next() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xab0c );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[1] );
  UNODB_EXPECT_TRUE( b.next() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xab0d );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[2] );
  UNODB_EXPECT_TRUE( b.next() == e ); // nothing more in the iterator.
}

// unit test for the following tree structure, which is setup by how we choose the keys.
//
//       I4
//   L0     I4
//        L1 L2
TYPED_TEST(ARTIteratorTest, single_node_iterators_three_values_left_axis_one_deep_right_axis_two_deep__reverse_scan) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0xaa00, unodb::test::test_values[0] );
  verifier.insert( 0xab0c, unodb::test::test_values[1] );
  verifier.insert( 0xab0d, unodb::test::test_values[2] );
  auto b = last(db); // obtain iterators.
  const auto e = end(db);
  UNODB_EXPECT_TRUE( b != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xab0d );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[2] );
  UNODB_EXPECT_TRUE( b.prior() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xab0c );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[1] );
  UNODB_EXPECT_TRUE( b.prior() != e );
  UNODB_EXPECT_TRUE( b.get_key() && b.get_key().value() == 0xaa00 );
  UNODB_EXPECT_TRUE( b.get_val() && b.get_val().value() == unodb::test::test_values[0] );
  UNODB_EXPECT_TRUE( b.prior() == e ); // nothing more in the iterator.
}

//
// seek()
//

// unit test with an empty tree.
TYPED_TEST(ARTIteratorTest, empty_tree__seek) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  const auto e = end(db);
  bool match = false;
  UNODB_EXPECT_TRUE( end(db).seek( unodb::detail::art_key{0}, match, true/*fwd*/ ) == e );
  UNODB_EXPECT_EQ( match, false );
  UNODB_EXPECT_TRUE( end(db).seek( unodb::detail::art_key{0}, match, false/*fwd*/ ) == e );
  UNODB_EXPECT_EQ( match, false );
}

// unit test where the root is a single leaf.
TYPED_TEST(ARTIteratorTest, single_leaf__seek) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 1, unodb::test::test_values[1] );
  //std::cerr<<"db state::\n"; db.dump(std::cerr);
  const auto e = end(db);
  { // exact match, forward traversal (GTE)
    bool match = false;
    auto it = end(db).seek( unodb::detail::art_key{1}, match, true/*fwd*/ );
    UNODB_EXPECT_TRUE( it != e );
    UNODB_EXPECT_EQ( match, true );
    UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == 1 );
    UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    UNODB_EXPECT_TRUE( it.next() == e ); // nothing more in the iterator.
  }
  { // exact match, reverse traversal (LTE)
    bool match = false;
    auto it = end(db).seek( unodb::detail::art_key{1}, match, false/*fwd*/ );  
    UNODB_EXPECT_TRUE( it != e );
    UNODB_EXPECT_EQ( match, true );
    UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == 1 );
    UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    UNODB_EXPECT_TRUE( it.next() == e ); // nothing more in the iterator.
  }
  { // forward traversal, before the first key in the data.
    // match=false and iterator is positioned on the first key in the
    // data.
    bool match = true;
    auto it = end(db).seek( unodb::detail::art_key{0}, match, true/*fwd*/ );
    //std::cerr<<"end(db).seek(0,&match,true)::\n"; it.dump(std::cerr);
    UNODB_EXPECT_TRUE( it != e );
    UNODB_EXPECT_EQ( match, false );
    UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == 1 );
    UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    UNODB_EXPECT_TRUE( it.next() == e ); // nothing more in the iterator.
  }
  { // forward traversal, after the last key in the data.  match=false
    // and iterator is positioned at end().
    bool match = true;
    auto it = end(db).seek( unodb::detail::art_key{2}, match, true/*fwd*/ );
    UNODB_EXPECT_TRUE( it == e );
    UNODB_EXPECT_EQ( match, false );
  }
  { // reverse traversal, before the first key in the data.
    // match=false and iterator is positioned at end().
    bool match = true;
    auto it = end(db).seek( unodb::detail::art_key{0}, match, false/*fwd*/ );
    UNODB_EXPECT_TRUE( it == e );
    UNODB_EXPECT_EQ( match, false );
  }
  { // reverse traversal, after the last key in the data.  match=false
    // and iterator is positioned at the last key.
    bool match = true;
    auto it = end(db).seek( unodb::detail::art_key{2}, match, false/*fwd*/ );
    //std::cerr<<"end(db).seek(2,&match,false)::\n"; it.dump(std::cerr);
    UNODB_EXPECT_TRUE( it != e );
    UNODB_EXPECT_EQ( match, false );
    UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == 1 );
    UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    UNODB_EXPECT_TRUE( it.next() == e ); // nothing more in the iterator.
  }
}

// unit test for the following tree structure, which is setup by how we choose the keys.
//
//       I4
//   I4     L2
// L0 L1
TYPED_TEST(ARTIteratorTest, seek_three_values_left_axis_two_deep_right_axis_one_deep) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  const unodb::key k0 = 0xaa00;
  const unodb::key k1 = 0xaa10;
  const unodb::key k2 = 0xab10;
  verifier.insert( k0, unodb::test::test_values[0] );
  verifier.insert( k1, unodb::test::test_values[1] );
  verifier.insert( k2, unodb::test::test_values[2] );
  //std::cerr<<"db state::\n"; db.dump(std::cerr);
  const auto e = end(db);
  { // exact match, forward traversal
    { // exact match, forward traversal (GTE), first key.
      bool match = false;
      auto it = end(db).seek( unodb::detail::art_key{k0}, match, true/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k0 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[0] );
    }
    { // exact match, forward traversal (GTE), middle key.
      bool match = false;
      auto it = end(db).seek( unodb::detail::art_key{k1}, match, true/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k1 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    }
    { // exact match, forward traversal (GTE), last key.
      bool match = false;
      auto it = end(db).seek( unodb::detail::art_key{k2}, match, true/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k2 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[2] );
    }
  }
  { // exact match, reverse traversal
    { // exact match, reverse traversal (LTE), first key.
      bool match = false;
      auto it = end(db).seek( unodb::detail::art_key{k0}, match, false/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k0 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[0] );
    }
    { // exact match, reverse traversal (LTE), middle key.
      bool match = false;
      auto it = end(db).seek( unodb::detail::art_key{k1}, match, false/*fwd*/ );  
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k1 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    }
    { // exact match, reverse traversal (LTE), last key.
      bool match = false;
      auto it = end(db).seek( unodb::detail::art_key{k2}, match, false/*fwd*/ );  
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k2 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[2] );
    }
  }
  { // before and after the first and last key
    { // forward traversal, before the first key in the data.
      // match=false and iterator is positioned on the first key in the
      // data.
      bool match = true;
      auto it = end(db).seek( unodb::detail::art_key{0}, match, true/*fwd*/ );
      //std::cerr<<"end(db).seek(0,&match,true)::\n"; it.dump(std::cerr);
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, false );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k0 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[0] );
    }
    { // forward traversal, after the last key in the data.  match=false
      // and iterator is positioned at end().
      bool match = true;
      auto it = end(db).seek( unodb::detail::art_key{0xffff}, match, true/*fwd*/ );
      //std::cerr<<"end(db).seek(0xffff,&match,true)::\n"; it.dump(std::cerr);
      UNODB_EXPECT_TRUE( it == e );
      UNODB_EXPECT_EQ( match, false );
    }
    { // reverse traversal, before the first key in the data.
      // match=false and iterator is positioned at end().
      bool match = true;
      auto it = end(db).seek( unodb::detail::art_key{0}, match, false/*fwd*/ );
      UNODB_EXPECT_TRUE( it == e );
      UNODB_EXPECT_EQ( match, false );
    }
    { // reverse traversal, after the last key in the data.  match=false
      // and iterator is positioned at the last key.
      bool match = true;
      auto it = end(db).seek( unodb::detail::art_key{0xffff}, match, false/*fwd*/ );
      //std::cerr<<"end(db).seek(2,&match,false)::\n"; it.dump(std::cerr);
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, false );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k2 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[2] );
      UNODB_EXPECT_TRUE( it.next() == e ); // nothing more in the iterator.
    }
  }
}

//    I4
// L0 L1 L2
//
TYPED_TEST(ARTIteratorTest, seek_three_leaves_under_the_root) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  const unodb::key k0 = 0xaa10;
  const unodb::key k1 = 0xaa20;
  const unodb::key k2 = 0xaa30;
  verifier.insert( k0, unodb::test::test_values[0] );
  verifier.insert( k1, unodb::test::test_values[1] );
  verifier.insert( k2, unodb::test::test_values[2] );
  std::cerr<<"db state::\n"; db.dump(std::cerr);
  const auto e = end(db);
  if(true) { // exact match, forward traversal
    { // exact match, forward traversal (GTE), first key.
      bool match = false;
      auto it = end(db).seek(unodb::detail::art_key{k0}, match, true/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k0 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[0] );
    }
    { // exact match, forward traversal (GTE), middle key.
      bool match = false;
      auto it = end(db).seek(unodb::detail::art_key{k1}, match, true/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k1 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    }
    { // exact match, forward traversal (GTE), last key.
      bool match = false;
      auto it = end(db).seek(unodb::detail::art_key{k2}, match, true/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k2 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[2] );
    }
  }
  if(true) { // exact match, reverse traversal
    { // exact match, reverse traversal (LTE), first key.
      bool match = false;
      auto it = end(db).seek(unodb::detail::art_key{k0}, match, false/*fwd*/ );
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k0 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[0] );
    }
    { // exact match, reverse traversal (LTE), middle key.
      bool match = false;
      auto it = end(db).seek(unodb::detail::art_key{k1}, match, false/*fwd*/ );  
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k1 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[1] );
    }
    { // exact match, reverse traversal (LTE), last key.
      bool match = false;
      auto it = end(db).seek(unodb::detail::art_key{k2}, match, false/*fwd*/ );  
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, true );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k2 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[2] );
    }
  }
  { // before and after the first and last key
    if(true){ // forward traversal, before the first key in the data.
      // match=false and iterator is positioned on the first key in the
      // data.
      bool match = true;
      auto it = end(db).seek(unodb::detail::art_key{0}, match, true/*fwd*/ );
      std::cerr<<"end(db).seek(0,&match,true)::\n"; it.dump(std::cerr);
      UNODB_EXPECT_TRUE( it != e );
      UNODB_EXPECT_EQ( match, false );
      UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k0 );
      UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[0] );
    }
    if(true) { // forward traversal, after the last key in the data.  match=false
      // and iterator is positioned at end().
      bool match = true;
      auto it = end(db).seek(unodb::detail::art_key{0xffff}, match, true/*fwd*/ );
      std::cerr<<"end(db).seek(0xffff,&match,true)::\n"; it.dump(std::cerr);
      UNODB_EXPECT_TRUE( it == e );
      UNODB_EXPECT_EQ( match, false );
    }
    {
      if(true) { // reverse traversal, before the first key in the data.
        // match=false and iterator is positioned at end().
        bool match = true;
        auto it = end(db).seek(unodb::detail::art_key{0}, match, false/*fwd*/ );
        std::cerr<<"end(db).seek(0,&match,true)::\n"; it.dump(std::cerr);
        UNODB_EXPECT_TRUE( it == e );
        UNODB_EXPECT_EQ( match, false );
      }
      if(true){ // reverse traversal, after the last key in the data.  match=false
        // and iterator is positioned at the last key.
        bool match = true;
        auto it = end(db).seek(unodb::detail::art_key{0xffff}, match, false/*fwd*/ );
        std::cerr<<"end(db).seek(0xffff,&match,false)::\n"; it.dump(std::cerr);
        UNODB_EXPECT_TRUE( it != e );
        UNODB_EXPECT_EQ( match, false );
        UNODB_EXPECT_TRUE( it.get_key() && it.get_key().value() == k2 );
        UNODB_EXPECT_TRUE( it.get_val() && it.get_val().value() == unodb::test::test_values[2] );
        UNODB_EXPECT_TRUE( it.next() == e ); // nothing more in the iterator.
      }
    }
  }
}

UNODB_END_TESTS()

}  // namespace
