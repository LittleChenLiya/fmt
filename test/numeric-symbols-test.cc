// Tests for fmt::numeric, the explicit numeric symbol wrapper.

#include <fmt/numeric-symbols.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>

using fmt::numeric;
using fmt::numeric_profile;

namespace {

// Arabic-Indic digit profile with an Arabic minus sign and Arabic punctuation.
numeric_profile make_arabic_profile() {
  numeric_profile p;
  p.digits[0] = "\u0660";
  p.digits[1] = "\u0661";
  p.digits[2] = "\u0662";
  p.digits[3] = "\u0663";
  p.digits[4] = "\u0664";
  p.digits[5] = "\u0665";
  p.digits[6] = "\u0666";
  p.digits[7] = "\u0667";
  p.digits[8] = "\u0668";
  p.digits[9] = "\u0669";
  p.plus_sign = "+";
  p.minus_sign = "\u061c";
  p.decimal_point = "\u066b";
  p.grouping_separator = "\u066c";
  return p;
}

// ASCII (English) profile.
numeric_profile make_english_profile() {
  numeric_profile p;
  for (int i = 0; i < 10; ++i)
    p.digits[i] = std::string(1, static_cast<char>('0' + i));
  p.plus_sign = "+";
  p.minus_sign = "-";
  p.decimal_point = ".";
  p.grouping_separator = ",";
  return p;
}

}  // namespace

TEST(numeric_test, arabic_grouping_and_width) {
  numeric_profile ar = make_arabic_profile();
  // 1234.5 -> grouped as 1,234.5, Arabic digits, width 10 in display columns.
  std::string s = fmt::format("{:>10.1f}", numeric(1234.5, ar));
  // "1,234.5" is 7 display columns; 3 spaces of padding reach 10.
  EXPECT_EQ(s, "   \u0661\u066c\u0662\u0663\u0664\u066b\u0665");
}

TEST(numeric_test, grouping_integer) {
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{}", numeric(1234567, en)), "1,234,567");
  EXPECT_EQ(fmt::format("{}", numeric(1234, en)), "1,234");
  EXPECT_EQ(fmt::format("{}", numeric(123, en)), "123");
}

TEST(numeric_test, rounding_matches_fmt) {
  numeric_profile en = make_english_profile();
  // The wrapper must use fmt's precision and rounding rules verbatim.
  EXPECT_EQ(fmt::format("{:.2f}", numeric(2.675, en)),
            fmt::format("{:.2f}", 2.675));
  EXPECT_EQ(fmt::format("{:.0f}", numeric(2.5, en)),
            fmt::format("{:.0f}", 2.5));
  EXPECT_EQ(fmt::format("{:.2f}", numeric(1.005, en)),
            fmt::format("{:.2f}", 1.005));
}

TEST(numeric_test, two_profiles_no_pollution) {
  numeric_profile ar = make_arabic_profile();
  numeric_profile en = make_english_profile();
  // Interleave formatting with two profiles; neither should affect the other.
  std::string a1 = fmt::format("{}", numeric(12345, ar));
  std::string e1 = fmt::format("{}", numeric(12345, en));
  std::string a2 = fmt::format("{}", numeric(6789, ar));
  std::string e2 = fmt::format("{}", numeric(6789, en));
  EXPECT_EQ(e1, "12,345");
  EXPECT_EQ(e2, "6,789");
  EXPECT_EQ(a1, "\u0661\u0662\u066c\u0663\u0664\u0665");  // 12,345
  EXPECT_EQ(a2, "\u0666\u066c\u0667\u0668\u0669");
  // Profiles are immutable and unchanged after formatting.
  EXPECT_EQ(en.digits[0], "0");
  EXPECT_EQ(ar.digits[0], "\u0660");
}

TEST(numeric_test, negative_sign) {
  numeric_profile ar = make_arabic_profile();
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{}", numeric(-42, en)), "-42");
  EXPECT_EQ(fmt::format("{}", numeric(-42.5, en)), "-42.5");
  EXPECT_EQ(fmt::format("{}", numeric(-42, ar)), "\u061c\u0664\u0662");
}

TEST(numeric_test, plus_sign) {
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{:+}", numeric(42, en)), "+42");
  EXPECT_EQ(fmt::format("{:+}", numeric(-42, en)), "-42");
}

TEST(numeric_test, width_alignment) {
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{:>8}", numeric(123, en)), "     123");
  EXPECT_EQ(fmt::format("{:<8}", numeric(123, en)), "123     ");
  EXPECT_EQ(fmt::format("{:^8}", numeric(123, en)), "  123   ");
  EXPECT_EQ(fmt::format("{:08}", numeric(123, en)), "00000123");
}

TEST(numeric_test, width_display_columns_not_bytes) {
  numeric_profile ar = make_arabic_profile();
  // Arabic digits are 3 UTF-8 bytes each but 1 display column.
  // Width 8 must be measured in columns, not bytes.
  std::string s = fmt::format("{:>8}", numeric(123, ar));
  // "123" is 3 columns -> 5 spaces + 3 Arabic digits = 8 columns.
  EXPECT_EQ(s, "     \u0661\u0662\u0663");
  // Byte length: 5 spaces + 3*2 bytes = 11 bytes, but 8 columns.
  EXPECT_EQ(s.size(), 11u);
}

TEST(numeric_test, exponent_notation) {
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{:.2e}", numeric(1234.5, en)), "1.23e+03");
  EXPECT_EQ(fmt::format("{:.2E}", numeric(1234.5, en)), "1.23E+03");
}

TEST(numeric_test, default_float_format) {
  numeric_profile en = make_english_profile();
  // Default float formatting uses fmt's shortest representation, but the
  // wrapper always groups the integer part.
  EXPECT_EQ(fmt::format("{}", numeric(1234.5, en)), "1,234.5");
  EXPECT_EQ(fmt::format("{}", numeric(0.1, en)), "0.1");
  EXPECT_EQ(fmt::format("{}", numeric(123.456, en)), "123.456");
}

TEST(numeric_test, non_finite) {
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{}", numeric(std::numeric_limits<double>::infinity(), en)),
            "inf");
  EXPECT_EQ(fmt::format("{}", numeric(-std::numeric_limits<double>::infinity(), en)),
            "-inf");
  EXPECT_EQ(fmt::format("{}", numeric(std::numeric_limits<double>::quiet_NaN(), en)),
            "nan");
}

TEST(numeric_test, invalid_empty_digit) {
  numeric_profile bad = make_english_profile();
  bad.digits[0] = "";
  EXPECT_THROW((void)fmt::format("{}", numeric(123, bad)), fmt::format_error);
}

TEST(numeric_test, invalid_duplicate_digits) {
  numeric_profile bad = make_english_profile();
  bad.digits[1] = bad.digits[0];
  EXPECT_THROW((void)fmt::format("{}", numeric(123, bad)), fmt::format_error);
}

TEST(numeric_test, invalid_decimal_equals_grouping) {
  numeric_profile bad = make_english_profile();
  bad.decimal_point = bad.grouping_separator = ".";
  EXPECT_THROW((void)fmt::format("{}", numeric(123.4, bad)), fmt::format_error);
}

TEST(numeric_test, invalid_empty_symbol) {
  numeric_profile bad = make_english_profile();
  bad.minus_sign = "";
  EXPECT_THROW((void)fmt::format("{}", numeric(-123, bad)), fmt::format_error);
}

TEST(numeric_test, reject_hex) {
  numeric_profile en = make_english_profile();
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:x}"), numeric(255, en)), fmt::format_error);
}

TEST(numeric_test, reject_octal) {
  numeric_profile en = make_english_profile();
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:o}"), numeric(8, en)), fmt::format_error);
}

TEST(numeric_test, reject_binary) {
  numeric_profile en = make_english_profile();
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:b}"), numeric(3, en)), fmt::format_error);
}

TEST(numeric_test, reject_hexfloat) {
  numeric_profile en = make_english_profile();
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:a}"), numeric(1.5, en)), fmt::format_error);
}

TEST(numeric_test, localized_flag_ignored) {
  numeric_profile en = make_english_profile();
  // The 'L' flag must not consult the global locale; grouping comes from the
  // profile.  Both should produce the same result.
  EXPECT_EQ(fmt::format("{:L}", numeric(12345, en)), "12,345");
  EXPECT_EQ(fmt::format("{}", numeric(12345, en)), "12,345");
}

TEST(numeric_test, fill_character) {
  numeric_profile en = make_english_profile();
  EXPECT_EQ(fmt::format("{:*^10}", numeric(123, en)), "***123****");
  EXPECT_EQ(fmt::format("{:*^10}", numeric(1234, en)), "**1,234***");
}
