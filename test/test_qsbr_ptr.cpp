// Copyright 2021 Laurynas Biveinis

#include "global.hpp"

#include <algorithm>  // IWYU pragma: keep
#include <array>
#include <utility>  // IWYU pragma: keep

#include <gtest/gtest.h>  // IWYU pragma: keep
#include <gsl/span>

#include "qsbr_ptr.hpp"

namespace {

const char x = 'X';  // -V707
const char* const raw_ptr_x = &x;

const char y = 'Y';  // -V707
const char* const raw_ptr_y = &y;

const std::array<const char, 2> two_chars = {'A', 'B'};
const gsl::span<const char> gsl_span{two_chars.cbegin(), two_chars.cend()};

const std::array<const char, 3> three_chars = {'C', 'D', 'E'};
const gsl::span<const char> gsl_span2{three_chars.cbegin(), three_chars.cend()};

TEST(QSBRPtr, Ctor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  ASSERT_EQ(*ptr, x);
}

TEST(QSBRPtr, CopyCtor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{ptr};

  ASSERT_EQ(*ptr2, x);
  ASSERT_EQ(*ptr, x);
}

TEST(QSBRPtr, MoveCtor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{std::move(ptr)};

  ASSERT_EQ(*ptr2, x);
  ASSERT_EQ(ptr.get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
TEST(QSBRPtr, CopyAssignment) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{raw_ptr_y};

  ASSERT_EQ(*ptr, x);
  ASSERT_EQ(*ptr2, y);
  ptr2 = ptr;
  ASSERT_EQ(*ptr2, x);
  ASSERT_EQ(*ptr, x);

  ptr2 = ptr2;  // -V570
  ASSERT_EQ(*ptr, x);
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

TEST(QSBRPtr, MoveAssignment) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{raw_ptr_y};

  ASSERT_EQ(*ptr, x);
  ASSERT_EQ(*ptr2, y);
  ptr2 = std::move(ptr);
  ASSERT_EQ(*ptr2, x);
  ASSERT_EQ(ptr.get(), nullptr);
}

TEST(QSBRPtr, ModifyThroughDereference) {
  char obj = 'A';
  unodb::qsbr_ptr<char> ptr{&obj};

  ASSERT_EQ(*ptr, 'A');
  *ptr = 'B';
  ASSERT_EQ(*ptr, 'B');
}

TEST(QSBRPtr, Preincrement) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  ASSERT_EQ(*ptr, two_chars[0]);
  ++ptr;
  ASSERT_EQ(*ptr, two_chars[1]);
  ASSERT_EQ(ptr.get(), &two_chars[1]);
}

TEST(QSBRPtr, Subtraction) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  ASSERT_EQ(ptr - ptr, 0);  // -V501

  unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  ASSERT_EQ(ptr2 - ptr, 1);

  ++ptr2;
  ASSERT_EQ(ptr2 - ptr, 2);
}

TEST(QSBRPtr, Equal) {
  unodb::qsbr_ptr<const char> ptr{&x};
  unodb::qsbr_ptr<const char> ptr2{&x};
  ASSERT_TRUE(ptr == ptr2);
}

TEST(QSBRPtr, NotEqual) {
  unodb::qsbr_ptr<const char> ptr{&x};
  ASSERT_FALSE(ptr != ptr);  // -V501

  unodb::qsbr_ptr<const char> ptr2{&x};
  ASSERT_FALSE(ptr != ptr2);

  unodb::qsbr_ptr<const char> ptr3{&y};
  ASSERT_TRUE(ptr != ptr3);
  ASSERT_TRUE(ptr != ptr3);
}

TEST(QSBRPtr, LessThanEqual) {
  unodb::qsbr_ptr<const char> ptr{&x};
  unodb::qsbr_ptr<const char> ptr2{&x};

  ASSERT_TRUE(ptr <= ptr2);
  ASSERT_TRUE(ptr2 <= ptr);

  unodb::qsbr_ptr<const char> ptr3{&two_chars[0]};
  unodb::qsbr_ptr<const char> ptr4{&two_chars[1]};
  ASSERT_TRUE(ptr3 <= ptr4);
  ASSERT_FALSE(ptr4 <= ptr3);
}

TEST(QSBRPtr, Get) {
  unodb::qsbr_ptr<const char> ptr{&x};
  ASSERT_EQ(ptr.get(), &x);
}

TEST(QSBRPtrSpan, CopyGslSpanCtor) {
  unodb::qsbr_ptr_span span{gsl_span};

  ASSERT_TRUE(std::equal(span.cbegin(), span.cend(), gsl_span.cbegin(),
                         gsl_span.cend()));
}

TEST(QSBRPtrSpan, CopyCtor) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{span};

  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span.cbegin(),
                         gsl_span.cend()));
}

TEST(QSBRPtrSpan, MoveCtor) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{std::move(span)};

  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span.cbegin(),
                         gsl_span.cend()));
  ASSERT_EQ(span.cbegin().get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
TEST(QSBRPtrSpan, CopyAssignment) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{gsl_span2};

  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span2.cbegin(),
                         gsl_span2.cend()));
  span2 = span;
  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span.cbegin(),
                         gsl_span.cend()));

  span2 = span2;  // -V570
  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span.cbegin(),
                         gsl_span.cend()));
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

TEST(QSBRPtrSpan, MoveAssignment) {
  unodb::qsbr_ptr_span span{gsl_span};
  unodb::qsbr_ptr_span span2{gsl_span2};

  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span2.cbegin(),
                         gsl_span2.cend()));
  span2 = std::move(span);
  ASSERT_TRUE(std::equal(span2.cbegin(), span2.cend(), gsl_span.cbegin(),
                         gsl_span.cend()));
  ASSERT_EQ(span.cbegin().get(), nullptr);
}

TEST(QSBRPtrSpan, Cbegin) {
  unodb::qsbr_ptr_span span{gsl_span};
  ASSERT_EQ(span.cbegin().get(), &two_chars[0]);
}

TEST(QSBRPtrSpan, Cend) {
  unodb::qsbr_ptr_span span{gsl_span};
  // Do not write &two_chars[2] directly or the libstdc++ debug assertions fire
  ASSERT_EQ(span.cend().get(), &two_chars[1] + 1);
}

}  // namespace
