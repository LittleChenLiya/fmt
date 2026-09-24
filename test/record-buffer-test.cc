// Formatting library for C++ - record buffer tests
//
// For the license information refer to format.h.

#include "fmt/record-buffer.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "fmt/args.h"
#include "gtest/gtest.h"

namespace {

// A type whose formatter writes a prefix and then throws.
struct prefix_then_throw {};

// A type whose formatter counts its invocations.
struct call_counter {};

int g_format_calls = 0;

// A commit callback that records deliveries and can be configured to fail
// a given number of times before succeeding.
class sink {
 public:
  int calls = 0;
  std::vector<std::string> records;

  explicit sink(int failures = 0) : failures_(failures) {}

  void operator()(fmt::string_view record) {
    ++calls;
    if (failures_ > 0) {
      --failures_;
      throw std::runtime_error("commit failure");
    }
    records.push_back(std::string(record.data(), record.size()));
  }

 private:
  int failures_;
};

auto to_string(fmt::string_view view) -> std::string {
  return std::string(view.data(), view.size());
}

}  // namespace

FMT_BEGIN_NAMESPACE
template <> struct formatter<prefix_then_throw> {
  FMT_CONSTEXPR auto parse(format_parse_context& ctx)
      -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  auto format(prefix_then_throw, format_context& ctx) const
      -> decltype(ctx.out()) {
    auto out = ctx.out();
    *out++ = 'x';  // Writes a prefix before failing.
    throw std::runtime_error("formatter failure");
  }
};

template <> struct formatter<call_counter> {
  FMT_CONSTEXPR auto parse(format_parse_context& ctx)
      -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  auto format(call_counter, format_context& ctx) const
      -> decltype(ctx.out()) {
    ++g_format_calls;
    return fmt::format_to(ctx.out(), "counted");
  }
};
FMT_END_NAMESPACE

TEST(record_buffer_test, commits_complete_record) {
  fmt::record_buffer buf(64);
  auto snk = sink();
  buf.format(std::ref(snk), "{}={}", "answer", 42);
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0], "answer=42");
  EXPECT_FALSE(buf.has_pending());
}

TEST(record_buffer_test, formatter_failure_means_no_commit) {
  fmt::record_buffer buf(64);
  auto snk = sink();
  // The formatter writes a prefix and then throws.
  EXPECT_THROW(buf.format(std::ref(snk), "{}", prefix_then_throw{}),
               std::runtime_error);
  EXPECT_EQ(snk.calls, 0);          // The sink is never invoked.
  EXPECT_FALSE(buf.has_pending());  // Nothing is kept pending.
}

TEST(record_buffer_test, exact_capacity_boundary) {
  fmt::record_buffer buf(5);
  auto snk = sink();
  buf.format(std::ref(snk), "{}", "12345");  // Exactly the capacity.
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0], "12345");

  // One byte over the capacity: the record is rejected before commit.
  EXPECT_THROW(buf.format(std::ref(snk), "{}", "123456"), fmt::format_error);
  EXPECT_EQ(snk.calls, 1);
  EXPECT_FALSE(buf.has_pending());

  // The buffer is still usable after a capacity error.
  buf.format(std::ref(snk), "{}", "ok");
  ASSERT_EQ(snk.records.size(), 2u);
  EXPECT_EQ(snk.records[1], "ok");
}

TEST(record_buffer_test, utf8_capacity_counts_encoded_bytes) {
  // U+4F60 U+597D ("你好") is 6 bytes in UTF-8.
  fmt::record_buffer buf(6);
  auto snk = sink();
  buf.format(std::ref(snk), "{}", "你好");
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0].size(), 6u);
  EXPECT_EQ(snk.records[0], "你好");

  fmt::record_buffer small(5);
  EXPECT_THROW(small.format(std::ref(snk), "{}", "你好"), fmt::format_error);
  EXPECT_EQ(snk.calls, 1);  // No additional commit happened.
  EXPECT_FALSE(small.has_pending());
}

TEST(record_buffer_test, commit_failure_keeps_record_for_retry) {
  g_format_calls = 0;
  fmt::record_buffer buf(64);
  auto snk = sink(1);  // The first commit attempt fails.
  EXPECT_THROW(buf.format(std::ref(snk), "{}", call_counter{}),
               std::runtime_error);
  EXPECT_TRUE(buf.has_pending());
  EXPECT_EQ(to_string(buf.pending_record()), "counted");
  EXPECT_EQ(snk.calls, 1);
  EXPECT_TRUE(snk.records.empty());

  buf.retry(std::ref(snk));
  EXPECT_FALSE(buf.has_pending());
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0], "counted");
  // The formatter ran exactly once: the retry did not reformat.
  EXPECT_EQ(g_format_calls, 1);
}

TEST(record_buffer_test, multiple_retries) {
  g_format_calls = 0;
  fmt::record_buffer buf(64);
  auto snk = sink(3);  // Three commit attempts fail.
  EXPECT_THROW(buf.format(std::ref(snk), "{}", call_counter{}),
               std::runtime_error);
  EXPECT_THROW(buf.retry(std::ref(snk)), std::runtime_error);
  EXPECT_THROW(buf.retry(std::ref(snk)), std::runtime_error);
  EXPECT_TRUE(buf.has_pending());
  EXPECT_EQ(snk.calls, 3);
  EXPECT_TRUE(snk.records.empty());

  buf.retry(std::ref(snk));
  EXPECT_FALSE(buf.has_pending());
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0], "counted");
  EXPECT_EQ(snk.calls, 4);
  EXPECT_EQ(g_format_calls, 1);
}

TEST(record_buffer_test, reentrant_commit_rejected) {
  fmt::record_buffer buf(64);
  auto delivered = std::string();
  auto noop = [](fmt::string_view) {};
  auto commit = [&](fmt::string_view record) {
    delivered = to_string(record);
    // Reentrant calls from inside the commit callback are rejected.
    EXPECT_THROW(buf.format(noop, "x"), fmt::format_error);
    EXPECT_THROW(buf.retry(noop), fmt::format_error);
    EXPECT_THROW(buf.discard(), fmt::format_error);
  };
  buf.format(commit, "{}", 42);
  EXPECT_EQ(delivered, "42");
  EXPECT_FALSE(buf.has_pending());
}

TEST(record_buffer_test, format_with_pending_record_rejected) {
  fmt::record_buffer buf(64);
  auto snk = sink(1);
  EXPECT_THROW(buf.format(std::ref(snk), "first"), std::runtime_error);
  EXPECT_TRUE(buf.has_pending());
  // A new record cannot replace an uncommitted one.
  EXPECT_THROW(buf.format(std::ref(snk), "second"), fmt::format_error);
  EXPECT_EQ(to_string(buf.pending_record()), "first");

  buf.discard();
  EXPECT_FALSE(buf.has_pending());
  buf.format(std::ref(snk), "second");
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0], "second");
}

TEST(record_buffer_test, retry_without_pending_record_throws) {
  fmt::record_buffer buf(64);
  EXPECT_THROW(buf.retry([](fmt::string_view) {}), fmt::format_error);
}

TEST(record_buffer_test, buffer_reused_after_commit) {
  fmt::record_buffer buf(8);
  auto snk = sink();
  buf.format(std::ref(snk), "{}", 1);
  buf.format(std::ref(snk), "{}", 2);
  auto expected = std::vector<std::string>{"1", "2"};
  EXPECT_EQ(snk.records, expected);
}

TEST(record_buffer_test, dynamic_args) {
  fmt::record_buffer buf(64);
  auto snk = sink();
  auto store = fmt::dynamic_format_arg_store<fmt::format_context>();
  store.push_back(42);
  store.push_back("abc");
  buf.vformat(std::ref(snk), "{} and {}", store);
  ASSERT_EQ(snk.records.size(), 1u);
  EXPECT_EQ(snk.records[0], "42 and abc");
}

