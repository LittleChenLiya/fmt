// Formatting library for C++ - format string analysis tests

#include "fmt/analyze.h"

#include <string>

#include "fmt/args.h"
#include "fmt/compile.h"
#include "gtest-extra.h"

using fmt::analyzed_arg_kind;
using fmt::analyzed_ref_role;

namespace {

void expect_arg(const fmt::analyzed_arg& arg, analyzed_arg_kind kind,
                int index) {
  EXPECT_EQ(kind, arg.kind);
  EXPECT_EQ(index, arg.index);
}

void expect_arg_name(const fmt::analyzed_arg& arg, const char* name) {
  EXPECT_EQ(analyzed_arg_kind::name, arg.kind);
  EXPECT_EQ(name, arg.name);
}

// A formatter sentinel: analysis must never invoke it.
struct custom_type {};
bool custom_parse_called = false;

}  // namespace

FMT_BEGIN_NAMESPACE
template <> struct formatter<custom_type> {
  auto parse(parse_context<>& ctx) -> const char* {
    custom_parse_called = true;
    auto it = ctx.begin();
    while (it != ctx.end() && *it != '}') ++it;
    return it;
  }
  auto format(const custom_type&, format_context& ctx) const
      -> format_context::iterator {
    return ctx.out();
  }
};
FMT_END_NAMESPACE

TEST(analyze_test, acceptance_example) {
  auto analysis = fmt::analyze_format_string("{name:>{width}} {0:.{1}f} {{ok}}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(2u, analysis.fields().size());

  const auto& f0 = analysis.fields()[0];
  EXPECT_EQ(0u, f0.begin);
  EXPECT_EQ(15u, f0.end);
  EXPECT_EQ(6u, f0.spec_begin);
  EXPECT_EQ(14u, f0.spec_end);
  expect_arg_name(f0.arg, "name");
  EXPECT_TRUE(f0.standard_specs);
  ASSERT_EQ(1u, f0.refs.size());
  EXPECT_EQ(analyzed_ref_role::width, f0.refs[0].role);
  EXPECT_EQ(7u, f0.refs[0].offset);
  expect_arg_name(f0.refs[0].arg, "width");

  const auto& f1 = analysis.fields()[1];
  EXPECT_EQ(16u, f1.begin);
  EXPECT_EQ(25u, f1.end);
  EXPECT_EQ(19u, f1.spec_begin);
  EXPECT_EQ(24u, f1.spec_end);
  expect_arg(f1.arg, analyzed_arg_kind::manual, 0);
  EXPECT_TRUE(f1.standard_specs);
  ASSERT_EQ(1u, f1.refs.size());
  EXPECT_EQ(analyzed_ref_role::precision, f1.refs[0].role);
  EXPECT_EQ(20u, f1.refs[0].offset);
  expect_arg(f1.refs[0].arg, analyzed_arg_kind::manual, 1);

  // "{{ok}}" is escaped text, not a field.
  EXPECT_EQ("{{ok}}", std::string("{name:>{width}} {0:.{1}f} {{ok}}").substr(26));
}

TEST(analyze_test, escaped_braces_are_not_fields) {
  auto analysis = fmt::analyze_format_string("{{}}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  EXPECT_EQ(0u, analysis.fields().size());

  analysis = fmt::analyze_format_string("{{{}}}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  EXPECT_EQ(2u, analysis.fields()[0].begin);
  EXPECT_EQ(4u, analysis.fields()[0].end);
  expect_arg(analysis.fields()[0].arg, analyzed_arg_kind::automatic, 0);

  analysis = fmt::analyze_format_string("a{{b}}c{}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  EXPECT_EQ(7u, analysis.fields()[0].begin);
  EXPECT_EQ(9u, analysis.fields()[0].end);
}

TEST(analyze_test, empty_format_string) {
  auto analysis = fmt::analyze_format_string("");
  EXPECT_TRUE(analysis.ok());
  EXPECT_EQ(0u, analysis.fields().size());
}

TEST(analyze_test, mixing_automatic_and_manual_indexing) {
  auto analysis = fmt::analyze_format_string("{0} {}");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(4u, analysis.error_offset());
  EXPECT_EQ("cannot switch from manual to automatic argument indexing",
            analysis.error_message());

  analysis = fmt::analyze_format_string("{} {0}");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(3u, analysis.error_offset());
  EXPECT_EQ("cannot switch from automatic to manual argument indexing",
            analysis.error_message());
}

TEST(analyze_test, nested_dynamic_args) {
  auto analysis = fmt::analyze_format_string("{:{}.{}}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  const auto& field = analysis.fields()[0];
  EXPECT_TRUE(field.standard_specs);
  expect_arg(field.arg, analyzed_arg_kind::automatic, 0);
  ASSERT_EQ(2u, field.refs.size());
  EXPECT_EQ(analyzed_ref_role::width, field.refs[0].role);
  EXPECT_EQ(2u, field.refs[0].offset);
  expect_arg(field.refs[0].arg, analyzed_arg_kind::automatic, 1);
  EXPECT_EQ(analyzed_ref_role::precision, field.refs[1].role);
  EXPECT_EQ(5u, field.refs[1].offset);
  expect_arg(field.refs[1].arg, analyzed_arg_kind::automatic, 2);
  // The same numbering drives actual formatting.
  EXPECT_EQ("     3.1", fmt::format("{:{}.{}}", 3.14159, 8, 2));
}

TEST(analyze_test, dynamic_width_across_fields) {
  auto analysis = fmt::analyze_format_string("{} {:{}}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(2u, analysis.fields().size());
  expect_arg(analysis.fields()[0].arg, analyzed_arg_kind::automatic, 0);
  expect_arg(analysis.fields()[1].arg, analyzed_arg_kind::automatic, 1);
  ASSERT_EQ(1u, analysis.fields()[1].refs.size());
  EXPECT_EQ(5u, analysis.fields()[1].refs[0].offset);
  expect_arg(analysis.fields()[1].refs[0].arg, analyzed_arg_kind::automatic,
             2);
}

TEST(analyze_test, invalid_braces_report_offsets) {
  auto analysis = fmt::analyze_format_string("{");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(0u, analysis.error_offset());

  analysis = fmt::analyze_format_string("}");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(0u, analysis.error_offset());
  EXPECT_EQ("unmatched '}' in format string", analysis.error_message());

  analysis = fmt::analyze_format_string("ab}");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(2u, analysis.error_offset());

  analysis = fmt::analyze_format_string("{0");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(0u, analysis.error_offset());
  EXPECT_EQ("invalid format string", analysis.error_message());

  analysis = fmt::analyze_format_string("{:");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ("missing '}' in format string", analysis.error_message());

  analysis = fmt::analyze_format_string("{:5");
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ("missing '}' in format string", analysis.error_message());
}

TEST(analyze_test, utf8_byte_offsets) {
  // A multibyte prefix shifts byte offsets, not character offsets.
  auto analysis = fmt::analyze_format_string("\xce\xb1\xce\xb2{}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  EXPECT_EQ(4u, analysis.fields()[0].begin);
  EXPECT_EQ(6u, analysis.fields()[0].end);

  // A multibyte fill character is standard syntax.
  analysis = fmt::analyze_format_string("{:\xe4\xb8\x96>5}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  EXPECT_TRUE(analysis.fields()[0].standard_specs);
  EXPECT_EQ(2u, analysis.fields()[0].spec_begin);
  EXPECT_EQ(7u, analysis.fields()[0].spec_end);
  EXPECT_NO_THROW((void)fmt::format(fmt::runtime("{:\xe4\xb8\x96>5}"), 42));
}

TEST(analyze_test, custom_spec_is_type_dependent) {
  custom_parse_called = false;
  auto analysis = fmt::analyze_format_string("{:custom-spec!}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  const auto& field = analysis.fields()[0];
  // The raw spec is preserved and marked as not known to be valid.
  EXPECT_FALSE(field.standard_specs);
  EXPECT_EQ(2u, field.spec_begin);
  EXPECT_EQ(14u, field.spec_end);
  // Analysis does not invoke the formatter.
  EXPECT_FALSE(custom_parse_called);
  // The same spec may still be valid for a custom formatter.
  (void)fmt::format(fmt::runtime("{:custom-spec!}"), custom_type());
  EXPECT_TRUE(custom_parse_called);
}

TEST(analyze_test, nested_ref_in_custom_spec) {
  auto analysis = fmt::analyze_format_string("{:{x}y}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  const auto& field = analysis.fields()[0];
  EXPECT_FALSE(field.standard_specs);
  ASSERT_EQ(1u, field.refs.size());
  EXPECT_EQ(analyzed_ref_role::nested, field.refs[0].role);
  EXPECT_EQ(2u, field.refs[0].offset);
  expect_arg_name(field.refs[0].arg, "x");
}

TEST(analyze_test, standard_specs_match_formatting) {
  struct test_case {
    const char* fmt;
    const char* expected;
  };
  // Every spec recognized as standard must format without errors.
  EXPECT_EQ("42", fmt::format("{:d}", 42));
  EXPECT_EQ("+0042", fmt::format("{:+05d}", 42));
  EXPECT_EQ("0x2a", fmt::format("{:#x}", 42));
  EXPECT_EQ("3.14", fmt::format("{:.2f}", 3.14159));
  EXPECT_EQ("    hi", fmt::format("{:>6}", "hi"));
  EXPECT_EQ("h", fmt::format("{:.1}", "hi"));
  const char* standard[] = {"{:d}", "{:+05d}", "{:#x}", "{:.2f}",
                            "{:>6}", "{:.1}",   "{:{}}", "{:.{}}"};
  for (auto fmt_str : standard) {
    auto analysis = fmt::analyze_format_string(fmt_str);
    ASSERT_TRUE(analysis.ok()) << fmt_str;
    ASSERT_EQ(1u, analysis.fields().size()) << fmt_str;
    EXPECT_TRUE(analysis.fields()[0].standard_specs) << fmt_str;
  }
  // A non-standard spec is type-dependent and rejected for built-in types.
  auto analysis = fmt::analyze_format_string("{:q}");
  ASSERT_TRUE(analysis.ok());
  EXPECT_FALSE(analysis.fields()[0].standard_specs);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:q}"), 42), fmt::format_error);
  // A standard spec can still fail type checking at format time.
  analysis = fmt::analyze_format_string("{:d}");
  EXPECT_TRUE(analysis.fields()[0].standard_specs);
  EXPECT_THROW((void)fmt::format(fmt::runtime("{:d}"), "x"), fmt::format_error);
}

TEST(analyze_test, max_fields_limit) {
  auto analysis = fmt::analyze_format_string("{} {}", 1);
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(3u, analysis.error_offset());
  EXPECT_EQ("too many replacement fields in format string",
            analysis.error_message());

  analysis = fmt::analyze_format_string("{} {}", 2);
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  EXPECT_EQ(2u, analysis.fields().size());

  analysis = fmt::analyze_format_string("{}", 0);
  EXPECT_FALSE(analysis.ok());
  EXPECT_EQ(0u, analysis.error_offset());
}

TEST(analyze_test, named_args_and_compile_unchanged) {
  // Named argument identities are reported; normal formatting is unchanged.
  auto analysis = fmt::analyze_format_string("{answer}");
  ASSERT_TRUE(analysis.ok()) << analysis.error_message();
  ASSERT_EQ(1u, analysis.fields().size());
  expect_arg_name(analysis.fields()[0].arg, "answer");
  EXPECT_EQ("42", fmt::format("{answer}", fmt::arg("answer", 42)));
  // FMT_COMPILE behavior is unchanged.
  EXPECT_EQ("42", fmt::format(FMT_COMPILE("{:d}"), 42));
}
