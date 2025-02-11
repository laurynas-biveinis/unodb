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
// IWYU pragma: no_include <span>
// IWYU pragma: no_include <string>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "art_common.hpp"

#include "db_test_utils.hpp"
#include "gtest_utils.hpp"

namespace {

// Test suite for scan() API for the ART.
//
// TODO(thompsonbry) variable length keys: unit tests for std::span<std::byte>
//
template <class Db>
class ARTScanTest : public ::testing::Test {
 public:
  using Test::Test;
};

// decode a uint64_t key.
inline std::uint64_t decode(unodb::key_view akey) {
  unodb::key_decoder dec{akey};
  std::uint64_t k;
  dec.decode(k);
  return k;
}

// used with conditional compilation for debug.
//
// LCOV_EXCL_START
[[maybe_unused]] void dump(
    const std::vector<std::pair<std::uint64_t, unodb::value_view>>& x) {
  std::cerr << "[";
  for (const auto& key_and_val : x) {
    std::cerr << "(" << key_and_val.first << ") ";
  }
  std::cerr << "]\n";
}
// LCOV_EXCL_STOP

// Test help creates an index and populates it with the ODD keys in
// [0:limit] so the first key is always ONE (1).  It then verifies the
// correct behavior of scan_range(from_key,to_key) against that index.
// Since the data only contains the ODD keys, you can probe with EVEN
// keys and verify that the scan() is carried out from the appropriate
// key in the data when the from_key and/or to_key do not exist in the
// data.
//
// @param from_key
//
// @param to_key
//
// @param limit The largest key to be installed (ODD).
template <typename TypeParam>
void do_scan_range_test(std::uint64_t from_key, std::uint64_t to_key,
                        std::uint32_t limit) {
  constexpr bool debug = false;
  if (!(limit % 2)) FAIL() << "limit=" << limit << " must be odd";
  if (debug)
    std::cerr << "from_key=" << from_key << ", to_key=" << to_key
              << ", limit=" << limit << "\n";
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  // Insert odd keys into the database and into an ordered container.
  std::vector<std::pair<std::uint64_t, unodb::value_view>> expected{};
  if (from_key < to_key) {
    for (uint64_t i = 1; i < limit; i += 2) {
      const auto val =
          unodb::test::test_values[i % unodb::test::test_values.size()];
      verifier.insert(i, val);
      if (i >= from_key && i < to_key) {
        expected.emplace_back(i, val);
      }
    }
  } else {  // reverse scan
    for (auto i = static_cast<std::int64_t>(limit); i >= 0; i -= 2) {
      const auto j = static_cast<std::uint64_t>(i);
      const auto val =
          unodb::test::test_values[j % unodb::test::test_values.size()];
      verifier.insert(j, val);
      if (j <= from_key && j > to_key) {
        expected.emplace_back(j, val);
      }
    }
  }
  if constexpr (debug) {
    std::cerr << "db state::\n";
    verifier.get_db().dump(std::cerr);
  }
  const uint64_t nexpected = expected.size();
  if constexpr (debug) {
    std::cerr << "scan_test"
              << ": from_key=" << from_key << ", to_key=" << to_key
              << ", limit=" << limit << ", nexpected=" << nexpected
              << ", expected keys=";
    dump(expected);
  }
  uint64_t nactual{0};  // actual number visited.
  auto eit = expected.begin();
  auto eit2 = expected.end();
  auto fn = [&nactual, &eit,
             eit2](const unodb::visitor<typename TypeParam::iterator>& v) {
    if (eit == eit2) {
      // LCOV_EXCL_START
      EXPECT_TRUE(false) << "ART scan should have halted.";
      return true;  // halt early.
      // LCOV_EXCL_STOP
    }
    const auto ekey = (*eit).first;         // expected key to visit
    const auto eval = (*eit).second;        // expected val to visit
    const auto akey = decode(v.get_key());  // actual key visited.
    const auto aval = v.get_value();        // actual val visited.
    if constexpr (debug) {
      std::cerr << "nactual=" << nactual << ", ekey=" << ekey
                << ", akey=" << akey << "\n";
    }
    if (akey != ekey) {
      // LCOV_EXCL_START
      EXPECT_EQ(akey, ekey);
      return true;  // halt early.
      // LCOV_EXCL_STOP
    }
    EXPECT_TRUE(std::ranges::equal(aval, eval));
    nactual++;     // count #of visited keys.
    eit++;         // advance iterator over the expected keys.
    return false;  // !halt (aka continue scan).
  };
  db.scan_range(from_key, to_key, fn);
  // LCOV_EXCL_START
  EXPECT_TRUE(eit == eit2)
      << "Expected iterator should have been fully consumed, but was not (ART "
         "scan visited too little).";
  EXPECT_EQ(nactual, nexpected) << ", from_key=" << from_key
                                << ", to_key=" << to_key << ", limit=" << limit;
  // LCOV_EXCL_STOP
}

//
// template meta parameters.
//
using ARTTypes =
    ::testing::Types<unodb::test::u64_db, unodb::test::u64_mutex_db,
                     unodb::test::u64_olc_db>;

UNODB_TYPED_TEST_SUITE(ARTScanTest, ARTTypes)

UNODB_START_TYPED_TESTS()

//
// forward scan
//

TYPED_TEST(ARTScanTest, scanForwardEmptyTree) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  {
    uint64_t n = 0;
    auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
      n++;           // LCOV_EXCL_LINE
      return false;  // LCOV_EXCL_LINE
    };
    db.scan(fn);
    UNODB_EXPECT_EQ(0, n);
  }
  {
    uint64_t n = 0;
    auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
      n++;           // LCOV_EXCL_LINE
      return false;  // LCOV_EXCL_LINE
    };
    db.scan_from(0x0000, fn);
    UNODB_EXPECT_EQ(0, n);
  }
  {
    uint64_t n = 0;
    auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
      n++;           // LCOV_EXCL_LINE
      return false;  // LCOV_EXCL_LINE
    };
    db.scan_range(0x0000, 0xffff, fn);
    UNODB_EXPECT_EQ(0, n);
  }
}

// Scan one leaf, verifying that we visit the leaf and can access its key and
// value.
TYPED_TEST(ARTScanTest, scanForwardOneLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  uint64_t n = 0;
  std::uint64_t visited_key{~0ULL};
  typename TypeParam::value_view visited_val{};
  auto fn = [&n, &visited_key, &visited_val](
                const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = decode(v.get_key());
    visited_val = v.get_value();
    return false;
  };
  db.scan(fn);
  UNODB_EXPECT_EQ(1, n);
  UNODB_EXPECT_EQ(visited_key, 0);
  UNODB_EXPECT_TRUE(
      std::ranges::equal(visited_val, unodb::test::test_values[0]));
}

TYPED_TEST(ARTScanTest, scanFromForwardOneLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  uint64_t n = 0;
  std::uint64_t visited_key{~0ULL};
  typename TypeParam::value_view visited_val{};
  auto fn = [&n, &visited_key, &visited_val](
                const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = decode(v.get_key());
    visited_val = v.get_value();
    return false;
  };
  db.scan_from(0, fn);
  UNODB_EXPECT_EQ(1, n);
  UNODB_EXPECT_EQ(visited_key, 0);
  UNODB_EXPECT_TRUE(
      std::ranges::equal(visited_val, unodb::test::test_values[0]));
}

TYPED_TEST(ARTScanTest, scanRangeForwardOneLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  uint64_t n = 0;
  std::uint64_t visited_key{~0ULL};
  typename TypeParam::value_view visited_val{};
  auto fn = [&n, &visited_key, &visited_val](
                const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = decode(v.get_key());
    visited_val = v.get_value();
    return false;
  };
  db.scan_range(0, 1, fn);
  UNODB_EXPECT_EQ(1, n);
  UNODB_EXPECT_EQ(visited_key, 0);
  UNODB_EXPECT_TRUE(
      std::ranges::equal(visited_val, unodb::test::test_values[0]));
}

TYPED_TEST(ARTScanTest, scanForwardTwoLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  uint64_t n = 0;
  std::vector<std::pair<std::uint64_t, typename TypeParam::value_view>>
      visited{};
  auto fn = [&n,
             &visited](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back(decode(v.get_key()), v.get_value());
    return false;
  };
  db.scan(fn, true /*fwd*/);
  UNODB_EXPECT_EQ(2, n);
  UNODB_EXPECT_EQ(2, visited.size());
  UNODB_EXPECT_EQ(0, visited[0].first);  // verify visited in forward order.
  UNODB_EXPECT_EQ(1, visited[1].first);
}

TYPED_TEST(ARTScanTest, scanFromForwardTwoLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  uint64_t n = 0;
  std::vector<std::pair<std::uint64_t, typename TypeParam::value_view>>
      visited{};
  auto fn = [&n,
             &visited](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back(decode(v.get_key()), v.get_value());
    return false;
  };
  db.scan_from(0, fn, true /*fwd*/);
  UNODB_EXPECT_EQ(2, n);
  UNODB_EXPECT_EQ(2, visited.size());
  UNODB_EXPECT_EQ(0, visited[0].first);  // verify visited in forward order.
  UNODB_EXPECT_EQ(1, visited[1].first);
}

TYPED_TEST(ARTScanTest, scanRangeForwardTwoLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  uint64_t n = 0;
  std::vector<std::pair<std::uint64_t, typename TypeParam::value_view>>
      visited{};
  auto fn = [&n,
             &visited](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back(decode(v.get_key()), v.get_value());
    return false;
  };
  db.scan_range(0, 2, fn);
  UNODB_EXPECT_EQ(2, n);
  UNODB_EXPECT_EQ(2, visited.size());
  UNODB_EXPECT_EQ(0, visited[0].first);  // verify visited in forward order.
  UNODB_EXPECT_EQ(1, visited[1].first);
}

TYPED_TEST(ARTScanTest, scanForwardThreeLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan(fn, true /*fwd*/);
  UNODB_EXPECT_EQ(3, n);
}

TYPED_TEST(ARTScanTest, scanForwardFourLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan(fn);
  UNODB_EXPECT_EQ(4, n);
}

TYPED_TEST(ARTScanTest, scanForwardFiveLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan(fn);
  UNODB_EXPECT_EQ(5, n);
}

TYPED_TEST(ARTScanTest, scanForwardFiveLeavesHaltEarly) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;
    return n == 1;  // halt early!
  };
  db.scan(fn);
  UNODB_EXPECT_EQ(1, n);
}

TYPED_TEST(ARTScanTest, scanFromForwardFiveLeavesHaltEarly) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;
    return n == 1;  // halt early!
  };
  db.scan_from(1, fn);
  UNODB_EXPECT_EQ(1, n);
}

TYPED_TEST(ARTScanTest, scanRangeForwardFiveLeavesHaltEarly) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;
    return n == 1;  // halt early!
  };
  db.scan_range(1, 3, fn);
  UNODB_EXPECT_EQ(1, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanForward100) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 100);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan(fn);
  UNODB_EXPECT_EQ(100, n);
}

TYPED_TEST(ARTScanTest, scanFromForward100) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 100);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan_from(0, fn);
  UNODB_EXPECT_EQ(100, n);
}

TYPED_TEST(ARTScanTest, scanRangeForward100) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 100);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan_range(0, 100, fn);
  UNODB_EXPECT_EQ(100, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanForward1000) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 1000);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan(fn);
  UNODB_EXPECT_EQ(1000, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanFromForward1000) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 1000);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan_from(0, fn);
  UNODB_EXPECT_EQ(1000, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanRangeForward1000) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 1000);
  uint64_t n = 0;
  uint64_t expected = 0;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected++;
    return false;
  };
  db.scan_range(0, 1000, fn);
  UNODB_EXPECT_EQ(1000, n);
}

//
// reverse scan
//

TYPED_TEST(ARTScanTest, scanReverseEmptyTree) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;           // LCOV_EXCL_LINE
    return false;  // LCOV_EXCL_LINE
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(0, n);
}

// Scan one leaf, verifying that we visit the leaf and can access its
// key and value.
TYPED_TEST(ARTScanTest, scanReverseOneLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  uint64_t n = 0;
  std::uint64_t visited_key{~0ULL};
  typename TypeParam::value_view visited_val{};
  auto fn = [&n, &visited_key, &visited_val](
                const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = decode(v.get_key());
    visited_val = v.get_value();
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(1, n);
  UNODB_EXPECT_EQ(visited_key, 0);
  UNODB_EXPECT_TRUE(
      std::ranges::equal(visited_val, unodb::test::test_values[0]));
}

TYPED_TEST(ARTScanTest, scanFromReverseOneLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  uint64_t n = 0;
  std::uint64_t visited_key{~0ULL};
  typename TypeParam::value_view visited_val{};
  auto fn = [&n, &visited_key, &visited_val](
                const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = decode(v.get_key());
    visited_val = v.get_value();
    return false;
  };
  db.scan_from(0, fn, false /*fwd*/);
  UNODB_EXPECT_EQ(1, n);
  UNODB_EXPECT_EQ(visited_key, 0);
  UNODB_EXPECT_TRUE(
      std::ranges::equal(visited_val, unodb::test::test_values[0]));
}

TYPED_TEST(ARTScanTest, scanRangeReverseOneLeaf) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  uint64_t n = 0;
  std::uint64_t visited_key{~1ULL};
  typename TypeParam::value_view visited_val{};
  auto fn = [&n, &visited_key, &visited_val](
                const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited_key = decode(v.get_key());
    visited_val = v.get_value();
    return false;
  };
  db.scan_range(1, 0, fn);
  UNODB_EXPECT_EQ(1, n);
  UNODB_EXPECT_EQ(visited_key, 1);
  UNODB_EXPECT_TRUE(
      std::ranges::equal(visited_val, unodb::test::test_values[0]));
}

TYPED_TEST(ARTScanTest, scanReverseTwoLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  uint64_t n = 0;
  std::vector<std::pair<std::uint64_t, typename TypeParam::value_view>>
      visited{};
  auto fn = [&n,
             &visited](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back(decode(v.get_key()), v.get_value());
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(2, n);
  UNODB_EXPECT_EQ(2, visited.size());
  UNODB_EXPECT_EQ(2, visited[0].first);  // make sure visited in reverse order.
  UNODB_EXPECT_EQ(1, visited[1].first);
}

TYPED_TEST(ARTScanTest, scanFromReverseTwoLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  uint64_t n = 0;
  std::vector<std::pair<std::uint64_t, typename TypeParam::value_view>>
      visited{};
  auto fn = [&n,
             &visited](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back(decode(v.get_key()), v.get_value());
    return false;
  };
  db.scan_from(2, fn, false /*fwd*/);
  UNODB_EXPECT_EQ(2, n);
  UNODB_EXPECT_EQ(2, visited.size());
  UNODB_EXPECT_EQ(2, visited[0].first);  // make sure visited in reverse order.
  UNODB_EXPECT_EQ(1, visited[1].first);
}

TYPED_TEST(ARTScanTest, scanRangeReverseTwoLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  uint64_t n = 0;
  std::vector<std::pair<std::uint64_t, typename TypeParam::value_view>>
      visited{};
  auto fn = [&n,
             &visited](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    visited.emplace_back(decode(v.get_key()), v.get_value());
    return false;
  };
  db.scan_range(2, 0, fn);
  UNODB_EXPECT_EQ(2, n);
  UNODB_EXPECT_EQ(2, visited.size());
  UNODB_EXPECT_EQ(2, visited[0].first);  // make sure visited in reverse order.
  UNODB_EXPECT_EQ(1, visited[1].first);
}

TYPED_TEST(ARTScanTest, scanReverseThreeLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  verifier.insert(3, unodb::test::test_values[2]);
  uint64_t n = 0;
  uint64_t expected = 3;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(3, n);
}

TYPED_TEST(ARTScanTest, scanReverseFourLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  uint64_t n = 0;
  uint64_t expected = 3;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(4, n);
}

TYPED_TEST(ARTScanTest, scanReverseFiveLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  verifier.insert(3, unodb::test::test_values[2]);
  verifier.insert(4, unodb::test::test_values[3]);
  verifier.insert(5, unodb::test::test_values[4]);
  uint64_t n = 0;
  uint64_t expected = 5;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(5, n);
}

TYPED_TEST(ARTScanTest, scanFromReverseFiveLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  verifier.insert(3, unodb::test::test_values[2]);
  verifier.insert(4, unodb::test::test_values[3]);
  verifier.insert(5, unodb::test::test_values[4]);
  uint64_t n = 0;
  uint64_t expected = 5;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan_from(5, fn, false /*fwd*/);
  UNODB_EXPECT_EQ(5, n);
}

TYPED_TEST(ARTScanTest, scanRangeReverseFiveLeaves) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(1, unodb::test::test_values[0]);
  verifier.insert(2, unodb::test::test_values[1]);
  verifier.insert(3, unodb::test::test_values[2]);
  verifier.insert(4, unodb::test::test_values[3]);
  verifier.insert(5, unodb::test::test_values[4]);
  uint64_t n = 0;
  uint64_t expected = 5;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan_range(5, 0, fn);
  UNODB_EXPECT_EQ(5, n);
}

TYPED_TEST(ARTScanTest, scanReverseFiveLeavesHaltEarly) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;
    return n == 1;  // halt early!
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(1, n);
}

TYPED_TEST(ARTScanTest, scanFromReverseFiveLeavesHaltEarly) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;
    return n == 1;  // halt early!
  };
  db.scan_from(3, fn, false /*fwd*/);
  UNODB_EXPECT_EQ(1, n);
}

TYPED_TEST(ARTScanTest, scanRangeReverseFiveLeavesHaltEarly) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert(0, unodb::test::test_values[0]);
  verifier.insert(1, unodb::test::test_values[1]);
  verifier.insert(2, unodb::test::test_values[2]);
  verifier.insert(3, unodb::test::test_values[3]);
  verifier.insert(4, unodb::test::test_values[4]);
  uint64_t n = 0;
  auto fn = [&n](const unodb::visitor<typename TypeParam::iterator>&) {
    n++;
    return n == 1;  // halt early!
  };
  db.scan_range(4, 1, fn);
  UNODB_EXPECT_EQ(1, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanReverse100) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 100);
  uint64_t n = 0;
  uint64_t expected = 99;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(100, n);
}

TYPED_TEST(ARTScanTest, scanFromReverse100) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 100);
  uint64_t n = 0;
  uint64_t expected = 99;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan_from(100, fn, false /*fwd*/);
  UNODB_EXPECT_EQ(100, n);
}

TYPED_TEST(ARTScanTest, scanRangeReverse100) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 100);
  uint64_t n = 0;
  uint64_t expected = 99;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan_range(99, 0, fn);
  UNODB_EXPECT_EQ(99, n);  // only 99 since to_key is exclusive lower bound
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanReverse1000) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 1000);
  uint64_t n = 0;
  uint64_t expected = 999;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan(fn, false /*fwd*/);
  UNODB_EXPECT_EQ(1000, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanFromReverse1000) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 1000);
  uint64_t n = 0;
  uint64_t expected = 999;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan_from(1000, fn, false /*fwd*/);
  UNODB_EXPECT_EQ(1000, n);
}

// iterator scan test on a larger tree.
TYPED_TEST(ARTScanTest, scanRangeReverse1000) {
  unodb::test::tree_verifier<TypeParam> verifier;
  TypeParam& db = verifier.get_db();  // reference to test db instance.
  verifier.insert_key_range(0, 1000);
  uint64_t n = 0;
  uint64_t expected = 999;
  auto fn = [&n,
             &expected](const unodb::visitor<typename TypeParam::iterator>& v) {
    n++;
    auto key = decode(v.get_key());
    UNODB_EXPECT_EQ(expected, key);
    expected--;
    return false;
  };
  db.scan_range(1000, 0, fn);
  UNODB_EXPECT_EQ(999, n);  // only 999 since to_key is exclusive lower bound
}

// Tests for edge cases for scan_range() including first key missing,
// last key missing, both end keys missing, both end keys are the same
// (and both exist or one exists or both are missing), etc.
//
// Check the edge conditions for the single leaf iterator (limit=1, so
// only ONE (1) is installed into the ART index).  Check all iterator
// flavors for this.
TYPED_TEST(ARTScanTest, scanRangeC100) {
  do_scan_range_test<TypeParam>(0, 1, 1);
}  // nothing
TYPED_TEST(ARTScanTest, scanRangeC102) {
  do_scan_range_test<TypeParam>(1, 2, 1);
}  // one key
TYPED_TEST(ARTScanTest, scanRangeC103) {
  do_scan_range_test<TypeParam>(2, 3, 1);
}  // nothing
TYPED_TEST(ARTScanTest, scanRangeC104) {
  do_scan_range_test<TypeParam>(0, 2, 1);
}  // one key
TYPED_TEST(ARTScanTest, scanRangeC105) {
  do_scan_range_test<TypeParam>(2, 2, 1);
}  // nothing

// fromKey is odd (exists); toKey is even (hence does not exist).
TYPED_TEST(ARTScanTest, scanRangeC110) {
  do_scan_range_test<TypeParam>(1, 2, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC111) {
  do_scan_range_test<TypeParam>(1, 4, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC112) {
  do_scan_range_test<TypeParam>(1, 6, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC113) {
  do_scan_range_test<TypeParam>(2, 1, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC114) {
  do_scan_range_test<TypeParam>(4, 1, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC115) {
  do_scan_range_test<TypeParam>(6, 1, 5);
}
// fromKey is odd (exists); toKey is odd (exists).
TYPED_TEST(ARTScanTest, scanRangeC120) {
  do_scan_range_test<TypeParam>(1, 1, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC121) {
  do_scan_range_test<TypeParam>(1, 3, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC122) {
  do_scan_range_test<TypeParam>(1, 5, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC123) {
  do_scan_range_test<TypeParam>(3, 1, 5);
}
TYPED_TEST(ARTScanTest, scanRangeC124) {
  do_scan_range_test<TypeParam>(5, 1, 5);
}

TYPED_TEST(ARTScanTest, scanRangeC130) {
  do_scan_range_test<TypeParam>(0, 9, 9);
}
TYPED_TEST(ARTScanTest, scanRangeC131) {
  do_scan_range_test<TypeParam>(9, 0, 9);
}

TYPED_TEST(ARTScanTest, scanRangeC140) {
  do_scan_range_test<TypeParam>(1, 999, 999);
}
TYPED_TEST(ARTScanTest, scanRangeC141) {
  do_scan_range_test<TypeParam>(999, 1, 999);
}
TYPED_TEST(ARTScanTest, scanRangeC142) {
  do_scan_range_test<TypeParam>(247, 823, 999);
}
TYPED_TEST(ARTScanTest, scanRangeC143) {
  do_scan_range_test<TypeParam>(823, 247, 999);
}

UNODB_END_TESTS()

}  // namespace
