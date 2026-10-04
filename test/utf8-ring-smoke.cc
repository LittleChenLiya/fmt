// Formatting library for C++ - standalone UTF-8 ring buffer smoke test
//
// A minimal check that does not require gtest. Build from the repo root:
//   g++ -std=c++11 -I include test/utf8-ring-smoke.cc -o utf8-ring-smoke
//   ./utf8-ring-smoke

#include <cstdio>
#include <string>

#include "fmt/utf8-ring.h"
#include "fmt/format.h"

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  // Capacity 5, "A猫BC" (6 bytes): keeps the complete suffix "猫BC".
  auto ring = fmt::utf8_ring_buffer(5);
  fmt::format_to(ring.out(), "A猫BC");
  check(ring.str() == "\xE7\x8C\xAB"
                     "BC",
        "capacity 5 keeps complete suffix");
  check(ring.discarded() == 1, "capacity 5 discarded count");

  // A 4-byte emoji larger than the capacity is dropped whole and counted.
  auto tiny = fmt::utf8_ring_buffer(3);
  fmt::format_to(tiny.out(), "a\xF0\x9F\x98\x80"
                             "b");
  check(tiny.str() == "ab", "oversized emoji dropped");
  check(tiny.discarded() == 4, "oversized emoji discarded count");

  // Eviction counting across the wrap position.
  auto wrap = fmt::utf8_ring_buffer(4);
  fmt::format_to(wrap.out(), "abcdef");
  check(wrap.str() == "cdef", "wrap-around contents");
  check(wrap.discarded() == 2, "wrap-around discarded count");

  if (failures == 0) std::puts("utf8-ring smoke test passed");
  return failures == 0 ? 0 : 1;
}
