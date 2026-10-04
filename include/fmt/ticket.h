// Formatting library for C++ - deferred formatting tickets
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.

#ifndef FMT_TICKET_H
#define FMT_TICKET_H

#include "format.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

FMT_BEGIN_NAMESPACE

/**
 * An explicit owning wrapper for a custom argument passed to a format_ticket.
 *
 * A custom argument is only copied into the ticket when wrapped in `own` (or
 * `owned`), so a ticket never silently references an external object:
 *
 *     struct point { int x; int y; };
 *     fmt::format_ticket ticket("{}", fmt::own(point{1, 2}));
 *
 * Passing a custom argument without the wrapper is a compile-time error.
 */
template <typename T>
class owned {
 public:
  using value_type = T;

  explicit owned(const T& value) : value_(value) {}
  explicit owned(T&& value) : value_(std::move(value)) {}

  auto get() const& noexcept -> const T& { return value_; }
  auto get() & noexcept -> T& { return value_; }
  auto get() && noexcept -> T&& { return std::move(value_); }

 private:
  T value_;
};

template <typename T>
struct is_owned : std::false_type {};
template <typename T>
struct is_owned<owned<T>> : std::true_type {};

/**
 * Wraps a custom argument so that it is owned by value in a format_ticket.
 */
template <typename T>
auto own(T&& value) -> owned<typename std::decay<T>::type> {
  return owned<typename std::decay<T>::type>(std::forward<T>(value));
}

namespace detail {

// A named argument's name, copied into the ticket's own storage.
template <typename Char>
struct ticket_named_arg {
  std::basic_string<Char> name;
  int id;
};

// Type-erased, owning storage for a single ticket argument.
template <typename Char>
class ticket_arg_base {
 public:
  virtual ~ticket_arg_base() {}
  virtual auto make_arg() -> basic_format_arg<buffered_context<Char>> = 0;
};

template <typename Char, typename T>
class ticket_builtin_arg : public ticket_arg_base<Char> {
 private:
  T value_;

 public:
  explicit ticket_builtin_arg(T value) : value_(std::move(value)) {}
  auto make_arg() -> basic_format_arg<buffered_context<Char>> override {
    return basic_format_arg<buffered_context<Char>>(value_);
  }
};

template <typename Char>
class ticket_string_arg : public ticket_arg_base<Char> {
 private:
  std::basic_string<Char> value_;

 public:
  explicit ticket_string_arg(std::basic_string<Char> value)
      : value_(std::move(value)) {}
  auto make_arg() -> basic_format_arg<buffered_context<Char>> override {
    return basic_format_arg<buffered_context<Char>>(
        basic_string_view<Char>(value_));
  }
};

template <typename Char, typename T>
class ticket_custom_arg : public ticket_arg_base<Char> {
 private:
  T value_;

 public:
  explicit ticket_custom_arg(T value) : value_(std::move(value)) {}
  auto make_arg() -> basic_format_arg<buffered_context<Char>> override {
    return basic_format_arg<buffered_context<Char>>(value_);
  }
};

// Copies an argument into the ticket's own storage. String arguments are
// copied into a std::basic_string<Char>; custom arguments must be wrapped in
// `own`.
template <typename Char, typename T,
          FMT_ENABLE_IF(is_named_arg<typename std::decay<T>::type>::value)>
auto ticket_classify(T&& arg, int index,
                     std::vector<ticket_named_arg<Char>>& named)
    -> std::unique_ptr<ticket_arg_base<Char>> {
  named.push_back(
      ticket_named_arg<Char>{std::basic_string<Char>(arg.name), index});
  return ticket_classify<Char>(arg.value, index, named);
}

template <typename Char, typename T,
          FMT_ENABLE_IF(is_owned<typename std::decay<T>::type>::value)>
auto ticket_classify(T&& arg, int index,
                     std::vector<ticket_named_arg<Char>>& named)
    -> std::unique_ptr<ticket_arg_base<Char>> {
  ignore_unused(index, named);
  using value_type = typename std::decay<T>::type::value_type;
  return std::unique_ptr<ticket_arg_base<Char>>(
      new ticket_custom_arg<Char, value_type>(std::forward<T>(arg).get()));
}

template <typename Char, typename T,
          FMT_ENABLE_IF(!is_named_arg<typename std::decay<T>::type>::value &&
                        !is_owned<typename std::decay<T>::type>::value &&
                        has_to_string_view<T>::value)>
auto ticket_classify(T&& arg, int index,
                     std::vector<ticket_named_arg<Char>>& named)
    -> std::unique_ptr<ticket_arg_base<Char>> {
  ignore_unused(index, named);
  auto sv = to_string_view(arg);
  return std::unique_ptr<ticket_arg_base<Char>>(
      new ticket_string_arg<Char>(std::basic_string<Char>(sv.data(), sv.size())));
}

template <typename Char, typename T,
          FMT_ENABLE_IF(!is_named_arg<typename std::decay<T>::type>::value &&
                        !is_owned<typename std::decay<T>::type>::value &&
                        !has_to_string_view<T>::value &&
                        stored_type_constant<typename std::decay<T>::type,
                                             buffered_context<Char>>::value !=
                            detail::type::custom_type &&
                        stored_type_constant<typename std::decay<T>::type,
                                             buffered_context<Char>>::value !=
                            detail::type::none_type)>
auto ticket_classify(T&& arg, int index,
                     std::vector<ticket_named_arg<Char>>& named)
    -> std::unique_ptr<ticket_arg_base<Char>> {
  ignore_unused(index, named);
  using value_type = typename std::decay<T>::type;
  return std::unique_ptr<ticket_arg_base<Char>>(
      new ticket_builtin_arg<Char, value_type>(std::forward<T>(arg)));
}

template <typename Char, typename T,
          FMT_ENABLE_IF(!is_named_arg<typename std::decay<T>::type>::value &&
                        !is_owned<typename std::decay<T>::type>::value &&
                        !has_to_string_view<T>::value &&
                        (stored_type_constant<typename std::decay<T>::type,
                                              buffered_context<Char>>::value ==
                            detail::type::custom_type ||
                        stored_type_constant<typename std::decay<T>::type,
                                              buffered_context<Char>>::value ==
                            detail::type::none_type))>
auto ticket_classify(T&& arg, int index,
                     std::vector<ticket_named_arg<Char>>& named)
    -> std::unique_ptr<ticket_arg_base<Char>> {
  ignore_unused(arg, index, named);
  static_assert(sizeof(typename std::decay<T>::type) == 0,
                "ticket: custom arguments must be wrapped in fmt::own(...)");
  return nullptr;
}

// Validates the format string's reference contract (positional indices and
// named argument names) at ticket creation. Nested replacement fields in
// format specs are validated recursively.
template <typename Char>
class ticket_ref_checker {
 public:
  using named_ref = ticket_named_arg<Char>;

 private:
  int num_args_;
  const named_ref* named_;
  int num_named_;
  int auto_id_ = 0;
  bool have_auto_ = false;
  bool have_manual_ = false;

  void check_index(int id) {
    if (id < 0 || id >= num_args_) report_error("argument not found");
  }
  auto check_name(basic_string_view<Char> name) -> int {
    for (int i = 0; i < num_named_; ++i) {
      if (basic_string_view<Char>(named_[i].name) == name)
        return named_[i].id;
    }
    report_error("argument not found");
    return -1;
  }

 public:
  ticket_ref_checker(int num_args, const named_ref* named, int num_named)
      : num_args_(num_args),
        named_(named),
        num_named_(num_named) {}

  void on_text(const Char*, const Char*) {}
  auto on_arg_id() -> int {
    if (have_manual_)
      report_error("cannot switch from manual to automatic argument indexing");
    have_auto_ = true;
    int id = auto_id_++;
    check_index(id);
    return id;
  }
  auto on_arg_id(int id) -> int {
    if (have_auto_)
      report_error("cannot switch from automatic to manual argument indexing");
    have_manual_ = true;
    check_index(id);
    return id;
  }
  auto on_arg_id(basic_string_view<Char> name) -> int {
    return check_name(name);
  }
  void on_replacement_field(int, const Char*) {}
  auto on_format_specs(int, const Char* begin, const Char* end) -> const Char* {
    while (begin != end) {
      Char c = *begin++;
      if (c == '{') {
        if (begin == end) report_error("invalid format string");
        begin = parse_replacement_field(begin - 1, end, *this);
      } else if (c == '}') {
        return begin - 1;
      }
    }
    report_error("unknown format specifier");
    return end;
  }
  FMT_NORETURN void on_error(const char* message) { report_error(message); }
};

}  // namespace detail

/**
 * A deferred formatting ticket that owns its arguments.
 *
 * A ticket is created with a format string and arguments, but formatting only
 * happens when `consume` is called. The ticket copies string arguments into
 * its own storage and never retains a temporary string_view. Custom arguments
 * must be wrapped in `own` so that the ticket owns them.
 *
 * The format string's reference contract (positional indices and named
 * argument names) is checked at creation. `consume` is thread-safe: exactly
 * one caller obtains the formatted result; any further consumption (including
 * concurrent consumption) reports an error.
 */
template <typename Char>
class basic_format_ticket {
 public:
  using char_type = Char;

  basic_format_ticket() = default;

  basic_format_ticket(const basic_format_ticket&) = delete;
  auto operator=(const basic_format_ticket&)
      -> basic_format_ticket& = delete;

  basic_format_ticket(basic_format_ticket&& other) noexcept
      : fmt_(std::move(other.fmt_)),
        args_(std::move(other.args_)),
        named_(std::move(other.named_)),
        consumed_(other.consumed_.load(std::memory_order_acquire)) {
    other.consumed_.store(true, std::memory_order_release);
  }

  auto operator=(basic_format_ticket&& other) noexcept
      -> basic_format_ticket& {
    if (this != &other) {
      fmt_ = std::move(other.fmt_);
      args_ = std::move(other.args_);
      named_ = std::move(other.named_);
      consumed_.store(other.consumed_.load(std::memory_order_acquire),
                      std::memory_order_release);
      other.consumed_.store(true, std::memory_order_release);
    }
    return *this;
  }

  template <typename... Args>
  basic_format_ticket(basic_string_view<Char> fmt, Args&&... args)
      : fmt_(fmt.data(), fmt.size()) {
    args_.reserve(sizeof...(Args));
    named_.reserve(sizeof...(Args));
    int index = 0;
    using expander = int[];
    (void)expander{
        0, (args_.push_back(detail::ticket_classify<Char>(
                std::forward<Args>(args), index, named_)),
            ++index, 0)...};
    detail::ticket_ref_checker<Char> checker(
        static_cast<int>(args_.size()), named_.data(),
        static_cast<int>(named_.size()));
    detail::parse_format_string<Char>(fmt_, checker);
  }

  template <typename... Args>
  basic_format_ticket(runtime_format_string<Char> fmt, Args&&... args)
      : basic_format_ticket(fmt.str, std::forward<Args>(args)...) {}

  /**
   * Formats the ticket. Formatting happens only once: exactly one caller
   * obtains the result; any other call (concurrent or subsequent) reports an
   * error.
   */
  auto consume() -> std::basic_string<Char> {
    bool expected = false;
    if (!consumed_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel))
      FMT_THROW(format_error("format ticket has already been consumed"));
    const int n = static_cast<int>(args_.size());
    std::vector<basic_format_arg<buffered_context<Char>>> argv(n + 1);
    for (int i = 0; i < n; ++i) argv[i + 1] = args_[i]->make_arg();
    std::vector<detail::named_arg_info<Char>> infos;
    if (!named_.empty()) {
      infos.reserve(named_.size());
      for (const auto& nr : named_) infos.push_back({nr.name.c_str(), nr.id});
      argv[0] = basic_format_arg<buffered_context<Char>>(
          infos.data(), static_cast<int>(infos.size()));
    }
    basic_format_args<buffered_context<Char>> all_args(
        argv.data() + 1, n, !named_.empty());
    auto buffer = basic_memory_buffer<Char>();
    auto out = basic_appender<Char>(buffer);
    detail::parse_format_string<Char>(
        fmt_, detail::format_handler<Char>{
                  parse_context<Char>(fmt_), {out, all_args, locale_ref()}});
    return std::basic_string<Char>(buffer.data(), buffer.size());
  }

  explicit operator bool() const noexcept {
    return !consumed_.load(std::memory_order_acquire);
  }

 private:
  std::basic_string<Char> fmt_;
  std::vector<std::unique_ptr<detail::ticket_arg_base<Char>>> args_;
  std::vector<detail::ticket_named_arg<Char>> named_;
  std::atomic<bool> consumed_{false};
};

using format_ticket = basic_format_ticket<char>;

FMT_END_NAMESPACE

#endif  // FMT_TICKET_H
