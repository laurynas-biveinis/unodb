// Copyright 2022 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>  // IWYU pragma: keep
#include <array>
#include <utility>  // IWYU pragma: keep

#include <gtest/gtest.h>  // IWYU pragma: keep
#include <gsl/span>

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
TEST(QSBRPtr, Ctor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  UNODB_ASSERT_EQ(*ptr, x);
}

TEST(QSBRPtr, CopyCtor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{ptr};

  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(*ptr, x);
}

TEST(QSBRPtr, MoveCtor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{std::move(ptr)};

  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
TEST(QSBRPtr, CopyAssignment) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
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
  unodb::qsbr_ptr<char> ptr{&obj};

  UNODB_ASSERT_EQ(*ptr, 'A');
  *ptr = 'B';
  UNODB_ASSERT_EQ(*ptr, 'B');
}

TEST(QSBRPtr, Preincrement) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  ++ptr;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

TEST(QSBRPtr, Subtraction) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  UNODB_ASSERT_EQ(ptr - ptr, 0);  // -V501

  unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_EQ(ptr2 - ptr, 1);

  ++ptr2;
  UNODB_ASSERT_EQ(ptr2 - ptr, 2);
}

TEST(QSBRPtr, Equal) {
  unodb::qsbr_ptr<const char> ptr{&x};
  unodb::qsbr_ptr<const char> ptr2{&x};
  UNODB_ASSERT_TRUE(ptr == ptr2);
}

TEST(QSBRPtr, NotEqual) {
  unodb::qsbr_ptr<const char> ptr{&x};
  UNODB_ASSERT_FALSE(ptr != ptr);  // -V501

  unodb::qsbr_ptr<const char> ptr2{&x};
  UNODB_ASSERT_FALSE(ptr != ptr2);

  unodb::qsbr_ptr<const char> ptr3{&y};
  UNODB_ASSERT_TRUE(ptr != ptr3);
  UNODB_ASSERT_TRUE(ptr != ptr3);
}

TEST(QSBRPtr, LessThanEqual) {
  unodb::qsbr_ptr<const char> ptr{&x};
  unodb::qsbr_ptr<const char> ptr2{&x};

  UNODB_ASSERT_TRUE(ptr <= ptr2);
  UNODB_ASSERT_TRUE(ptr2 <= ptr);

  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr3{&two_chars[0]};
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr4{&two_chars[1]};
  UNODB_ASSERT_TRUE(ptr3 <= ptr4);
  UNODB_ASSERT_FALSE(ptr4 <= ptr3);
}

TEST(QSBRPtr, Get) {
  unodb::qsbr_ptr<const char> ptr{&x};
  UNODB_ASSERT_EQ(ptr.get(), &x);
}

TEST(QSBRPtrSpan, CopyGslSpanCtor) {
  unodb::qsbr_ptr_span span{gsl_span};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span), std::cend(span),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
}

TEST(QSBRPtrSpan, CopyCtor) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{span};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
}

TEST(QSBRPtrSpan, MoveCtor) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{std::move(span)};

  UNODB_ASSERT_TRUE(std::equal(std::cbegin(span2), std::cend(span2),
                               std::cbegin(gsl_span), std::cend(gsl_span)));
  UNODB_ASSERT_EQ(std::cbegin(span).get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
TEST(QSBRPtrSpan, CopyAssignment) {
  unodb::qsbr_ptr_span span{gsl_span};
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
  unodb::qsbr_ptr_span span{gsl_span};
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(std::cbegin(span).get(), &two_chars[0]);
}

TEST(QSBRPtrSpan, Cend) {
  unodb::qsbr_ptr_span span{gsl_span};
  // Do not write &two_chars[2] directly or the libstdc++ debug assertions fire
  UNODB_ASSERT_EQ(std::cend(span).get(), &two_chars[1] + 1);
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

UNODB_END_TESTS()

}  // namespace
