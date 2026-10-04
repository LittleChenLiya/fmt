// Formatting library for C++ - deferred formatting ticket tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/deferred.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"

namespace {

// A custom type whose formatter counts format() calls.
struct counted {
  int* hits;
};

// A custom type whose formatter counts format() calls atomically.
struct atomic_counted {
  std::atomic<int>* hits;
};

// A move-only custom type.
struct move_only {
  int value;
  explicit move_only(int v) : value(v) {}
  move_only(move_only&&) = default;
  move_only& operator=(move_only&&) = default;
  move_only(const move_only&) = delete;
  move_only& operator=(const move_only&) = delete;
};

// A simple two-phase barrier for starting racing threads together.
class barrier {
 public:
  explicit barrier(int count) : remaining_(count) {}
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (--remaining_ == 0) {
      cv_.notify_all();
      return;
    }
    cv_.wait(lock, [this] { return remaining_ == 0; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int remaining_;
};

}  // namespace

FMT_BEGIN_NAMESPACE
template <> struct formatter<counted> {
  FMT_CONSTEXPR auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const counted& c, format_context& ctx) const
      -> format_context::iterator {
    ++*c.hits;
    return fmt::format_to(ctx.out(), "counted");
  }
};

template <> struct formatter<atomic_counted> {
  FMT_CONSTEXPR auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const atomic_counted& c, format_context& ctx) const
      -> format_context::iterator {
    c.hits->fetch_add(1);
    return fmt::format_to(ctx.out(), "raced");
  }
};

template <> struct formatter<move_only> {
  FMT_CONSTEXPR auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const move_only& m, format_context& ctx) const
      -> format_context::iterator {
    return fmt::format_to(ctx.out(), "move_only({})", m.value);
  }
};
FMT_END_NAMESPACE

TEST(deferred_test, string_argument_outlives_source) {
  // The ticket is created in a local scope; the source string is destroyed
  // before consume() runs, yet the output must be correct.
  auto t = [] {
    std::string text =
        "a string long enough to defeat the small string optimization";
    return fmt::deferred::ticket("{}", text);
  }();
  EXPECT_EQ(t.consume(),
            "a string long enough to defeat the small string optimization");
}

TEST(deferred_test, cstring_argument_is_copied) {
  auto t = [] {
    std::string text =
        "another long string that will not fit in a small string buffer";
    return fmt::deferred::ticket("{}", text.c_str());
  }();
  EXPECT_EQ(t.consume(),
            "another long string that will not fit in a small string buffer");
}

TEST(deferred_test, string_view_argument_is_copied) {
  // A string_view referring to a temporary must be copied, not retained.
  auto t = [] {
    return fmt::deferred::ticket(
        "{}", fmt::string_view(std::string("temporary string data")));
  }();
  EXPECT_EQ(t.consume(), "temporary string data");
}

TEST(deferred_test, format_string_is_copied) {
  auto t = [] {
    std::string fmt_str = "{} + {} = {}";
    return fmt::deferred::ticket(fmt::runtime(fmt_str), 1, 2, 3);
  }();
  EXPECT_EQ(t.consume(), "1 + 2 = 3");
}

TEST(deferred_test, positional_arguments) {
  auto t = fmt::deferred::ticket("{0} {1} {0}", "ab", 42);
  EXPECT_EQ(t.consume(), "ab 42 ab");
}

TEST(deferred_test, no_arguments) {
  auto t = fmt::deferred::ticket("no arguments {{here}}");
  EXPECT_EQ(t.consume(), "no arguments {here}");
}

TEST(deferred_test, builtin_argument_types) {
  auto t = fmt::deferred::ticket("{} {} {} {}", true, 'x', 3.5, -7);
  EXPECT_EQ(t.consume(), "true x 3.5 -7");
}

TEST(deferred_test, named_arguments) {
  auto t = fmt::deferred::ticket("{greeting}, {who}!",
                                 fmt::deferred::arg("greeting", "hello"),
                                 fmt::deferred::arg("who", std::string("world")));
  EXPECT_EQ(t.consume(), "hello, world!");
}

TEST(deferred_test, named_argument_name_is_copied) {
  auto t = [] {
    std::string name = "value";
    return fmt::deferred::ticket("{{{}}}", fmt::deferred::arg(name, 42));
  }();
  EXPECT_EQ(t.consume(), "{42}");
}

TEST(deferred_test, mixed_positional_and_named_arguments) {
  auto t = fmt::deferred::ticket("{} plus {value}", "pos",
                                 fmt::deferred::arg("value", 1));
  EXPECT_EQ(t.consume(), "pos plus 1");
}

TEST(deferred_test, dynamic_format_specs) {
  auto t = fmt::deferred::ticket("[{:{}}]", 5, 4);
  EXPECT_EQ(t.consume(), "[   5]");
}

TEST(deferred_test, formatting_is_deferred_until_consume) {
  int hits = 0;
  auto t = fmt::deferred::ticket("{}", fmt::deferred::own(counted{&hits}));
  EXPECT_EQ(hits, 0);  // No formatting at creation.
  EXPECT_EQ(t.consume(), "counted");
  EXPECT_EQ(hits, 1);  // Exactly one formatting at consume.
}

TEST(deferred_test, double_consume_throws) {
  auto t = fmt::deferred::ticket("{}", 42);
  EXPECT_EQ(t.consume(), "42");
  EXPECT_TRUE(t.consumed());
  EXPECT_THROW(t.consume(), fmt::deferred::consumption_error);
}

TEST(deferred_test, concurrent_consume_has_single_winner) {
  for (int round = 0; round < 50; ++round) {
    std::atomic<int> hits(0);
    auto t =
        fmt::deferred::ticket("{}", fmt::deferred::own(atomic_counted{&hits}));
    barrier start(2);
    std::string results[2];
    bool succeeded[2] = {false, false};
    std::thread threads[2];
    for (int i = 0; i < 2; ++i) {
      threads[i] = std::thread([&t, &start, &results, &succeeded, i] {
        start.wait();
        try {
          results[i] = t.consume();
          succeeded[i] = true;
        } catch (const fmt::deferred::consumption_error&) {
        }
      });
    }
    threads[0].join();
    threads[1].join();
    // Exactly one caller obtained the result and formatting ran once.
    EXPECT_EQ(hits.load(), 1);
    EXPECT_EQ(succeeded[0] + succeeded[1], 1);
    EXPECT_EQ(results[0] + results[1], "raced");
  }
}

TEST(deferred_test, invalid_format_string_rejected_at_creation) {
  // Positional index out of range.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{1}"), 42),
               fmt::format_error);
  // More references than arguments.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{} {}"), 42),
               fmt::format_error);
  // Unknown named argument.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{missing}"),
                                     fmt::deferred::arg("name", 1)),
               fmt::format_error);
  // Unmatched braces.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{"), 42),
               fmt::format_error);
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("}"), 42),
               fmt::format_error);
  // Automatic indexing after a named reference.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{name} {}"),
                                     fmt::deferred::arg("name", 1), 2),
               fmt::format_error);
  // Duplicate named arguments.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{x}"),
                                     fmt::deferred::arg("x", 1),
                                     fmt::deferred::arg("x", 2)),
               fmt::format_error);
  // Dynamic width referencing a missing argument.
  EXPECT_THROW(fmt::deferred::ticket(fmt::runtime("{:{}}"), 5),
               fmt::format_error);
}

TEST(deferred_test, move_transfers_ownership) {
  auto a = fmt::deferred::ticket("{}", 42);
  auto b = std::move(a);
  EXPECT_THROW(a.consume(), fmt::deferred::consumption_error);
  EXPECT_EQ(b.consume(), "42");

  auto c = fmt::deferred::ticket("{}", 7);
  auto d = fmt::deferred::ticket("{}", 8);
  c = std::move(d);
  EXPECT_THROW(d.consume(), fmt::deferred::consumption_error);
  EXPECT_EQ(c.consume(), "8");
}

TEST(deferred_test, moved_consumed_ticket_stays_consumed) {
  auto a = fmt::deferred::ticket("{}", 42);
  EXPECT_EQ(a.consume(), "42");
  auto b = std::move(a);
  EXPECT_TRUE(b.consumed());
  EXPECT_THROW(b.consume(), fmt::deferred::consumption_error);
}

TEST(deferred_test, move_only_custom_argument) {
  auto t = fmt::deferred::ticket("{}", fmt::deferred::own(move_only(9)));
  EXPECT_EQ(t.consume(), "move_only(9)");
}

TEST(deferred_test, custom_arguments_require_owning_wrapper) {
  // Unwrapped custom arguments are rejected at compile time.
  static_assert(!std::is_constructible<fmt::deferred::ticket,
                                       fmt::runtime_format_string<>,
                                       counted&>::value,
                "unwrapped custom arguments must be rejected");
  static_assert(!std::is_constructible<fmt::deferred::ticket,
                                       fmt::runtime_format_string<>,
                                       counted>::value,
                "unwrapped custom arguments must be rejected");
  // Explicitly owned custom arguments are accepted.
  static_assert(std::is_constructible<fmt::deferred::ticket,
                                      fmt::runtime_format_string<>,
                                      fmt::deferred::owned<counted>&&>::value,
                "owned<>-wrapped custom arguments must be accepted");
  // Built-in arguments are accepted directly.
  static_assert(std::is_constructible<fmt::deferred::ticket,
                                      fmt::runtime_format_string<>, int>::value,
                "built-in arguments are accepted");
  static_assert(std::is_constructible<fmt::deferred::ticket,
                                      fmt::runtime_format_string<>,
                                      const std::string&>::value,
                "string arguments are accepted");
}

TEST(deferred_test, unevaluated_ticket_does_not_format) {
  int hits = 0;
  {
    auto t = fmt::deferred::ticket("{}", fmt::deferred::own(counted{&hits}));
    EXPECT_FALSE(t.consumed());
  }  // Destroying without consuming is fine and formats nothing.
  EXPECT_EQ(hits, 0);
}
