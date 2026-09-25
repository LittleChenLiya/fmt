// Formatting library for C++ - record buffer tests
//
// For the license information refer to format.h.

#include "fmt/record_buffer.h"

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fmt/args.h"
#include "gtest/gtest.h"

namespace {

// A sink that records calls and can be configured to fail a number of times
// before accepting records.
struct test_sink {
  int calls = 0;
  int failures = 0;
  std::vector<std::string> records;

  void operator()(fmt::string_view record) {
    ++calls;
    if (failures > 0) {
      --failures;
      throw std::runtime_error("sink failure");
    }
    records.emplace_back(record.data(), record.size());
  }
};

}  // namespace

// A type whose formatter writes a prefix and then throws.
struct throw_after_prefix {};

// A type whose formatter counts its invocations.
struct counted {};
static int counted_format_calls = 0;

FMT_BEGIN_NAMESPACE
template <> struct formatter<throw_after_prefix> : formatter<string_view> {
  auto format(throw_after_prefix, format_context& ctx) const
      -> decltype(ctx.out()) {
    fmt::format_to(ctx.out(), "prefix");
    throw std::runtime_error("formatter failure");
  }
};

template <> struct formatter<counted> : formatter<string_view> {
  auto format(counted, format_context& ctx) const -> decltype(ctx.out()) {
    ++counted_format_calls;
    return formatter<string_view>::format("counted", ctx);
  }
};
FMT_END_NAMESPACE

TEST(record_buffer_test, format_and_commit) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  EXPECT_FALSE(buf.pending());
  EXPECT_TRUE(buf.format("answer={}", 42));
  EXPECT_TRUE(buf.pending());
  EXPECT_EQ(9u, buf.size());
  EXPECT_EQ("answer=42", buf.record());
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(1, sink.calls);
  ASSERT_EQ(1u, sink.records.size());
  EXPECT_EQ("answer=42", sink.records[0]);
  EXPECT_FALSE(buf.pending());
  EXPECT_EQ(0u, buf.size());

  // The buffer state is reusable after a successful commit.
  EXPECT_TRUE(buf.format("second {}", 2));
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(2, sink.calls);
  ASSERT_EQ(2u, sink.records.size());
  EXPECT_EQ("second 2", sink.records[1]);
}

TEST(record_buffer_test, commit_without_record_is_noop) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  EXPECT_FALSE(buf.commit(sink));
  EXPECT_EQ(0, sink.calls);
}

TEST(record_buffer_test, formatter_throw_means_no_commit) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  // The formatter writes a prefix and then throws: no record becomes
  // pending and the sink is never called.
  EXPECT_THROW(buf.format("{}", throw_after_prefix()), std::runtime_error);
  EXPECT_FALSE(buf.pending());
  EXPECT_EQ(0u, buf.size());
  EXPECT_EQ("", buf.record());
  EXPECT_FALSE(buf.commit(sink));
  EXPECT_EQ(0, sink.calls);
}

TEST(record_buffer_test, format_error_means_no_commit) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  EXPECT_THROW(buf.format(fmt::runtime("{:d}"), "not a number"),
               fmt::format_error);
  EXPECT_FALSE(buf.pending());
  EXPECT_FALSE(buf.commit(sink));
  EXPECT_EQ(0, sink.calls);
}

TEST(record_buffer_test, capacity_exact_fit) {
  auto buf = fmt::record_buffer(9);
  auto sink = test_sink();
  // A record of exactly the capacity in encoded bytes fits.
  EXPECT_TRUE(buf.format("{}", "中文中"));  // 9 UTF-8 bytes
  EXPECT_TRUE(buf.pending());
  EXPECT_EQ(9u, buf.size());
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(1, sink.calls);
  ASSERT_EQ(1u, sink.records.size());
  EXPECT_EQ("中文中", sink.records[0]);
}

TEST(record_buffer_test, capacity_overflow) {
  auto buf = fmt::record_buffer(8);
  auto sink = test_sink();
  // One byte over the capacity: formatting fails and the sink is not called.
  EXPECT_FALSE(buf.format("{}", "中文中"));  // 9 UTF-8 bytes > 8
  EXPECT_FALSE(buf.pending());
  EXPECT_EQ(0u, buf.size());
  EXPECT_FALSE(buf.commit(sink));
  EXPECT_EQ(0, sink.calls);

  // A multibyte sequence is never partially committed.
  auto tiny = fmt::record_buffer(2);
  EXPECT_FALSE(tiny.format("{}", "中"));  // 3 UTF-8 bytes > 2
  EXPECT_FALSE(tiny.pending());
  EXPECT_FALSE(tiny.commit(sink));
  EXPECT_EQ(0, sink.calls);

  // The buffer is reusable after an overflow.
  EXPECT_TRUE(buf.format("{}", "中文"));  // 6 UTF-8 bytes <= 8
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(1, sink.calls);
  EXPECT_EQ("中文", sink.records[0]);
}

TEST(record_buffer_test, commit_failure_keeps_record_for_retry) {
  counted_format_calls = 0;
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  sink.failures = 1;
  EXPECT_TRUE(buf.format("{}", counted()));
  EXPECT_EQ(1, counted_format_calls);

  // The sink throws: the record is kept and remains pending.
  EXPECT_THROW(buf.commit(sink), std::runtime_error);
  EXPECT_EQ(1, sink.calls);
  EXPECT_TRUE(buf.pending());
  EXPECT_EQ("counted", buf.record());

  // Retrying resubmits the same record without re-running the formatter.
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(2, sink.calls);
  EXPECT_EQ(1, counted_format_calls);
  ASSERT_EQ(1u, sink.records.size());
  EXPECT_EQ("counted", sink.records[0]);
  EXPECT_FALSE(buf.pending());
}

TEST(record_buffer_test, multiple_retries) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  sink.failures = 3;
  EXPECT_TRUE(buf.format("retry {}", "me"));
  for (int i = 0; i < 3; ++i) {
    EXPECT_THROW(buf.commit(sink), std::runtime_error);
    EXPECT_TRUE(buf.pending());
  }
  EXPECT_EQ(3, sink.calls);
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(4, sink.calls);
  ASSERT_EQ(1u, sink.records.size());
  EXPECT_EQ("retry me", sink.records[0]);
}

TEST(record_buffer_test, reentrant_commit_rejected) {
  auto buf = fmt::record_buffer(64);
  EXPECT_TRUE(buf.format("rec"));
  int calls = 0;
  bool rejected = false;
  auto reentrant_sink = [&](fmt::string_view) {
    ++calls;
    try {
      buf.commit([](fmt::string_view) {});
    } catch (const std::logic_error&) {
      rejected = true;
    }
  };
  EXPECT_TRUE(buf.commit(reentrant_sink));
  EXPECT_EQ(1, calls);
  EXPECT_TRUE(rejected);
  EXPECT_FALSE(buf.pending());
}

TEST(record_buffer_test, format_replaces_uncommitted_record) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  EXPECT_TRUE(buf.format("old"));
  EXPECT_TRUE(buf.format("new"));
  EXPECT_TRUE(buf.commit(sink));
  EXPECT_EQ(1, sink.calls);
  ASSERT_EQ(1u, sink.records.size());
  EXPECT_EQ("new", sink.records[0]);
}

TEST(record_buffer_test, discard) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  EXPECT_TRUE(buf.format("abc"));
  buf.discard();
  EXPECT_FALSE(buf.pending());
  EXPECT_FALSE(buf.commit(sink));
  EXPECT_EQ(0, sink.calls);
}

TEST(record_buffer_test, dynamic_args) {
  auto buf = fmt::record_buffer(64);
  auto sink = test_sink();
  auto store = fmt::dynamic_format_arg_store<fmt::format_context>();
  store.push_back(42);
  store.push_back("xy");
  EXPECT_TRUE(buf.vformat("{}:{}", store));
  EXPECT_TRUE(buf.commit(sink));
  ASSERT_EQ(1u, sink.records.size());
  EXPECT_EQ("42:xy", sink.records[0]);

  // Runtime format strings are supported as well.
  auto runtime_fmt = std::string("value={}");
  EXPECT_TRUE(buf.format(fmt::runtime(runtime_fmt), 7));
  EXPECT_TRUE(buf.commit(sink));
  ASSERT_EQ(2u, sink.records.size());
  EXPECT_EQ("value=7", sink.records[1]);
}

TEST(record_buffer_test, thread_local_buffers_are_not_shared) {
  auto& local = fmt::thread_record_buffer();
  fmt::record_buffer* remote_ptr = nullptr;
  bool remote_ok = false;
  auto remote_record = std::string();
  auto worker = std::thread([&] {
    auto& remote = fmt::thread_record_buffer();
    remote_ptr = &remote;
    remote_ok =
        remote.format("worker {}", 1) &&
        remote.commit([&](fmt::string_view record) {
          remote_record.assign(record.data(), record.size());
        });
  });
  worker.join();
  EXPECT_NE(remote_ptr, &local);
  EXPECT_TRUE(remote_ok);
  EXPECT_EQ("worker 1", remote_record);
  // The buffer of this thread is unaffected by the worker thread.
  EXPECT_FALSE(local.pending());
}
