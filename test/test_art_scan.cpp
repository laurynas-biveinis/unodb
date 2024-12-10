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

namespace {
using unodb::detail::thread_syncs;
using unodb::test::test_values;

// Test suite for scan() API for the ART.
//
// FIXME There are three scan() variants.  Each of them needs to be
// tested since they are independent.  The existing test coverage is
// enough to make sure that each of them compiles.
//
// FIXME Tests which focus on scan_range(fromKey,toKey) and insure
// proper upper bound and reordering of the keys to determine forward
// or reverse traversal.
//
// FIXME unit tests for gsl::span<std::byte>
//
template <class Db>
class ARTScanTest : public ::testing::Test {
 public:
  using Test::Test;
};

static inline bool odd(const unodb::key x) {return x % 2;}
static inline bool even(const unodb::key x) {return ! odd( x );}

static void dump(const std::vector<unodb::key>& x) {
  std::cerr<<"[";
  auto it = x.begin();
  while ( it != x.end() ) {
      std::cerr<<*it<<" ";
      it++;
  }
  std::cerr<<"]"<<std::endl;
}

// Test help creates an index and populates it with the ODD keys in
// [0:limit-1] so the first key is always ONE (1).  It then verifies
// the correct behavior of scan_range(fromKey,toKey) against that
// index.  Since the data only contains the ODD keys, you can probe
// with EVEN keys and verify that the scan() is carried out from the
// appropriate key in the data when the fromKey and/or toKey do not
// exist in the data.
//
// @param fromKey
// @param toKey
// @param limit The largest key to be installed (if EVEN, then the largest key is limit-1).
template <typename TypeParam>
void doScanTest(const unodb::key fromKey, const unodb::key toKey, const uint64_t limit) {
  constexpr bool debug = false;
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  // Insert odd keys into the database and into an ordered container.
  std::vector<unodb::key> expected {};
  for ( uint64_t i = 1; i < limit; i+=2 ) {
    verifier.insert( i, unodb::test::test_values[0] );
    if ( i >= fromKey && i < toKey ) {
      expected.push_back( i );
    }
  }
  if constexpr( debug ) {
    std::cerr<<"db state::\n"; verifier.get_db().dump(std::cerr);
  }
  const uint64_t nexpected = expected.size();
  if constexpr( debug ) {
    std::cerr<<"scan_test"
           <<": fromKey="<<fromKey<<", toKey="<<toKey<<", limit="<<limit
           <<", nexpected="<<nexpected<<", expected keys="; dump(expected);
  }
  uint64_t nactual { 0 };  // actual number visited.
  auto eit = expected.begin();
  auto eit2 = expected.end();
  auto fn = [&nactual,&eit,eit2](unodb::visitor<typename TypeParam::iterator>& v) {
    if ( eit == eit2 ) {
      EXPECT_TRUE(false)<<"ART scan should have halted.";
      return true;  // halt early.
    }
    const auto ekey = *eit;  // expected key to visit
    const auto akey = v.get_key(); // actual key visited.
    if constexpr( debug ) {
      std::cerr<< "nactual="<<nactual<<", ekey="<<ekey<<", akey="<<akey<<std::endl;
      v.dump(std::cerr);
    }
    if ( ekey != akey ) {
      EXPECT_EQ( ekey, akey );
      return true;  // halt early.
    }
    nactual++;    // count #of visited keys.
    eit++;        // advance iterator over the expected keys.
    return false; // !halt (aka continue scan).
  };
  db.scan_range( fromKey, toKey, fn );
  EXPECT_TRUE( eit == eit2 ) << "Expected iterator should have been fully consumed, but was not (ART scan visited too little).";
  EXPECT_EQ( nactual, nexpected )<<", fromKey="<<fromKey<<", toKey="<<toKey<<", limit="<<limit;
}

//
// template meta parameters.
//
using ARTTypes = ::testing::Types<unodb::db, unodb::mutex_db, unodb::olc_db>;

UNODB_TYPED_TEST_SUITE(ARTScanTest, ARTTypes)

UNODB_START_TYPED_TESTS()

//
// forward scan
//

TYPED_TEST(ARTScanTest, scan_forward__empty_tree_keys_and_values) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  {
    uint64_t n = 0;
    auto fn = [&n](unodb::visitor<typename TypeParam::iterator>&) {n++; return false;};
    db.scan( fn );
    UNODB_EXPECT_EQ( 0, n );
  }
  {
    uint64_t n = 0;
    auto fn = [&n](unodb::visitor<typename TypeParam::iterator>&) {n++; return false;};
    db.scan_from( 0x0000, fn );
    UNODB_EXPECT_EQ( 0, n );
  }
  {
    uint64_t n = 0;
    auto fn = [&n](unodb::visitor<typename TypeParam::iterator>&) {n++; return false;};
    db.scan_range( 0x0000, 0xffff, fn );
    UNODB_EXPECT_EQ( 0, n );
  }
}

// Scan one leaf, verifying that we visit the leaf and can access its key and value.
TYPED_TEST(ARTScanTest, scan_forward__one_leaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  uint64_t n = 0;
  unodb::key visited_key{~0ULL};
  unodb::value_view visited_val{};
  auto fn = [&n,&visited_key,&visited_val](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = v.get_key();
    visited_val = v.get_value();
    return false;
  };
  db.scan( fn );
  UNODB_EXPECT_EQ( 1, n );
  UNODB_EXPECT_EQ( 0, visited_key );
  UNODB_EXPECT_EQ( unodb::test::test_values[0], visited_val );
}

TYPED_TEST(ARTScanTest, scan_forward__two_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  uint64_t n = 0;
  std::vector<std::pair<unodb::key,unodb::value_view>> visited {};
  auto fn = [&n,&visited](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back( v.get_key(), v.get_value() );
    return false;
  };
  db.scan( fn, true/*fwd*/ );
  UNODB_EXPECT_EQ( 2, n );
  UNODB_EXPECT_EQ( 2, visited.size() );  
  UNODB_EXPECT_EQ( 0, visited[0].first ); // make sure we visited things in forward order.
  UNODB_EXPECT_EQ( 1, visited[1].first );
}

TYPED_TEST(ARTScanTest, scan_forward__three_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected++;
    return false;
  };
  db.scan( fn, true/*fwd*/ );
  UNODB_EXPECT_EQ( 3, n );
}

TYPED_TEST(ARTScanTest, scan_forward__four_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  verifier.insert( 3, unodb::test::test_values[3] );
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected++;
    return false;
  };
  db.scan( fn );
  UNODB_EXPECT_EQ( 4, n );
}

TYPED_TEST(ARTScanTest, scan_forward__five_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  verifier.insert( 3, unodb::test::test_values[3] );
  verifier.insert( 4, unodb::test::test_values[4] );
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected++;
    return false;
  };
  db.scan( fn );
  UNODB_EXPECT_EQ( 5, n );
}

TYPED_TEST(ARTScanTest, scan_forward__five_leaves_halt_early) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  verifier.insert( 3, unodb::test::test_values[3] );
  verifier.insert( 4, unodb::test::test_values[4] );
  uint64_t n = 0;
  auto fn = [&n](unodb::visitor<typename TypeParam::iterator>&) {n++; return n==1;};  // halt early!
  db.scan( fn );
  UNODB_EXPECT_EQ( 1, n );
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scan_forward__100_entries) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert_key_range( 0, 100 );
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected++;
    return false;
  };
  db.scan( fn );
  UNODB_EXPECT_EQ( 100, n );
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scan_forward__1000_entries) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert_key_range( 0, 1000 );
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected++;
    return false;
  };
  db.scan( fn );
  UNODB_EXPECT_EQ( 1000, n );
}

//
// reverse scan
//

TYPED_TEST(ARTScanTest, scan_reverse__empty_tree) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  uint64_t n = 0;
  auto fn = [&n](unodb::visitor<typename TypeParam::iterator>&) {n++; return false;};
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 0, n );
}

// Scan one leaf, verifying that we visit the leaf and can access its key and value.
TYPED_TEST(ARTScanTest, scan_reverse__one_leaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  uint64_t n = 0;
  unodb::key visited_key{~0ULL};
  unodb::value_view visited_val{};
  auto fn = [&n,&visited_key,&visited_val](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = v.get_key();
    visited_val = v.get_value();
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 1, n );
  UNODB_EXPECT_EQ( 0, visited_key );
  UNODB_EXPECT_EQ( unodb::test::test_values[0], visited_val );
}

TYPED_TEST(ARTScanTest, scan_reverse__two_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  uint64_t n = 0;
  std::vector<std::pair<unodb::key,unodb::value_view>> visited {};
  auto fn = [&n,&visited](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back( v.get_key(), v.get_value() );
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 2, n );
  UNODB_EXPECT_EQ( 2, visited.size() );  
  UNODB_EXPECT_EQ( 1, visited[0].first ); // make sure we visited things in reverse order.
  UNODB_EXPECT_EQ( 0, visited[1].first );
}

TYPED_TEST(ARTScanTest, scan_reverse__three_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  uint64_t n = 0;
  uint64_t expected = 2;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected--;
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 3, n );
}

TYPED_TEST(ARTScanTest, scan_reverse__four_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  verifier.insert( 3, unodb::test::test_values[3] );
  uint64_t n = 0;
  uint64_t expected = 3;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected--;
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 4, n );
}

TYPED_TEST(ARTScanTest, scan_reverse__five_leaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  verifier.insert( 3, unodb::test::test_values[3] );
  verifier.insert( 4, unodb::test::test_values[4] );
  uint64_t n = 0;
  uint64_t expected = 4;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected--;
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 5, n );
}

TYPED_TEST(ARTScanTest, scan_reverse__five_leaves_halt_early) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert( 0, unodb::test::test_values[0] );
  verifier.insert( 1, unodb::test::test_values[1] );
  verifier.insert( 2, unodb::test::test_values[2] );
  verifier.insert( 3, unodb::test::test_values[3] );
  verifier.insert( 4, unodb::test::test_values[4] );
  uint64_t n = 0;
  auto fn = [&n](unodb::visitor<typename TypeParam::iterator>&) {n++; return n==1;};  // halt early!
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 1, n );
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scan_reverse__100_entries) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert_key_range( 0, 100 );
  uint64_t n = 0;
  uint64_t expected = 99;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected--;
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 100, n );
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scan_reverse__1000_entries) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db(); // reference to the database instance under test.
  verifier.insert_key_range( 0, 1000 );
  uint64_t n = 0;
  uint64_t expected = 999;
  auto fn = [&n,&expected](unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = v.get_key();
    UNODB_EXPECT_EQ( expected, key );
    expected--;
    return false;
  };
  db.scan( fn, false/*fwd*/ );
  UNODB_EXPECT_EQ( 1000, n );
}

// Tests for edge cases for scan() including first key missing, last
// key missing, both end keys missing, both end keys are the same (and
// both exist or one exists or both are missing), etc.
//
// Check the edge conditions for the single leaf iterator (limit=1, so
// only ONE (1) is installed into the ART index).  Check all iterator
// flavors for this.
TYPED_TEST(ARTScanTest, scan_from__fromKey_0__toKey_1__entries_1) {doScanTest<TypeParam>( 0, 1, 1 );} // nothing
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_2__entries_1) {doScanTest<TypeParam>( 1, 2, 1 );} // one key
TYPED_TEST(ARTScanTest, scan_from__fromKey_2__toKey_3__entries_1) {doScanTest<TypeParam>( 2, 3, 1 );} // nothing
TYPED_TEST(ARTScanTest, scan_from__fromKey_0__toKey_2__entries_1) {doScanTest<TypeParam>( 0, 2, 1 );} // one key
TYPED_TEST(ARTScanTest, scan_from__fromKey_2__toKey_2__entries_1) {doScanTest<TypeParam>( 2, 2, 1 );} // nothing

//
// TODO Do reverse traversal checks.
//

//
// FIXME (***) DO GENERAL CHECKS FOR LARGER TREES. For example, we
// could generate trees with a space between each pair of keys and use
// that to examine the before/after semantics of seek() for both
// forward and reverse traversal.  For this, make sure that we hit
// enough cases to (a) test a variety of internal node types; and (b)
// check a variety of key prefix length conditions.

// fromKey is odd (exists); toKey is even (hence does not exist).
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_2__entries_5) {doScanTest<TypeParam>( 1, 2, 5 );}
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_4__entries_5) {doScanTest<TypeParam>( 1, 4, 5 );}
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_6__entries_5) {doScanTest<TypeParam>( 1, 6, 5 );}
// fromKey is odd (exists); toKey is odd (exists).
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_1__entries_5) {doScanTest<TypeParam>( 1, 1, 5 );}
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_3__entries_5) {doScanTest<TypeParam>( 1, 3, 5 );}
TYPED_TEST(ARTScanTest, scan_from__fromKey_1__toKey_5__entries_5) {doScanTest<TypeParam>( 1, 5, 5 );}

TYPED_TEST(ARTScanTest, scan_from__fromKey_0__toKey_10__entries_10) {doScanTest<TypeParam>( 0, 10, 10 );}

TYPED_TEST(ARTScanTest, scan_from__1000_entries__fromKey_1__toKey_999) {doScanTest<TypeParam>( 1, 999, 1000 );}

UNODB_END_TESTS()

}  // namespace
