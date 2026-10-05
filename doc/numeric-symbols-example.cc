// Example: formatting numbers with explicit symbol profiles.
//
// This example shows how to define immutable digit/symbol profiles and use
// fmt::numeric to format numbers without modifying the global locale.
//
// Build (local, single-threaded):
//   g++ -std=c++17 -DFMT_HEADER_ONLY -I../include numeric-symbols-example.cc -o example
//
// Or against a built fmt library:
//   g++ -std=c++17 -I../include numeric-symbols-example.cc -lfmt -o example

#include <fmt/numeric-symbols.h>

#include <iostream>
#include <string>

// Arabic-Indic digit profile.
static fmt::numeric_profile make_arabic_profile() {
  fmt::numeric_profile p;
  p.digits[0] = "\u0660";  // ٠
  p.digits[1] = "\u0661";  // ١
  p.digits[2] = "\u0662";  // ٢
  p.digits[3] = "\u0663";  // ٣
  p.digits[4] = "\u0664";  // ٤
  p.digits[5] = "\u0665";  // ٥
  p.digits[6] = "\u0666";  // ٦
  p.digits[7] = "\u0667";  // ٧
  p.digits[8] = "\u0668";  // ٨
  p.digits[9] = "\u0669";  // ٩
  p.plus_sign = "+";
  p.minus_sign = "\u061c";  // Arabic letter mark
  p.decimal_point = "\u066b";  // ٫
  p.grouping_separator = "\u066c";  // ٬
  return p;
}

// ASCII (English) profile.
static fmt::numeric_profile make_english_profile() {
  fmt::numeric_profile p;
  for (int i = 0; i < 10; ++i)
    p.digits[i] = std::string(1, static_cast<char>('0' + i));
  p.plus_sign = "+";
  p.minus_sign = "-";
  p.decimal_point = ".";
  p.grouping_separator = ",";
  return p;
}

int main() {
  fmt::numeric_profile ar = make_arabic_profile();
  fmt::numeric_profile en = make_english_profile();

  // 1234.5 in Arabic profile: grouped, Arabic digits, width 10 in columns.
  std::cout << "ar 1234.5  : [" << fmt::format("{:>10.1f}", fmt::numeric(1234.5, ar)) << "]\n";

  // Same value in English profile.
  std::cout << "en 1234.5  : [" << fmt::format("{:>10.1f}", fmt::numeric(1234.5, en)) << "]\n";

  // Rounding uses fmt's own rules (2.675 -> "2.67" with .2f).
  std::cout << "ar rounding: " << fmt::format("{:.2f}", fmt::numeric(2.675, ar)) << "\n";
  std::cout << "en rounding: " << fmt::format("{:.2f}", fmt::numeric(2.675, en)) << "\n";

  // Negative number with sign.
  std::cout << "ar -42     : " << fmt::format("{}", fmt::numeric(-42, ar)) << "\n";
  std::cout << "en -42     : " << fmt::format("{}", fmt::numeric(-42, en)) << "\n";

  // Integer grouping.
  std::cout << "ar 1234567 : " << fmt::format("{}", fmt::numeric(1234567, ar)) << "\n";
  std::cout << "en 1234567 : " << fmt::format("{}", fmt::numeric(1234567, en)) << "\n";

  // Two profiles interleaved do not affect each other.
  std::string a = fmt::format("{}", fmt::numeric(12345, ar));
  std::string e = fmt::format("{}", fmt::numeric(12345, en));
  std::cout << "concurrent : ar=" << a << " en=" << e << "\n";

  return 0;
}
