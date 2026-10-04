// Formatting library for C++ - format ticket tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/ticket.h"

#include <thread>
#include <mutex>
#include <condition_variable>

#include "gmock/gmock.h"

// A formatter that counts how many times it has been formatted.
struct counting_formatter {
  std::string value;
};

static int format_count = 0;

template <>
struct fmt::formatter<counting_formatter> {
  constexpr auto parse(fmt::parse_context<>& ctx)
      -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  auto format(const counting_formatter& arg, fmt::format_context& ctx) const
      -> decltype(ctx.out()) {
    ++format_count;
    return fmt::format_to(ctx.out(), "{}", arg.value);
  }
};

namespace {

// A formattable custom type used to test owning wrappers.
struct point {
  int x;
  int y;
};

}  // namespace

template <>
struct fmt::formatter<point> {
  constexpr auto parse(fmt::parse_context<>& ctx) -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  auto format(const point& p, fmt::format_context& ctx) const
      -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "({}, {})", p.x, p.y);
  }
};

namespace {

// A reusable barrier used to maximize contention in concurrent consume tests.
class barrier {
 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int count_;

 public:
  explicit barrier(int count) : count_(count) {}

  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    --count_;
    if (count_ == 0) {
      cv_.notify_all();
    } else {
      cv_.wait(lock, [this] { return count_ == 0; });
    }
  }
};

}  // namespace

TEST(ticket_test, positional) {
  auto ticket = fmt::format_ticket("{0} {1}", 42, 3.14);
  EXPECT_EQ(ticket.consume(), "42 3.14");
}

TEST(ticket_test, string_arguments_are_copied) {
  fmt::format_ticket ticket;
  {
    std::string s = "hello";
    ticket = fmt::format_ticket("{}", s);
  }
  // s has been destroyed; the ticket owns its own copy.
  EXPECT_EQ(ticket.consume(), "hello");
}

TEST(ticket_test, string_view_arguments_are_copied) {
  fmt::format_ticket ticket;
  {
    std::string s = "world";
    fmt::string_view sv(s);
    ticket = fmt::format_ticket("{}", sv);
  }
  EXPECT_EQ(ticket.consume(), "world");
}

TEST(ticket_test, named_arguments) {
  auto ticket = fmt::format_ticket(
      "{name} is {age}", fmt::arg("name", "Alice"), fmt::arg("age", 30));
  EXPECT_EQ(ticket.consume(), "Alice is 30");
}

TEST(ticket_test, named_argument_with_own) {
  auto ticket = fmt::format_ticket(
      "{p}", fmt::arg("p", fmt::own(point{1, 2})));
  EXPECT_EQ(ticket.consume(), "(1, 2)");
}

TEST(ticket_test, custom_argument) {
  // Custom arguments must be wrapped in fmt::own so the ticket owns them.
  auto ticket = fmt::format_ticket("{}", fmt::own(point{1, 2}));
  EXPECT_EQ(ticket.consume(), "(1, 2)");
}

TEST(ticket_test, runtime_format_string) {
  std::string fmt_str = "{}";
  auto ticket = fmt::format_ticket(fmt::runtime(fmt_str), 42);
  EXPECT_EQ(ticket.consume(), "42");
}

TEST(ticket_test, invalid_format_string_throws_at_creation) {
  EXPECT_THROW(fmt::format_ticket("{", 42), fmt::format_error);
  EXPECT_THROW(fmt::format_ticket("}"), fmt::format_error);
  EXPECT_THROW(fmt::format_ticket("{}"), fmt::format_error);
  EXPECT_THROW(fmt::format_ticket("{0} {1}", 42), fmt::format_error);
  EXPECT_THROW(fmt::format_ticket("{name}", fmt::arg("other", 1)),
               fmt::format_error);
}

TEST(ticket_test, nested_references_are_checked_at_creation) {
  // The nested replacement field references an argument that does not exist.
  EXPECT_THROW(fmt::format_ticket("{:>{}}", "x"), fmt::format_error);
  // Valid nested reference.
  auto ticket = fmt::format_ticket("{:>{}}", "x", 5);
  EXPECT_EQ(ticket.consume(), "    x");
}

TEST(ticket_test, double_consume_reports_error) {
  auto ticket = fmt::format_ticket("{}", 42);
  EXPECT_EQ(ticket.consume(), "42");
  EXPECT_THROW(ticket.consume(), fmt::format_error);
}

TEST(ticket_test, move_ownership) {
  auto ticket1 = fmt::format_ticket("{}", "hello");
  auto ticket2 = std::move(ticket1);
  EXPECT_EQ(ticket2.consume(), "hello");
  EXPECT_THROW(ticket1.consume(), fmt::format_error);
}

TEST(ticket_test, move_assignment) {
  auto ticket1 = fmt::format_ticket("{}", "hello");
  auto ticket2 = fmt::format_ticket("{}", "world");
  ticket2 = std::move(ticket1);
  EXPECT_EQ(ticket2.consume(), "hello");
  EXPECT_THROW(ticket1.consume(), fmt::format_error);
}

TEST(ticket_test, counting_formatter_not_formatted_at_creation) {
  format_count = 0;
  auto ticket = fmt::format_ticket("{}", fmt::own(counting_formatter{"hello"}));
  EXPECT_EQ(format_count, 0);
  EXPECT_EQ(ticket.consume(), "hello");
  EXPECT_EQ(format_count, 1);
}

TEST(ticket_test, concurrent_consume_formats_only_once) {
  format_count = 0;
  auto ticket = fmt::format_ticket("{}", fmt::own(counting_formatter{"hello"}));

  barrier sync(2);
  std::string result1, result2;
  bool error1 = false, error2 = false;
  std::thread thread1([&] {
    sync.arrive_and_wait();
    try {
      result1 = ticket.consume();
    } catch (const fmt::format_error&) {
      error1 = true;
    }
  });
  std::thread thread2([&] {
    sync.arrive_and_wait();
    try {
      result2 = ticket.consume();
    } catch (const fmt::format_error&) {
      error2 = true;
    }
  });
  thread1.join();
  thread2.join();

  EXPECT_EQ(format_count, 1);
  EXPECT_TRUE((error1 && !error2) || (!error1 && error2));
  EXPECT_TRUE((result1 == "hello") ^ (result2 == "hello"));
}

TEST(ticket_test, escaped_braces) {
  auto ticket = fmt::format_ticket("{{}} {}", 42);
  EXPECT_EQ(ticket.consume(), "{} 42");
}

TEST(ticket_test, bool_and_char) {
  auto ticket = fmt::format_ticket("{} {}", true, 'a');
  EXPECT_EQ(ticket.consume(), "true a");
}
