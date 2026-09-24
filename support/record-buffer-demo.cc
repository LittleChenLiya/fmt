// Formatting library for C++ - record buffer demo
//
// For the license information refer to format.h.
//
// Builds and runs in a few seconds, e.g.:
//
//   c++ -std=c++11 -DFMT_HEADER_ONLY -I include \
//       support/record-buffer-demo.cc -o record-buffer-demo
//   ./record-buffer-demo
//
// Prints the number of sink (commit callback) invocations and the full
// records, including a simulated commit failure followed by a retry.

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "fmt/record-buffer.h"

int main() {
  auto records = std::vector<std::string>();
  int sink_calls = 0;
  bool fail_once = true;

  // The commit callback ("sink") receives complete records only. The first
  // invocation simulates a transient failure.
  auto commit = [&](fmt::string_view record) {
    ++sink_calls;
    if (fail_once) {
      fail_once = false;
      throw std::runtime_error("simulated commit failure");
    }
    records.push_back(std::string(record.data(), record.size()));
  };

  fmt::record_buffer buf(64);
  try {
    buf.format(commit, "answer={}", 42);
  } catch (const std::exception& e) {
    std::printf("commit failed: %s (record kept for retry)\n", e.what());
  }
  buf.retry(commit);  // Commits the pending record without reformatting.
  buf.format(commit, "greeting={}", "你好");  // UTF-8, counted in bytes.

  std::printf("sink calls: %d\n", sink_calls);
  for (const auto& record : records)
    std::printf("record: %s\n", record.c_str());
}

