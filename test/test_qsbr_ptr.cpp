// Copyright 2022-2025 UnoDB contributors

// Should be the first include
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <string>
// IWYU pragma: no_include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <span>
#include <utility>

#include "gtest_utils.hpp"
#include "qsbr_ptr.hpp"

namespace {

constexpr char x = 'X';  // -V707
constinit const char* const raw_ptr_x = &x;

constexpr char y = 'Y';  // -V707
constinit const char* const raw_ptr_y = &y;

constinit const std::array<const char, 2> two_chars = {'A', 'B'};
constinit const std::span<const char> std_span{two_chars};

constinit const std::array<const char, 3> three_chars = {'C', 'D', 'E'};
constinit const std::span<const char> std_span2{three_chars};

UNODB_TEST(QSBRPtr, DefaultCtor) {
  const unodb::qsbr_ptr<const char> ptr;
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

UNODB_TEST(QSBRPtr, Ctor) {
  const unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  UNODB_ASSERT_EQ(*ptr, x);
}

UNODB_TEST(QSBRPtr, CopyCtor) {
  const unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  const unodb::qsbr_ptr<const char> ptr2{ptr};

  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(*ptr, x);
}

UNODB_TEST(QSBRPtr, MoveCtor) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  const unodb::qsbr_ptr<const char> ptr2{std::move(ptr)};

  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
UNODB_TEST(QSBRPtr, CopyAssignment) {
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

UNODB_TEST(QSBRPtr, MoveAssignment) {
  unodb::qsbr_ptr<const char> ptr{raw_ptr_x};
  unodb::qsbr_ptr<const char> ptr2{raw_ptr_y};

  UNODB_ASSERT_EQ(*ptr, x);
  UNODB_ASSERT_EQ(*ptr2, y);
  ptr2 = std::move(ptr);
  UNODB_ASSERT_EQ(*ptr2, x);
  UNODB_ASSERT_EQ(ptr.get(), nullptr);
}

UNODB_TEST(QSBRPtr, ModifyThroughDereference) {
  char obj = 'A';
  const unodb::qsbr_ptr<char> ptr{&obj};

  UNODB_ASSERT_EQ(*ptr, 'A');
  *ptr = 'B';
  UNODB_ASSERT_EQ(*ptr, 'B');
}

UNODB_TEST(QSBRPtr, ArraySubscript) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  UNODB_ASSERT_EQ(ptr[0], two_chars[0]);
  UNODB_ASSERT_EQ(ptr[1], two_chars[1]);
}

UNODB_TEST(QSBRPtr, Preincrement) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  ++ptr;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

UNODB_TEST(QSBRPtr, Postincrement) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  const auto old_ptr = ptr++;

  UNODB_ASSERT_EQ(*old_ptr, two_chars[0]);
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

UNODB_TEST(QSBRPtr, Predecrement) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);

  --ptr;
  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[0]);
}

UNODB_TEST(QSBRPtr, Postdecrement) {
  unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  const auto old_ptr = ptr--;

  UNODB_ASSERT_EQ(*old_ptr, two_chars[1]);
  UNODB_ASSERT_EQ(*ptr, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[0]);
}

UNODB_TEST(QSBRPtr, AdditionAssignment) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  unodb::qsbr_ptr<const char> ptr{&two_chars[0]};
  ptr += 1;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);

  ptr += 0;
  UNODB_ASSERT_EQ(*ptr, two_chars[1]);
  UNODB_ASSERT_EQ(ptr.get(), &two_chars[1]);
}

UNODB_TEST(QSBRPtr, Addition) {
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

UNODB_TEST(QSBRPtr, FriendAddition) {
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

UNODB_TEST(QSBRPtr, SubtractionAssignment) {
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

UNODB_TEST(QSBRPtr, SubtractionOperator) {
  const unodb::qsbr_ptr<const char> ptr{&two_chars[1]};
  auto result = ptr - 1;
  UNODB_ASSERT_EQ(*result, two_chars[0]);
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(result.get(), &two_chars[0]);

  result = ptr - 0;
  UNODB_ASSERT_EQ(*result, two_chars[1]);
  UNODB_ASSERT_EQ(result.get(), &two_chars[1]);
}

UNODB_TEST(QSBRPtr, Subtraction) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr{&two_chars[0]};

  UNODB_ASSERT_EQ(ptr - ptr, 0);  // -V501

  unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_EQ(ptr2 - ptr, 1);

  ++ptr2;
  UNODB_ASSERT_EQ(ptr2 - ptr, 2);
}

UNODB_TEST(QSBRPtr, Equal) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  const unodb::qsbr_ptr<const char> ptr2{&x};
  UNODB_ASSERT_TRUE(ptr == ptr2);
}

UNODB_TEST(QSBRPtr, NotEqual) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  UNODB_ASSERT_FALSE(ptr != ptr);  // -V501

  const unodb::qsbr_ptr<const char> ptr2{&x};
  UNODB_ASSERT_FALSE(ptr != ptr2);

  const unodb::qsbr_ptr<const char> ptr3{&y};
  UNODB_ASSERT_TRUE(ptr != ptr3);
  UNODB_ASSERT_TRUE(ptr != ptr3);
}

UNODB_TEST(QSBRPtr, LessThanEqual) {
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

UNODB_TEST(QSBRPtr, Get) {
  const unodb::qsbr_ptr<const char> ptr{&x};
  UNODB_ASSERT_EQ(ptr.get(), &x);
}

UNODB_TEST(QSBRPtrSpan, DefaultCtor) {
  const unodb::qsbr_ptr_span<const char> span{};
  UNODB_ASSERT_EQ(std::cbegin(span).get(), nullptr);
  UNODB_ASSERT_EQ(span.size(), 0);
}

UNODB_TEST(QSBRPtrSpan, CopyStdSpanCtor) {
  const unodb::qsbr_ptr_span span{std_span};

  UNODB_ASSERT_TRUE(std::ranges::equal(span, std_span));
}

UNODB_TEST(QSBRPtrSpan, CopyCtor) {
  const unodb::qsbr_ptr_span span{std_span};
  const unodb::qsbr_ptr_span span2{span};

  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span));
}

UNODB_TEST(QSBRPtrSpan, MoveCtor) {
  unodb::qsbr_ptr_span span{std_span};
  const unodb::qsbr_ptr_span span2{std::move(span)};

  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span));
}

UNODB_DETAIL_DISABLE_CLANG_WARNING("-Wself-assign-overloaded")
UNODB_TEST(QSBRPtrSpan, CopyAssignment) {
  const unodb::qsbr_ptr_span span{std_span};
  unodb::qsbr_ptr_span span2{std_span2};

  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span2));
  span2 = span;
  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span));

  span2 = span2;  // -V570
  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span));
}
UNODB_DETAIL_RESTORE_CLANG_WARNINGS()

UNODB_TEST(QSBRPtrSpan, MoveAssignment) {
  unodb::qsbr_ptr_span span{std_span};
  unodb::qsbr_ptr_span span2{std_span2};

  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span2));
  span2 = std::move(span);
  UNODB_ASSERT_TRUE(std::ranges::equal(span2, std_span));
}

UNODB_TEST(QSBRPtrSpan, Cbegin) {
  const unodb::qsbr_ptr_span span{std_span};
  // NOLINTNEXTLINE(readability-container-data-pointer)
  UNODB_ASSERT_EQ(std::cbegin(span).get(), &two_chars[0]);
}

UNODB_TEST(QSBRPtrSpan, Cend) {
  const unodb::qsbr_ptr_span span{std_span};
  // Do not write &two_chars[2] directly or the libstdc++ debug assertions fire
  UNODB_ASSERT_EQ(std::cend(span).get(), &two_chars[1] + 1);
}

UNODB_TEST(QSBRPtrSpan, Size) {
  const unodb::qsbr_ptr_span span{std_span};
  UNODB_ASSERT_EQ(span.size(), std_span.size());
  const unodb::qsbr_ptr_span span2{std_span2};
  UNODB_ASSERT_EQ(span2.size(), std_span2.size());
}

UNODB_TEST(QSBRPtr, GreaterThan) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr1{&two_chars[0]};
  const unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_FALSE(ptr1 > ptr1);
  UNODB_ASSERT_TRUE(ptr2 > ptr1);
  UNODB_ASSERT_FALSE(ptr1 > ptr2);
}

UNODB_TEST(QSBRPtr, GreaterThanEqual) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr1{&two_chars[0]};
  const unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_TRUE(ptr1 >= ptr1);
  UNODB_ASSERT_TRUE(ptr2 >= ptr1);
  UNODB_ASSERT_FALSE(ptr1 >= ptr2);
}

UNODB_TEST(QSBRPtr, LessThan) {
  // NOLINTNEXTLINE(readability-container-data-pointer)
  const unodb::qsbr_ptr<const char> ptr1{&two_chars[0]};
  const unodb::qsbr_ptr<const char> ptr2{&two_chars[1]};

  UNODB_ASSERT_FALSE(ptr1 < ptr1);
  UNODB_ASSERT_TRUE(ptr1 < ptr2);
  UNODB_ASSERT_FALSE(ptr2 < ptr1);
}

}  // namespace
