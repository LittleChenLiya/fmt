// Formatting library for C++ - UTF-8 ring buffer iterator tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/ring_buffer.h"

#include <cstring>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

// Test that capacity 5 with "A猫BC" keeps the complete suffix "猫BC"
// and does not produce broken UTF-8.
TEST(ring_buffer_test, capacity_five_keeps_complete_suffix) {
  char buf[5];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "A{}BC", "\xe7\x8c\xab");  // 猫 (U+732B)

  EXPECT_EQ(it.size(), 5u);
  EXPECT_EQ(it.discarded(), 1u);
  // The buffer should contain "猫BC" (5 bytes, complete UTF-8).
  std::string result(buf, it.size());
  EXPECT_EQ(result, "\xe7\x8c\xab" "BC");
}

// Test that an emoji (4 bytes) larger than capacity is discarded
// with a clear count.
TEST(ring_buffer_test, emoji_larger_than_capacity_discarded) {
  char buf[3];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  // Write an emoji (4 bytes) which is larger than capacity 3.
  fmt::format_to(it, "{}", "\xf0\x9f\x98\x80");  // 😀 (U+1F600)

  EXPECT_EQ(it.size(), 0u);
  EXPECT_EQ(it.discarded(), 4u);
}

// Test that the discarded count around the wrap-around position is correct.
TEST(ring_buffer_test, wrap_around_discard_count) {
  char buf[5];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  // Write characters that cause multiple wrap-arounds.
  // "ABCDEFGH" = 8 bytes, capacity 5, so 3 bytes discarded.
  fmt::format_to(it, "ABCDEFGH");

  EXPECT_EQ(it.size(), 5u);
  EXPECT_EQ(it.discarded(), 3u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "DEFGH");
}

// Test that multi-byte characters are not split during wrap-around.
TEST(ring_buffer_test, no_split_multibyte_on_wrap) {
  char buf[6];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  // "A猫B" = 1 + 3 + 1 = 5 bytes, fits in capacity 6.
  // Then "C" = 1 byte, total 6, fits.
  // Then "D" = 1 byte, need to discard oldest.
  fmt::format_to(it, "A{}B{}D", "\xe7\x8c\xab", "\xe7\x8c\xab");

  // "A猫B猫D" = 1 + 3 + 1 + 3 + 1 = 9 bytes, capacity 6.
  // Discard oldest complete characters until it fits.
  // After discarding 'A' (1 byte): "猫B猫D" = 8 bytes, still > 6.
  // After discarding '猫' (3 bytes): "B猫D" = 5 bytes, fits.
  EXPECT_EQ(it.size(), 5u);
  EXPECT_EQ(it.discarded(), 4u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "B\xe7\x8c\xab" "D");
}

// Test that newlines are treated as ordinary content.
TEST(ring_buffer_test, newline_is_ordinary_content) {
  char buf[5];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "A\nB\nC");

  EXPECT_EQ(it.size(), 5u);
  EXPECT_EQ(it.discarded(), 0u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "A\nB\nC");
}

// Test that newlines cause wrap-around and are counted as discarded bytes.
TEST(ring_buffer_test, newline_wrap_around_count) {
  char buf[4];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "A\nB\nC");

  // "A\nB\nC" = 5 bytes, capacity 4, so 1 byte discarded.
  EXPECT_EQ(it.size(), 4u);
  EXPECT_EQ(it.discarded(), 1u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "\nB\nC");
}

// A custom formatter type that counts how many times it is called.
struct counting_type {
  int* call_count;
};

FMT_BEGIN_NAMESPACE
template <> struct formatter<counting_type> {
  auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const counting_type& c, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    ++(*c.call_count);
    return fmt::format_to(ctx.out(), "counted");
  }
};
FMT_END_NAMESPACE

// Test that the formatter is called exactly once per formatting.
TEST(ring_buffer_test, formatter_called_once) {
  char buf[20];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  int call_count = 0;
  counting_type ct{&call_count};

  // The formatter should be called exactly once.
  fmt::format_to(it, "{}", ct);

  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(it.discarded(), 0u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "counted");
}

// Test that the formatter is called exactly once even when content is discarded.
TEST(ring_buffer_test, formatter_called_once_with_discard) {
  char buf[3];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  int call_count = 0;
  counting_type ct{&call_count};

  // "counted" = 7 bytes, capacity 3, so 4 bytes discarded.
  fmt::format_to(it, "{}", ct);

  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(it.discarded(), 4u);
  EXPECT_EQ(it.size(), 3u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "ted");
}

// Test reset functionality.
TEST(ring_buffer_test, reset_clears_state) {
  char buf[5];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "ABCDEFGH");
  EXPECT_EQ(it.discarded(), 3u);

  it.reset();
  EXPECT_EQ(it.discarded(), 0u);
  EXPECT_EQ(it.size(), 0u);

  fmt::format_to(it, "XY");
  EXPECT_EQ(it.discarded(), 0u);
  EXPECT_EQ(it.size(), 2u);
}

// Test empty format string.
TEST(ring_buffer_test, empty_format_string) {
  char buf[5];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "");

  EXPECT_EQ(it.size(), 0u);
  EXPECT_EQ(it.discarded(), 0u);
}

// Test single byte capacity.
TEST(ring_buffer_test, single_byte_capacity) {
  char buf[1];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "AB");

  EXPECT_EQ(it.size(), 1u);
  EXPECT_EQ(it.discarded(), 1u);
  std::string result(buf, it.size());
  EXPECT_EQ(result, "B");
}

// Test that a 2-byte character in a 1-byte buffer is discarded and counted.
TEST(ring_buffer_test, multibyte_in_tiny_buffer_discarded) {
  char buf[1];
  fmt::ring_buffer_iterator it(buf, sizeof(buf));
  fmt::format_to(it, "{}", "\xc3\xa9");  // é (U+00E9)

  EXPECT_EQ(it.size(), 0u);
  EXPECT_EQ(it.discarded(), 2u);
}
