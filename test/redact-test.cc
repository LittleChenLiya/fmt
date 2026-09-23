// Formatting library for C++ - redacted formatting tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/redact.h"

#include <string>

#include "gtest-extra.h"

namespace {


// A marker that must never appear in output or error messages.
const char sensitive_marker[] = "hunter2";

struct secret {
  std::string value;
};

struct plain {
  int value;
};

struct formatter_counts {
  int constructed = 0;
  int parsed = 0;
  int formatted = 0;
};

auto secret_counts() -> formatter_counts& {
  static auto counts = formatter_counts();
  return counts;
}

auto plain_counts() -> formatter_counts& {
  static auto counts = formatter_counts();
  return counts;
}

void reset_counts() {
  secret_counts() = formatter_counts();
  plain_counts() = formatter_counts();
}

template <typename F>
auto throws_format_error_without_leak(F&& f) -> std::string {
  try {
    f();
  } catch (const fmt::format_error& e) {
    EXPECT_EQ(std::string::npos, std::string(e.what()).find(sensitive_marker));
    return e.what();
  }
  ADD_FAILURE() << "expected fmt::format_error";
  return "";
}

}  // namespace

template <> struct fmt::formatter<secret> {
  formatter() { ++secret_counts().constructed; }

  auto parse(fmt::format_parse_context& ctx)
      -> fmt::format_parse_context::iterator {
    ++secret_counts().parsed;
    return ctx.begin();
  }

  auto format(const secret& s, fmt::format_context& ctx) const
      -> fmt::format_context::iterator {
    ++secret_counts().formatted;
    return fmt::format_to(ctx.out(), "{}", s.value);
  }
};

template <> struct fmt::formatter<plain> {
  formatter() { ++plain_counts().constructed; }

  auto parse(fmt::format_parse_context& ctx)
      -> fmt::format_parse_context::iterator {
    ++plain_counts().parsed;
    return ctx.begin();
  }

  auto format(const plain& p, fmt::format_context& ctx) const
      -> fmt::format_context::iterator {
    ++plain_counts().formatted;
    return fmt::format_to(ctx.out(), "plain:{}", p.value);
  }
};

TEST(redact_test, positional_masked_arg_is_never_formatted) {
  reset_counts();
  auto s = secret{sensitive_marker};
  auto result = fmt::redacted_format("{}", {{0}, {}}, s);
  EXPECT_EQ("[redacted]", result);
  EXPECT_EQ(0, secret_counts().constructed);
  EXPECT_EQ(0, secret_counts().parsed);
  EXPECT_EQ(0, secret_counts().formatted);
  EXPECT_EQ(std::string::npos, result.find(sensitive_marker));
}

TEST(redact_test, named_masked_arg_is_never_formatted) {
  reset_counts();
  auto s = secret{sensitive_marker};
  auto result =
      fmt::redacted_format("{token}", {{}, {"token"}}, fmt::arg("token", s));
  EXPECT_EQ("[redacted]", result);
  EXPECT_EQ(0, secret_counts().constructed);
  EXPECT_EQ(0, secret_counts().parsed);
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, name_and_position_refer_to_same_masked_arg) {
  reset_counts();
  auto s = secret{sensitive_marker};
  // Masked by name, referenced by position and by name.
  auto by_name = fmt::redacted_format("{0} {token}", {{}, {"token"}},
                                      fmt::arg("token", s));
  EXPECT_EQ("[redacted] [redacted]", by_name);
  // Masked by position, referenced by name.
  auto by_index =
      fmt::redacted_format("{token}", {{0}, {}}, fmt::arg("token", s));
  EXPECT_EQ("[redacted]", by_index);
  EXPECT_EQ(0, secret_counts().constructed);
  EXPECT_EQ(0, secret_counts().parsed);
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, repeated_fields_are_all_masked) {
  reset_counts();
  auto s = secret{sensitive_marker};
  auto result = fmt::redacted_format("{0} {0} {0}", {{0}, {}}, s);
  EXPECT_EQ("[redacted] [redacted] [redacted]", result);
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, masked_field_supports_fill_align_and_width) {
  reset_counts();
  auto s = secret{sensitive_marker};
  EXPECT_EQ("  [redacted]", fmt::redacted_format("{:>12}", {{0}, {}}, s));
  EXPECT_EQ("[redacted]  ", fmt::redacted_format("{:<12}", {{0}, {}}, s));
  EXPECT_EQ("[redacted]**", fmt::redacted_format("{:*<12}", {{0}, {}}, s));
  EXPECT_EQ("( [redacted] )", fmt::redacted_format("({:^12})", {{0}, {}}, s));
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, masked_field_allows_dynamic_width_of_unmasked_arg) {
  reset_counts();
  auto s = secret{sensitive_marker};
  auto result = fmt::redacted_format("{:>{}}", {{0}, {}}, s, 12);
  EXPECT_EQ("  [redacted]", result);
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, dynamic_width_of_masked_arg_is_rejected) {
  reset_counts();
  auto s = secret{sensitive_marker};
  // Positional dynamic width referring to a masked argument.
  EXPECT_THROW_MSG((void)fmt::redacted_format("{0:{1}}", {{1}, {}}, "x", 5),
                   fmt::format_error,
                   "dynamic width or precision refers to a redacted argument");
  // Named dynamic width referring to a masked argument.
  EXPECT_THROW_MSG((void)fmt::redacted_format("{:{w}}", {{}, {"w"}}, "x", fmt::arg("w", 5)),
      fmt::format_error,
      "dynamic width or precision refers to a redacted argument");
  // Dynamic precision referring to a masked argument.
  EXPECT_THROW_MSG((void)fmt::redacted_format("{0:.{1}}", {{1}, {}}, 1.5, 3),
                   fmt::format_error,
                   "dynamic width or precision refers to a redacted argument");
  // The sensitive value is never read to decide the output length.
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, masked_field_rejects_precision_and_type) {
  reset_counts();
  auto s = secret{sensitive_marker};
  EXPECT_THROW_MSG((void)fmt::redacted_format("{:.3}", {{0}, {}}, s),
                   fmt::format_error,
                   "invalid format specifier for redacted argument");
  EXPECT_THROW_MSG((void)fmt::redacted_format("{:s}", {{0}, {}}, s),
                   fmt::format_error,
                   "invalid format specifier for redacted argument");
  EXPECT_THROW_MSG((void)fmt::redacted_format("{:?}", {{0}, {}}, s),
                   fmt::format_error,
                   "invalid format specifier for redacted argument");
  EXPECT_EQ(0, secret_counts().constructed);
  EXPECT_EQ(0, secret_counts().parsed);
  EXPECT_EQ(0, secret_counts().formatted);
}

TEST(redact_test, late_error_prevents_any_formatting) {
  reset_counts();
  // An invalid spec after an unmasked custom argument: nothing is formatted.
  EXPECT_THROW_MSG((void)fmt::redacted_format("{} {:d}", {{}, {}}, plain{42}, "not a number"),
      fmt::format_error, "invalid format specifier");
  EXPECT_EQ(0, plain_counts().formatted);
  // An unknown argument after an unmasked custom argument.
  EXPECT_THROW_MSG((void)fmt::redacted_format("{0} {5}", {{}, {}}, plain{42}),
                   fmt::format_error, "argument not found");
  EXPECT_EQ(0, plain_counts().formatted);
  // A bad index after an unmasked custom argument.
  EXPECT_THROW_MSG((void)fmt::redacted_format("{} {}", {{}, {}}, plain{42}),
                   fmt::format_error, "argument not found");
  EXPECT_EQ(0, plain_counts().formatted);
}

TEST(redact_test, unmasked_args_keep_fmt_semantics) {
  reset_counts();
  auto s = secret{sensitive_marker};
  // Unmasked custom argument is formatted normally.
  auto result = fmt::redacted_format("{} {}", {{1}, {}}, plain{42}, s);
  EXPECT_EQ("plain:42 [redacted]", result);
  // The unmasked custom formatter is parsed twice: once by the validation
  // pass and once by the formatting pass.
  EXPECT_EQ(2, plain_counts().constructed);
  EXPECT_EQ(2, plain_counts().parsed);
  EXPECT_EQ(1, plain_counts().formatted);
  EXPECT_EQ(0, secret_counts().constructed);
  EXPECT_EQ(0, secret_counts().parsed);
  EXPECT_EQ(0, secret_counts().formatted);
  // Built-in types keep their specs, including dynamic width.
  EXPECT_EQ("   hi", fmt::redacted_format("{:>5}", {{}, {}}, "hi"));
  EXPECT_EQ("hi   ", fmt::redacted_format("{:{}}", {{}, {}}, "hi", 5));
  EXPECT_EQ("0042", fmt::redacted_format("{:04d}", {{}, {}}, 42));
  EXPECT_EQ("a [redacted] c",
            fmt::redacted_format("{0} {1} {2}", {{1}, {}}, "a", "b", "c"));
  // Escaped braces still work.
  EXPECT_EQ("{[redacted]}", fmt::redacted_format("{{{0}}}", {{0}, {}}, s));
}

TEST(redact_test, unknown_redaction_name_is_rejected) {
  EXPECT_THROW_MSG((void)fmt::redacted_format("{}", {{}, {"nope"}}, 42),
                   fmt::format_error, "unknown redaction target");
}

TEST(redact_test, out_of_range_redaction_index_is_rejected) {
  EXPECT_THROW_MSG((void)fmt::redacted_format("{}", {{3}, {}}, 42),
                   fmt::format_error, "redaction index out of range");
}

TEST(redact_test, errors_do_not_leak_sensitive_value) {
  reset_counts();
  auto s = secret{sensitive_marker};
  // Invalid spec on a masked field.
  throws_format_error_without_leak(
      [&] { (void)fmt::redacted_format("{:.3}", {{0}, {}}, s); });
  // Dynamic width referring to a masked argument.
  throws_format_error_without_leak(
      [&] { (void)fmt::redacted_format("{0:{1}}", {{1}, {}}, "x", 5); });
  // Unknown argument.
  throws_format_error_without_leak(
      [&] { (void)fmt::redacted_format("{0} {9}", {{0}, {}}, s); });
  // Invalid spec on an unmasked argument.
  throws_format_error_without_leak(
      [&] { (void)fmt::redacted_format("{0} {1:d}", {{0}, {}}, s, "text"); });
  // Unmatched brace.
  throws_format_error_without_leak(
      [&] { (void)fmt::redacted_format("{0} }", {{0}, {}}, s); });
  EXPECT_EQ(0, secret_counts().formatted);
}
