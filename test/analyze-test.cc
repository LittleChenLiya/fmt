// Formatting library for C++ - format string analysis tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/analyze.h"

#include <string>

#include "gtest/gtest.h"

using fmt::analyzed_arg_kind;

TEST(analyze_test, no_fields) {
  auto result = fmt::analyze_format_string("hello");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.fields().empty());
}

TEST(analyze_test, escaped_braces_are_not_fields) {
  auto result = fmt::analyze_format_string("{{}} {{ok}}");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.fields().empty());
}

TEST(analyze_test, example) {
  auto result = fmt::analyze_format_string("{name:>{width}} {0:.{1}f} {{ok}}");
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.fields().size(), 2u);

  const auto& name_field = result.fields()[0];
  EXPECT_EQ(name_field.begin, 0u);
  EXPECT_EQ(name_field.end, 15u);
  EXPECT_EQ(name_field.arg.kind, analyzed_arg_kind::name);
  EXPECT_EQ(name_field.arg.name, "name");
  EXPECT_EQ(name_field.arg.begin, 1u);
  EXPECT_EQ(name_field.arg.end, 5u);
  ASSERT_TRUE(name_field.has_spec);
  EXPECT_EQ(name_field.spec, ">{width}");
  EXPECT_EQ(name_field.spec_begin, 6u);
  EXPECT_EQ(name_field.spec_end, 14u);
  EXPECT_FALSE(name_field.spec_type_dependent);
  ASSERT_TRUE(name_field.has_dynamic_width);
  EXPECT_EQ(name_field.width_ref.kind, analyzed_arg_kind::name);
  EXPECT_EQ(name_field.width_ref.name, "width");
  EXPECT_FALSE(name_field.has_dynamic_precision);

  const auto& index_field = result.fields()[1];
  EXPECT_EQ(index_field.begin, 16u);
  EXPECT_EQ(index_field.end, 25u);
  EXPECT_EQ(index_field.arg.kind, analyzed_arg_kind::index);
  EXPECT_EQ(index_field.arg.index, 0u);
  EXPECT_EQ(index_field.spec, ".{1}f");
  EXPECT_FALSE(index_field.spec_type_dependent);
  EXPECT_FALSE(index_field.has_dynamic_width);
  ASSERT_TRUE(index_field.has_dynamic_precision);
  EXPECT_EQ(index_field.precision_ref.kind, analyzed_arg_kind::index);
  EXPECT_EQ(index_field.precision_ref.index, 1u);
}

TEST(analyze_test, automatic_indexing) {
  auto result = fmt::analyze_format_string("{} {}");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.fields().size(), 2u);
  EXPECT_EQ(result.fields()[0].arg.kind, analyzed_arg_kind::automatic);
  EXPECT_EQ(result.fields()[0].arg.index, 0u);
  EXPECT_EQ(result.fields()[1].arg.kind, analyzed_arg_kind::automatic);
  EXPECT_EQ(result.fields()[1].arg.index, 1u);
}

TEST(analyze_test, automatic_with_spec) {
  auto result = fmt::analyze_format_string("{:>6}");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.fields().size(), 1u);
  EXPECT_EQ(result.fields()[0].arg.kind, analyzed_arg_kind::automatic);
  EXPECT_EQ(result.fields()[0].spec, ">6");
}

TEST(analyze_test, manual_to_automatic_error) {
  auto result = fmt::analyze_format_string("{0} {}");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error_message(),
            "cannot switch from manual to automatic argument indexing");
  EXPECT_EQ(result.error_offset(), 5u);
}

TEST(analyze_test, automatic_to_manual_error) {
  auto result = fmt::analyze_format_string("{} {0}");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error_message(),
            "cannot switch from automatic to manual argument indexing");
  EXPECT_EQ(result.error_offset(), 4u);
}

TEST(analyze_test, nested_dynamic_args) {
  auto result = fmt::analyze_format_string("{0:{1}.{2}}");
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.fields().size(), 1u);
  const auto& field = result.fields()[0];
  EXPECT_EQ(field.spec, "{1}.{2}");
  ASSERT_TRUE(field.has_dynamic_width);
  EXPECT_EQ(field.width_ref.kind, analyzed_arg_kind::index);
  EXPECT_EQ(field.width_ref.index, 1u);
  ASSERT_TRUE(field.has_dynamic_precision);
  EXPECT_EQ(field.precision_ref.kind, analyzed_arg_kind::index);
  EXPECT_EQ(field.precision_ref.index, 2u);
}

TEST(analyze_test, dynamic_arg_automatic) {
  auto result = fmt::analyze_format_string("{:{}}");
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.fields().size(), 1u);
  ASSERT_TRUE(result.fields()[0].has_dynamic_width);
  EXPECT_EQ(result.fields()[0].width_ref.kind, analyzed_arg_kind::automatic);
  EXPECT_EQ(result.fields()[0].width_ref.index, 1u);
}

TEST(analyze_test, nested_spec_is_an_error) {
  auto result = fmt::analyze_format_string("{:{:{}}}");
  EXPECT_FALSE(result.ok());
}

TEST(analyze_test, unmatched_close_brace) {
  auto result = fmt::analyze_format_string("ab}cd");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error_message(), "unmatched '}' in format string");
  EXPECT_EQ(result.error_offset(), 2u);
}

TEST(analyze_test, unmatched_open_brace) {
  EXPECT_FALSE(fmt::analyze_format_string("{").ok());
  EXPECT_FALSE(fmt::analyze_format_string("{0").ok());
  EXPECT_FALSE(fmt::analyze_format_string("{0:").ok());
  EXPECT_FALSE(fmt::analyze_format_string("{0:{1}").ok());
  EXPECT_FALSE(fmt::analyze_format_string("{!}").ok());
  EXPECT_FALSE(fmt::analyze_format_string("{0!}").ok());
}

TEST(analyze_test, unicode_fill_and_text) {
  // U+03B1 U+03B2 are 2 bytes each, U+2728 (sparkles) is a 3-byte fill.
  auto result = fmt::analyze_format_string("\xce\xb1\xce\xb2{0:\xe2\x9c\xa8>5}");
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.fields().size(), 1u);
  const auto& field = result.fields()[0];
  EXPECT_EQ(field.begin, 4u);
  EXPECT_EQ(field.spec, "\xe2\x9c\xa8>5");
  EXPECT_FALSE(field.spec_type_dependent);
  // Cross-check with actual formatting.
  EXPECT_EQ(fmt::format("{0:\xe2\x9c\xa8>5}", 42), "\xe2\x9c\xa8\xe2\x9c\xa8\xe2\x9c\xa8" "42");
}

TEST(analyze_test, custom_spec_is_type_dependent) {
  auto result = fmt::analyze_format_string("{0:%Y-%m-%d}");
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.fields().size(), 1u);
  EXPECT_EQ(result.fields()[0].spec, "%Y-%m-%d");
  EXPECT_TRUE(result.fields()[0].spec_type_dependent);
}

TEST(analyze_test, standard_specs_cross_check) {
  // Specs that conform to the standard grammar are not type-dependent and
  // format correctly with matching argument types.
  const char* cases[] = {"{:>10}", "{:.2f}", "{:#x}", "{:^9}", "{:{}}",
                         "{:.{}}", "{:+}", "{: L}", "{:c}", "{:s}", "{:p}"};
  for (const char* fmt_str : cases) {
    auto result = fmt::analyze_format_string(fmt_str);
    ASSERT_TRUE(result.ok()) << fmt_str;
    ASSERT_EQ(result.fields().size(), 1u) << fmt_str;
    EXPECT_FALSE(result.fields()[0].spec_type_dependent) << fmt_str;
  }
  EXPECT_EQ(fmt::format("{:>10}", 42), "        42");
  EXPECT_EQ(fmt::format("{:.2f}", 3.14159), "3.14");
  EXPECT_EQ(fmt::format("{:#x}", 255), "0xff");
  EXPECT_EQ(fmt::format("{:^9}", "hi"), "   hi    ");
}

TEST(analyze_test, type_dependent_validity_is_left_to_type_checking) {
  // "{:+}" is a standard spec but invalid for strings. Analysis succeeds and
  // the error is only diagnosed when formatting with an actual argument.
  auto result = fmt::analyze_format_string("{:+}");
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.fields()[0].spec_type_dependent);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:+}"), "hi"),
               fmt::format_error);
}

namespace {
struct sentinel {};
bool sentinel_parse_called = false;
}  // namespace

FMT_BEGIN_NAMESPACE
template <> struct formatter<sentinel> {
  auto parse(format_parse_context& ctx) -> format_parse_context::iterator {
    sentinel_parse_called = true;
    return ctx.begin();
  }
  auto format(sentinel, format_context& ctx) const -> format_context::iterator {
    return ctx.out();
  }
};
FMT_END_NAMESPACE

TEST(analyze_test, analysis_does_not_call_formatter) {
  sentinel_parse_called = false;
  auto result = fmt::analyze_format_string("{0:anything goes}");
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.fields()[0].spec_type_dependent);
  EXPECT_FALSE(sentinel_parse_called);
}

TEST(analyze_test, max_fields) {
  auto result = fmt::analyze_format_string("{} {}", 1);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error_message(), "too many fields in format string");
  EXPECT_EQ(result.error_offset(), 3u);
  EXPECT_EQ(result.fields().size(), 1u);
}

TEST(analyze_test, result_is_reusable) {
  auto result = fmt::analyze_format_string("{0}");
  ASSERT_TRUE(result.ok());
  const auto& first = result.fields();
  const auto& second = result.fields();
  EXPECT_EQ(&first, &second);
  EXPECT_EQ(first[0].arg.index, 0u);
}
