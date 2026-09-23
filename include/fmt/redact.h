// Formatting library for C++ - redacted (masked) formatting
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_REDACT_H_
#define FMT_REDACT_H_

#include <cstddef>
#include <initializer_list>

#include "format.h"

FMT_BEGIN_NAMESPACE

/**
 * A set of arguments to redact in `redacted_format`, given as positional
 * argument indexes and/or named argument names. Both forms refer to the same
 * underlying arguments: redacting an argument by name also redacts positional
 * references to it and vice versa.
 */
struct redaction {
  std::initializer_list<std::size_t> indexes;
  std::initializer_list<string_view> names;
};

namespace detail {

// Tracks automatic/manual argument indexing with the same semantics as
// parse_context while exposing the state, so that redaction decisions can be
// made for every replacement field before any formatting takes place.
struct redaction_id_tracker {
  int next_id;  // -1 denotes manual (or named) indexing mode.

  redaction_id_tracker() : next_id(0) {}

  auto next_auto() -> int {
    if (next_id < 0)
      report_error("cannot switch from manual to automatic argument indexing");
    return next_id++;
  }
  void use_manual() {
    if (next_id > 0)
      report_error("cannot switch from automatic to manual argument indexing");
    next_id = -1;
  }
  void use_named() { next_id = -1; }
};

// The first pass over the format string. It validates the whole format string
// and the redaction set before a single argument is formatted, so unknown
// arguments, bad indexes and invalid specs cannot trigger partial execution
// of user formatters.
//
// Custom formatter parse rules are reused exactly as in ordinary formatting:
// for each unmasked argument of custom type the validation pass constructs
// formatter<T> and calls its parse() on the field's spec range (via
// invoke_parse, the same helper used by fmt's own format string checker), and
// unmasked fields are later formatted by the ordinary format_handler. For a
// masked argument the formatter is never constructed, parsed or called, and
// the value is never converted to a string.
struct redaction_validator {
  using parse_func = auto (*)(parse_context<char>&) -> const char*;

  format_args args;
  int num_args;
  const bool* masked;
  const parse_func* parse_funcs;
  redaction_id_tracker tracker;

  void check_arg(int id) const {
    if (id < 0 || id >= num_args) report_error("argument not found");
  }
  void check_dynamic_ref(int id) const {
    check_arg(id);
    if (masked[id])
      report_error("dynamic width or precision refers to a redacted argument");
  }

  void on_text(const char*, const char*) {}

  auto on_arg_id() -> int {
    int id = tracker.next_auto();
    check_arg(id);
    return id;
  }
  auto on_arg_id(int id) -> int {
    tracker.use_manual();
    check_arg(id);
    return id;
  }
  auto on_arg_id(string_view id) -> int {
    tracker.use_named();
    int arg_id = args.get_id(id);
    if (arg_id < 0) report_error("argument not found");
    return arg_id;
  }

  void on_replacement_field(int id, const char* begin) {
    validate_specs(id, begin, begin);
  }

  auto on_format_specs(int id, const char* begin, const char* end)
      -> const char* {
    return validate_specs(id, begin, end);
  }

  FMT_NORETURN void on_error(const char* message) { report_error(message); }

  // Scans a spec range for dynamic width/precision references ("{...}") and
  // rejects ones referring to redacted arguments, so that a sensitive value
  // is never read to decide the output length. Returns the argument indexing
  // state to seed the spec parse context with.
  auto scan_dynamic_refs(const char* begin, const char* end) -> int {
    int seed = tracker.next_id;
    auto p = begin;
    while (p != end) {
      char c = *p;
      if (c == '}') break;
      if (c != '{') {
        ++p;
        continue;
      }
      ++p;
      if (p != end && *p == '}') {
        check_dynamic_ref(tracker.next_auto());
        ++p;
        continue;
      }
      struct adapter {
        redaction_validator& v;
        int id;
        void on_index(int i) {
          v.tracker.use_manual();
          id = i;
        }
        void on_name(string_view n) {
          v.tracker.use_named();
          id = v.args.get_id(n);
          if (id < 0) report_error("argument not found");
        }
      } ad{*this, 0};
      p = parse_arg_id(p, end, ad);
      if (p == end || *p != '}') report_error("invalid format string");
      check_dynamic_ref(ad.id);
      ++p;
    }
    return seed;
  }

  auto validate_specs(int id, const char* begin, const char* end)
      -> const char* {
    int seed = scan_dynamic_refs(begin, end);
    auto spec_ctx = parse_context<char>(
        string_view(begin, to_unsigned(end - begin)), seed);
    if (masked[id]) {
      // Masked fields only support the fill, alignment and width options
      // valid for strings. Parse with the string rules and reject anything
      // beyond them (precision and presentation type).
      auto specs = dynamic_format_specs<char>();
      auto p = parse_format_specs(begin, end, specs, spec_ctx,
                                  type::string_type);
      if (specs.precision >= 0 ||
          specs.dynamic_precision() != arg_id_kind::none ||
          specs.type() != presentation_type::none)
        report_error("invalid format specifier for redacted argument");
      return p;
    }
    if (args.get(id).type() == type::custom_type)
      return parse_funcs[id](spec_ctx);
    auto specs = dynamic_format_specs<char>();
    return parse_format_specs(begin, end, specs, spec_ctx,
                              args.get(id).type());
  }
};

// The second pass: ordinary fmt formatting except that masked replacement
// fields emit the fixed text "[redacted]" without touching the argument.
struct redacting_format_handler {
  format_handler<>& handler;
  const bool* masked;

  void on_text(const char* begin, const char* end) {
    handler.on_text(begin, end);
  }
  auto on_arg_id() -> int { return handler.on_arg_id(); }
  auto on_arg_id(int id) -> int { return handler.on_arg_id(id); }
  auto on_arg_id(string_view id) -> int { return handler.on_arg_id(id); }

  void on_replacement_field(int id, const char* begin) {
    if (!masked[id]) return handler.on_replacement_field(id, begin);
    write<char>(handler.ctx.out(), string_view("[redacted]"));
  }

  auto on_format_specs(int id, const char* begin, const char* end)
      -> const char* {
    if (!masked[id]) return handler.on_format_specs(id, begin, end);
    // The validation pass has already restricted the spec to the
    // string-compatible fill/alignment/width options.
    auto specs = dynamic_format_specs<char>();
    begin = parse_format_specs(begin, end, specs, handler.parse_ctx,
                               type::string_type);
    handle_dynamic_spec(specs.dynamic_width(), specs.width, specs.width_ref,
                        handler.ctx);
    write<char>(handler.ctx.out(), string_view("[redacted]"), specs);
    return begin;
  }

  FMT_NORETURN void on_error(const char* message) { report_error(message); }
};
}  // namespace detail

/**
 * Formats `args` according to `fmt` with the arguments selected by `mask`
 * replaced by the fixed text "[redacted]".
 *
 * The format string is parsed and validated in full, together with the
 * redaction set, before any argument is formatted. A redacted argument's
 * formatter is never constructed or invoked and the argument's value is never
 * converted to a string, so sensitive data cannot leak through formatting or
 * error messages. Errors are reported as `fmt::format_error`, same as in
 * ordinary formatting.
 *
 * Only `char` format strings are supported. Masked replacement fields
 * support the string-compatible fill, alignment and width format options;
 * any other option is rejected during validation.
 *
 * **Example**:
 *
 *     auto password = std::string("hunter2");
 *     // Prints "password: [redacted]".
 *     fmt::print("password: {}", fmt::redacted_format("{}", {{0}, {}},
 *                 password));
 */
template <typename... T>
FMT_NODISCARD inline auto redacted_format(string_view fmt,
                                          const redaction& mask, T&&... args)
    -> std::string {
  constexpr int num_args = sizeof...(T);
  auto arg_store = make_format_args(args...);
  format_args args_view(arg_store);

  bool masked[num_args != 0 ? num_args : 1] = {};
  for (std::size_t index : mask.indexes) {
    if (index >= static_cast<std::size_t>(num_args))
      report_error("redaction index out of range");
    masked[index] = true;
  }
  for (string_view name : mask.names) {
    int id = args_view.get_id(name);
    if (id < 0) report_error("unknown redaction target");
    masked[id] = true;
  }

  using parse_func = auto (*)(parse_context<char>&) -> const char*;
  const parse_func parse_funcs[num_args != 0 ? num_args : 1] = {
      &detail::invoke_parse<T, char>...};

  // Pass 1: validate the whole format string and the redaction set before
  // formatting anything.
  auto validator = detail::redaction_validator{
      args_view, num_args, masked, parse_funcs, detail::redaction_id_tracker()};
  detail::parse_format_string<char>(fmt, validator);

  // Pass 2: format, substituting "[redacted]" for masked arguments.
  auto buf = memory_buffer();
  auto handler =
      detail::format_handler<>{parse_context<>(fmt),
                               {appender(buf), args_view, locale_ref()}};
  auto redacting = detail::redacting_format_handler{handler, masked};
  detail::parse_format_string<char>(fmt, redacting);
  return to_string(buf);
}

FMT_END_NAMESPACE

#endif  // FMT_REDACT_H_
