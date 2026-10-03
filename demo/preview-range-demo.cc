// Demo for fmt::preview_range - preview formatting of the leading elements
// of a range, with support for single-pass input ranges.
//
// Delivery notes (交付说明):
//   * New API: fmt::preview_range(range, max_items), declared in
//     <fmt/ranges.h>. It formats at most max_items leading elements of a
//     range, reading at most max_items + 1 elements (the extra element is a
//     probe used only to detect truncation), so single-pass input ranges
//     are supported without collecting all elements first.
//   * Output: "[e0, e1, ...] (total: unknown)" when truncated and the total
//     length cannot be known without consuming the source; the total is a
//     number when it is known upfront (e.g. from range.size()) or when the
//     whole range fits in the preview. A probe never fabricates a total.
//   * Each kept element is formatted exactly once; if formatting an element
//     throws, the exception propagates and no more elements are consumed.
//   * max_items == 0 still distinguishes an empty range ("[] (total: 0)")
//     from a non-empty one ("[...] (total: ...)").
//   * Formatting of ordinary ranges is unchanged.
//   * Tests: see the preview_range_* tests in test/ranges-test.cc.
//
// Demo build command (compiles a single program, run from the repo root):
//   g++ -std=c++11 -I include demo/preview-range-demo.cc src/format.cc -o preview-range-demo
//   ./preview-range-demo

#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "fmt/ranges.h"

// A move-only single-pass input range backed by a stream.
struct int_stream_range {
  std::istringstream stream;
  explicit int_stream_range(const char* s) : stream(s) {}
  auto begin() -> std::istream_iterator<int> {
    return std::istream_iterator<int>(stream);
  }
  auto end() -> std::istream_iterator<int> { return {}; }
};

int main() {
  auto v = std::vector<int>{1, 2, 3, 4, 5};

  // Sized range: the total is known upfront, only the head is formatted.
  fmt::print("{}\n", fmt::preview_range(v, 2));   // [1, 2, ...] (total: 5)
  fmt::print("{}\n", fmt::preview_range(v, 10));  // [1, 2, 3, 4, 5] (total: 5)

  // Single-pass input range: only 3 elements are read (2 kept + 1 probe)
  // and the total stays unknown instead of being guessed from the probe.
  auto in = int_stream_range("1 2 3 4 5");
  fmt::print("{}\n", fmt::preview_range(in, 2));  // [1, 2, ...] (total: unknown)

  // max_items == 0 still distinguishes an empty range from a non-empty one.
  auto empty = std::vector<int>{};
  fmt::print("{}\n", fmt::preview_range(empty, 0));  // [] (total: 0)
  fmt::print("{}\n", fmt::preview_range(v, 0));      // [...] (total: 5)

  // String elements are escaped, like in ordinary range formatting.
  auto s = std::vector<std::string>{"a\nb", "c\"d", "e"};
  fmt::print("{}\n", fmt::preview_range(s, 2));
  // Output: ["a\nb", "c\"d", ...] (total: 3)

  // Ordinary range formatting is unchanged.
  fmt::print("{}\n", v);  // [1, 2, 3, 4, 5]
}
