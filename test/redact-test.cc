// Formatting library for C++ - redaction tests
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/redact.h"

#include <string>

#include "gtest-extra.h"
#include "util.h"

namespace {

// A sensitive value together with counters recording whether its formatter
// is constructed or used. The marker must never appear in output or errors.
const char sensitive_marker[] = "TOP-SECRET-7f3a9c";

struct secret_stats {
  int constructed = 0;
  int parsed = 0;
  int formatted = 0;
};

auto secret_stats_instance() -> secret_stats& {
  static secret_stats stats;
  return stats;
}

void reset_secret_stats() { secret_stats_instance() = secret_stats(); }

struct secret {
  std::string token;
  explicit secret(std::string t) : token(std::move(t)) {}
};

auto widget_format_count() -> int& {
  static int count = 0;
  return count;
}

struct widget {
  std::string name;
};

}  // namespace

FMT_BEGIN_NAMESPACE
template <>
struct formatter<secret> {
  formatter() { ++secret_stats_instance().constructed; }

  auto parse(parse_context<>& ctx) -> const char* {
    ++secret_stats_instance().parsed;
    return ctx.begin();
  }

  auto format(const secret& s, format_context& ctx) const
      -> format_context::iterator {
    ++secret_stats_instance().formatted;
    return fmt::format_to(ctx.out(), "{}", s.token);
  }
};

template <>
struct formatter<widget> {
  FMT_CONSTEXPR auto parse(parse_context<>& ctx) -> const char* {
    return ctx.begin();
  }

  auto format(const widget& w, format_context& ctx) const
      -> format_context::iterator {
    ++widget_format_count();
    return fmt::format_to(ctx.out(), "<widget:{}>", w.name);
  }
};
FMT_END_NAMESPACE

TEST(redact_test, positional_redaction) {
  reset_secret_stats();
  auto s = secret(sensitive_marker);
  auto result = fmt::redact_format("{}", fmt::redaction().at(0), s);
  EXPECT_EQ(result, "[redacted]");
  const secret_stats& stats = secret_stats_instance();
  EXPECT_EQ(stats.constructed, 0);
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.formatted, 0);
}

TEST(redact_test, named_redaction) {
  reset_secret_stats();
  auto s = secret(sensitive_marker);
  auto result = fmt::redact_format("{token}", fmt::redaction().named("token"),
                                   fmt::arg("token", s));
  EXPECT_EQ(result, "[redacted]");
  const secret_stats& stats = secret_stats_instance();
  EXPECT_EQ(stats.constructed, 0);
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.formatted, 0);
}

TEST(redact_test, named_and_positional_refer_to_same_arg) {
  reset_secret_stats();
  auto s = secret(sensitive_marker);
  auto result = fmt::redact_format("{token} {0}",
                                   fmt::redaction().named("token"),
                                   fmt::arg("token", s));
  EXPECT_EQ(result, "[redacted] [redacted]");
  EXPECT_EQ(secret_stats_instance().formatted, 0);
}

TEST(redact_test, repeated_fields_are_all_redacted) {
  reset_secret_stats();
  auto s = secret(sensitive_marker);
  auto result = fmt::redact_format("{0} {0} {0}", fmt::redaction().at(0), s);
  EXPECT_EQ(result, "[redacted] [redacted] [redacted]");
  EXPECT_EQ(secret_stats_instance().formatted, 0);
}

TEST(redact_test, redacted_field_with_string_specs) {
  auto s = secret(sensitive_marker);
  EXPECT_EQ(fmt::redact_format("[{:>15}]", fmt::redaction().at(0), s),
            "[     [redacted]]");
  EXPECT_EQ(fmt::redact_format("[{:<12}]", fmt::redaction().at(0), s),
            "[[redacted]  ]");
  EXPECT_EQ(fmt::redact_format("[{:_^14}]", fmt::redaction().at(0), s),
            "[__[redacted]__]");
}

TEST(redact_test, unmasked_args_keep_fmt_semantics) {
  widget_format_count() = 0;
  auto w = widget{"gizmo"};
  EXPECT_EQ(fmt::redact_format("{} {:>6} {:.2f}", fmt::redaction(), w, "ab",
                               1.005),
            "<widget:gizmo>     ab 1.00");
  EXPECT_EQ(widget_format_count(), 1);
  // Dynamic width referring to an unmasked argument works.
  EXPECT_EQ(fmt::redact_format("[{:{}}]", fmt::redaction(), "ab", 6),
            "[ab    ]");
}

TEST(redact_test, mixed_masked_and_unmasked) {
  reset_secret_stats();
  auto s = secret(sensitive_marker);
  auto w = widget{"gizmo"};
  auto result = fmt::redact_format("{} {}", fmt::redaction().at(1), w, s);
  EXPECT_EQ(result, "<widget:gizmo> [redacted]");
  EXPECT_EQ(secret_stats_instance().formatted, 0);
}

TEST(redact_test, dynamic_width_on_redacted_arg_rejected) {
  auto s = secret(sensitive_marker);
  const char* message =
      "dynamic width or precision refers to a redacted argument";
  EXPECT_THROW_MSG(fmt::redact_format("{:{}}", fmt::redaction().at(1), "ab",
                                      s),
                   fmt::format_error, message);
  EXPECT_THROW_MSG(
      fmt::redact_format("{:{w}}", fmt::redaction().named("w"), "ab",
                         fmt::arg("w", s)),
      fmt::format_error, message);
  EXPECT_THROW_MSG(fmt::redact_format("{:.{}}", fmt::redaction().at(1), "ab",
                                      s),
                   fmt::format_error, message);
}

TEST(redact_test, redacted_field_rejects_non_string_specs) {
  auto s = secret(sensitive_marker);
  const char* message = "invalid format specifier for a redacted argument";
  EXPECT_THROW_MSG(fmt::redact_format("{:.3}", fmt::redaction().at(0), s),
                   fmt::format_error, message);
  EXPECT_THROW_MSG(fmt::redact_format("{:s}", fmt::redaction().at(0), s),
                   fmt::format_error, message);
  EXPECT_THROW_MSG(fmt::redact_format("{:+}", fmt::redaction().at(0), s),
                   fmt::format_error, message);
  EXPECT_THROW_MSG(fmt::redact_format("{:05}", fmt::redaction().at(0), s),
                   fmt::format_error, message);
}

TEST(redact_test, unknown_redaction_name_rejected) {
  EXPECT_THROW_MSG(fmt::redact_format("{}", fmt::redaction().named("nope"), 1),
                   fmt::format_error, "unknown redaction argument name");
}

TEST(redact_test, redaction_index_out_of_range_rejected) {
  EXPECT_THROW_MSG(fmt::redact_format("{}", fmt::redaction().at(5), 1),
                   fmt::format_error, "redaction index out of range");
}

TEST(redact_test, unknown_argument_rejected) {
  EXPECT_THROW_MSG(fmt::redact_format("{1}", fmt::redaction(), 1),
                   fmt::format_error, "argument not found");
  EXPECT_THROW_MSG(fmt::redact_format("{nope}", fmt::redaction(), 1),
                   fmt::format_error, "argument not found");
}

TEST(redact_test, late_error_prevents_any_formatting) {
  widget_format_count() = 0;
  reset_secret_stats();
  auto w = widget{"gizmo"};
  // An invalid argument index after a valid field.
  EXPECT_THROW_MSG(fmt::redact_format("{0} {5}", fmt::redaction(), w),
                   fmt::format_error, "argument not found");
  // An invalid format spec after a valid field.
  EXPECT_THROW(fmt::redact_format("{} {:q}", fmt::redaction(), w, 1),
               fmt::format_error);
  // An unmatched brace after a valid field.
  EXPECT_THROW(fmt::redact_format("{} {", fmt::redaction(), w),
               fmt::format_error);
  EXPECT_EQ(widget_format_count(), 0);
  EXPECT_EQ(secret_stats_instance().formatted, 0);
}

TEST(redact_test, errors_do_not_leak_value) {
  reset_secret_stats();
  auto s = secret(sensitive_marker);
  const char* formats[] = {"{:d}", "{:.3}", "{:{}}", "{0} {1}"};
  for (auto f : formats) {
    try {
      fmt::redact_format(f, fmt::redaction().at(0), s);
      fmt::redact_format(f, fmt::redaction(), s);
    } catch (const fmt::format_error& e) {
      EXPECT_EQ(std::string(e.what()).find(sensitive_marker),
                std::string::npos)
          << "error leaks the sensitive value for format: " << f;
      continue;
    }
    // Not all format strings above are errors for both redaction sets.
  }
  // No value is ever formatted, and errors carry no argument values.
  EXPECT_EQ(secret_stats_instance().formatted, 0);
}

TEST(redact_test, text_and_escaped_braces) {
  EXPECT_EQ(fmt::redact_format("hello", fmt::redaction()), "hello");
  EXPECT_EQ(fmt::redact_format("{{}}", fmt::redaction()), "{}");
  EXPECT_EQ(fmt::redact_format("{{{}}}", fmt::redaction().at(0), 1),
            "{[redacted]}");
}
