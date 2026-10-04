// Formatting library for C++ - UTF-8 ring buffer tests
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.
//
// Build and run this test alone (single job):
//   cmake -S . -B build -DFMT_TEST=ON
//   cmake --build build --target utf8-ring-test -j1
//   ./build/test/utf8-ring-test
// Or without CMake, from the repo root:
//   g++ -std=c++11 -I include utf8-ring-smoke.cc -o utf8-ring-smoke
//   (see doc/utf8-ring.md for details)

#include "fmt/utf8-ring.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

TEST(utf8_ring_test, keeps_complete_suffix_without_splitting_utf8) {
  // "A猫BC" is 6 bytes (A=1, 猫=3, B=1, C=1); capacity 5 keeps the newest
  // complete code points: A is evicted, 猫 is never split.
  auto ring = fmt::utf8_ring_buffer(5);
  fmt::format_to(ring.out(), "A猫BC");
  EXPECT_EQ(ring.str(), "猫BC");
  EXPECT_EQ(ring.discarded(), 1u);
}

TEST(utf8_ring_test, drops_partial_leading_code_point) {
  // Capacity 4 cannot hold "A猫B" plus C without splitting 猫, so the whole
  // 猫 code point is evicted.
  auto ring = fmt::utf8_ring_buffer(4);
  fmt::format_to(ring.out(), "A猫BC");
  EXPECT_EQ(ring.str(), "BC");
  EXPECT_EQ(ring.discarded(), 4u);  // 'A' (1) + 猫 (3)
}

TEST(utf8_ring_test, oversized_code_point_is_dropped_and_counted) {
  // 😀 is 4 bytes and never fits in capacity 3.
  auto ring = fmt::utf8_ring_buffer(3);
  fmt::format_to(ring.out(), "a😀b");
  EXPECT_EQ(ring.str(), "ab");
  EXPECT_EQ(ring.discarded(), 4u);
}

TEST(utf8_ring_test, zero_capacity_discards_everything) {
  auto ring = fmt::utf8_ring_buffer(0);
  fmt::format_to(ring.out(), "A猫");
  EXPECT_EQ(ring.str(), "");
  EXPECT_EQ(ring.discarded(), 4u);
}

TEST(utf8_ring_test, counts_evictions_across_the_wrap_position) {
  // ASCII bytes wrap the ring one at a time.
  auto ring = fmt::utf8_ring_buffer(4);
  fmt::format_to(ring.out(), "abcdef");
  EXPECT_EQ(ring.str(), "cdef");
  EXPECT_EQ(ring.discarded(), 2u);

  // Multi-byte code points wrap the ring whole: each 猫 (3 bytes) evicts the
  // previous one in a capacity-5 ring.
  auto wide = fmt::utf8_ring_buffer(5);
  fmt::format_to(wide.out(), "猫猫猫");
  EXPECT_EQ(wide.str(), "猫");
  EXPECT_EQ(wide.discarded(), 6u);
}

TEST(utf8_ring_test, newline_is_ordinary_content) {
  auto ring = fmt::utf8_ring_buffer(4);
  fmt::format_to(ring.out(), "ab\ncd");
  EXPECT_EQ(ring.str(), "b\ncd");
  EXPECT_EQ(ring.discarded(), 1u);
}

TEST(utf8_ring_test, formats_arguments) {
  auto ring = fmt::utf8_ring_buffer(16);
  fmt::format_to(ring.out(), "n={}", 42);
  EXPECT_EQ(ring.str(), "n=42");
  EXPECT_EQ(ring.discarded(), 0u);
}

namespace {
struct counted_type {};
int counted_format_calls = 0;
}  // namespace

template <> struct fmt::formatter<counted_type> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(counted_type, format_context& ctx) const
      -> decltype(ctx.out()) {
    ++counted_format_calls;
    return fmt::format_to(ctx.out(), "counted");
  }
};

TEST(utf8_ring_test, formatter_is_called_exactly_once) {
  counted_format_calls = 0;
  auto ring = fmt::utf8_ring_buffer(64);
  fmt::format_to(ring.out(), "{}", counted_type());
  EXPECT_EQ(ring.str(), "counted");
  EXPECT_EQ(counted_format_calls, 1);
}
