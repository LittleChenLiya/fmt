// Format string contract analysis: inspects the replacement fields of a
// runtime format string without any format arguments.

#ifndef FMT_ANALYZE_H_
#define FMT_ANALYZE_H_

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "format.h"

FMT_BEGIN_NAMESPACE

/// The kind of an argument identity referenced from a format string.
enum class analyzed_arg_kind {
  none,      // No argument.
  automatic, // Automatic positional indexing, e.g. "{}".
  manual,    // Manual positional indexing, e.g. "{0}".
  name       // A named argument, e.g. "{name}".
};

/// An argument identity referenced by a replacement field or a dynamic spec.
struct analyzed_arg {
  analyzed_arg_kind kind = analyzed_arg_kind::none;
  int index = -1;    // Argument index for the automatic/manual kinds.
  std::string name;  // Argument name for the name kind.
};

/// The role of a dynamic argument reference inside a replacement field.
enum class analyzed_ref_role {
  width,     // Dynamic width, e.g. "{:{width}}".
  precision, // Dynamic precision, e.g. "{:.{precision}}".
  nested     // A nested reference inside a type-dependent (non-standard) spec.
};

/// A dynamic argument reference inside a replacement field.
struct analyzed_ref {
  analyzed_ref_role role = analyzed_ref_role::nested;
  analyzed_arg arg;
  size_t offset = 0;  // Byte offset of the opening brace of the reference.
};

/// A single replacement field of a format string.
struct analyzed_field {
  size_t begin = 0;       // Byte offset of the opening brace.
  size_t end = 0;         // Byte offset one past the closing brace.
  size_t spec_begin = 0;  // Byte range of the raw format spec text, empty
  size_t spec_end = 0;    // when the field has no colon.
  analyzed_arg arg;       // Identity of the formatted argument.
  // True if the spec was recognized as standard format spec syntax for at
  // least one built-in argument type. Even then, validity for the actual
  // argument type is only fully checked at format time. False means the spec
  // is type-dependent: it is preserved verbatim and may be valid for a custom
  // formatter, but it is not known to be valid.
  bool standard_specs = false;
  std::vector<analyzed_ref> refs;  // Dynamic width/precision references.
};

namespace detail {
class format_string_analyzer;
}  // namespace detail

/// The result of analyzing a format string. It is an immutable snapshot that
/// can be queried any number of times and outlives the analyzed string.
class format_analysis {
 public:
  /// The replacement fields in order of appearance. Escaped braces ("{{" and
  /// "}}") are literal text and are not reported as fields.
  auto fields() const -> const std::vector<analyzed_field>& {
    return fields_;
  }
  /// Whether the format string is free of definite syntax errors. Errors
  /// that depend on argument types are not reported here; they are left to
  /// type checking at format time.
  auto ok() const -> bool { return error_message_.empty(); }
  /// The byte offset of the error in the format string, valid when !ok().
  auto error_offset() const -> size_t { return error_offset_; }
  /// The error message, valid when !ok().
  auto error_message() const -> const std::string& { return error_message_; }

 private:
  friend class detail::format_string_analyzer;
  std::vector<analyzed_field> fields_;
  size_t error_offset_ = 0;
  std::string error_message_;
};

namespace detail {

class format_string_analyzer {
 private:
  parse_context<char> ctx_;
  size_t max_fields_;
  const char* fmt_begin_;
  const char* fmt_end_;
  const char* field_begin_;  // The opening brace of the field being parsed.
  const char* scan_pos_;     // Where literal-text scanning last resumed.
  analyzed_arg pending_arg_;
  bool failed_ = false;

  auto offset_of(const char* p) const -> size_t {
    return static_cast<size_t>(p - fmt_begin_);
  }

  void set_error(const char* message, const char* pos) {
    if (failed_) return;
    failed_ = true;
    analysis_.error_offset_ = offset_of(pos);
    analysis_.error_message_ = message;
  }

  struct nested_ref_recorder {
    analyzed_arg& arg;
    parse_context<char>& ctx;
    void on_index(int id) {
      ctx.check_arg_id(id);
      arg.kind = analyzed_arg_kind::manual;
      arg.index = id;
    }
    void on_name(string_view name) {
      ctx.check_arg_id(name);
      arg.kind = analyzed_arg_kind::name;
      arg.name = std::string(name.data(), name.size());
    }
  };

  auto make_ref(analyzed_ref_role role, arg_id_kind kind,
                const arg_ref<char>& ref, const char* open) const
      -> analyzed_ref {
    auto result = analyzed_ref();
    result.role = role;
    result.offset = offset_of(open);
    if (kind == arg_id_kind::name) {
      result.arg.kind = analyzed_arg_kind::name;
      result.arg.name = std::string(ref.name.data(), ref.name.size());
    } else {
      // Distinguish "{}" from "{0}" by the source text.
      result.arg.kind = open[1] == '}' ? analyzed_arg_kind::automatic
                                       : analyzed_arg_kind::manual;
      result.arg.index = ref.index;
    }
    return result;
  }

  // Records width/precision references of a standard spec and finds their
  // offsets by locating the brace groups in the spec text.
  void add_standard_refs(analyzed_field& field, const char* spec_begin,
                         const char* spec_end,
                         const dynamic_format_specs<char>& specs) const {
    std::vector<const char*> groups;
    for (auto p = spec_begin; p != spec_end; ++p) {
      if (*p != '{') continue;
      auto close = p + 1;
      while (close != spec_end && *close != '}') ++close;
      if (close == spec_end) break;
      groups.push_back(p);
      p = close;
    }
    size_t group_index = 0;
    if (specs.dynamic_width() != arg_id_kind::none &&
        group_index < groups.size()) {
      field.refs.push_back(make_ref(analyzed_ref_role::width,
                                    specs.dynamic_width(), specs.width_ref,
                                    groups[group_index++]));
    }
    if (specs.dynamic_precision() != arg_id_kind::none &&
        group_index < groups.size()) {
      field.refs.push_back(make_ref(analyzed_ref_role::precision,
                                    specs.dynamic_precision(),
                                    specs.precision_ref,
                                    groups[group_index++]));
    }
  }

  // Scans a type-dependent (non-standard) spec for the closing brace of the
  // field, recording nested "{...}" argument references on the way.
  auto scan_nested_specs(const char* p, const char* end,
                         analyzed_field& field) -> const char* {
    int depth = 0;
    auto ctx_copy = ctx_;
    while (p != end) {
      if (*p == '}') {
        if (depth == 0) {
          ctx_ = ctx_copy;
          return p;
        }
        --depth;
        ++p;
        continue;
      }
      if (*p == '{') {
        auto ref = analyzed_ref();
        ref.role = analyzed_ref_role::nested;
        ref.offset = offset_of(p);
        auto q = p + 1;
        bool ok = false;
        try {
          if (q != end && *q == '}') {
            ref.arg.kind = analyzed_arg_kind::automatic;
            ref.arg.index = ctx_copy.next_arg_id();
            ++q;
            ok = true;
          } else {
            auto recorder = nested_ref_recorder{ref.arg, ctx_copy};
            auto r = parse_arg_id(q, end, recorder);
            if (r != end && *r == '}') {
              q = r + 1;
              ok = true;
            }
          }
        } catch (const format_error&) {
          ok = false;
        }
        if (ok) {
          field.refs.push_back(ref);
          p = q;
          continue;
        }
        ++depth;
        ++p;
        continue;
      }
      ++p;
    }
    return end;
  }

  void add_field(analyzed_field&& field, const char* field_end) {
    if (failed_) return;
    if (analysis_.fields_.size() >= max_fields_) {
      set_error("too many replacement fields in format string", field_begin_);
      return;
    }
    field.begin = offset_of(field_begin_);
    field.end = offset_of(field_end);
    field.arg = pending_arg_;
    pending_arg_ = analyzed_arg();
    analysis_.fields_.push_back(std::move(field));
    scan_pos_ = field_end;
  }

 public:
  format_analysis analysis_;

  format_string_analyzer(string_view fmt, size_t max_fields)
      : ctx_(fmt),
        max_fields_(max_fields),
        fmt_begin_(fmt.data()),
        fmt_end_(fmt.data() + fmt.size()),
        field_begin_(fmt.data()),
        scan_pos_(fmt.data()) {}

  void on_text(const char*, const char* end) {
    // "end" is the position of the next field opening brace or the string
    // end.
    field_begin_ = end;
    scan_pos_ = end;
  }

  auto on_arg_id() -> int {
    int id = ctx_.next_arg_id();
    pending_arg_.kind = analyzed_arg_kind::automatic;
    pending_arg_.index = id;
    return id;
  }
  auto on_arg_id(int id) -> int {
    ctx_.check_arg_id(id);
    pending_arg_.kind = analyzed_arg_kind::manual;
    pending_arg_.index = id;
    return id;
  }
  auto on_arg_id(string_view name) -> int {
    ctx_.check_arg_id(name);
    pending_arg_.kind = analyzed_arg_kind::name;
    pending_arg_.name = std::string(name.data(), name.size());
    return 0;
  }

  void on_replacement_field(int, const char* begin) {
    auto field = analyzed_field();
    field.standard_specs = true;  // No spec is trivially standard.
    add_field(std::move(field), begin + 1);
  }

  auto on_format_specs(int, const char* begin, const char* end)
      -> const char* {
    if (failed_) return end;
    auto field = analyzed_field();
    field.spec_begin = offset_of(begin);

    // Try the standard spec parser with each built-in argument type since
    // the actual type is unknown. Type-dependent failures are not errors.
    static const type candidate_types[] = {
        type::int_type,     type::uint_type,   type::bool_type,
        type::char_type,    type::double_type, type::long_double_type,
        type::cstring_type, type::string_type, type::pointer_type};
    const char* spec_end = end;
    for (auto candidate : candidate_types) {
      auto ctx_copy = ctx_;
      auto specs = dynamic_format_specs<char>();
      const char* p = nullptr;
      try {
        p = parse_format_specs(begin, end, specs, ctx_copy, candidate);
      } catch (const format_error&) {
        continue;
      }
      // A standard parse must consume the whole spec up to the closing
      // brace. Trailing text means the spec is not standard.
      if (p == end || *p != '}') {
        if (p == end) spec_end = end;
        continue;
      }
      spec_end = p;
      ctx_ = ctx_copy;
      field.standard_specs = true;
      add_standard_refs(field, begin, p, specs);
      break;
    }
    if (!field.standard_specs)
      spec_end = scan_nested_specs(begin, end, field);

    field.spec_end = offset_of(spec_end);
    if (spec_end == end) {
      set_error("missing '}' in format string", end);
      return end;
    }
    add_field(std::move(field), spec_end + 1);
    return spec_end;
  }

  void on_error(const char* message) {
    const char* pos = field_begin_;
    if (std::strcmp(message, "unmatched '}' in format string") == 0) {
      pos = scan_pos_;
      while (pos != fmt_end_ && *pos != '}') ++pos;
    }
    set_error(message, pos);
  }

  void on_exception(const char* message) { set_error(message, field_begin_); }
};

}  // namespace detail

/**
  Analyzes a runtime format string without formatting arguments.

  Returns an immutable snapshot listing every replacement field with its
  UTF-8 byte range, argument identity, raw format spec and dynamic
  width/precision references. Definite syntax errors are reported with their
  byte offset; anything that depends on argument types is left to type
  checking at format time. At most max_fields fields are reported. Neither
  custom formatters nor format arguments are invoked or accessed.
  */
FMT_NODISCARD inline auto analyze_format_string(string_view format_str,
                                                size_t max_fields = 1024)
    -> format_analysis {
  auto analyzer = detail::format_string_analyzer(format_str, max_fields);
  try {
    detail::parse_format_string(format_str, analyzer);
  } catch (const format_error& e) {
    analyzer.on_exception(e.what());
  }
  return std::move(analyzer.analysis_);
}

FMT_END_NAMESPACE

#endif  // FMT_ANALYZE_H_
