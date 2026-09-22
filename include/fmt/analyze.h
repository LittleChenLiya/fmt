// Formatting library for C++ - format string contract analysis
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_ANALYZE_H_
#define FMT_ANALYZE_H_

#include <cstddef>
#include <string>
#include <vector>

#include "format.h"

FMT_BEGIN_NAMESPACE

/**
 * The kind of an argument reference found by format string analysis.
 */
enum class analyzed_arg_kind {
  automatic,  // "{}" - the argument is selected by automatic indexing.
  index,      // "{0}" - the argument is selected by a manual index.
  name        // "{name}" - the argument is selected by name.
};

/**
 * A reference to a format argument in a format string.
 *
 * The byte offsets and the name view refer to the analyzed format string,
 * which must outlive the analysis result.
 */
struct analyzed_arg_ref {
  analyzed_arg_kind kind = analyzed_arg_kind::automatic;
  // The 0-based argument index. Valid for the automatic and index kinds.
  size_t index = 0;
  // The argument name. Valid for the name kind only.
  string_view name;
  // The UTF-8 byte range of the argument id in the format string, excluding
  // any braces. Empty for automatic references.
  size_t begin = 0;
  size_t end = 0;
};

/**
 * A single replacement field found by format string analysis.
 *
 * All offsets are UTF-8 byte offsets into the analyzed format string, which
 * must outlive the analysis result.
 */
struct analyzed_field {
  // The byte range of the whole replacement field including the braces.
  size_t begin = 0;
  size_t end = 0;
  // The identity of the argument to be formatted.
  analyzed_arg_ref arg;
  // Whether the field has a format spec, i.e. a ':' followed by spec text.
  bool has_spec = false;
  // The byte range of the format spec text, excluding ':' and '}'.
  size_t spec_begin = 0;
  size_t spec_end = 0;
  // The raw format spec text.
  string_view spec;
  // True if the spec does not conform to the standard format spec grammar of
  // any built-in type, e.g. a spec consumed by a custom formatter. Such specs
  // are preserved as-is and their validity is left to type checking. Note
  // that even a standard-conforming spec may be invalid for a particular
  // argument type; that can only be diagnosed by type checking.
  bool spec_type_dependent = false;
  // Dynamic width/precision argument references, e.g. {:{width}}.
  bool has_dynamic_width = false;
  bool has_dynamic_precision = false;
  analyzed_arg_ref width_ref;
  analyzed_arg_ref precision_ref;
};

class format_string_analysis;

/**
 * Analyzes a format string without accessing any format arguments.
 *
 * Returns an immutable analysis result listing every replacement field
 * together with its byte range, argument identity, raw format spec and
 * dynamic width/precision references. Escaped braces ("{{" and "}}") are not
 * fields. Syntax errors that can be determined without argument types are
 * reported with a byte offset; checks that require argument types are left
 * to type checking. At most max_fields fields are analyzed.
 */
auto analyze_format_string(string_view fmt, size_t max_fields = ~size_t(0))
    -> format_string_analysis;

/**
 * The immutable result of format string analysis.
 */
class format_string_analysis {
 public:
  format_string_analysis() = default;

  /** Returns whether the format string was analyzed without errors. */
  auto ok() const -> bool { return error_message_.empty(); }

  /** Returns the byte offset of the error in the format string. */
  auto error_offset() const -> size_t { return error_offset_; }

  /** Returns the error message. Valid only if !ok(). */
  auto error_message() const -> const std::string& { return error_message_; }

  /** Returns the replacement fields in order of appearance. */
  auto fields() const -> const std::vector<analyzed_field>& { return fields_; }

 private:
  std::vector<analyzed_field> fields_;
  size_t error_offset_ = 0;
  std::string error_message_;

  friend auto analyze_format_string(string_view fmt, size_t max_fields)
      -> format_string_analysis;
};

namespace detail {

struct analysis_arg_handler {
  analyzed_arg_ref* ref;
  parse_context<char>* ctx;

  void on_index(int id) {
    ref->kind = analyzed_arg_kind::index;
    ref->index = static_cast<size_t>(id);
    ctx->check_arg_id(id);
  }
  void on_name(string_view name) {
    ref->kind = analyzed_arg_kind::name;
    ref->name = name;
    ctx->check_arg_id(name);
  }
};

// Checks whether a format spec conforms to the standard format spec grammar
// of at least one built-in type. Specs that don't conform may still be valid
// for a custom formatter, so they are not errors.
inline auto is_standard_format_spec(string_view spec) -> bool {
#if FMT_USE_EXCEPTIONS
  static const detail::type types[] = {
      detail::type::int_type,  detail::type::double_type,
      detail::type::cstring_type, detail::type::string_type,
      detail::type::char_type, detail::type::bool_type};
  for (detail::type t : types) {
    auto specs = detail::dynamic_format_specs<char>();
    auto ctx = parse_context<char>(spec);
    const char* begin = spec.data();
    const char* end = begin + spec.size();
    FMT_TRY {
      if (detail::parse_format_specs(begin, end, specs, ctx, t) == end)
        return true;
    }
    FMT_CATCH(const format_error&) {
      // The spec is not valid for this type; try the next one.
    }
  }
  return false;
#else
  // Probing can report errors and errors cannot be caught without
  // exceptions, so standard conformance cannot be verified and the spec is
  // treated as type-dependent.
  return false;
#endif
}

}  // namespace detail

inline auto analyze_format_string(string_view fmt, size_t max_fields)
    -> format_string_analysis {
  auto result = format_string_analysis();
  const char* const base = fmt.data();
  const char* const end = base + fmt.size();
  const char* p = base;
  // Reuse the standard parse context to enforce the automatic/manual
  // argument indexing rules.
  auto ctx = parse_context<char>(fmt);
  const char* cur = p;  // Position of the token being parsed, for errors.

  auto fail = [&](const char* message, const char* at) {
    result.error_offset_ = static_cast<size_t>(at - base);
    result.error_message_ = message;
  };

#if FMT_USE_EXCEPTIONS
  try {
#endif
    while (p != end) {
      char c = *p;
      if (c == '}') {
        if (p + 1 != end && p[1] == '}') {
          p += 2;  // Escaped brace, not a field.
          continue;
        }
        fail("unmatched '}' in format string", p);
        return result;
      }
      if (c != '{') {
        ++p;
        continue;
      }
      if (p + 1 != end && p[1] == '{') {
        p += 2;  // Escaped brace, not a field.
        continue;
      }
      if (result.fields_.size() >= max_fields) {
        fail("too many fields in format string", p);
        return result;
      }

      auto field = analyzed_field();
      field.begin = static_cast<size_t>(p - base);
      ++p;
      if (p == end) {
        fail("invalid format string", p - 1);
        return result;
      }

      // Parse the argument id.
      if (*p == '}' || *p == ':') {
        cur = p;
        field.arg.kind = analyzed_arg_kind::automatic;
        field.arg.index = static_cast<size_t>(ctx.next_arg_id());
        field.arg.begin = field.arg.end = static_cast<size_t>(p - base);
      } else {
        cur = p;
        const char* id_begin = p;
        p = detail::parse_arg_id(
            p, end,
            detail::analysis_arg_handler{&field.arg, &ctx});
        field.arg.begin = static_cast<size_t>(id_begin - base);
        field.arg.end = static_cast<size_t>(p - base);
        if (p == end) {
          fail("missing '}' in format string", p);
          return result;
        }
        if (*p != '}' && *p != ':') {
          fail("missing '}' in format string", p);
          return result;
        }
      }

      // Parse the format spec if present.
      if (*p == ':') {
        field.has_spec = true;
        ++p;
        field.spec_begin = static_cast<size_t>(p - base);
        // Scan the spec, recording dynamic width/precision references. A '{'
        // before the precision '.' introduces a width reference, one after it
        // a precision reference. Nested specs are not allowed.
        bool precision_dot = false;
        while (p != end && *p != '}') {
          if (*p == '.') precision_dot = true;
          if (*p != '{') {
            ++p;
            continue;
          }
          cur = p;
          ++p;
          if (p == end) {
            fail("invalid format string", p - 1);
            return result;
          }
          auto ref = analyzed_arg_ref();
          if (*p == '}') {
            ref.kind = analyzed_arg_kind::automatic;
            ref.index = static_cast<size_t>(ctx.next_arg_id());
            ref.begin = ref.end = static_cast<size_t>(p - base);
            ++p;
          } else {
            const char* id_begin = p;
            p = detail::parse_arg_id(
                p, end, detail::analysis_arg_handler{&ref, &ctx});
            ref.begin = static_cast<size_t>(id_begin - base);
            ref.end = static_cast<size_t>(p - base);
            if (p == end || *p != '}') {
              fail("invalid format string", p);
              return result;
            }
            ++p;
          }
          if (precision_dot) {
            if (!field.has_dynamic_precision) {
              field.has_dynamic_precision = true;
              field.precision_ref = ref;
            }
          } else if (!field.has_dynamic_width) {
            field.has_dynamic_width = true;
            field.width_ref = ref;
          }
        }
        if (p == end) {
          fail("missing '}' in format string", p);
          return result;
        }
        field.spec_end = static_cast<size_t>(p - base);
        field.spec = string_view(base + field.spec_begin,
                                 field.spec_end - field.spec_begin);
        field.spec_type_dependent =
            !detail::is_standard_format_spec(field.spec);
      }

      // p points at the closing brace.
      field.end = static_cast<size_t>(p - base) + 1;
      ++p;
      result.fields_.push_back(field);
    }
#if FMT_USE_EXCEPTIONS
  } catch (const format_error& e) {
    fail(e.what(), cur);
  }
#endif
  return result;
}

FMT_END_NAMESPACE

#endif  // FMT_ANALYZE_H_
