// Formatting library for C++ - field-boundary formatting tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/boundary.h"

#include <iterator>
#include <string>

#include "gtest/gtest.h"

namespace {

// Returns true if `s` is well-formed UTF-8.
auto is_valid_utf8(const std::string& s) -> bool {
  for (size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    if (c < 0x80)
      len = 1;
    else if (c >= 0xC2 && c < 0xE0)
      len = 2;
    else if (c >= 0xE0 && c < 0xF0)
      len = 3;
    else if (c >= 0xF0 && c < 0xF5)
      len = 4;
    else
      return false;
    if (i + len > s.size()) return false;
    for (size_t j = 1; j < len; ++j) {
      auto cont = static_cast<unsigned char>(s[i + j]);
      if (cont < 0x80 || cont >= 0xC0) return false;
    }
    i += len;
  }
  return true;
}

struct counted {
  int value;
};

int format_call_count = 0;

}  // namespace

FMT_BEGIN_NAMESPACE
template <> struct formatter<counted> : formatter<int> {
  auto format(counted c, format_context& ctx) const -> decltype(ctx.out()) {
    ++format_call_count;
    return formatter<int>::format(c.value, ctx);
  }
};
FMT_END_NAMESPACE

// "用户：" is 9 bytes, "张伟" is 6 bytes, "，分数：" is 12 bytes and "98" is
// 2 bytes, so the full output "用户：张伟，分数：98" is 29 bytes.

TEST(boundary_test, exact_budget) {
  auto res = fmt::format_boundary(29, "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(res.str, "用户：张伟，分数：98");
  EXPECT_EQ(res.fields_completed, 2u);
  EXPECT_FALSE(res.truncated);
  EXPECT_TRUE(is_valid_utf8(res.str));
}

TEST(boundary_test, one_byte_short) {
  // The last field "98" (2 bytes) does not fit in the remaining 1 byte, so
  // it is not written at all and formatting stops at field 1.
  auto res = fmt::format_boundary(28, "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(res.str, "用户：张伟，分数：");
  EXPECT_EQ(res.fields_completed, 1u);
  EXPECT_TRUE(res.truncated);
  EXPECT_TRUE(res.stopped_at_field);
  EXPECT_EQ(res.stop_field, 1u);
  EXPECT_TRUE(is_valid_utf8(res.str));
}

TEST(boundary_test, first_field_does_not_fit) {
  // The literal "用户：" (9 bytes) fits but "张伟" (6 bytes) does not fit in
  // the remaining 1 byte, so nothing of the first field is written.
  auto res = fmt::format_boundary(10, "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(res.str, "用户：");
  EXPECT_EQ(res.fields_completed, 0u);
  EXPECT_TRUE(res.truncated);
  EXPECT_TRUE(res.stopped_at_field);
  EXPECT_EQ(res.stop_field, 0u);
  EXPECT_TRUE(is_valid_utf8(res.str));
}

TEST(boundary_test, text_cut_at_code_point_boundary) {
  // Only "用" (3 bytes) fits in 5 bytes; "户" would need 3 more.
  auto res = fmt::format_boundary(5, "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(res.str, "用");
  EXPECT_EQ(res.fields_completed, 0u);
  EXPECT_TRUE(res.truncated);
  EXPECT_FALSE(res.stopped_at_field);
  EXPECT_TRUE(is_valid_utf8(res.str));
}

TEST(boundary_test, unexecuted_fields_are_not_formatted) {
  format_call_count = 0;
  // Field 0 fits, field 1 is attempted but does not fit, field 2 must not
  // be formatted at all.
  auto res =
      fmt::format_boundary(1, "{}{}{}", counted{1}, counted{2}, counted{3});
  EXPECT_EQ(res.str, "1");
  EXPECT_EQ(res.fields_completed, 1u);
  EXPECT_TRUE(res.truncated);
  EXPECT_TRUE(res.stopped_at_field);
  EXPECT_EQ(res.stop_field, 1u);
  EXPECT_EQ(format_call_count, 2);
}

TEST(boundary_test, huge_dynamic_width) {
  // The field expands to 1000000 bytes which exceeds the budget, so it is
  // not written.
  auto res = fmt::format_boundary(16, "{:{}}", "x", 1000000);
  EXPECT_EQ(res.str, "");
  EXPECT_EQ(res.fields_completed, 0u);
  EXPECT_TRUE(res.truncated);
  EXPECT_TRUE(res.stopped_at_field);
  EXPECT_EQ(res.stop_field, 0u);
}

TEST(boundary_test, format_error) {
  EXPECT_THROW((void)fmt::format_boundary(100, fmt::runtime("用户：{"), "张伟"),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format_boundary(100, fmt::runtime("}")),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format_boundary(100, fmt::runtime("{:q}"), 42),
               fmt::format_error);
}

TEST(boundary_test, empty_budget) {
  auto res = fmt::format_boundary(0, "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(res.str, "");
  EXPECT_EQ(res.fields_completed, 0u);
  EXPECT_TRUE(res.truncated);
  EXPECT_FALSE(res.stopped_at_field);
  // An empty format string consumes no budget and is not truncated.
  auto empty = fmt::format_boundary(0, "");
  EXPECT_EQ(empty.str, "");
  EXPECT_FALSE(empty.truncated);
}

TEST(boundary_test, emoji_not_split) {
  // An emoji argument (4 bytes) is all-or-nothing.
  auto res = fmt::format_boundary(3, "a{}b", "\xF0\x9F\x98\x80");
  EXPECT_EQ(res.str, "a");
  EXPECT_EQ(res.fields_completed, 0u);
  EXPECT_TRUE(res.stopped_at_field);
  EXPECT_TRUE(is_valid_utf8(res.str));
  // A literal emoji is never split either.
  auto lit = fmt::format_boundary(3, "x\xF0\x9F\x98\x80y{}", 1);
  EXPECT_EQ(lit.str, "x");
  EXPECT_EQ(lit.fields_completed, 0u);
  EXPECT_FALSE(lit.stopped_at_field);
  EXPECT_TRUE(is_valid_utf8(lit.str));
}

TEST(boundary_test, escaped_braces_and_specs) {
  auto res = fmt::format_boundary(100, "{{}}{:>5}", 7);
  EXPECT_EQ(res.str, "{}    7");
  EXPECT_EQ(res.fields_completed, 1u);
  EXPECT_FALSE(res.truncated);
}

TEST(boundary_test, format_to_boundary_iterator) {
  auto s = std::string();
  auto res = fmt::format_to_boundary(std::back_inserter(s), 29,
                                     "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(s, "用户：张伟，分数：98");
  EXPECT_EQ(res.size, 29u);
  EXPECT_EQ(res.fields_completed, 2u);
  EXPECT_FALSE(res.truncated);
}

TEST(boundary_test, format_to_boundary_truncated) {
  auto s = std::string();
  auto res = fmt::format_to_boundary(std::back_inserter(s), 12,
                                     "用户：{}，分数：{}", "张伟", 98);
  EXPECT_EQ(s, "用户：");
  EXPECT_EQ(res.size, 9u);
  EXPECT_EQ(res.fields_completed, 0u);
  EXPECT_TRUE(res.truncated);
  EXPECT_TRUE(res.stopped_at_field);
  EXPECT_EQ(res.stop_field, 0u);
  EXPECT_TRUE(is_valid_utf8(s));
}
