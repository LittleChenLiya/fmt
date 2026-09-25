// Formatting library for C++ - buffered record formatting with commit
// callbacks.
//
// For the license information refer to format.h.

#ifndef FMT_RECORD_BUFFER_H_
#define FMT_RECORD_BUFFER_H_

#include <memory>     // std::unique_ptr
#include <stdexcept>  // std::logic_error

#include "format.h"

FMT_BEGIN_NAMESPACE
namespace detail {

// Thrown internally when a record exceeds the record_buffer capacity. It is
// always caught by record_buffer and never escapes into user code.
class record_overflow {
 public:
  auto what() const noexcept -> const char* {
    return "record exceeds the record_buffer capacity";
  }
};

// A grow handler that turns buffer growth into a record_overflow exception,
// enforcing the fixed capacity of a record_buffer.
inline void record_store_grow(buffer<char>&, size_t) {
  FMT_THROW(record_overflow());
}

// A fixed-capacity contiguous buffer that never allocates.
class record_store : public buffer<char> {
 public:
  record_store(char* data, size_t capacity)
      : buffer<char>(&record_store_grow, data, 0, capacity) {}
};

// Resets a flag on destruction, including during exception unwinding.
struct flag_guard {
  bool& flag;
  ~flag_guard() { flag = false; }
};
}  // namespace detail

/**
 * A fixed-capacity buffer that formats a complete record first and then
 * hands it to a caller-provided commit callback (sink).
 *
 * A record is formatted in full into an internal buffer limited to
 * `capacity` encoded bytes. Only a successfully formatted record can be
 * submitted to the sink with `commit`. If formatting throws, the record
 * exceeds the capacity, or a user-defined formatter fails, the sink is
 * never called. The capacity is enforced on the encoded output, e.g. on
 * UTF-8 bytes for `char` records.
 *
 * If the sink throws during `commit`, the record is kept and can be
 * resubmitted by calling `commit` again without re-running the formatter.
 * Only a sink call that returns normally consumes the record. After a
 * successful commit the buffer is empty and can be reused.
 *
 * This facility is independent of `fmt::print` and does not change its
 * semantics in any way.
 *
 * **Example**:
 *
 *     auto buf = fmt::record_buffer(1024);
 *     buf.format("answer={}", 42);
 *     buf.commit([](fmt::string_view record) {
 *       sink_write(record.data(), record.size());  // may throw
 *     });
 */
class record_buffer {
 public:
  /** Constructs a record buffer with the given capacity in bytes. */
  explicit record_buffer(size_t capacity)
      : data_(capacity != 0 ? new char[capacity] : nullptr),
        buf_(data_.get(), capacity),
        capacity_(capacity) {}

  record_buffer(record_buffer&&) = default;
  record_buffer(const record_buffer&) = delete;
  void operator=(const record_buffer&) = delete;

  /** Returns the buffer capacity in encoded bytes. */
  auto capacity() const noexcept -> size_t { return capacity_; }

  /** Returns whether a formatted record is waiting to be committed. */
  auto pending() const noexcept -> bool { return pending_; }

  /** Returns the size of the pending record in bytes or 0 if there is none. */
  auto size() const noexcept -> size_t { return pending_ ? buf_.size() : 0; }

  /** Returns the pending record or an empty view if there is none. */
  auto record() const -> string_view {
    return pending_ ? string_view(buf_.data(), buf_.size()) : string_view();
  }

  /**
   * Formats a record into the buffer, replacing any uncommitted record.
   *
   * Returns `true` if the whole record fits into the buffer and `false`
   * if its encoded length exceeds the capacity. On failure, as well as when
   * a format error or a user-defined formatter throws, the buffer is left
   * empty (strong exception guarantee) and no record becomes pending, so a
   * subsequent `commit` does not call the sink.
   *
   * Both compile-time and runtime format strings are supported, e.g. via
   * `fmt::runtime`. Use `vformat` for type-erased dynamic arguments.
   */
  template <typename... T>
  auto format(format_string<T...> fmt, T&&... args) -> bool {
    return vformat(fmt.str, vargs<T...>{{args...}});
  }

  /**
   * A dynamic-argument counterpart of `format` taking a runtime format
   * string and type-erased arguments, e.g. from
   * `fmt::dynamic_format_arg_store`.
   */
  auto vformat(string_view fmt, format_args args) -> bool {
    if (committing_)
      FMT_THROW(std::logic_error("fmt::record_buffer: format during commit"));
    buf_.clear();
    pending_ = false;
#if FMT_USE_EXCEPTIONS
    try {
      detail::vformat_to(buf_, fmt, args, locale_ref());
    } catch (const detail::record_overflow&) {
      // The record exceeds the capacity: discard it and report failure.
      buf_.clear();
      return false;
    } catch (...) {
      // Strong guarantee: no partially formatted record is left behind.
      buf_.clear();
      throw;
    }
#else
    detail::vformat_to(buf_, fmt, args, locale_ref());
#endif
    // Re-check the encoded length at the capacity boundary.
    if (buf_.size() > capacity_) {
      buf_.clear();
      return false;
    }
    pending_ = true;
    return true;
  }

  /**
   * Submits the pending record to `sink`, a callable taking
   * `fmt::string_view`.
   *
   * Returns `false` without calling `sink` if there is no pending
   * record. If `sink` throws, the exception propagates to the caller, the
   * record is kept and remains pending, so it can be resubmitted by calling
   * `commit` again; the formatter is not re-run on retry. Only a `sink`
   * call that returns normally consumes the record: the buffer is then
   * cleared and becomes reusable. Calling `commit` on the same buffer
   * from within the sink is rejected with `std::logic_error`.
   */
  template <typename Commit>
  auto commit(Commit&& sink) -> bool {
    if (!pending_) return false;
    if (committing_)
      FMT_THROW(std::logic_error("fmt::record_buffer: reentrant commit"));
    committing_ = true;
    auto guard = detail::flag_guard{committing_};
    sink(string_view(buf_.data(), buf_.size()));
    // The sink returned normally: the record is consumed.
    buf_.clear();
    pending_ = false;
    return true;
  }

  /** Discards the pending record, if any, making the buffer reusable. */
  void discard() noexcept {
    buf_.clear();
    pending_ = false;
  }

 private:
  std::unique_ptr<char[]> data_;
  detail::record_store buf_;
  size_t capacity_;
  bool pending_ = false;
  bool committing_ = false;
};

/**
 * Returns a record buffer local to the current thread. Each thread gets its
 * own buffer on first use; buffers are never shared between threads. The
 * `capacity` argument only takes effect on the first call in each thread.
 */
inline auto thread_record_buffer(size_t capacity = 4096) -> record_buffer& {
  thread_local record_buffer buf(capacity);
  return buf;
}

FMT_END_NAMESPACE

#endif  // FMT_RECORD_BUFFER_H_
