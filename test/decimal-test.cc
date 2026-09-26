// Formatting library for C++ - decimal fixed-point formatting tests

#include "fmt/decimal.h"

#include <cstdint>
#include <limits>

#include "gtest-extra.h"

using fmt::decimal;
using fmt::decimal_rounding;

namespace {
constexpr auto half_even = decimal_rounding::half_even;
constexpr auto half_away = decimal_rounding::half_away_from_zero;
constexpr auto int64_min = (std::numeric_limits<int64_t>::min)();
constexpr auto int64_max = (std::numeric_limits<int64_t>::max)();
}  // namespace

TEST(decimal_test, construct) {
  auto value = decimal(1234, 2);
  EXPECT_EQ(value.coefficient(), 1234);
  EXPECT_EQ(value.scale(), 2);
  EXPECT_EQ(value.rounding(), half_even);
  EXPECT_THROW_MSG(decimal(1, 19), fmt::format_error,
                   "decimal scale is out of range (expected 0 to 18)");
  EXPECT_THROW_MSG(decimal(1, -1), fmt::format_error,
                   "decimal scale is out of range (expected 0 to 18)");
}

TEST(decimal_test, format_default) {
  EXPECT_EQ(fmt::format("{}", decimal(123, 2)), "1.23");
  EXPECT_EQ(fmt::format("{}", decimal(5, 0)), "5");
  EXPECT_EQ(fmt::format("{}", decimal(-5, 0)), "-5");
  EXPECT_EQ(fmt::format("{}", decimal(0, 0)), "0");
  EXPECT_EQ(fmt::format("{}", decimal(0, 2)), "0.00");
  EXPECT_EQ(fmt::format("{}", decimal(-123, 2)), "-1.23");
  EXPECT_EQ(fmt::format("{}", decimal(1, 18)), "0.000000000000000001");
  EXPECT_EQ(fmt::format("{:f}", decimal(123, 2)), "1.23");
}

TEST(decimal_test, format_rounding_acceptance) {
  // Coefficient 125 with scale 2 formatted with one output digit.
  EXPECT_EQ(fmt::format("{:.1f}", decimal(125, 2, half_even)), "1.2");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(125, 2, half_away)), "1.3");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(-125, 2, half_even)), "-1.2");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(-125, 2, half_away)), "-1.3");
}

TEST(decimal_test, format_rounding_ties) {
  // Ties: the digit before the discarded 5 decides half-even results.
  EXPECT_EQ(fmt::format("{:.1f}", decimal(105, 2, half_even)), "1.0");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(105, 2, half_away)), "1.1");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(115, 2, half_even)), "1.2");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(115, 2, half_away)), "1.2");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(135, 2, half_even)), "1.4");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(145, 2, half_even)), "1.4");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(145, 2, half_away)), "1.5");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(-105, 2, half_even)), "-1.0");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(-105, 2, half_away)), "-1.1");
  // Non-tie rounding is towards nearest in both modes.
  EXPECT_EQ(fmt::format("{:.1f}", decimal(124, 2, half_even)), "1.2");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(126, 2, half_even)), "1.3");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(-126, 2, half_away)), "-1.3");
}

TEST(decimal_test, format_min_max_coefficient) {
  EXPECT_EQ(fmt::format("{}", decimal(int64_max, 0)), "9223372036854775807");
  EXPECT_EQ(fmt::format("{}", decimal(int64_min, 0)), "-9223372036854775808");
  EXPECT_EQ(fmt::format("{}", decimal(int64_max, 18)), "9.223372036854775807");
  EXPECT_EQ(fmt::format("{}", decimal(int64_min, 18)),
            "-9.223372036854775808");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(int64_max, 18)), "9.2");
  EXPECT_EQ(fmt::format("{:.0f}", decimal(int64_max, 18)), "9");
  EXPECT_EQ(fmt::format("{:.0f}", decimal(int64_min, 18)), "-9");
}

TEST(decimal_test, format_zero_padding) {
  // Requesting more digits than the scale pads with zeros.
  EXPECT_EQ(fmt::format("{:.3f}", decimal(5, 0)), "5.000");
  EXPECT_EQ(fmt::format("{:.4f}", decimal(123, 2)), "1.2300");
  EXPECT_EQ(fmt::format("{:.2f}", decimal(-5, 0)), "-5.00");
  EXPECT_EQ(fmt::format("{:.18f}", decimal(1, 0)), "1.000000000000000000");
  EXPECT_EQ(fmt::format("{:.18f}", decimal(-1, 18)),
            "-0.000000000000000001");
}

TEST(decimal_test, format_reduced_precision) {
  // 123.456 rounded down to fewer output digits.
  EXPECT_EQ(fmt::format("{:.2f}", decimal(123456, 3)), "123.46");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(123456, 3)), "123.5");
  EXPECT_EQ(fmt::format("{:.0f}", decimal(123456, 3)), "123");
  // 123.45 is a tie at one output digit.
  EXPECT_EQ(fmt::format("{:.1f}", decimal(123450, 3, half_even)), "123.4");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(123450, 3, half_away)), "123.5");
}

TEST(decimal_test, format_negative_zero) {
  // Negative values that round to zero are printed without the sign.
  EXPECT_EQ(fmt::format("{:.0f}", decimal(-1, 2)), "0");
  EXPECT_EQ(fmt::format("{:.0f}", decimal(-4, 1)), "0");
  EXPECT_EQ(fmt::format("{:.0f}", decimal(-5, 1, half_even)), "0");
  EXPECT_EQ(fmt::format("{:.0f}", decimal(-5, 1, half_away)), "-1");
  EXPECT_EQ(fmt::format("{:+.0f}", decimal(-1, 2)), "+0");
  EXPECT_EQ(fmt::format("{: .0f}", decimal(-1, 2)), " 0");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(-10, 2)), "-0.1");
}

TEST(decimal_test, format_alignment_and_fill) {
  EXPECT_EQ(fmt::format("{:8}", decimal(123, 2)), "    1.23");
  EXPECT_EQ(fmt::format("{:>8}", decimal(123, 2)), "    1.23");
  EXPECT_EQ(fmt::format("{:<8}", decimal(123, 2)), "1.23    ");
  EXPECT_EQ(fmt::format("{:^8}", decimal(123, 2)), "  1.23  ");
  EXPECT_EQ(fmt::format("{:*^8}", decimal(123, 2)), "**1.23**");
  EXPECT_EQ(fmt::format("{:>8}", decimal(-123, 2)), "   -1.23");
  EXPECT_EQ(fmt::format("{:+}", decimal(123, 2)), "+1.23");
  EXPECT_EQ(fmt::format("{: }", decimal(123, 2)), " 1.23");
  EXPECT_EQ(fmt::format("{:+8}", decimal(123, 2)), "   +1.23");
  // Zero fill pads after the sign like other numeric types.
  EXPECT_EQ(fmt::format("{:08.2f}", decimal(12345, 3, half_away)), "00012.35");
  EXPECT_EQ(fmt::format("{:08.2f}", decimal(12345, 3, half_even)), "00012.34");
  EXPECT_EQ(fmt::format("{:08.1f}", decimal(-125, 2, half_away)), "-00001.3");
  EXPECT_EQ(fmt::format("{:+08.2f}", decimal(123, 2)), "+0001.23");
}

TEST(decimal_test, format_dynamic_specs) {
  EXPECT_EQ(fmt::format("{:.{}}", decimal(125, 2, half_away), 1), "1.3");
  EXPECT_EQ(fmt::format("{:.{}}", decimal(125, 2, half_even), 1), "1.2");
  EXPECT_EQ(fmt::format("{:.{}}", decimal(123, 2), 0), "1");
  EXPECT_EQ(fmt::format("{:{}}", decimal(123, 2), 6), "  1.23");
  EXPECT_EQ(fmt::format("{:{}.{}}", decimal(125, 2, half_away), 8, 1),
            "     1.3");
  EXPECT_EQ(fmt::format("{:{width}.{precision}}", decimal(123, 2),
                        fmt::arg("width", 7), fmt::arg("precision", 1)),
            "    1.2");
}

TEST(decimal_test, format_overflow) {
  // Scaling up the largest coefficients overflows the 64-bit coefficient.
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:.18f}"), decimal(int64_max, 0)),
                   fmt::format_error, "decimal value is too large");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:.1f}"), decimal(int64_min, 0)),
                   fmt::format_error, "decimal value is too large");
  // The same values are fine without scaling.
  EXPECT_EQ(fmt::format("{}", decimal(int64_max, 0)), "9223372036854775807");
}

TEST(decimal_test, format_precision_out_of_range) {
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:.19f}"), decimal(1, 0)),
                   fmt::format_error,
                   "precision is out of range (expected 0 to 18)");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:.{}}"), decimal(1, 0), 19),
                   fmt::format_error,
                   "precision is out of range (expected 0 to 18)");
}

TEST(decimal_test, format_unsupported_specs) {
  // Scientific notation, locale grouping and other type specifiers are
  // rejected with a format error.
  auto value = decimal(123, 2);
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:e}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:E}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:g}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:a}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:L}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:#}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:d}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:s}"), value), fmt::format_error,
                   "invalid format specifier");
  EXPECT_THROW_MSG((void)fmt::format(fmt::runtime("{:x}"), value), fmt::format_error,
                   "invalid format specifier");
}

TEST(decimal_test, builtin_formatters_unchanged) {
  // The wrapper does not affect the standard integer and float formatters.
  EXPECT_EQ(fmt::format("{}", 42), "42");
  EXPECT_EQ(fmt::format("{:04}", -42), "-042");
  EXPECT_EQ(fmt::format("{}", 1.5), "1.5");
  EXPECT_EQ(fmt::format("{:.2f}", 1.5), "1.50");
  EXPECT_EQ(fmt::format("{:e}", 1.5), "1.500000e+00");
}

TEST(decimal_test, examples) {
  // Runnable versions of the examples from fmt/decimal.h.
  EXPECT_EQ(fmt::format("{}", decimal(1234, 2)), "12.34");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(125, 2)), "1.2");
  EXPECT_EQ(fmt::format("{:.1f}", decimal(125, 2, half_away)), "1.3");
  // A monetary amount stays exact: no binary floating-point is involved.
  EXPECT_EQ(fmt::format("{:>10.2f}", decimal(999999, 2)), "   9999.99");
#if FMT_USE_FCNTL
  EXPECT_WRITE(stdout, fmt::print("{:.2f}\n", decimal(1000000000000000001, 18)),
               "1.00\n");
#endif
}
