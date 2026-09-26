// Formatting library for C++ - explicit decimal fixed-point formatting.
//
// Provides fmt::decimal, a lightweight wrapper that stores an integer
// coefficient and a decimal scale so that exact decimal values (such as
// monetary amounts) can be formatted without converting to binary
// floating-point first.

#ifndef FMT_DECIMAL_H_
#define FMT_DECIMAL_H_

#include <cstdint>  // int64_t

#include "format.h"

FMT_BEGIN_NAMESPACE

/// The maximum number of fractional decimal digits supported by fmt::decimal.
constexpr int decimal_max_scale = 18;

/// Rounding modes used when the output precision discards decimal digits.
enum class decimal_rounding {
  half_even,  ///< Round to nearest, ties to even (banker's rounding).
  half_away_from_zero  ///< Round to nearest, ties away from zero.
};

/**
 * An explicit decimal fixed-point value stored as a signed 64-bit integer
 * coefficient and a decimal scale, representing "coefficient * 10^-scale"
 * with scale in the range [0, 18].
 *
 * All arithmetic is done on integers with overflow checking, so exact
 * decimal quantities such as monetary amounts never go through double.
 *
 * **Example**:
 *
 *     // 12.34 stored as 1234 * 10^-2:
 *     fmt::print("{}", fmt::decimal(1234, 2));           // prints 12.34
 *     fmt::print("{:.1f}", fmt::decimal(125, 2));        // prints 1.2
 *     fmt::print("{:.1f}", fmt::decimal(
 *         125, 2, fmt::decimal_rounding::half_away_from_zero));  // 1.3
 */
class decimal {
 private:
  int64_t coefficient_;
  int scale_;
  decimal_rounding rounding_;

 public:
  FMT_CONSTEXPR decimal(int64_t coefficient, int scale,
                        decimal_rounding rounding = decimal_rounding::half_even)
      : coefficient_(coefficient), scale_(scale), rounding_(rounding) {
    if (scale < 0 || scale > decimal_max_scale)
      report_error("decimal scale is out of range (expected 0 to 18)");
  }

  FMT_CONSTEXPR auto coefficient() const -> int64_t { return coefficient_; }
  FMT_CONSTEXPR auto scale() const -> int { return scale_; }
  FMT_CONSTEXPR auto rounding() const -> decimal_rounding { return rounding_; }
};

namespace detail {

// Returns 10^exp for exp in [0, 18].
FMT_CONSTEXPR auto decimal_pow10(int exp) -> unsigned long long {
  auto result = 1ull;
  for (int i = 0; i < exp; ++i) result *= 10;
  return result;
}

// Multiplies value by 10^exp, reporting a format error on overflow.
FMT_CONSTEXPR auto decimal_mul_pow10(unsigned long long value, int exp)
    -> unsigned long long {
  for (int i = 0; i < exp; ++i) {
    if (value > max_value<unsigned long long>() / 10)
      report_error("decimal value is too large");
    value *= 10;
  }
  return value;
}

}  // namespace detail

FMT_BEGIN_EXPORT
template <> struct formatter<decimal> {
 private:
  detail::dynamic_format_specs<> specs_;

 public:
  FMT_CONSTEXPR auto parse(parse_context<>& ctx) -> const char* {
    auto end = parse_format_specs(ctx.begin(), ctx.end(), specs_, ctx,
                                  detail::type::double_type);
    // Only fixed-point output is supported: no scientific notation, no
    // locale-dependent grouping and no alternate form.
    if (specs_.alt() || specs_.localized())
      report_error("invalid format specifier");
    auto type = specs_.type();
    if (type != presentation_type::none && type != presentation_type::fixed)
      report_error("invalid format specifier");
    return end;
  }

  template <typename FormatContext>
  auto format(decimal value, FormatContext& ctx) const -> decltype(ctx.out()) {
    auto specs = specs_;
    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width,
                                specs.width_ref, ctx);
    detail::handle_dynamic_spec(specs.dynamic_precision(), specs.precision,
                                specs.precision_ref, ctx);

    int scale = value.scale();
    int precision = specs.precision >= 0 ? specs.precision : scale;
    if (precision > decimal_max_scale)
      report_error("precision is out of range (expected 0 to 18)");

    // Work on the magnitude as unsigned so that INT64_MIN is handled.
    auto negative = value.coefficient() < 0;
    auto magnitude = static_cast<unsigned long long>(value.coefficient());
    if (negative) magnitude = 0ull - magnitude;

    // Round the coefficient to the requested number of output digits.
    unsigned long long rounded = 0;
    if (precision >= scale) {
      rounded = detail::decimal_mul_pow10(magnitude, precision - scale);
    } else {
      auto divisor = detail::decimal_pow10(scale - precision);
      auto quotient = magnitude / divisor;
      auto remainder = magnitude % divisor;
      // The divisor is a power of 10 greater than 1 and therefore even, so a
      // tie is exactly remainder == divisor - remainder.
      auto round_up = false;
      if (remainder > divisor - remainder) {
        round_up = true;
      } else if (remainder == divisor - remainder && remainder != 0) {
        round_up = value.rounding() == decimal_rounding::half_away_from_zero ||
                   (quotient & 1) != 0;
      }
      rounded = round_up ? quotient + 1 : quotient;
    }

    // A negative value that rounds to zero is printed without the sign.
    char sign_char = '\0';
    if (negative && rounded != 0) {
      sign_char = '-';
    } else if (specs.sign() == sign::plus) {
      sign_char = '+';
    } else if (specs.sign() == sign::space) {
      sign_char = ' ';
    }

    auto int_part = rounded;
    auto frac_part = 0ull;
    if (precision != 0) {
      auto frac_divisor = detail::decimal_pow10(precision);
      int_part = rounded / frac_divisor;
      frac_part = rounded % frac_divisor;
    }
    int num_int_digits = detail::count_digits(int_part);

    using Char = typename FormatContext::char_type;
    // sign + up to 20 integer digits + '.' + up to 18 fraction digits.
    Char buffer[1 + 20 + 1 + decimal_max_scale];
    auto out_it = buffer;
    if (sign_char != '\0') *out_it++ = static_cast<Char>(sign_char);
    out_it = detail::format_decimal(out_it, int_part, num_int_digits);
    if (precision != 0) {
      *out_it++ = static_cast<Char>('.');
      // Pad the fraction with leading zeros up to the requested precision.
      int frac_digits = detail::count_digits(frac_part);
      out_it = detail::fill_n(out_it, precision - frac_digits, Char('0'));
      out_it = detail::format_decimal(out_it, frac_part, frac_digits);
    }
    auto size = detail::to_unsigned(out_it - buffer);
    size_t sign_size = sign_char != '\0' ? 1 : 0;

    auto out = ctx.out();
    if (specs.align() == align::numeric) {
      if (detail::to_unsigned(specs.width) > size) {
        // Zero padding is inserted after the sign, e.g. "-0001.20".
        out = detail::copy<Char>(buffer, buffer + sign_size, out);
        out = detail::fill_n(out, detail::to_unsigned(specs.width) - size,
                             Char('0'));
        return detail::copy<Char>(buffer + sign_size, buffer + size, out);
      }
      // No padding is needed; numeric alignment degrades to right.
      specs.set_align(align::right);
    }
    const Char* data = buffer;
    return detail::write_padded<Char, align::right>(
        out, specs, size,
        [data, size](detail::reserve_iterator<decltype(out)> it) {
          return detail::copy<Char>(data, data + size, it);
        });
  }
};
FMT_END_EXPORT

FMT_END_NAMESPACE

#endif  // FMT_DECIMAL_H_
