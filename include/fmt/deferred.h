// Formatting library for C++ - deferred (lazy) formatting tickets
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_DEFERRED_H_
#define FMT_DEFERRED_H_

#include <atomic>     // std::atomic
#include <memory>     // std::unique_ptr
#include <stdexcept>  // std::logic_error
#include <string>     // std::string
#include <tuple>      // std::tuple
#include <utility>    // std::forward, std::move

#include "format.h"

FMT_BEGIN_NAMESPACE

// Deferred formatting tickets for asynchronous logging.
//
// A fmt::deferred::ticket captures a format string together with an
// *owned* snapshot of its arguments so that the expensive formatting work
// can be handed off (e.g. to a logger thread) and performed later.
//
// Lifecycle:
//
// 1. Creation (cheap, no formatting): string arguments (const char*,
//    std::string, fmt::string_view, ...) are *copied* into the ticket's
//    own storage - the ticket never retains a caller-side string_view or
//    pointer to a temporary. Arithmetic and other built-in arguments are
//    stored by value. User-defined (custom) types are only accepted when
//    explicitly wrapped with fmt::deferred::own(), which moves/copies the
//    object into the ticket; passing an unwrapped custom object is a
//    compile-time error, so the ticket can never silently reference an
//    external object. Named arguments are created with
//    fmt::deferred::arg() which copies both the name and the value.
//    The format string's reference contract (argument indices, named
//    argument references, brace balance) is validated at creation: at
//    compile time for string literals (the usual fmt compile-time checks)
//    and additionally with a runtime parse for fmt::runtime() strings,
//    throwing fmt::format_error on violation.
//
// 2. Transfer: tickets are move-only. Moving transfers the formatting
//    state; a moved-from ticket is considered consumed.
//
// 3. Consumption: consume() performs the actual formatting and returns
//    the resulting std::string. It is single-shot and thread-safe:
//    concurrent consume() calls race and exactly one caller obtains the
//    result; every other call (including any repeat call) throws
//    fmt::deferred::consumption_error.
//
// 4. Destruction: destroying a ticket that was never consumed is fine
//    and performs no formatting work.
//
// Example:
//
//     auto t = fmt::deferred::ticket("answer: {answer}",
//                                    fmt::deferred::arg("answer", 42));
//     // ... hand t off to a worker thread ...
//     std::string s = std::move(t).consume();  // formats exactly here
namespace deferred {

// An owning wrapper for a custom (user-defined) formatting argument.
// Created with fmt::deferred::own(). The wrapped object is stored by
// value inside a deferred ticket.
template <typename T> struct owned {
  using stored_type = T;
  T value;
};

// An owning named argument for a deferred ticket, created with
// fmt::deferred::arg(). Both the name and the value are owned.
template <typename V> struct named {
  using stored_type = V;
  std::string name;
  V value;
};

namespace detail {

template <typename T> struct is_owned_wrapper : std::false_type {};
template <typename T> struct is_owned_wrapper<owned<T>> : std::true_type {};
template <typename T> struct is_named_wrapper : std::false_type {};
template <typename T> struct is_named_wrapper<named<T>> : std::true_type {};

// A minimal index_sequence implementation (fmt targets C++11).
template <size_t... Is> struct index_sequence {};
template <size_t N, size_t... Is>
struct make_index_sequence_impl
    : make_index_sequence_impl<N - 1, N - 1, Is...> {};
template <size_t... Is> struct make_index_sequence_impl<0, Is...> {
  using type = index_sequence<Is...>;
};
template <size_t N>
using make_index_sequence = typename make_index_sequence_impl<N>::type;

// Argument storage categories.
struct string_tag {};  // copied into a std::string
struct value_tag {};   // stored by value as-is
struct owned_tag {};   // unwrapped from fmt::deferred::owned
struct named_tag {};   // fmt::deferred::named, already owning

template <typename T>
struct is_string_like
    : std::integral_constant<
          bool,
          fmt::detail::mapped_type_constant<T, char>::value ==
                  fmt::detail::type::string_type ||
              fmt::detail::mapped_type_constant<T, char>::value ==
                  fmt::detail::type::cstring_type> {};

template <typename A> struct arg_traits {
  using decayed = typename std::decay<A>::type;
  static constexpr bool owned = is_owned_wrapper<decayed>::value;
  static constexpr bool named = is_named_wrapper<decayed>::value;
  static constexpr bool string =
      !owned && !named && is_string_like<decayed>::value;
  using tag = typename std::conditional<
      owned, owned_tag,
      typename std::conditional<
          named, named_tag,
          typename std::conditional<string, string_tag, value_tag>::type>::
          type>::type;
};

// Maps a user-supplied argument to the type stored inside a ticket.
template <typename A, typename Tag = typename arg_traits<A>::tag>
struct value_storage;
template <typename A> struct value_storage<A, string_tag> {
  using type = std::string;
};
template <typename A> struct value_storage<A, value_tag> {
  using type = typename std::decay<A>::type;
};
template <typename A> struct value_storage<A, owned_tag> {
  using type = typename std::decay<A>::type::stored_type;
};
template <typename A> struct value_storage<A, named_tag> {
  using type = typename std::decay<A>::type;  // named<V> is its own storage
};
template <typename A> using value_storage_t = typename value_storage<A>::type;

// Converts a user-supplied argument into its owned storage form.
inline auto store(string_tag, string_view s) -> std::string {
  return std::string(s.data(), s.size());
}
template <typename A>
auto store(value_tag, A&& a) -> typename std::decay<A>::type {
  return std::forward<A>(a);
}
template <typename T> auto store(owned_tag, owned<T>& a) -> T {
  return a.value;
}
template <typename T> auto store(owned_tag, owned<T>&& a) -> T {
  return std::move(a.value);
}
template <typename V> auto store(named_tag, named<V>& a) -> named<V> {
  return a;
}
template <typename V> auto store(named_tag, named<V>&& a) -> named<V> {
  return std::move(a);
}

// Maps a user-supplied argument to the type used in the compile-time
// format string check (and produced at consume time).
template <typename A, typename Tag = typename arg_traits<A>::tag>
struct view_of;
template <typename A> struct view_of<A, string_tag> {
  using type = string_view;
};
template <typename A> struct view_of<A, value_tag> {
  using type = typename std::decay<A>::type;
};
template <typename A> struct view_of<A, owned_tag> {
  using type = const typename std::decay<A>::type::stored_type&;
};
template <typename A> struct view_of<A, named_tag> {
  using type =
      fmt::named_arg<typename std::decay<A>::type::stored_type>;
};
template <typename A> using view_t = typename view_of<A>::type;

// An argument is accepted if it is an owning wrapper, an owning named
// argument, or a built-in (non-custom) formattable type. Custom types must
// be wrapped with fmt::deferred::own().
template <typename A>
struct is_valid_arg
    : std::integral_constant<
          bool,
          is_owned_wrapper<typename std::decay<A>::type>::value ||
              is_named_wrapper<typename std::decay<A>::type>::value ||
              (fmt::detail::mapped_type_constant<
                       typename std::decay<A>::type, char>::value !=
                   fmt::detail::type::custom_type &&
               fmt::is_formattable<typename std::decay<A>::type,
                                   char>::value)> {};

template <typename... Args> struct all_valid : std::true_type {};
template <typename A, typename... Rest>
struct all_valid<A, Rest...>
    : std::integral_constant<bool, is_valid_arg<A>::value &&
                                       all_valid<Rest...>::value> {};

// Validates the format string's reference contract (argument indices,
// named argument references, brace balance) without formatting anything.
class reference_checker {
 public:
  reference_checker(int num_args, const char* const* names, int num_named)
      : num_args_(num_args), names_(names), num_named_(num_named) {}

  void on_text(const char*, const char*) {}

  auto on_arg_id() -> int {
    if (next_arg_id_ < 0)
      fmt::report_error(
          "cannot switch from manual to automatic argument indexing");
    return check_range(next_arg_id_++);
  }
  auto on_arg_id(int id) -> int {
    if (next_arg_id_ > 0)
      fmt::report_error(
          "cannot switch from automatic to manual argument indexing");
    next_arg_id_ = -1;
    return check_range(id);
  }
  auto on_arg_id(string_view name) -> int {
    next_arg_id_ = -1;
    for (int i = 0; i < num_named_; ++i) {
      if (name.compare(string_view(names_[i])) == 0) return i;
    }
    fmt::report_error("argument not found");
    return 0;
  }

  void on_replacement_field(int, const char*) {}

  auto on_format_specs(int, const char* begin, const char* end)
      -> const char* {
    const char* it = begin;
    while (it != end && *it != '}') {
      if (*it == '{')
        it = fmt::detail::parse_replacement_field(it, end, *this);
      else
        ++it;
    }
    return it;
  }

  FMT_NORETURN void on_error(const char* message) {
    fmt::report_error(message);
  }

 private:
  auto check_range(int id) -> int {
    if (id < 0 || id >= num_args_)
      fmt::report_error("argument not found");
    return id;
  }

  int num_args_;
  int next_arg_id_ = 0;
  const char* const* names_;
  int num_named_;
};

template <typename T>
void collect_name(const char**, int&, const T&) {}
template <typename V>
void collect_name(const char** names, int& n, const named<V>& a) {
  for (int i = 0; i < n; ++i) {
    if (a.name == names[i])
      fmt::report_error("duplicate named argument");
  }
  names[n] = a.name.c_str();
  ++n;
}

// Produces the fmt argument for a stored value at consume time.
inline auto as_arg(const std::string& s) -> string_view {
  return string_view(s.data(), s.size());
}
template <typename T> auto as_arg(const T& value) -> const T& {
  return value;
}
// A named argument references the stored value directly: binding a
// temporary to named_arg's reference member would dangle.
template <typename V>
auto as_arg(const named<V>& a) -> fmt::named_arg<V> {
  return fmt::named_arg<V>(a.name.c_str(), a.value);
}

struct holder_base {
  virtual ~holder_base() {}
  virtual auto format() const -> std::string = 0;
};

template <typename... Stored> struct holder final : holder_base {
  std::string format_str;
  std::tuple<Stored...> args;

  holder(string_view fs, Stored... a)
      : format_str(fs.data(), fs.size()), args(std::move(a)...) {}

  // Validates the format string's reference contract without formatting.
  void validate() const {
    const char* names[sizeof...(Stored) != 0 ? sizeof...(Stored) : 1];
    int num_named = 0;
    collect_names(names, num_named,
                  make_index_sequence<sizeof...(Stored)>());
    reference_checker checker(int(sizeof...(Stored)), names, num_named);
    fmt::detail::parse_format_string<char>(
        string_view(format_str.data(), format_str.size()), checker);
  }

  auto format() const -> std::string override {
    return do_format(make_index_sequence<sizeof...(Stored)>());
  }

 private:
  template <size_t... Is>
  void collect_names(const char** names, int& n,
                     index_sequence<Is...>) const {
    using expand = int[];
    (void)expand{0, (collect_name(names, n, std::get<Is>(args)), 0)...};
  }

  template <size_t... Is>
  auto do_format(index_sequence<Is...>) const -> std::string {
    return fmt::format(fmt::runtime(format_str),
                       as_arg(std::get<Is>(args))...);
  }
};

template <typename... Args>
auto make_holder(string_view format_str, Args&&... args)
    -> std::unique_ptr<holder_base> {
  typedef holder<value_storage_t<Args>...> holder_type;
  std::unique_ptr<holder_type> h(
      new holder_type(format_str,
                      store(typename arg_traits<Args>::tag(),
                            std::forward<Args>(args))...));
  h->validate();
  return std::move(h);
}

}  // namespace detail

// Wraps a custom (user-defined) formatting argument so that it can be
// stored in a deferred ticket. The object is moved or copied into the
// wrapper and later into the ticket; the ticket never references the
// original object.
//
// Example:
//
//     auto t = fmt::deferred::ticket("{}", fmt::deferred::own(my_object));
template <typename T>
auto own(T&& value) -> owned<typename std::decay<T>::type> {
  static_assert(
      !detail::is_owned_wrapper<typename std::decay<T>::type>::value,
      "nested fmt::deferred::own() is not allowed");
  return owned<typename std::decay<T>::type>{std::forward<T>(value)};
}

// Creates an owning named argument for a deferred ticket. Both the name
// and the value are copied into the ticket's own storage; custom types
// must be wrapped with fmt::deferred::own().
//
// Example:
//
//     auto t = fmt::deferred::ticket("x = {x}", fmt::deferred::arg("x", 42));
template <typename A>
auto arg(string_view name, A&& value) -> named<detail::value_storage_t<A>> {
  typedef typename std::decay<A>::type decayed;
  static_assert(!detail::is_named_wrapper<decayed>::value,
                "nested named arguments are not allowed");
  static_assert(
      detail::is_owned_wrapper<decayed>::value ||
          detail::is_string_like<decayed>::value ||
          (fmt::detail::mapped_type_constant<decayed, char>::value !=
               fmt::detail::type::custom_type &&
           fmt::is_formattable<decayed, char>::value),
      "custom argument types must be wrapped with fmt::deferred::own()");
  return named<detail::value_storage_t<A>>{
      std::string(name.data(), name.size()),
      detail::store(typename detail::arg_traits<A>::tag(),
                    std::forward<A>(value))};
}

// An error reported when a deferred ticket is consumed more than once or
// after being moved from.
class consumption_error : public std::logic_error {
 public:
  explicit consumption_error(const char* message)
      : std::logic_error(message) {}
};

// A deferred formatting ticket: owns its format string and arguments,
// validates the format string's reference contract at creation, and
// formats exactly once when consume() is called. See the documentation of
// the fmt::deferred namespace for the lifecycle description.
class ticket {
 public:
  ticket(ticket&& other) noexcept
      : impl_(std::move(other.impl_)),
        consumed_(other.consumed_.exchange(true)) {}
  auto operator=(ticket&& other) noexcept -> ticket& {
    impl_ = std::move(other.impl_);
    consumed_.store(other.consumed_.exchange(true));
    return *this;
  }
  ticket(const ticket&) = delete;
  auto operator=(const ticket&) -> ticket& = delete;

  // Captures args and the format string. Arguments are stored as
  // described in the fmt::deferred namespace documentation; the format
  // string's reference contract is validated at creation.
  template <typename... Args,
            FMT_ENABLE_IF(deferred::detail::all_valid<Args...>::value)>
  explicit ticket(format_string<deferred::detail::view_t<Args>...> fmt_str,
                  Args&&... args)
      : impl_(deferred::detail::make_holder(fmt_str.get(),
                                            std::forward<Args>(args)...)) {}

  // Performs the deferred formatting and returns the result. Exactly one
  // call (across all threads) succeeds; any other call throws
  // fmt::deferred::consumption_error. If formatting itself fails,
  // fmt::format_error propagates and the ticket is still consumed.
  auto consume() -> std::string {
    if (!impl_ || consumed_.exchange(true))
      FMT_THROW(consumption_error(
          "deferred format ticket has already been consumed"));
    return impl_->format();
  }

  // Returns whether this ticket has been consumed (or moved from).
  auto consumed() const noexcept -> bool { return consumed_.load(); }

 private:
  std::unique_ptr<deferred::detail::holder_base> impl_;
  std::atomic<bool> consumed_{false};
};

}  // namespace deferred
FMT_END_NAMESPACE

#endif  // FMT_DEFERRED_H_
