// Formatting of numeric values with user-supplied digit and sign symbols.
//
// This header provides fmt::numeric, a wrapper that formats a number using
// the precision and rounding rules of the ordinary fmt formatter, then maps
// the ASCII digits, signs and decimal point to symbols from an immutable
// profile.  Width and alignment are computed in display columns rather than
// UTF-8 code units.  The global locale is never modified.
//
// Only the decimal presentations are supported: the default decimal, 'd',
// 'f'/'F', 'g'/'G' and 'e'/'E'.  Hexadecimal, octal, binary, character,
// string and pointer presentations are rejected.
//
// Example:
//   fmt::numeric_profile ar;
//   ar.digits[0] = "\u0660"; ... ar.digits[9] = "\u0669";
//   ar.minus_sign = "\u061c";
//   ar.decimal_point = "\u066b";
//   ar.grouping_separator = "\u066c";
//   fmt::format("{:>10.1f}", fmt::numeric(1234.5, ar));
//   // -> " \u0661\u066c\u0662\u0663\u0664\u066b\u0665"

#ifndef FMT_NUMERIC_SYMBOLS_H_
#define FMT_NUMERIC_SYMBOLS_H_

#include <fmt/format.h>

#include <string>

FMT_BEGIN_NAMESPACE

/**
 * An immutable set of symbols used to format a number.
 *
 * The ten digits, the plus and minus signs, the decimal point and the
 * grouping separator are all provided as UTF-8 strings.  They need not be
 * single bytes, but each must be a valid non-empty UTF-8 string with a
 * positive display width.  The ten digits must be pairwise distinct and the
 * decimal point must differ from the grouping separator.
 */
struct numeric_profile {
  std::string digits[10];
  std::string plus_sign;
  std::string minus_sign;
  std::string decimal_point;
  std::string grouping_separator;

  /// Checks that the symbol set is usable; throws format_error otherwise.
  void validate() const {
    const std::string* all[] = {
        &digits[0], &digits[1], &digits[2], &digits[3], &digits[4],
        &digits[5], &digits[6], &digits[7], &digits[8], &digits[9],
        &plus_sign, &minus_sign, &decimal_point, &grouping_separator};
    for (const std::string* s : all) {
      if (s->empty())
        report_error("numeric_profile: symbol must not be empty");
      size_t w = 0;
      bool ok = true;
      detail::for_each_codepoint(
          *s, [&w, &ok](uint32_t cp, string_view) {
            if (cp == detail::invalid_code_point) {
              ok = false;
              return false;
            }
            w += detail::display_width_of(cp);
            return true;
          });
      if (!ok) report_error("numeric_profile: symbol is not valid UTF-8");
      if (w == 0) report_error("numeric_profile: symbol has zero display width");
    }
    for (int i = 0; i < 10; ++i) {
      for (int j = i + 1; j < 10; ++j) {
        if (digits[i] == digits[j])
          report_error("numeric_profile: digits must be distinct");
      }
    }
    if (decimal_point == grouping_separator)
      report_error(
          "numeric_profile: decimal point and grouping separator must differ");
  }
};

/**
 * A reference to a numeric value and an immutable symbol profile.
 *
 * Formatting a numeric<T> applies the ordinary fmt precision and rounding
 * rules to the value and then maps the resulting ASCII symbols to the ones
 * in the profile.  The profile is referenced, not copied; it must outlive
 * the wrapper.
 */
template <typename T>
class numeric {
 public:
  constexpr numeric(T value, const numeric_profile& profile)
      : value_(value), profile_(profile) {}
  constexpr auto value() const -> T { return value_; }
  constexpr auto profile() const -> const numeric_profile& { return profile_; }

 private:
  T value_;
  const numeric_profile& profile_;
};

template <typename T, typename Char>
struct formatter<numeric<T>, Char> {
  template <typename ParseContext>
  FMT_CONSTEXPR auto parse(ParseContext& ctx) -> const Char* {
    auto end = detail::parse_format_specs(ctx.begin(), ctx.end(), specs_, ctx,
                                          detail::type_constant<T, Char>::value);
    // The presentation_type enum shares values across contexts (dec ==
    // general == 3, exp == debug == 1, fixed == string == 2), so validate
    // against the kind of T rather than switching on the value alone.
    auto t = specs_.type();
    bool decimal_ok;
    if (detail::is_integral<T>::value) {
      decimal_ok = (t == presentation_type::none || t == presentation_type::dec);
    } else {
      decimal_ok = (t != presentation_type::hexfloat);
    }
    if (!decimal_ok)
      report_error("numeric: only decimal presentations are supported");
    return end;
  }

  template <typename FormatContext>
  auto format(const numeric<T>& num, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    static_assert(detail::is_integral<T>::value ||
                      detail::is_floating_point<T>::value,
                  "numeric: only integral and floating-point types are supported");

    const numeric_profile& profile = num.profile();
    profile.validate();

    // Format with fmt's own precision and rounding rules, but without width
    // (padding is done below in display columns) and without locale grouping
    // (grouping comes from the profile instead).
    format_specs specs = specs_;
    specs.width = 0;
    specs.clear_localized();

    memory_buffer buf;
    detail::write<char>(appender(buf), num.value(), specs, locale_ref());

    // Group the integer part and map symbols to the profile.
    std::string sign_str;
    std::string rest_str;
    bool has_sign = false;
    size_t sign_cols = 0;
    build_mapped(buf, profile, sign_str, rest_str, has_sign, sign_cols);

    size_t rest_cols = display_columns(rest_str);
    size_t rest_bytes = rest_str.size();
    size_t sign_bytes = sign_str.size();

    auto out = ctx.out();
    if (specs_.align() == align::numeric && has_sign) {
      // Numeric alignment puts the sign first, then padding, then the rest.
      format_specs pad_specs = specs_;
      pad_specs.width =
          specs_.width > sign_cols ? specs_.width - static_cast<int>(sign_cols) : 0;
      auto it = detail::copy<char>(sign_str.begin(), sign_str.end(), out);
      return detail::write_padded<char, align::right>(
          it, pad_specs, rest_bytes, rest_cols, [&](auto o) {
            return detail::copy<char>(rest_str.begin(), rest_str.end(), o);
          });
    }
    return detail::write_padded<char, align::right>(
        out, specs_, sign_bytes + rest_bytes, sign_cols + rest_cols,
        [&](auto o) {
          auto it = detail::copy<char>(sign_str.begin(), sign_str.end(), o);
          return detail::copy<char>(rest_str.begin(), rest_str.end(), it);
        });
  }

 private:
  detail::dynamic_format_specs<Char> specs_;

  // Splits the ASCII output of detail::write into a sign and the rest,
  // grouping the integer part and mapping digits, signs and the decimal
  // point to profile symbols.
  static void build_mapped(const memory_buffer& buf,
                           const numeric_profile& profile,
                           std::string& sign_str, std::string& rest_str,
                           bool& has_sign, size_t& sign_cols) {
    const char* p = buf.data();
    const char* end = p + buf.size();

    // Optional sign.
    const std::string* sign_sym = nullptr;
    if (p != end && (*p == '-' || *p == '+' || *p == ' ')) {
      has_sign = true;
      char c = *p++;
      if (c == '-')
        sign_sym = &profile.minus_sign;
      else if (c == '+')
        sign_sym = &profile.plus_sign;
      // sign::space (' ') is a placeholder, not a profile symbol.
      if (sign_sym) {
        sign_str = *sign_sym;
        sign_cols = display_columns(sign_str);
      } else {
        sign_str = ' ';
        sign_cols = 1;
      }
    }

    // Integer digits.
    const char* int_start = p;
    while (p != end && *p >= '0' && *p <= '9') ++p;
    const char* int_end = p;

    // Fractional part.
    bool has_frac = false;
    const char* frac_start = nullptr;
    const char* frac_end = nullptr;
    if (p != end && *p == '.') {
      has_frac = true;
      ++p;
      frac_start = p;
      while (p != end && *p >= '0' && *p <= '9') ++p;
      frac_end = p;
    }

    // Exponent part.
    bool has_exp = false;
    char exp_char = 0;
    const char* exp_sign = nullptr;
    const char* exp_digits_start = nullptr;
    const char* exp_digits_end = nullptr;
    if (p != end && (*p == 'e' || *p == 'E')) {
      has_exp = true;
      exp_char = *p++;
      if (p != end && (*p == '-' || *p == '+')) {
        exp_sign = p++;
      }
      exp_digits_start = p;
      while (p != end && *p >= '0' && *p <= '9') ++p;
      exp_digits_end = p;
    }

    // Build the rest, grouping the integer part from the right in groups
    // of three.  Grouping separators are emitted directly as profile
    // symbols so they are not re-mapped.
    size_t int_len = static_cast<size_t>(int_end - int_start);
    for (size_t i = 0; i < int_len; ++i) {
      if (i > 0 && (int_len - i) % 3 == 0)
        rest_str += profile.grouping_separator;
      rest_str += profile.digits[static_cast<size_t>(int_start[i] - '0')];
    }

    if (has_frac) {
      rest_str += profile.decimal_point;
      for (const char* q = frac_start; q != frac_end; ++q)
        rest_str += profile.digits[static_cast<size_t>(*q - '0')];
    }

    if (has_exp) {
      rest_str += exp_char;
      if (exp_sign) {
        if (*exp_sign == '-')
          rest_str += profile.minus_sign;
        else
          rest_str += profile.plus_sign;
      }
      for (const char* q = exp_digits_start; q != exp_digits_end; ++q)
        rest_str += profile.digits[static_cast<size_t>(*q - '0')];
    }

    // Anything left (e.g. "nan", "inf") is passed through.
    rest_str.append(p, end);
  }

  // Returns the number of terminal columns occupied by a UTF-8 string.
  static size_t display_columns(string_view s) {
    size_t w = 0;
    detail::for_each_codepoint(s, [&w](uint32_t cp, string_view) {
      w += detail::display_width_of(cp);
      return true;
    });
    return w;
  }
};

FMT_END_NAMESPACE

#endif  // FMT_NUMERIC_SYMBOLS_H_
