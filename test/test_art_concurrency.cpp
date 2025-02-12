// Copyright 2021-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <string>
// IWYU pragma: no_forward_declare unodb::visitor

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <tuple>
#include <type_traits>

#include <gtest/gtest.h>

#include "art_common.hpp"
#include "assert.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"

#include "db_test_utils.hpp"
#include "gtest_utils.hpp"
#include "qsbr_test_utils.hpp"

namespace {

inline bool odd(const std::uint64_t x) { return static_cast<bool>(x % 2); }

template <class Db>
class ARTConcurrencyTest : public ::testing::Test {
 public:
  UNODB_DETAIL_DISABLE_MSVC_WARNING(26447)
  ~ARTConcurrencyTest() noexcept override {
    if constexpr (std::is_same_v<Db, unodb::olc_db<typename Db::key_type>>) {
      unodb::this_thread().quiescent();
      unodb::test::expect_idle_qsbr();
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

 protected:
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ARTConcurrencyTest() noexcept {
    if constexpr (std::is_same_v<Db, unodb::olc_db<typename Db::key_type>>)
      unodb::test::expect_idle_qsbr();
  }

  // TestFn is void(unodb::test::tree_verifier<Db> *verifier, std::size_t
  // thread_i, std::size_t ops_per_thread)
  template <std::size_t ThreadCount, std::size_t OpsPerThread, typename TestFn>
  void parallel_test(TestFn test_function) {
    if constexpr (std::is_same_v<Db, unodb::olc_db<typename Db::key_type>>)
      unodb::this_thread().qsbr_pause();

    std::array<unodb::test::thread<Db>, ThreadCount> threads;
    for (decltype(ThreadCount) i = 0; i < ThreadCount; ++i) {
      threads[i] =
          unodb::test::thread<Db>{test_function, &verifier, i, OpsPerThread};
    }
    for (auto &t : threads) {
      t.join();
    }

    if constexpr (std::is_same_v<Db, unodb::olc_db<typename Db::key_type>>)
      unodb::this_thread().qsbr_resume();
  }

  template <unsigned PreinsertLimit, std::size_t ThreadCount,
            std::size_t OpsPerThread>
  void key_range_op_test() {
    verifier.insert_key_range(0, PreinsertLimit, true);

    parallel_test<ThreadCount, OpsPerThread>(key_range_op_thread);
  }

  static void parallel_insert_thread(unodb::test::tree_verifier<Db> *verifier,
                                     std::size_t thread_i,
                                     std::size_t ops_per_thread) {
    verifier->insert_preinserted_key_range(thread_i * ops_per_thread,
                                           ops_per_thread);
  }

  static void parallel_remove_thread(unodb::test::tree_verifier<Db> *verifier,
                                     std::size_t thread_i,
                                     std::size_t ops_per_thread) {
    const auto start_key = thread_i * ops_per_thread;
    for (decltype(ops_per_thread) i = 0; i < ops_per_thread; ++i) {
      verifier->remove(start_key + i, true);
    }
  }

  // decode a uint64_t key.
  static std::uint64_t decode(unodb::key_view akey) {
    unodb::key_decoder dec{akey};
    std::uint64_t k;
    dec.decode(k);
    return k;
  }

  // test helper for scan() verification.
  static void do_scan_verification(unodb::test::tree_verifier<Db> *verifier,
                                   std::uint64_t key) {
    const bool fwd = odd(key);  // select scan direction
    const auto k0 = (key > 100) ? (key - 100) : key;
    const auto k1 = key + 100;
    uint64_t n = 0;
    uint64_t sum = 0;
    std::uint64_t prior{};
    auto fn = [&n, &sum, &fwd, &k0, &k1,
               &prior](const unodb::visitor<typename Db::iterator> &v) {
      n++;
      const auto &akey = decode(v.get_key());  // actual visited key.
      sum += akey;
      const auto expected =  // Note: same value formula as insert().
          unodb::test::test_values[akey % unodb::test::test_values.size()];
      const auto actual = v.get_value();
      // LCOV_EXCL_START
      EXPECT_TRUE(std::ranges::equal(actual, expected));
      std::ignore = v.get_value();
      if (fwd) {  // [k0,k1) -- k0 is from_key, k1 is to_key
        EXPECT_TRUE(akey >= k0 && akey < k1)
            << "fwd=" << fwd << ", key=" << akey << ", k0=" << k0
            << ", k1=" << k1;
      } else {  // (k1,k0]
        EXPECT_TRUE(akey > k0 && akey <= k1)
            << "fwd=" << fwd << ", key=" << akey << ", k0=" << k0
            << ", k1=" << k1;
      }
      if (n > 1) {
        EXPECT_TRUE(fwd ? (akey > prior) : (akey < prior))
            << "fwd=" << fwd << ", prior=" << prior << ", key=" << akey
            << ", k0=" << k0 << ", k1=" << k1;
      }
      // LCOV_EXCL_STOP
      prior = akey;
      return false;
    };
    if (fwd) {
      verifier->get_db().scan_range(k0, k1, fn);
    } else {
      verifier->get_db().scan_range(k1, k0, fn);
    }
    if constexpr (std::is_same_v<Db, unodb::olc_db<typename Db::key_type>>) {
      unodb::this_thread().quiescent();
    }
  }

  static void key_range_op_thread(unodb::test::tree_verifier<Db> *verifier,
                                  std::size_t thread_i,
                                  std::size_t ops_per_thread) {
    constexpr auto ntasks = 4;  // Note: 4 to enable scan tests.
    std::uint64_t key = thread_i / ntasks * ntasks;
    for (decltype(ops_per_thread) i = 0; i < ops_per_thread; ++i) {
      switch (thread_i % ntasks) {
        case 0: /* insert (same value formula as insert_key_range!) */
          verifier->try_insert(
              key,
              unodb::test::test_values[key % unodb::test::test_values.size()]);
          break;
        case 1: /* remove */
          verifier->try_remove(key);
          break;
        case 2: /* get */
          verifier->try_get(key);
          break;
        case 3: /* scan */
          do_scan_verification(verifier, key);
          break;
          // LCOV_EXCL_START
        default:
          UNODB_DETAIL_CANNOT_HAPPEN();
          // LCOV_EXCL_STOP
      }
      key++;
    }
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26496)
  static void random_op_thread(unodb::test::tree_verifier<Db> *verifier,
                               std::size_t thread_i,
                               std::size_t ops_per_thread) {
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::geometric_distribution<std::uint64_t> key_generator{0.5};
    constexpr auto ntasks = 4;  // Note: 4 to enable scan tests.
    for (decltype(ops_per_thread) i = 0; i < ops_per_thread; ++i) {
      const auto key{key_generator(gen)};
      switch (thread_i % ntasks) {
        case 0: /* insert (same value formula as insert_key_range!) */
          verifier->try_insert(
              key,
              unodb::test::test_values[key % unodb::test::test_values.size()]);
          break;
        case 1: /* remove */
          verifier->try_remove(key);
          break;
        case 2: /* get */
          verifier->try_get(key);
          break;
        case 3: /* scan */
          do_scan_verification(verifier, key);
          break;
          // LCOV_EXCL_START
        default:
          UNODB_DETAIL_CANNOT_HAPPEN();
          // LCOV_EXCL_STOP
      }
    }
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  unodb::test::tree_verifier<Db> verifier{true};

 public:
  ARTConcurrencyTest(const ARTConcurrencyTest<Db> &) = delete;
  ARTConcurrencyTest(ARTConcurrencyTest<Db> &&) = delete;
  ARTConcurrencyTest<Db> &operator=(const ARTConcurrencyTest<Db> &) = delete;
  ARTConcurrencyTest<Db> &operator=(ARTConcurrencyTest<Db> &&) = delete;
};

using ConcurrentARTTypes =
    ::testing::Types<unodb::test::u64_mutex_db, unodb::test::u64_olc_db>;

UNODB_TYPED_TEST_SUITE(ARTConcurrencyTest, ConcurrentARTTypes)

UNODB_START_TYPED_TESTS()

TYPED_TEST(ARTConcurrencyTest, ParallelInsertOneTree) {
  constexpr auto thread_count = 4;
  constexpr auto total_keys = 1024;
  constexpr auto ops_per_thread = total_keys / thread_count;

  this->verifier.preinsert_key_range_to_verifier_only(0, total_keys);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::parallel_insert_thread);
  this->verifier.check_present_values();
}

TYPED_TEST(ARTConcurrencyTest, ParallelTearDownOneTree) {
  constexpr auto thread_count = 8;
  constexpr auto total_keys = 2048;
  constexpr auto ops_per_thread = total_keys / thread_count;

  this->verifier.insert_key_range(0, total_keys);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::parallel_remove_thread);
  this->verifier.assert_empty();
}

TYPED_TEST(ARTConcurrencyTest, Node4ParallelOps) {
  this->template key_range_op_test<3, 8, 6>();
}

TYPED_TEST(ARTConcurrencyTest, Node16ParallelOps) {
  this->template key_range_op_test<10, 8, 12>();
}

TYPED_TEST(ARTConcurrencyTest, Node48ParallelOps) {
  this->template key_range_op_test<32, 8, 32>();
}

TYPED_TEST(ARTConcurrencyTest, Node256ParallelOps) {
  this->template key_range_op_test<152, 8, 208>();
}

TYPED_TEST(ARTConcurrencyTest, ParallelRandomInsertDeleteGetScan) {
  constexpr auto thread_count = 4;
  constexpr auto initial_keys = 128;
  constexpr auto ops_per_thread = 500;

  this->verifier.insert_key_range(0, initial_keys, true);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::random_op_thread);
}

TYPED_TEST(ARTConcurrencyTest,
           DISABLED_MediumParallelRandomInsertDeleteGetScan) {
  constexpr auto thread_count = 4 * 3;
  constexpr auto initial_keys = 2048;
  constexpr auto ops_per_thread = 10'000;

  this->verifier.insert_key_range(0, initial_keys, true);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::random_op_thread);
}

// A more challenging test using a smaller key range and the same
// number of threads and operations per thread.  The goal of this test
// is to try an increase coverage of the N256 case.
TYPED_TEST(ARTConcurrencyTest, DISABLED_ParallelRandomInsertDeleteGetScan2) {
  constexpr auto thread_count = 4 * 3;
  constexpr auto initial_keys = 152;
  constexpr auto ops_per_thread = 100'000;

  this->verifier.insert_key_range(0, initial_keys, true);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::random_op_thread);
}

// A more challenging test using an even smaller key range and the
// same number of threads and operations per thread.  The goal of this
// test is to try an increase coverage of the N48 case.
TYPED_TEST(ARTConcurrencyTest, DISABLED_ParallelRandomInsertDeleteGetScan3) {
  constexpr auto thread_count = 4 * 3;
  constexpr auto initial_keys = 32;
  constexpr auto ops_per_thread = 100'000;

  this->verifier.insert_key_range(0, initial_keys, true);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::random_op_thread);
}

// Optionally enable this for more confidence in debug builds. Set the
// thread_count for your machine.  Fewer keys, more threads, and more
// operations per thread is more challenging.
//
// LCOV_EXCL_START
TYPED_TEST(ARTConcurrencyTest,
           DISABLED_ParallelRandomInsertDeleteGetScanStressTest) {
  constexpr auto thread_count = 48;
  constexpr auto initial_keys = 152;
  constexpr auto ops_per_thread = 10'000'000;

  this->verifier.insert_key_range(0, initial_keys, true);
  this->template parallel_test<thread_count, ops_per_thread>(
      TestFixture::random_op_thread);
}
// LCOV_EXCL_STOP

UNODB_END_TESTS()

}  // namespace
