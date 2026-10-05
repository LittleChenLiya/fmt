// Formatting library for C++ - digit profile tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

// clang-format off
#include "fmt/digit-profile.h"
// clang-format on

#include <atomic>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

using fmt::digit_profile;

namespace {
// Arabic-Indic symbols (UTF-8): digits U+0660..U+0669, decimal point
// U+066B, thousands separator U+066C.
const char* const kArDecimal = "\xD9\xAB";
const char* const kArGroup = "\xD9\xAC";

std::string ar_grouped_1234_5() {
  // 1,234.5 in Arabic-Indic digits with Arabic separators.
  return std::string("\xD9\xA1") + kArGroup + "\xD9\xA2\xD9\xA3\xD9\xA4" +
         kArDecimal + "\xD9\xA5";
}
}  // namespace

TEST(digit_profile_test, ascii_profile_is_default) {
  auto p = digit_profile::ascii();
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, 1234567)), "1,234,567");
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, 1234.5)), "1,234.5");
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, -42)), "-42");
  EXPECT_EQ(fmt::format("{:d}", fmt::profiled(p, 1000000)), "1,000,000");
}

TEST(digit_profile_test, arabic_indic_grouping) {
  auto p = digit_profile::arabic_indic();
  // 1234.5 gets the Arabic thousands separator and decimal point.
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, 1234.5)), ar_grouped_1234_5());
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, 1234567)),
            std::string("\xD9\xA1") + kArGroup + "\xD9\xA2\xD9\xA3\xD9\xA4" +
                kArGroup + "\xD9\xA5\xD9\xA6\xD9\xA7");
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, -42)),
            std::string("-") + "\xD9\xA4\xD9\xA2");
  // No grouping for fewer than group_size + 1 digits.
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, 123)),
            std::string("\xD9\xA1\xD9\xA2\xD9\xA3"));
}

TEST(digit_profile_test, width_counts_display_columns_not_bytes) {
  auto p = digit_profile::arabic_indic();
  auto grouped = ar_grouped_1234_5();
  // The grouped value is 7 display columns but 14 UTF-8 bytes.
  EXPECT_EQ(grouped.size(), 14u);
  auto s = fmt::format("{:10}", fmt::profiled(p, 1234.5));
  // Padded to 10 display columns with 3 spaces, not to 10 bytes.
  EXPECT_EQ(s, "   " + grouped);
  EXPECT_EQ(s.size(), 17u);
  // Left and center alignment also count columns.
  EXPECT_EQ(fmt::format("{:<10}", fmt::profiled(p, 1234.5)), grouped + "   ");
  EXPECT_EQ(fmt::format("{:^11}", fmt::profiled(p, 1234.5)),
            "  " + grouped + "  ");
  EXPECT_EQ(fmt::format("{:*>10}", fmt::profiled(p, 1234.5)),
            "***" + grouped);
}

TEST(digit_profile_test, rounding_matches_plain_fmt) {
  auto p = digit_profile::ascii();
  p.group_size = 0;  // Disable grouping so output is directly comparable.
  const double values[] = {0.125, 2.675, 1.005, 9999.999, 0.5,
                           1.5,   2.5,   123456789.987654};
  const char* const specs[] = {"{}",     "{:.0f}", "{:.1f}", "{:.2f}",
                               "{:.3f}", "{:.4e}", "{:.3g}", "{:.10g}"};
  for (double v : values)
    for (const char* spec : specs)
      EXPECT_EQ(fmt::format(fmt::runtime(spec), fmt::profiled(p, v)),
                  fmt::format(fmt::runtime(spec), v))
          << "spec=" << spec << " value=" << v;
}

TEST(digit_profile_test, signs) {
  auto p = digit_profile::ascii();
  p.group_size = 0;
  EXPECT_EQ(fmt::format("{:+}", fmt::profiled(p, 42)), "+42");
  EXPECT_EQ(fmt::format("{: }", fmt::profiled(p, 42)), " 42");
  EXPECT_EQ(fmt::format("{:+}", fmt::profiled(p, -42)), "-42");
  // Custom sign symbols.
  p.plus_sign = "\xEF\xBC\x8B";   // U+FF0B FULLWIDTH PLUS SIGN
  p.minus_sign = "\xE2\x88\x92";  // U+2212 MINUS SIGN
  EXPECT_EQ(fmt::format("{:+}", fmt::profiled(p, 5)), "\xEF\xBC\x8B" "5");
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, -5)), "\xE2\x88\x92" "5");
}

TEST(digit_profile_test, zero_flag) {
  auto p = digit_profile::ascii();
  p.group_size = 0;
  EXPECT_EQ(fmt::format("{:08.1f}", fmt::profiled(p, 12.5)), "000012.5");
  EXPECT_EQ(fmt::format("{:08}", fmt::profiled(p, -1234)), "-0001234");
  // Grouping is applied after zero-padding.
  auto g = digit_profile::ascii();
  EXPECT_EQ(fmt::format("{:06}", fmt::profiled(g, 1234)), "01,234");
  // An explicit alignment overrides the zero flag, as in fmt.
  EXPECT_EQ(fmt::format("{:<08}", fmt::profiled(p, 42)), "42      ");
}

TEST(digit_profile_test, exponent_symbols_mapped) {
  auto p = digit_profile::arabic_indic();
  p.group_size = 0;
  // 1.23e+04 with Arabic-Indic digits.
  EXPECT_EQ(fmt::format("{:.2e}", fmt::profiled(p, 12345.0)),
            std::string("\xD9\xA1") + kArDecimal + "\xD9\xA2\xD9\xA3" + "e+" +
                "\xD9\xA0\xD9\xA4");
}

TEST(digit_profile_test, nonfinite_values) {
  auto p = digit_profile::arabic_indic();
  double inf = std::numeric_limits<double>::infinity();
  double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, inf)), "inf");
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, -inf)), "-inf");
  EXPECT_EQ(fmt::format("{}", fmt::profiled(p, nan)), "nan");
  EXPECT_EQ(fmt::format("{:8}", fmt::profiled(p, inf)), "     inf");
}

TEST(digit_profile_test, custom_profile) {
  // Devanagari digits, no grouping.
  digit_profile p;
  const char* const d[10] = {"\xE0\xA5\xA6", "\xE0\xA5\xA7", "\xE0\xA5\xA8",
                             "\xE0\xA5\xA9", "\xE0\xA5\xAA", "\xE0\xA5\xAB",
                             "\xE0\xA5\xAC", "\xE0\xA5\xAD", "\xE0\xA5\xAE",
                             "\xE0\xA5\xAF"};
  for (int i = 0; i < 10; ++i) p.digits[i] = d[i];
  p.group_size = 0;
  // 2.75 rounds to 2.8 with one fractional digit, as in plain fmt.
  EXPECT_EQ(fmt::format("{:.1f}", fmt::profiled(p, 2.75)),
            std::string("\xE0\xA5\xA8") + "." + "\xE0\xA5\xAE");
}

TEST(digit_profile_test, invalid_profile_rejected) {
  digit_profile p;
  p.digits[3] = "";
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);

  p = digit_profile();
  p.digits[3] = p.digits[4];  // Duplicate digit symbols.
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);

  p = digit_profile();
  p.decimal_point = ",";  // Collides with the group separator.
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);

  p = digit_profile();
  p.minus_sign = "";
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);

  p = digit_profile();
  p.digits[0] = "\xC3";  // Truncated UTF-8 sequence.
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);

  p = digit_profile();
  p.group_size = -1;
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);

  p = digit_profile();
  p.digits[7] = ".";  // Collides with the decimal point.
  EXPECT_THROW((void)fmt::format("{}", fmt::profiled(p, 1)), fmt::format_error);
}

TEST(digit_profile_test, invalid_profile_rejected_before_output) {
  digit_profile p;
  p.digits[0] = p.digits[1];
  auto buf = fmt::memory_buffer();
  EXPECT_THROW(
      fmt::format_to(fmt::appender(buf), "{}", fmt::profiled(p, 12345)),
      fmt::format_error);
  EXPECT_EQ(buf.size(), 0u);
}

TEST(digit_profile_test, unsupported_specs_rejected) {
  auto p = digit_profile::ascii();
  // Non-decimal types and options outside the documented subset.
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:x}"), fmt::profiled(p, 255)),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:b}"), fmt::profiled(p, 5)),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:o}"), fmt::profiled(p, 8)),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:L}"), fmt::profiled(p, 1)),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:#}"), fmt::profiled(p, 1)),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:a}"), fmt::profiled(p, 1.0)),
               fmt::format_error);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:s}"), fmt::profiled(p, 1.0)),
               fmt::format_error);
  // Precision on an integer is rejected by the plain fmt formatter.
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:.2d}"), fmt::profiled(p, 1)),
               fmt::format_error);
  // 'd' is not valid for floating point.
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:d}"), fmt::profiled(p, 1.5)),
               fmt::format_error);
}

TEST(digit_profile_test, normal_formatter_unchanged) {
  EXPECT_EQ(fmt::format("{}", 1234.5), "1234.5");
  EXPECT_EQ(fmt::format("{:10.2f}", 1234.5), "   1234.50");
  EXPECT_EQ(fmt::format("{:x}", 255), "ff");
  EXPECT_EQ(fmt::format("{:+08.1f}", 12.5), "+00012.5");
}

TEST(digit_profile_test, concurrent_profiles_do_not_interfere) {
  const auto arabic = digit_profile::arabic_indic();
  const auto ascii = digit_profile::ascii();
  const std::string expected_arabic =
      fmt::format("{:>12}", fmt::profiled(arabic, 1234.5));
  const std::string expected_ascii =
      fmt::format("{:>12}", fmt::profiled(ascii, 1234.5));
  EXPECT_EQ(expected_ascii, "     1,234.5");
  EXPECT_NE(expected_arabic, expected_ascii);

  std::atomic<int> failures(0);
  auto worker = [&failures](const digit_profile& p, const std::string& want) {
    for (int i = 0; i < 20000; ++i) {
      if (fmt::format("{:>12}", fmt::profiled(p, 1234.5)) != want) ++failures;
      auto a = fmt::format("{}", fmt::profiled(p, -987654321));
      auto b = fmt::format("{}", fmt::profiled(p, -987654321));
      if (a != b) ++failures;
    }
  };
  std::thread t1(worker, std::cref(arabic), std::cref(expected_arabic));
  std::thread t2(worker, std::cref(ascii), std::cref(expected_ascii));
  std::thread t3(worker, std::cref(arabic), std::cref(expected_arabic));
  t1.join();
  t2.join();
  t3.join();
  EXPECT_EQ(failures.load(), 0);
}
