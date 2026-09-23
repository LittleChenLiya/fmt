// Formatting library for C++ - runtime output redaction
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_REDACT_H_
#define FMT_REDACT_H_

#include <initializer_list>
#include <vector>

#include "format.h"

namespace fmt {

/**
 * A set of format arguments to redact, specified by position and/or name.
 *
 * **Example**:
 *
 *     auto mask = fmt::redaction().at(1).named("password");
 *     auto s = fmt::redact_format("{0} {1} {password}", mask, user, secret,
 *                                 fmt::arg("password", token));
 *     // secret and token are never formatted; s contains "[redacted]"
 *     // in place of their values.
 */
class redaction {
 private:
  std::vector<size_t> positions_;
  std::vector<string_view> names_;

 public:
  redaction() = default;
  redaction(std::initializer_list<size_t> positions)
      : positions_(positions.begin(), positions.end()) {}

  /** Adds a positional argument index to the redaction set. */
  auto at(size_t index) -> redaction& {
    positions_.push_back(index);
    return *this;
  }

  /** Adds a named argument to the redaction set. */
  auto named(string_view name) -> redaction& {
    names_.push_back(name);
    return *this;
  }

  auto positions() const -> const std::vector<size_t>& { return positions_; }
  auto names() const -> const std::vector<string_view>& { return names_; }
};

namespace detail {

template <typename T>
auto redact_arg_name(const T&) -> string_view {
  return string_view();
}

template <typename T, typename Char>
auto redact_arg_name(const named_arg<T, Char>& arg) -> string_view {
  return string_view(arg.name);
}

// Validates a format string and a redaction set before any argument is
// formatted. Fields referring to unmasked arguments are checked with the
// formatters of the argument types, reusing the same parsing rules as
// fmt::format. Fields referring to redacted arguments are checked against a
// restricted grammar that only allows fill, alignment and width as in string
// format specs; formatters of redacted arguments are never constructed.
template <typename... T>
class redaction_checker {
 private:
  using parse_func = auto (*)(parse_context<>&) -> const char*;

  const bool* masked_;
  const string_view* names_;
  int num_args_;
  int next_arg_id_ = 0;
  parse_func parsers_[sizeof...(T) != 0 ? sizeof...(T) : 1];

  FMT_NORETURN void report(const char* message) const {
    FMT_THROW(format_error(message));
  }

  auto check_index(int id) const -> int {
    if (id < 0 || id >= num_args_) report("argument not found");
    return id;
  }

  void check_not_masked(int id) const {
    if (masked_[id])
      report("dynamic width or precision refers to a redacted argument");
  }

  // Parses a nested argument reference used by a dynamic width or precision.
  // The p iterator points past '{'.
  auto parse_nested_ref(const char*& p, const char* end) -> int {
    if (p == end) report("invalid format string");
    if (*p == '}') return on_arg_id();
    struct adapter {
      redaction_checker& self;
      int id;
      void on_index(int i) { id = self.on_arg_id(i); }
      void on_name(string_view n) { id = self.on_arg_id(n); }
    } adapt = {*this, 0};
    p = parse_arg_id(p, end, adapt);
    return adapt.id;
  }

  // Validates dynamic width/precision references in the range [begin, end).
  void check_nested_refs(const char* begin, const char* end) {
    for (auto p = begin; p != end; ++p) {
      if (*p != '{') continue;
      ++p;
      int id = parse_nested_ref(p, end);
      if (p == end || *p != '}') report("invalid format string");
      check_not_masked(id);
    }
  }

  // Parses the format specs of a redacted field, allowing only fill,
  // alignment and width as in string format specs.
  auto parse_redacted_specs(const char* begin, const char* end)
      -> const char* {
    auto p = begin;
    if (end - p > 1) {
      int fill_len = code_point_length(p);
      if (end - p > fill_len &&
          parse_align(to_ascii(p[fill_len])) != align::none) {
        if (*p == '{') report("invalid fill character '{'");
        p += fill_len + 1;
      }
    }
    if (p == begin && p != end && parse_align(to_ascii(*p)) != align::none)
      ++p;
    if (p != end && *p != '}') {
      if (*p >= '1' && *p <= '9') {
        do
          ++p;
        while (p != end && *p >= '0' && *p <= '9');
      } else if (*p == '{') {
        ++p;
        int id = parse_nested_ref(p, end);
        if (p == end || *p != '}') report("invalid format string");
        ++p;
        check_not_masked(id);
      } else {
        report("invalid format specifier for a redacted argument");
      }
    }
    if (p != end && *p != '}')
      report("invalid format specifier for a redacted argument");
    return p;
  }

 public:
  redaction_checker(const bool* masked, const string_view* names, int num_args)
      : masked_(masked),
        names_(names),
        num_args_(num_args),
        parsers_{&invoke_parse<T, char>...} {}

  void on_text(const char*, const char*) {}

  auto on_arg_id() -> int {
    if (next_arg_id_ < 0)
      report("cannot switch from manual to automatic argument indexing");
    return check_index(next_arg_id_++);
  }

  auto on_arg_id(int id) -> int {
    if (next_arg_id_ > 0)
      report("cannot switch from automatic to manual argument indexing");
    next_arg_id_ = -1;
    return check_index(id);
  }

  auto on_arg_id(string_view name) -> int {
    next_arg_id_ = -1;
    for (int i = 0; i < num_args_; ++i)
      if (names_[i] == name) return i;
    report("argument not found");
    return 0;
  }

  void on_replacement_field(int id, const char* begin) {
    if (masked_[id]) return;
    auto ctx = parse_context<>(string_view(begin, 0));
    parsers_[id](ctx);
  }

  auto on_format_specs(int id, const char* begin, const char* end)
      -> const char* {
    if (masked_[id]) return parse_redacted_specs(begin, end);
    auto ctx = parse_context<>(string_view(begin, size_t(end - begin)));
    const char* spec_end = parsers_[id](ctx);
    check_nested_refs(begin, spec_end);
    return spec_end;
  }

  FMT_NORETURN void on_error(const char* message) { report(message); }
};

// Formats arguments, replacing redacted arguments with a fixed "[redacted]"
// placeholder. Redacted arguments are never visited, so their formatters are
// neither constructed nor called and their values are never converted to
// strings.
class redacting_handler {
 private:
  parse_context<> parse_ctx_;
  format_context ctx_;
  const bool* masked_;

 public:
  redacting_handler(string_view fmt, appender out, format_args args,
                    const bool* masked)
      : parse_ctx_(fmt), ctx_(out, args), masked_(masked) {}

  void on_text(const char* begin, const char* end) {
    auto out = ctx_.out();
    for (; begin != end; ++begin) *out++ = *begin;
  }

  auto on_arg_id() -> int { return parse_ctx_.next_arg_id(); }
  auto on_arg_id(int id) -> int {
    parse_ctx_.check_arg_id(id);
    return id;
  }
  auto on_arg_id(string_view name) -> int {
    parse_ctx_.check_arg_id(name);
    int id = ctx_.arg_id(name);
    if (id < 0) FMT_THROW(format_error("argument not found"));
    return id;
  }

  void on_replacement_field(int id, const char*) {
    if (masked_[id]) {
      write<char>(ctx_.out(), string_view("[redacted]"));
      return;
    }
    ctx_.arg(id).visit(default_arg_formatter<char>{ctx_.out()});
  }

  auto on_format_specs(int id, const char* begin, const char* end)
      -> const char* {
    if (masked_[id]) {
      // The specs were validated by redaction_checker and only contain
      // string-applicable fill, alignment and width.
      auto specs = dynamic_format_specs<char>();
      begin =
          parse_format_specs(begin, end, specs, parse_ctx_, type::string_type);
      handle_dynamic_spec(specs.dynamic_width(), specs.width, specs.width_ref,
                          ctx_);
      write<char>(ctx_.out(), string_view("[redacted]"), specs);
      return begin;
    }
    auto arg = ctx_.arg(id);
    if (!arg) FMT_THROW(format_error("argument not found"));
    if (arg.format_custom(begin, parse_ctx_, ctx_)) return parse_ctx_.begin();
    auto specs = dynamic_format_specs<char>();
    begin = parse_format_specs(begin, end, specs, parse_ctx_, arg.type());
    if (specs.dynamic()) {
      handle_dynamic_spec(specs.dynamic_width(), specs.width, specs.width_ref,
                          ctx_);
      handle_dynamic_spec(specs.dynamic_precision(), specs.precision,
                          specs.precision_ref, ctx_);
    }
    arg.visit(arg_formatter<char>{ctx_.out(), specs, ctx_.locale()});
    return begin;
  }

  FMT_NORETURN void on_error(const char* message) {
    FMT_THROW(format_error(message));
  }
};
}  // namespace detail

/**
 * Formats args according to fmt_str, replacing every replacement field that
 * refers to an argument in mask with the fixed text "[redacted]".
 *
 * The format string is always treated as a runtime format string and only
 * char strings are supported. The whole format string and the redaction set
 * are parsed and validated before any argument is formatted, so errors
 * (unknown arguments, invalid indices or specs, unknown redaction names or
 * out-of-range redaction indices) are reported as fmt::format_error before
 * any user-defined formatter runs. A dynamic width or precision referring to
 * a redacted argument is rejected.
 *
 * Fields referring to unmasked arguments keep the regular fmt::format
 * semantics, including the parsing rules of user-defined formatters. For a
 * redacted argument only fill, alignment and width (as in string format
 * specs) are allowed and the argument's formatter is never constructed or
 * called, and its value is never converted to a string.
 *
 * **Example**:
 *
 *     auto s = fmt::redact_format("user={} token={}", fmt::redaction().at(1),
 *                                 "alice", "secret-token");
 *     // s == "user=alice token=[redacted]"
 */
template <typename... T>
auto redact_format(string_view fmt_str, const redaction& mask, T&&... args)
    -> std::string {
  enum { num_args = sizeof...(T) };
  const string_view arg_names[num_args != 0 ? num_args : 1] = {
      detail::redact_arg_name(args)...};
  bool masked[num_args != 0 ? num_args : 1] = {};
  for (size_t i : mask.positions()) {
    if (i >= size_t(num_args))
      FMT_THROW(format_error("redaction index out of range"));
    masked[i] = true;
  }
  for (string_view name : mask.names()) {
    size_t i = 0;
    for (; i != size_t(num_args); ++i)
      if (arg_names[i] == name) break;
    if (i == size_t(num_args))
      FMT_THROW(format_error("unknown redaction argument name"));
    masked[i] = true;
  }
  // Parse and validate the whole format string and the redaction set before
  // formatting any argument.
  auto checker = detail::redaction_checker<T...>(masked, arg_names, num_args);
  detail::parse_format_string<char>(fmt_str, checker);
  auto buf = memory_buffer();
  auto store = make_format_args(args...);
  auto handler =
      detail::redacting_handler(fmt_str, appender(buf), store, masked);
  detail::parse_format_string<char>(fmt_str, handler);
  return to_string(buf);
}
}  // namespace fmt

#endif  // FMT_REDACT_H_
