// Demo: deterministic formatting of unordered maps with fmt::stable_map.
//
// Build (from the repository root):
//   c++ -std=c++11 -I include examples/stable-map.cc src/format.cc -o stable-map
//
// Expected output:
//   report 1: {"alpha": 1, "bravo": 2, "charlie": 3, "say \"hi\"": 4}
//   report 2: {"alpha": 1, "bravo": 2, "charlie": 3, "say \"hi\"": 4}
//   reports are identical
//   after clearing the source map: {"alpha": 1, "bravo": 2, "charlie": 3, "say \"hi\"": 4}

#include <fmt/ranges.h>

#include <functional>
#include <string>
#include <unordered_map>

int main() {
  // The same entries inserted in two different orders.
  auto first = std::unordered_map<std::string, int>{
      {"say \"hi\"", 4}, {"charlie", 3}, {"alpha", 1}, {"bravo", 2}};
  auto second = std::unordered_map<std::string, int>{
      {"bravo", 2}, {"alpha", 1}, {"say \"hi\"", 4}, {"charlie", 3}};

  // The caller picks the key order; string keys keep debug escaping.
  auto by_key = std::less<std::string>();
  auto report1 = fmt::format("{}", fmt::stable_map(first, by_key));
  auto report2 = fmt::format("{}", fmt::stable_map(second, by_key));

  fmt::print("report 1: {}\n", report1);
  fmt::print("report 2: {}\n", report2);
  if (report1 != report2) {
    fmt::print("reports differ!\n");
    return 1;
  }
  fmt::print("reports are identical\n");

  // The snapshot is independent of the original map.
  first.clear();
  fmt::print("after clearing the source map: {}\n", report1);
  return 0;
}
