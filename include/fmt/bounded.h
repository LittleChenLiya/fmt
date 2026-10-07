// Formatting library for C++ - field-boundary formatting with a byte budget
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_BOUNDED_H_
#define FMT_BOUNDED_H_

#include <cstring>  // std::memcpy
#include <string>   // std::string, std::to_string

#include "format.h"

namespace fmt {

/**
 * The result of `format_to_bounded`.
 */
struct bounded_format_result {
  /// Pointer to just after the last written byte in the output buffer.
  char* out;
  /// The number of replacement fields that were fully written. When
  /// `truncated` is true and formatting stopped at a field, this is the
  /// index of the field that did not fit.
  size_t fields_written;
  /// Whether formatting stopped before consuming the whole format string.
  bool truncated;
};

namespace detail {

enum class bounded_indexing { none, automatic, manual };

inline auto is_bounded_name_start(char c) -> bool {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

// Rewrites the replacement field starting at fmt[pos] (which must be '{'),
// making automatic argument ids explicit so that the field can be formatted
// on its own with the original argument list. Appends the rewritten field to
// `out` and returns the position one past the closing '}'.
inline auto rewrite_field(string_view fmt, size_t pos, size_t& next_arg_id,
                          bounded_indexing& indexing, std::string& out,
                          int depth = 0) -> size_t {
  enum { max_nesting = 16 };
  if (depth > max_nesting) report_error("invalid format string");
  size_t n = fmt.size();
  size_t i = pos + 1;
  out.push_back('{');
  if (i == n) report_error("unmatched '{' in format string");
  char c = fmt[i];
  if (c == ':' || c == '}') {
    // Automatic argument indexing.
    if (indexing == bounded_indexing::manual)
      report_error("cannot switch from manual to automatic argument indexing");
    indexing = bounded_indexing::automatic;
    out += std::to_string(next_arg_id++);
  } else if (c >= '0' && c <= '9') {
    // Manual argument indexing.
    if (indexing == bounded_indexing::automatic)
      report_error("cannot switch from automatic to manual argument indexing");
    indexing = bounded_indexing::manual;
    do {
      out.push_back(fmt[i++]);
    } while (i != n && fmt[i] >= '0' && fmt[i] <= '9');
  } else if (is_bounded_name_start(c)) {
    // A named argument.
    do {
      out.push_back(fmt[i++]);
    } while (i != n &&
             (is_bounded_name_start(fmt[i]) ||
              (fmt[i] >= '0' && fmt[i] <= '9')));
  } else {
    report_error("invalid format string");
  }
  if (i == n) report_error("unmatched '{' in format string");
  c = fmt[i];
  if (c == '}') {
    out.push_back('}');
    return i + 1;
  }
  if (c != ':') report_error("invalid format string");
  out.push_back(':');
  ++i;
  while (i != n) {
    c = fmt[i];
    if (c == '}') {
      out.push_back('}');
      return i + 1;
    }
    if (c == '{') {
      // A nested replacement field, e.g. a dynamic width or precision.
      i = rewrite_field(fmt, i, next_arg_id, indexing, out, depth + 1);
      continue;
    }
    out.push_back(c);
    ++i;
  }
  report_error("unmatched '{' in format string");
  return n;  // Unreachable, silences a compiler warning.
}
}  // namespace detail

/**
 * Formats `args` according to `fmt` and writes the result to the buffer
 * pointed to by `out`, stopping at a replacement-field boundary once the
 * UTF-8 byte budget `budget` is exhausted.
 *
 * Output is produced in atomic units: literal text segments between
 * replacement fields and whole replacement fields. A unit is written only if
 * it fits in the remaining budget in its entirety; otherwise formatting
 * stops. As a result a multi-byte UTF-8 sequence (such as an emoji or a CJK
 * character) is never split and `[out, result.out)` is valid UTF-8 provided
 * the format string and the formatted arguments are valid UTF-8.
 *
 * The replacement field that does not fit may have its formatter invoked
 * once, but its output is discarded; replacement fields after it are never
 * formatted. Bytes in the buffer past `result.out` are left in an
 * unspecified state.
 *
 * **Example**:
 *
 *     char buf[16];
 *     auto result = fmt::format_to_bounded(buf, sizeof(buf), "{}+{}", 1, 2);
 *     // result == {buf + 3, 2, false} and buf starts with "1+2".
 */
inline auto vformat_to_bounded(char* out, size_t budget, string_view fmt,
                               format_args args) -> bounded_format_result {
  size_t n = fmt.size();
  size_t pos = 0;
  size_t used = 0;
  size_t fields_written = 0;
  size_t next_arg_id = 0;
  auto indexing = detail::bounded_indexing::none;
  std::string text;   // Pending literal text segment with escapes resolved.
  std::string field;  // The current replacement field, ids made explicit.

  auto result = [&](bool truncated) {
    return bounded_format_result{out + used, fields_written, truncated};
  };
  // Commits the pending literal text segment if it fits. Returns false if
  // it doesn't fit, in which case nothing is written.
  auto commit_text = [&]() -> bool {
    if (text.size() > budget - used) return false;
    if (!text.empty()) {
      std::memcpy(out + used, text.data(), text.size());
      used += text.size();
    }
    text.clear();
    return true;
  };

  while (pos != n) {
    char c = fmt[pos];
    if (c == '{' && pos + 1 != n && fmt[pos + 1] == '{') {
      text.push_back('{');
      pos += 2;
      continue;
    }
    if (c == '}' && pos + 1 != n && fmt[pos + 1] == '}') {
      text.push_back('}');
      pos += 2;
      continue;
    }
    if (c == '}') report_error("unmatched '}' in format string");
    if (c == '{') {
      if (!commit_text()) return result(true);
      field.clear();
      pos = detail::rewrite_field(fmt, pos, next_arg_id, indexing, field);
      size_t remaining = budget - used;
      // Format the field directly into the output with a hard limit. The
      // total (untruncated) size is reported back, so an oversized field is
      // detected without allocating storage for it and can be rolled back by
      // simply not advancing past the already committed prefix.
      auto res =
          vformat_to_n(out + used, remaining, string_view(field), args);
      if (res.size > remaining) return result(true);
      used += res.size;
      ++fields_written;
      continue;
    }
    text.push_back(c);
    ++pos;
  }
  if (!commit_text()) return result(true);
  return result(false);
}

/**
 * Formats `args` according to `fmt` and writes the result to the buffer
 * pointed to by `out`, stopping at a replacement-field boundary once the
 * UTF-8 byte budget `budget` is exhausted. See `vformat_to_bounded` for
 * details.
 */
template <typename... T>
FMT_INLINE auto format_to_bounded(char* out, size_t budget,
                                  format_string<T...> fmt, T&&... args)
    -> bounded_format_result {
  return vformat_to_bounded(out, budget, fmt.str, vargs<T...>{{args...}});
}

}  // namespace fmt

#endif  // FMT_BOUNDED_H_
