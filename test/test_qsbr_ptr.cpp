// Copyright 2022-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <iterator>
// IWYU pragma: no_include <string>
// IWYU pragma: no_include "gtest/gtest.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <gsl/span>
#include <utility>
#include "gtest_utils.hpp"
#include "qsbr_ptr.hpp"

namespace {

constexpr char x = 'X';  // -V707
const char* const raw_ptr_x = &x;

constexpr char y = 'Y';  // -V707
const char* const raw_ptr_y = &y;

const std::array<const char, 2> two_chars = {'A', 'B'};
const gsl::span<const char> gsl_span{two_chars};

const std::array<const char, 3> three_chars = {'C', 'D', 'E'};
const gsl::span<const char> gsl_span2{three_chars};

UNODB_START_TESTS()

UNODB_DETAIL_DISABLE_MSVC_WARNING(6326)

TEST(QSBRPtr, DefaultCtor) {
  const unodb::qsbr_ptr<const char> ptr;
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

TEST(QSBRPtr, Ctor) {
  const unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  UNODB_ASSERT_EQ(*ptr, x);
}

TEST(QSBRPtr, CopyCtor) {
  const unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  const unodb::qsbr_ptr<const char> ptr2{ptr};

  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(*ptr, x);
}

TEST(QSBRPtr, MoveCtor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  const unodb::qsbr_ptr<const char> ptr2{std::move(ptr)};

  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
TEST(QSBRPtr, CopyAssignment) {
  const unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{raw_ptr_y};

  UNODB_ASSERT_EQ(*ptr, x);
  UNODB_ASSERT_EQ(*ptr2, y);
  ptr2 = ptr;
  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(*ptr, x);

  ptr2 = ptr2;  // -V570
  UNODB_ASSERT_EQ(*ptr, x);
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

TEST(QSBRPtr, MoveAssignment) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{raw_ptr_y};

  UNODB_ASSERT_EQ(*ptr, x);
  UNODB_ASSERT_EQ(*ptr2, y);
  ptr2 = std::move(ptr);
  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

TEST(QSBRPtr, ModifyThroughDereference) {
  char obj = 'A';
  const unodb::qsbr_ptr<char> ptr{&obj};

  UNODB_ASSERT_EQ(*ptr, 'A');
  *ptr = 'B';
  UNODB_ASSERT_EQ(*ptr, 'B');
}

TEST(QSBRPtr, ArraySubscript) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  UNODB_ASSERT_EQ(ptr[0], two_chars[0]);
  UNODB_ASSERT_EQ(ptr[1], two_chars[1]);
}

TEST(QSBRPtr, Preincrement) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  ++ptr;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

TEST(QSBRPtr, Postincrement) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  const auto old_ptr = ptr++;

  UNODB_ASSERT_EQ(*old_ptr, two_chars[0]);
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

TEST(QSBRPtr, Predecrement) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);

  --ptr;
  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[0]);
}

TEST(QSBRPtr, Postdecrement) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  const auto old_ptr = ptr--;

  UNODB_ASSERT_EQ(*old_ptr, two_chars[1]);
  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[0]);
}

TEST(QSBRPtr, AdditionAssignment) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  ptr += 1;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);

  ptr += 0;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

TEST(QSBRPtr, Addition) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  auto result = ptr + 1;
  UNODB_ASSERT_EQ(*result, two_chars[1]);
  UNODB_ASSERT_EQ(result.get(), &two_chars[1]);

  result = ptr + 0;
  UNODB_ASSERT_EQ(*result, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(result.get(), &two_chars[0]);
}

TEST(QSBRPtr, FriendAddition) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  auto result = 1 + ptr;
  UNODB_ASSERT_EQ(*result, two_chars[1]);
  UNODB_ASSERT_EQ(result.get(), &two_chars[1]);

  result = 0 + ptr;
  UNODB_ASSERT_EQ(*result, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(result.get(), &two_chars[0]);
}

TEST(QSBRPtr, SubtractionAssignment) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  ptr -= 1;
  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[0]);

  ptr -= 0;
  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[0]);
}

TEST(QSBRPtr, SubtractionOperator) {
  const unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  auto result = ptr - 1;
  UNODB_ASSERT_EQ(*result, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(result.get(), &two_chars[0]);

  result = ptr - 0;
  UNODB_ASSERT_EQ(*result, two_chars[1]);
  UNODB_ASSERT_EQ(result.get(), &two_chars[1]);
}

TEST(QSBRPtr, Subtraction) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  UNODB_ASSERT_EQ(ptr - ptr, 0);  // -V501

  unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_EQ(ptr2 - ptr, 1);

  ++ptr2;
  UNODB_ASSERT_EQ(ptr2 - ptr, 2);
}

TEST(QSBRPtr, Equal) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  const unodb::qsbr_ptr<const char> ptr2{&x};
  UNODB_ASSERT_TRUE(ptr == ptr2);
}

TEST(QSBRPtr, NotEqual) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  UNODB_ASSERT_FALSE(ptr != ptr);  // -V501

  const unodb::qsbr_ptr<const char> ptr2{&x};
  UNODB_ASSERT_FALSE(ptr != ptr2);

  const unodb::qsbr_ptr<const char> ptr3{&y};
  UNODB_ASSERT_TRUE(ptr != ptr3);
  UNODB_ASSERT_TRUE(ptr != ptr3);
}

TEST(QSBRPtr, LessThanEqual) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  const unodb::qsbr_ptr<const char> ptr2{&x};

  UNODB_ASSERT_TRUE(ptr <= ptr2);
  UNODB_ASSERT_TRUE(ptr2 <= ptr);

  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr3{&two_chars[0]};
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr4{&two_chars[1]};
  UNODB_ASSERT_TRUE(ptr3 <= ptr4);
  UNODB_ASSERT_FALSE(ptr4 <= ptr3);
}

TEST(QSBRPtr, Get) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  UNODB_ASSERT_EQ(ptr.get(), &x);
}

TEST(QSBRPtrSpan, CopyGslSpanCtor) {
  const unodb::qsbr_ptr_span span{gsl_span};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span), std::cend(span),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
}

TEST(QSBRPtrSpan, CopyCtor) {
  const unodb::qsbr_ptr_span span{gsl_span};
  const unodb::qsbr_ptr_span span2{span};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
}

TEST(QSBRPtrSpan, MoveCtor) {
  unodb::qsbr_ptr_span span{gsl_span};
  const unodb::qsbr_ptr_span span2{std::move(span)};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
  UNODB_ASSERT_EQ(std::cbegin(span).get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
TEST(QSBRPtrSpan, CopyAssignment) {
  const unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{gsl_span2};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span2), std::cend(gsl_span2)));
  span2 = span;
  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));

  span2 = span2;  // -V570
  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

TEST(QSBRPtrSpan, MoveAssignment) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{gsl_span2};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span2), std::cend(gsl_span2)));
  span2 = std::move(span);
  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
  UNODB_ASSERT_EQ(std::cbegin(span).get(), nullptr);
}

TEST(QSBRPtrSpan, Cbegin) {
  const unodb::qsbr_ptr_span span{gsl_span};
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(std::cbegin(span).get(), &two_chars[0]);
}

TEST(QSBRPtrSpan, Cend) {
  const unodb::qsbr_ptr_span span{gsl_span};
  // Do not write &two_chars[2] directly or the libstdc++ debug assertions fire
  UNODB_ASSERT_EQ(std::cend(span).get(), &two_chars[1] + 1);
}

TEST(QSBRPtrSpan, Equal) {
  using T = std::uint64_t;
  const auto vx{static_cast<T>(0x0102030405060708)};
  const auto vx1{static_cast<T>(0x0102030405060708)};
  const auto vy{static_cast<T>(0x0506070801020304)};
  const unodb::qsbr_ptr<const T> ptrx{&vx};
  const unodb::qsbr_ptr<const T> ptrx1{&vx1};  // different pointer, same data
  const unodb::qsbr_ptr<const T> ptry{&vy};    // different data
  const unodb::qsbr_ptr<const T> ptr_null{nullptr};  // nullptr
  // Baseline expectations for qsbr_ptr vs self.
  //
  EXPECT_EQ(ptrx, ptrx);    // same ptr and len
  EXPECT_EQ(ptrx1, ptrx1);  // same ptr and len
  EXPECT_NE(ptrx, ptrx1);   // different data
  EXPECT_EQ(ptr_null, ptr_null);
  EXPECT_NE(ptrx, ptr_null);
  EXPECT_NE(ptr_null, ptrx);
  //
  // Setup some gsl::span values.
  //
  const gsl::span<const T> spanx{&vx, 1};
  const gsl::span<const T> spanx1{&vx1, 1};  // different ptr, same data
  const gsl::span<const T> span_empty{       // non-null, but zero len.
                                      &vx, static_cast<size_t>(0)};
  const gsl::span<const T> span_empty2{// non-null, but zero len.
                                       &vy, static_cast<size_t>(0)};
  const gsl::span<const T> span_null{
      // nullptr and zero len.
      static_cast<T*>(nullptr), static_cast<size_t>(0)};
  // Note: illegal (causes a fault in the gsl::span() ctor).
  // const gsl::span<const T> span_null1{  // nullptr and zero len.
  //   static_cast<T*>(nullptr),
  //   static_cast<size_t>(1)
  // };
  //
  const std::array<const T, 2> two_values = {vx, vy};
  const gsl::span<const T> span_two_values{two_values};
  //
  // Setup some qsbr_ptr_span values wrapping those gsl::span values.
  //
  const unodb::qsbr_ptr_span qspanx{spanx};
  const unodb::qsbr_ptr_span qspan_empty{span_empty};
  const unodb::qsbr_ptr_span qspan_empty2{span_empty2};
  const unodb::qsbr_ptr_span qspan_null{span_null};
  const unodb::qsbr_ptr_span qspan_null1{span_null};
  const unodb::qsbr_ptr_span qspan_two_values{span_two_values};
  //
  // Now compare qsbr_ptr instances to gsl::span instances.
  //
  EXPECT_EQ(qspanx, spanx);             // same ptr & len
  EXPECT_EQ(qspanx, spanx1);            // same data & len.
  EXPECT_NE(qspanx, span_empty);        // same ptr, different length.
  EXPECT_EQ(qspan_empty, span_empty);   // same ptr, same length (empty)
  EXPECT_EQ(qspan_empty, span_empty2);  // diff ptr, same length (empty)
  EXPECT_EQ(qspan_empty2, span_empty);  // diff ptr, same length (empty).
  EXPECT_EQ(qspan_null, span_empty);    // null and zero-length span.
  EXPECT_EQ(qspan_null, span_null);     // both null and zero-length.
  EXPECT_EQ(qspan_two_values, span_two_values);  // same data & len
  EXPECT_NE(qspan_two_values, spanx);            // different len, same 1st val
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

TEST(QSBRPtr, GreaterThan) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr1{&two_chars[0]};
  const unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_FALSE(ptr1 > ptr1);
  UNODB_ASSERT_TRUE(ptr2 > ptr1);
  UNODB_ASSERT_FALSE(ptr1 > ptr2);
}

TEST(QSBRPtr, GreaterThanEqual) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr1{&two_chars[0]};
  const unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_TRUE(ptr1 >= ptr1);
  UNODB_ASSERT_TRUE(ptr2 >= ptr1);
  UNODB_ASSERT_FALSE(ptr1 >= ptr2);
}

TEST(QSBRPtr, LessThan) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr1{&two_chars[0]};
  const unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_FALSE(ptr1 < ptr1);
  UNODB_ASSERT_TRUE(ptr1 < ptr2);
  UNODB_ASSERT_FALSE(ptr2 < ptr1);
}

UNODB_END_TESTS()

}  // namespace
