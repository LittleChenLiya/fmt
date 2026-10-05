// Formatting library for C++ - explicit digit-symbol profiles
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_DIGIT_PROFILE_H_
#define FMT_DIGIT_PROFILE_H_

#include <algorithm>    // std::copy
#include <string>       // std::string
#include <type_traits>  // std::is_arithmetic

#include "format.h"

FMT_BEGIN_NAMESPACE

namespace detail {
// Returns true if s is a well-formed UTF-8 string. Defined below.
inline auto profile_is_valid_utf8(string_view s) -> bool;
}  // namespace detail

/**
 * An immutable, locale-independent set of number symbols: the ten decimal
 * digits, the plus and minus signs, the decimal point and the group
 * separator. A profile is a plain value object: it can be shared between
 * threads and is copied into each fmt::profiled wrapper, so concurrent
 * formatting with different profiles never interferes and the global locale
 * is never touched.
 *
 * **Example**:
 *
 *     // Western Arabic (ASCII) symbols, the default:
 *     auto ascii = fmt::digit_profile::ascii();
 *
 *     // Arabic-Indic digits (U+0660..U+0669) with U+066B as the decimal
 *     // point and U+066C as the group separator:
 *     auto arabic = fmt::digit_profile::arabic_indic();
 *     fmt::print("{:10}\n", fmt::profiled(arabic, 1234.5));
 *     // Output: "   <ARABIC-INDIC 1>,<2><3><4>.<5>" padded to 10 display
 *     // columns, not bytes.
 *
 *     // A fully custom profile (Devanagari digits, no grouping):
 *     fmt::digit_profile devanagari;
 *     const char* d[10] = {"\xE0\xA5\xA6", "\xE0\xA5\xA7", "\xE0\xA5\xA8",
 *                          "\xE0\xA5\xA9", "\xE0\xA5\xAA", "\xE0\xA5\xAB",
 *                          "\xE0\xA5\xAC", "\xE0\xA5\xAD", "\xE0\xA5\xAE",
 *                          "\xE0\xA5\xAF"};
 *     for (int i = 0; i < 10; ++i) devanagari.digits[i] = d[i];
 *     devanagari.group_size = 0;
 */
class digit_profile {
 public:
  string_view digits[10];       // Symbols for '0' .. '9', UTF-8 encoded.
  string_view plus_sign;        // Symbol for the explicit '+' sign.
  string_view minus_sign;       // Symbol for the '-' sign.
  string_view decimal_point;    // Symbol for the decimal point.
  string_view group_separator;  // Symbol inserted between digit groups.
  int group_size;               // Digits per group; 0 disables grouping.

  digit_profile()
      : digits{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"},
        plus_sign("+"),
        minus_sign("-"),
        decimal_point("."),
        group_separator(","),
        group_size(3) {}

  /** Returns the default profile with plain ASCII symbols. */
  static auto ascii() -> digit_profile { return digit_profile(); }

  /**
   * Returns a profile with Arabic-Indic digits (U+0660..U+0669), the Arabic
   * decimal separator (U+066B) and the Arabic thousands separator (U+066C).
   */
  static auto arabic_indic() -> digit_profile {
    digit_profile p;
    static const char* const symbols[10] = {
        "\xD9\xA0", "\xD9\xA1", "\xD9\xA2", "\xD9\xA3", "\xD9\xA4",
        "\xD9\xA5", "\xD9\xA6", "\xD9\xA7", "\xD9\xA8", "\xD9\xA9"};
    for (int i = 0; i < 10; ++i) p.digits[i] = symbols[i];
    p.decimal_point = "\xD9\xAB";
    p.group_separator = "\xD9\xAC";
    return p;
  }

  /**
   * Validates the symbol set, throwing fmt::format_error if it is invalid.
   * fmt::profiled calls this before producing any output, so an invalid
   * profile can never result in partially formatted output. A profile is
   * valid if:
   *
   * - all ten digit symbols are non-empty, valid UTF-8 and pairwise
   *   distinct;
   * - the signs and the decimal point are non-empty valid UTF-8;
   * - group_size is non-negative and, if positive, the group separator is
   *   non-empty, valid UTF-8 and differs from the decimal point;
   * - no digit symbol collides with a sign, the decimal point or the group
   *   separator.
   */
  void validate() const {
    for (int i = 0; i < 10; ++i) {
      if (digits[i].size() == 0)
        FMT_THROW(format_error("digit profile: digit symbol is empty"));
      if (!detail::profile_is_valid_utf8(digits[i]))
        FMT_THROW(format_error("digit profile: digit symbol is not UTF-8"));
      for (int j = i + 1; j < 10; ++j)
        if (digits[i] == digits[j])
          FMT_THROW(format_error("digit profile: digit symbols not distinct"));
    }
    const string_view specials[4] = {plus_sign, minus_sign, decimal_point,
                                     group_separator};
    const char* const names[4] = {"plus sign", "minus sign", "decimal point",
                                  "group separator"};
    for (int i = 0; i < 4; ++i) {
      if (specials[i].size() == 0 && (i != 3 || group_size > 0)) {
        auto msg = std::string("digit profile: ");
        FMT_THROW(format_error(msg + names[i] + " is empty"));
      }
      if (specials[i].size() != 0 && !detail::profile_is_valid_utf8(specials[i]))
        FMT_THROW(format_error("digit profile: symbol is not UTF-8"));
    }
    if (group_size < 0)
      FMT_THROW(format_error("digit profile: negative group size"));
    if (group_size > 0 && group_separator == decimal_point)
      FMT_THROW(format_error(
          "digit profile: decimal point and group separator must differ"));
    for (int i = 0; i < 10; ++i)
      for (int j = 0; j < 4; ++j)
        if (specials[j].size() != 0 && digits[i] == specials[j])
          FMT_THROW(format_error(
              "digit profile: digit symbol collides with another symbol"));
  }
};

namespace detail {

// Returns true if s is a well-formed UTF-8 string.
inline auto profile_is_valid_utf8(string_view s) -> bool {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  size_t n = s.size();
  for (size_t i = 0; i < n;) {
    unsigned char c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    int len = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = c & 0x1F;
      if (cp < 2) return false;  // Overlong or stray 0xC0/0xC1.
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = c & 0x07;
      if (cp == 0 || cp > 4) return false;  // Overlong or out of range.
    } else {
      return false;  // Stray continuation byte or 0xF8+.
    }
    if (i + to_unsigned(len) > n) return false;
    for (int k = 1; k < len; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000))
      return false;  // Overlong encoding.
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // Surrogate.
    i += to_unsigned(len);
  }
  return true;
}

// Returns the display width of s in columns. Each Unicode code point counts
// as one column; UTF-8 continuation bytes count as zero. This is what makes
// width and alignment independent of the UTF-8 byte length of the symbols.
inline auto profile_display_columns(string_view s) -> size_t {
  size_t cols = 0;
  const char* p = s.data();
  for (size_t i = 0, n = s.size(); i < n; ++i)
    if ((static_cast<unsigned char>(p[i]) & 0xC0) != 0x80) ++cols;
  return cols;
}

// Parses a non-negative decimal integer in [it, end), advancing it.
FMT_CONSTEXPR inline auto profile_parse_int(const char*& it, const char* end)
    -> int {
  int value = 0;
  while (it != end && *it >= '0' && *it <= '9') {
    if (value > 1000000) report_error("number is too big");
    value = value * 10 + (*it - '0');
    ++it;
  }
  return value;
}

// Maps a number already formatted by the plain fmt formatter (ASCII digits,
// '.', 'e'/'E', '+'/'-'/' ') onto the symbols of profile, inserting group
// separators, and appends the result to out. If zero_pad is true, zero
// digits are inserted after the sign until the display width reaches width
// (grouping is applied after padding, so with grouping enabled the result
// may exceed width). Non-finite values ("inf"/"nan") are not grouped or
// zero-padded; only their sign is mapped.
inline void map_profiled_number(const digit_profile& profile,
                                const std::string& s, bool zero_pad, int width,
                                memory_buffer& out) {
  size_t pos = 0;
  char sign_char = 0;
  if (pos < s.size() && (s[pos] == '-' || s[pos] == '+' || s[pos] == ' '))
    sign_char = s[pos++];
  size_t exp_pos = s.find_first_of("eE", pos);
  size_t mantissa_end = exp_pos == std::string::npos ? s.size() : exp_pos;
  string_view exponent;
  if (exp_pos != std::string::npos)
    exponent = string_view(s.data() + exp_pos, s.size() - exp_pos);
  size_t dot = s.find('.', pos);
  bool has_dot = dot != std::string::npos && dot < mantissa_end;
  string_view int_part(s.data() + pos, (has_dot ? dot : mantissa_end) - pos);
  string_view frac_part;
  if (has_dot)
    frac_part = string_view(s.data() + dot + 1, mantissa_end - dot - 1);
  bool all_digits = int_part.size() != 0;
  for (size_t i = 0; i < int_part.size(); ++i)
    if (int_part[i] < '0' || int_part[i] > '9') all_digits = false;

  auto digits = std::string(int_part.data(), int_part.size());
  auto append = [&out](string_view sv) { out.append(sv.data(), sv.end()); };
  for (;;) {
    out.clear();
    if (sign_char == '-')
      append(profile.minus_sign);
    else if (sign_char == '+')
      append(profile.plus_sign);
    else if (sign_char == ' ')
      out.push_back(' ');
    size_t n = digits.size();
    auto group_size = to_unsigned(profile.group_size);
    bool group = all_digits && group_size != 0 && n > group_size;
    for (size_t i = 0; i < n; ++i) {
      if (group && i != 0 && (n - i) % group_size == 0)
        append(profile.group_separator);
      char c = digits[i];
      if (c >= '0' && c <= '9')
        append(profile.digits[c - '0']);
      else
        out.push_back(c);  // 'i', 'n', 'f', 'a' of inf/nan.
    }
    if (has_dot) {
      append(profile.decimal_point);
      for (size_t i = 0; i < frac_part.size(); ++i)
        append(profile.digits[frac_part[i] - '0']);
    }
    for (size_t i = 0; i < exponent.size(); ++i) {
      char c = exponent[i];
      if (c >= '0' && c <= '9')
        append(profile.digits[c - '0']);
      else if (c == '-')
        append(profile.minus_sign);
      else if (c == '+')
        append(profile.plus_sign);
      else
        out.push_back(c);  // 'e' or 'E'.
    }
    if (!zero_pad || !all_digits) break;
    auto cols = profile_display_columns(string_view(out.data(), out.size()));
    if (cols >= to_unsigned(width)) break;
    digits.insert(digits.begin(), '0');
  }
}

}  // namespace detail

/**
 * A value bundled with a digit profile. The profile is stored by value, so
 * the view owns an immutable snapshot of the symbol set.
 */
template <typename T> struct profiled_view {
  digit_profile profile;
  T value;
};

/**
 * Returns a view that formats "value" (an integer or floating-point
 * number) using the symbols of "profile" instead of the ASCII ones. The
 * value is first formatted by the ordinary fmt formatter with the requested
 * precision and rounding, then the resulting symbols are mapped; width and
 * alignment are applied to the final display column count, not to the UTF-8
 * byte count. The global locale is never consulted and the normal number
 * formatter is unaffected.
 *
 * Only the following documented decimal format specs are supported:
 *
 *     [[fill]align][sign]["0"][width]["." precision][type]
 *
 * where align is one of <, >, ^; sign is one of +, -, space; type is d for
 * integers or one of f, F, e, E, g, G for floating-point numbers (or
 * omitted). The "0" flag zero-pads after the sign when no explicit
 * alignment is given; grouping is applied after zero-padding. Anything else
 * (#, L, n, hex/octal/binary types, dynamic width or precision, ...) is
 * rejected with fmt::format_error.
 *
 * **Example**:
 *
 *     auto p = fmt::digit_profile::arabic_indic();
 *     fmt::format("{:10}", fmt::profiled(p, 1234.5));
 *     // "   " + Arabic-Indic "1,234.5" (7 columns padded to 10)
 */
template <typename T>
inline auto profiled(const digit_profile& profile, T value)
    -> profiled_view<T> {
  static_assert(std::is_arithmetic<T>::value &&
                    !std::is_same<T, bool>::value &&
                    !std::is_same<T, char>::value &&
                    !std::is_same<T, wchar_t>::value,
                "fmt::profiled only supports integer and floating-point "
                "types");
  return profiled_view<T>{profile, value};
}

template <typename T>
struct formatter<profiled_view<T>, char> {
 private:
  char fill_ = ' ';
  char align_ = 0;      // 0 (default: right), '<', '>' or '^'.
  char sign_ = 0;       // 0 (default), '+', '-' or ' '.
  bool zero_ = false;   // The '0' flag.
  int width_ = 0;
  int precision_ = -1;  // -1: no precision given.
  char type_ = 0;

 public:
  FMT_CONSTEXPR auto parse(parse_context<>& ctx) -> const char* {
    const char* it = ctx.begin();
    // The parse context extends to the end of the whole format string, so
    // find the '}' terminating this replacement field first.
    const char* end = it;
    while (end != ctx.end() && *end != '}') ++end;
    if (it != end && it + 1 != end &&
        (it[1] == '<' || it[1] == '>' || it[1] == '^')) {
      fill_ = it[0];
      if (fill_ == '{' || fill_ == '}')
        report_error("invalid fill character");
      align_ = it[1];
      it += 2;
    } else if (it != end && (*it == '<' || *it == '>' || *it == '^')) {
      align_ = *it++;
    }
    if (it != end && (*it == '+' || *it == '-' || *it == ' ')) sign_ = *it++;
    if (it != end && *it == '0') {
      zero_ = true;
      ++it;
    }
    if (it != end && *it >= '0' && *it <= '9')
      width_ = detail::profile_parse_int(it, end);
    if (it != end && *it == '.') {
      ++it;
      if (it == end || *it < '0' || *it > '9')
        report_error("missing precision in format spec for fmt::profiled");
      precision_ = detail::profile_parse_int(it, end);
    }
    if (it != end) {
      type_ = *it++;
      bool ok = std::is_integral<T>::value
                    ? type_ == 'd'
                    : (type_ == 'f' || type_ == 'F' || type_ == 'e' ||
                       type_ == 'E' || type_ == 'g' || type_ == 'G');
      if (!ok)
        report_error("unsupported type specifier for fmt::profiled; only "
                     "decimal specs are supported");
    }
    if (it != end) report_error("invalid format spec for fmt::profiled");
    return it;
  }

  template <typename FormatContext>
  auto format(const profiled_view<T>& view, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    // Reject an invalid symbol set before producing any output.
    view.profile.validate();

    // Format with the ordinary fmt formatter first so that precision and
    // rounding are exactly the same as for normal fmt formatting.
    auto spec = std::string("{:");
    if (sign_) spec += sign_;
    if (precision_ >= 0) {
      spec += '.';
      spec += fmt::format("{}", precision_);
    }
    if (type_) spec += type_;
    spec += '}';
    auto s = fmt::format(fmt::runtime(spec), view.value);

    // Map the symbols and insert group separators.
    auto buf = memory_buffer();
    detail::map_profiled_number(view.profile, s, zero_ && align_ == 0, width_,
                                buf);

    // Pad to width in display columns, not UTF-8 bytes.
    size_t cols =
        detail::profile_display_columns(string_view(buf.data(), buf.size()));
    size_t padding = width_ > 0 && detail::to_unsigned(width_) > cols
                         ? detail::to_unsigned(width_) - cols
                         : 0;
    size_t left = 0, right = 0;
    if (align_ == '<') {
      right = padding;
    } else if (align_ == '^') {
      left = padding / 2;
      right = padding - left;
    } else {
      left = padding;
    }
    auto out = ctx.out();
    for (size_t i = 0; i < left; ++i) *out++ = fill_;
    out = std::copy(buf.data(), buf.data() + buf.size(), out);
    for (size_t i = 0; i < right; ++i) *out++ = fill_;
    return out;
  }
};

FMT_END_NAMESPACE

#endif  // FMT_DIGIT_PROFILE_H_
