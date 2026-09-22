// Example: analyze a runtime format string and print its field tree and
// error offsets. Build (header-only):
//   c++ -std=c++11 -DFMT_HEADER_ONLY -I include example/analyze.cc

#include <iostream>
#include <string>

#include "fmt/analyze.h"

namespace {

void print_arg(const fmt::analyzed_arg& arg) {
  switch (arg.kind) {
  case fmt::analyzed_arg_kind::none:
    std::cout << "<none>";
    break;
  case fmt::analyzed_arg_kind::automatic:
    std::cout << "auto(" << arg.index << ")";
    break;
  case fmt::analyzed_arg_kind::manual:
    std::cout << "index(" << arg.index << ")";
    break;
  case fmt::analyzed_arg_kind::name:
    std::cout << "name(" << arg.name << ")";
    break;
  }
}

const char* role_name(fmt::analyzed_ref_role role) {
  switch (role) {
  case fmt::analyzed_ref_role::width: return "width";
  case fmt::analyzed_ref_role::precision: return "precision";
  case fmt::analyzed_ref_role::nested: return "nested";
  }
  return "?";
}

void analyze(const char* fmt_str) {
  std::cout << "format: \"" << fmt_str << "\"\n";
  auto analysis = fmt::analyze_format_string(fmt_str);
  if (!analysis.ok()) {
    std::cout << "  error at byte " << analysis.error_offset() << ": "
              << analysis.error_message() << "\n";
    return;
  }
  auto text = std::string(fmt_str);
  for (const auto& field : analysis.fields()) {
    std::cout << "  field [" << field.begin << ", " << field.end << ") arg=";
    print_arg(field.arg);
    if (field.spec_begin != field.spec_end)
      std::cout << " spec=\""
                << text.substr(field.spec_begin,
                               field.spec_end - field.spec_begin)
                << "\"";
    std::cout << (field.standard_specs ? " [standard]" : " [type-dependent]")
              << "\n";
    for (const auto& ref : field.refs) {
      std::cout << "    " << role_name(ref.role) << " ref at byte "
                << ref.offset << ": ";
      print_arg(ref.arg);
      std::cout << "\n";
    }
  }
}

}  // namespace

int main() {
  analyze("{name:>{width}} {0:.{1}f} {{ok}}");
  analyze("{} {:{}}");
  analyze("{0} {}");
  analyze("{:custom-spec!}");
  analyze("prefix } unmatched");
  return 0;
}
