// Demonstrates fmt::record_buffer: format a complete record first and submit
// it to a sink that fails once, then retry. Retrying does not re-run the
// formatter. The program prints the sink call count and the full record.
//
// For the license information refer to format.h.

#include <stdexcept>
#include <string>

#include "fmt/record_buffer.h"

int main() {
  // Each thread has its own buffer; buffers are never shared between threads.
  auto& buf = fmt::thread_record_buffer();

  int sink_calls = 0;
  auto committed = std::string();
  auto sink = [&](fmt::string_view record) {
    ++sink_calls;
    if (sink_calls == 1)
      throw std::runtime_error("simulated transient sink failure");
    committed.assign(record.data(), record.size());
  };

  // The whole record must fit into the capacity in encoded bytes.
  if (!buf.format("id={} msg={} utf8={}", 7, "hello", "中文")) {
    fmt::print(stderr, "record does not fit the buffer\n");
    return 1;
  }

  // Only a sink call that returns normally consumes the record: on failure
  // the record is kept and can be retried.
  while (buf.pending()) {
    try {
      buf.commit(sink);
    } catch (const std::exception& error) {
      fmt::print(stderr, "commit failed ({}), retrying\n", error.what());
    }
  }

  fmt::print("sink calls: {}\nrecord: {}\n", sink_calls, committed);
}
