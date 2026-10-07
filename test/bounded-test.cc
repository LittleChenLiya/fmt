// Formatting library for C++ - field-boundary formatting tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/bounded.h"

#include <string>

#include "gtest/gtest.h"

namespace {

// Returns true if [data, data + size) is a valid UTF-8 byte sequence.
bool is_valid_utf8(const char* data, size_t size) {
  const auto* p = reinterpret_cast<const unsigned char*>(data);
  size_t i = 0;
  while (i < size) {
    unsigned char c = p[i];
    size_t len = 0;
    if (c < 0x80) {
      len = 1;
    } else if ((c >> 5) == 0x6 && c >= 0xC2) {
      len = 2;
    } else if ((c >> 4) == 0xE) {
      len = 3;
    } else if ((c >> 3) == 0x4 && c <= 0xF4) {
      len = 4;
    } else {
      return false;
    }
    if (i + len > size) return false;
    for (size_t j = 1; j < len; ++j)
      if ((p[i + j] >> 6) != 0x2) return false;
    i += len;
  }
  return true;
}

std::string str(const char* buf, const fmt::bounded_format_result& res) {
  return std::string(buf, static_cast<size_t>(res.out - buf));
}

}  // namespace

// "用户：张伟，分数：95" is 29 bytes in UTF-8:
//   "用户：" 9, "张伟" 6, "，分数：" 12, "95" 2.
TEST(bounded_test, exact_budget) {
  char buf[64];
  auto res = fmt::format_to_bounded(buf, 29, "用户：{}，分数：{}", "张伟", 95);
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 2u);
  EXPECT_EQ(res.out - buf, 29);
  EXPECT_EQ(str(buf, res), "用户：张伟，分数：95");
}

TEST(bounded_test, one_byte_short) {
  char buf[64];
  // 28 bytes: the last field ("95", 2 bytes) no longer fits in the
  // remaining byte, so formatting stops right before it.
  auto res = fmt::format_to_bounded(buf, 28, "用户：{}，分数：{}", "张伟", 95);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(res.out - buf, 27);
  EXPECT_EQ(str(buf, res), "用户：张伟，分数：");
}

TEST(bounded_test, first_field_does_not_fit) {
  char buf[64];
  // The literal prefix "用户：" (9 bytes) fits but the name "张伟" (6 bytes)
  // does not fit in the remaining byte.
  auto res = fmt::format_to_bounded(buf, 10, "用户：{}，分数：{}", "张伟", 95);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(res.out - buf, 9);
  EXPECT_EQ(str(buf, res), "用户：");

  // A budget of exactly the prefix size behaves the same.
  res = fmt::format_to_bounded(buf, 9, "用户：{}，分数：{}", "张伟", 95);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(str(buf, res), "用户：");

  // Even the literal prefix does not fit: nothing is written at all.
  res = fmt::format_to_bounded(buf, 8, "用户：{}，分数：{}", "张伟", 95);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(res.out, buf);
}

TEST(bounded_test, utf8_prefix_valid_for_every_budget) {
  char buf[64];
  for (size_t budget = 0; budget <= 29; ++budget) {
    auto res =
        fmt::format_to_bounded(buf, budget, "用户：{}，分数：{}", "张伟", 95);
    EXPECT_LE(static_cast<size_t>(res.out - buf), budget);
    EXPECT_TRUE(is_valid_utf8(buf, static_cast<size_t>(res.out - buf)))
        << "budget = " << budget;
  }
}

TEST(bounded_test, emoji_never_split) {
  char buf[64];
  // An emoji (4 bytes) produced by a field is written whole or not at all.
  auto res = fmt::format_to_bounded(buf, 3, "{}", "🙂");
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(res.out, buf);
  res = fmt::format_to_bounded(buf, 4, "{}", "🙂");
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(str(buf, res), "🙂");

  // A literal text segment containing an emoji is atomic too.
  res = fmt::format_to_bounded(buf, 5, "ab🙂cd{}", 0);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(res.out, buf);
  res = fmt::format_to_bounded(buf, 8, "ab🙂cd{}", 0);
  EXPECT_TRUE(res.truncated);  // The field "0" does not fit anymore.
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(str(buf, res), "ab🙂cd");
}

namespace {
struct counted {
  int* counter;
  const char* text;
};
}  // namespace

template <> struct fmt::formatter<counted> : fmt::formatter<fmt::string_view> {
  auto format(counted c, fmt::format_context& ctx) const
      -> fmt::format_context::iterator {
    ++*c.counter;
    return fmt::formatter<fmt::string_view>::format(fmt::string_view(c.text),
                                                    ctx);
  }
};

TEST(bounded_test, unexecuted_fields_are_not_formatted) {
  char buf[64];
  int c0 = 0, c1 = 0, c2 = 0, c3 = 0;
  auto res = fmt::format_to_bounded(
      buf, 8, "{}{}{}{}", counted{&c0, "aaaa"}, counted{&c1, "bbbb"},
      counted{&c2, "cccc"}, counted{&c3, "dddd"});
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 2u);
  EXPECT_EQ(str(buf, res), "aaaabbbb");
  EXPECT_EQ(c0, 1);
  EXPECT_EQ(c1, 1);
  EXPECT_EQ(c2, 1);  // The field that does not fit: attempted, discarded.
  EXPECT_EQ(c3, 0);  // Past the cut-off point: never formatted.

  // Same with the cut-off one field earlier.
  c0 = c1 = c2 = c3 = 0;
  res = fmt::format_to_bounded(buf, 4, "{}{}{}{}", counted{&c0, "aaaa"},
                               counted{&c1, "bbbb"}, counted{&c2, "cccc"},
                               counted{&c3, "dddd"});
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(str(buf, res), "aaaa");
  EXPECT_EQ(c0, 1);
  EXPECT_EQ(c1, 1);  // Attempted, output discarded.
  EXPECT_EQ(c2, 0);  // Never formatted.
  EXPECT_EQ(c3, 0);  // Never formatted.
}

TEST(bounded_test, huge_dynamic_width) {
  char buf[32];
  // A dynamic width vastly exceeding the budget must not cause a huge
  // allocation; the field simply does not fit.
  auto res = fmt::format_to_bounded(buf, 16, "[{:{}}]", "x", 1000000);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(str(buf, res), "[");

  // A dynamic width that fits works as usual.
  res = fmt::format_to_bounded(buf, 16, "[{:{}}]", "x", 3);
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(str(buf, res), "[x  ]");
}

TEST(bounded_test, explicit_and_named_args) {
  char buf[64];
  // Manual indexing: fields are resolved against the full argument list.
  auto res = fmt::format_to_bounded(buf, 3, "{1} {0}", "ab", "cd");
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(str(buf, res), "cd ");

  // Named arguments.
  res = fmt::format_to_bounded(buf, 64, "{name}={value}",
                               fmt::arg("name", "张伟"), fmt::arg("value", 95));
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 2u);
  EXPECT_EQ(str(buf, res), "张伟=95");
}

TEST(bounded_test, escaped_braces) {
  char buf[64];
  auto res = fmt::format_to_bounded(buf, 64, "{{{}}}", "x");
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(str(buf, res), "{x}");
}

TEST(bounded_test, format_error) {
  char buf[64];
  auto no_args = fmt::make_format_args();
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "{", no_args),
               fmt::format_error);
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "}", no_args),
               fmt::format_error);
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "abc{", no_args),
               fmt::format_error);
  // Mixing manual and automatic argument indexing is an error.
  int one = 1;
  auto one_arg = fmt::make_format_args(one);
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "{0} {}", one_arg),
               fmt::format_error);
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "{} {0}", one_arg),
               fmt::format_error);
  // An invalid format spec for the argument type is an error.
  const char* x = "x";
  auto str_arg = fmt::make_format_args(x);
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "{:d}", str_arg),
               fmt::format_error);
  // An out-of-range argument id is an error.
  EXPECT_THROW((void)fmt::vformat_to_bounded(buf, 64, "{1}", one_arg),
               fmt::format_error);
}

TEST(bounded_test, empty_budget) {
  char buf[8] = {};
  auto res = fmt::format_to_bounded(buf, 0, "用户：{}，分数：{}", "张伟", 95);
  EXPECT_TRUE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(res.out, buf);

  // An empty format string needs no budget.
  res = fmt::format_to_bounded(buf, 0, "");
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 0u);
  EXPECT_EQ(res.out, buf);

  // A field formatting to an empty string fits in a zero budget.
  res = fmt::format_to_bounded(buf, 0, "{}", "");
  EXPECT_FALSE(res.truncated);
  EXPECT_EQ(res.fields_written, 1u);
  EXPECT_EQ(res.out, buf);
}
