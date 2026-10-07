// Formatting library for C++ - field-boundary formatting
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_BOUNDARY_H_
#define FMT_BOUNDARY_H_

#ifndef FMT_MODULE
#  include <cstddef>  // size_t
#  include <string>   // std::string
#endif

#include "format.h"

FMT_BEGIN_NAMESPACE
FMT_BEGIN_EXPORT

/**
 * The result of field-boundary formatting, see `format_to_boundary`.
 *
 * `format_to_boundary` formats into a byte budget: it writes literal text
 * and replacement fields only as long as they fit in the budget completely.
 * A replacement field is all-or-nothing - it is never partially written -
 * and literal text is only cut at a UTF-8 code point boundary, so the
 * output is always a valid UTF-8 prefix of the full formatting result.
 */
template <typename OutputIt> struct format_boundary_result {
  OutputIt out;  ///< An iterator past the end of the written prefix.
  size_t size = 0;  ///< The number of code units (bytes) written.
  size_t fields_completed = 0;  ///< Replacement fields written in full.
  /// The index of the replacement field that did not fit. Only meaningful
  /// when `stopped_at_field` is true.
  size_t stop_field = 0;
  /// Whether formatting stopped before the format string was fully consumed.
  bool truncated = false;
  /// True if formatting stopped at a replacement field, false if it stopped
  /// inside literal text.
  bool stopped_at_field = false;

  explicit format_boundary_result(OutputIt out) : out(out) {}
};

namespace detail {

// Returns the end of the longest prefix of [begin, end) that consists of
// whole UTF-8 code points and fits in `budget` bytes.
inline auto fit_utf8_prefix(const char* begin, const char* end, size_t budget)
    -> const char* {
  auto p = begin;
  while (p != end) {
    auto c = static_cast<unsigned char>(*p);
    size_t len = 1;
    if (c >= 0xC0 && c < 0xE0)
      len = 2;
    else if (c >= 0xE0 && c < 0xF0)
      len = 3;
    else if (c >= 0xF0 && c < 0xF8)
      len = 4;
    if (static_cast<size_t>(end - p) < len || len > budget) break;
    budget -= len;
    p += len;
  }
  return p;
}

// Scans format specs without formatting anything and returns a pointer to
// the closing '}' of the replacement field or `end` if there is none.
inline auto skip_format_specs(const char* begin, const char* end)
    -> const char* {
  auto p = begin;
  while (p != end) {
    char c = *p++;
    if (c == '{') {
      // A dynamic width/precision: skip to its closing '}'.
      while (p != end && *p != '}') ++p;
      if (p != end) ++p;
    } else if (c == '}') {
      return p - 1;
    }
  }
  return end;
}

// A format string handler that writes output only while it fits in a byte
// budget. Each replacement field is first formatted into a staging buffer
// and is written to the output only if it fits completely. Once something
// does not fit, subsequent fields are not formatted at all.
template <typename OutputIt> class boundary_handler {
 private:
  memory_buffer field_buf_;  // A staging buffer for the current field.

 public:
  parse_context<> parse_ctx;
  context ctx;  // Formats fields into field_buf_.
  OutputIt out;
  size_t budget;
  size_t size = 0;
  size_t fields_completed = 0;
  size_t stop_field = 0;
  size_t field_index = 0;  // The index of the current replacement field.
  bool truncated = false;
  bool stopped_at_field = false;

  boundary_handler(string_view fmt, format_args args, OutputIt out,
                   size_t budget)
      : parse_ctx(fmt),
        ctx(appender(field_buf_), args),
        out(out),
        budget(budget) {}

  void on_text(const char* begin, const char* end) {
    if (truncated) return;
    auto fit = fit_utf8_prefix(begin, end, budget);
    out = copy_noinline<char>(begin, fit, out);
    auto n = static_cast<size_t>(fit - begin);
    budget -= n;
    size += n;
    if (fit != end) truncated = true;  // Stopped inside literal text.
  }

  auto on_arg_id() -> int { return parse_ctx.next_arg_id(); }
  auto on_arg_id(int id) -> int {
    parse_ctx.check_arg_id(id);
    return id;
  }
  auto on_arg_id(string_view id) -> int {
    parse_ctx.check_arg_id(id);
    int arg_id = ctx.arg_id(id);
    if (arg_id < 0) report_error("argument not found");
    return arg_id;
  }

  void on_replacement_field(int id, const char*) {
    if (truncated) return;
    field_buf_.clear();
    ctx.arg(id).visit(default_arg_formatter<char>{ctx.out()});
    flush_field();
  }

  auto on_format_specs(int id, const char* begin, const char* end)
      -> const char* {
    if (truncated) return skip_format_specs(begin, end);
    field_buf_.clear();
    auto arg = ctx.arg(id);
    if (!arg) report_error("argument not found");
    // Not using a visitor for custom types gives better codegen.
    if (arg.format_custom(begin, parse_ctx, ctx)) {
      flush_field();
      return parse_ctx.begin();
    }
    auto specs = dynamic_format_specs<char>();
    begin = parse_format_specs(begin, end, specs, parse_ctx, arg.type());
    if (specs.dynamic()) {
      handle_dynamic_spec(specs.dynamic_width(), specs.width, specs.width_ref,
                          ctx);
      handle_dynamic_spec(specs.dynamic_precision(), specs.precision,
                          specs.precision_ref, ctx);
    }
    arg.visit(arg_formatter<char>{ctx.out(), specs, ctx.locale()});
    flush_field();
    return begin;
  }

  FMT_NORETURN void on_error(const char* message) { report_error(message); }

 private:
  void flush_field() {
    if (field_buf_.size() <= budget) {
      out = copy_noinline<char>(field_buf_.data(),
                                field_buf_.data() + field_buf_.size(), out);
      budget -= field_buf_.size();
      size += field_buf_.size();
      ++fields_completed;
    } else {
      truncated = true;
      stopped_at_field = true;
      stop_field = field_index;
    }
    ++field_index;
  }
};
}  // namespace detail

/**
 * Formats `args` according to `fmt` and writes the output to `out`, stopping
 * when the next literal text fragment or replacement field would exceed the
 * UTF-8 byte budget of `budget` bytes.
 *
 * Replacement fields are written completely or not at all and literal text
 * is only cut at a UTF-8 code point boundary, so the output is always a
 * valid UTF-8 prefix of the full formatting result. Fields past the
 * stopping point are not formatted.
 *
 * Returns a `format_boundary_result` describing how much was written and,
 * if formatting stopped at a replacement field, which field did not fit.
 */
template <typename OutputIt>
FMT_NODISCARD inline auto vformat_to_boundary(OutputIt out, size_t budget,
                                              string_view fmt,
                                              format_args args)
    -> format_boundary_result<OutputIt> {
  auto handler = detail::boundary_handler<OutputIt>(fmt, args, out, budget);
  detail::parse_format_string(fmt, handler);
  auto result = format_boundary_result<OutputIt>(handler.out);
  result.size = handler.size;
  result.fields_completed = handler.fields_completed;
  result.stop_field = handler.stop_field;
  result.truncated = handler.truncated;
  result.stopped_at_field = handler.stopped_at_field;
  return result;
}

template <typename OutputIt, typename... T,
          FMT_ENABLE_IF(detail::is_output_iterator<OutputIt, char>::value)>
FMT_NODISCARD inline auto format_to_boundary(OutputIt out, size_t budget,
                                             format_string<T...> fmt,
                                             T&&... args)
    -> format_boundary_result<OutputIt> {
  return vformat_to_boundary(out, budget, fmt.str, vargs<T...>{{args...}});
}

/// The result of `format_boundary`, a string-returning counterpart of
/// `format_to_boundary`.
struct format_boundary_string_result {
  std::string str;  ///< The written prefix, always valid UTF-8.
  size_t fields_completed = 0;  ///< Replacement fields written in full.
  /// The index of the replacement field that did not fit. Only meaningful
  /// when `stopped_at_field` is true.
  size_t stop_field = 0;
  /// Whether formatting stopped before the format string was fully consumed.
  bool truncated = false;
  /// True if formatting stopped at a replacement field, false if it stopped
  /// inside literal text.
  bool stopped_at_field = false;
};

/**
 * Formats `args` according to `fmt` into a string of at most `budget` bytes,
 * stopping when the next literal text fragment or replacement field would
 * exceed the budget. See `format_to_boundary` for details.
 */
template <typename... T>
FMT_NODISCARD inline auto format_boundary(size_t budget,
                                          format_string<T...> fmt,
                                          T&&... args)
    -> format_boundary_string_result {
  auto result = format_boundary_string_result();
  auto res = vformat_to_boundary(std::back_inserter(result.str), budget,
                                 fmt.str, vargs<T...>{{args...}});
  result.fields_completed = res.fields_completed;
  result.stop_field = res.stop_field;
  result.truncated = res.truncated;
  result.stopped_at_field = res.stopped_at_field;
  return result;
}

FMT_END_EXPORT
FMT_END_NAMESPACE

#endif  // FMT_BOUNDARY_H_
