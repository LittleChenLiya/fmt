// Formatting library for C++ - buffered record formatting with commit
// callbacks.
//
// For the license information refer to format.h.

#ifndef FMT_RECORD_BUFFER_H_
#define FMT_RECORD_BUFFER_H_

#include <cstddef>  // std::size_t

#include "format.h"

FMT_BEGIN_NAMESPACE

/**
  A buffer that formats a complete record and commits it through a
  caller-provided callback.

  A record is formatted in full before the commit callback is invoked, so the
  callback always observes a complete record and is never called for a record
  that failed to format.

  Exception guarantees:

  - If formatting throws (a format error or a failing custom formatter) or
    the encoded record exceeds the capacity, the commit callback is not
    executed, not even once, and no record is left pending.
  - If the commit callback throws, the record is kept and can be committed
    later with `retry` without re-invoking any formatter. Only a successful
    return from the callback consumes the record.
  - Calling `format`, `vformat`, `retry` or `discard` from inside the
    commit callback (reentrancy) is rejected with a `format_error`.

  The capacity limits the number of encoded code units (`sizeof(Char)` units
  each, e.g. bytes for UTF-8) of a single record. After a successful commit
  the buffer is reused for the next record.

  A `basic_record_buffer` is not thread-safe: each thread must use its own
  instance and instances must not be shared between threads.
 */
template <typename Char = char> class basic_record_buffer {
 private:
  basic_memory_buffer<Char> buffer_;
  size_t capacity_;
  bool pending_ = false;
  bool committing_ = false;

  void check_ready() const {
    if (committing_)
      FMT_THROW(format_error("reentrant record_buffer commit"));
    if (pending_)
      FMT_THROW(format_error("record_buffer has an uncommitted record"));
  }

  template <typename Commit> void do_commit(Commit& commit) {
    committing_ = true;
    try {
      commit(basic_string_view<Char>(buffer_.data(), buffer_.size()));
    } catch (...) {
      committing_ = false;
      throw;  // Keep the record pending so it can be retried.
    }
    committing_ = false;
    buffer_.clear();
    pending_ = false;
  }

 public:
  explicit basic_record_buffer(size_t capacity) : capacity_(capacity) {}

  basic_record_buffer(const basic_record_buffer&) = delete;
  void operator=(const basic_record_buffer&) = delete;

  /** Returns the maximum encoded size of a record in code units. */
  auto capacity() const -> size_t { return capacity_; }

  /** Returns whether a record is waiting to be committed. */
  auto has_pending() const -> bool { return pending_; }

  /** Returns the pending record, or an empty view if there is none. */
  auto pending_record() const -> basic_string_view<Char> {
    return pending_ ? basic_string_view<Char>(buffer_.data(), buffer_.size())
                    : basic_string_view<Char>();
  }

  /** Discards the pending record without committing it. */
  void discard() {
    if (committing_)
      FMT_THROW(format_error("reentrant record_buffer commit"));
    buffer_.clear();
    pending_ = false;
  }

  /**
    Formats a record and commits it via `commit`. If the commit callback
    throws, the record is kept pending and can be committed with `retry`
    without reformatting.
   */
  template <typename Commit, typename... T>
  void format(Commit&& commit, format_string<T...> fmt, T&&... args) {
    vformat(static_cast<Commit&&>(commit), fmt.str,
            make_format_args<buffered_context<Char>>(args...));
  }

  /**
    Formats a record from a runtime format string and type-erased arguments
    (e.g. a `dynamic_format_arg_store`) and commits it via `commit`.
   */
  template <typename Commit>
  void vformat(Commit&& commit, basic_string_view<Char> fmt,
               basic_format_args<buffered_context<Char>> args) {
    check_ready();
    buffer_.clear();
    detail::vformat_to(buffer_, fmt, args);
    if (buffer_.size() > capacity_) {
      buffer_.clear();
      FMT_THROW(format_error("record_buffer record exceeds capacity"));
    }
    pending_ = true;
    do_commit(commit);
  }

  /**
    Retries committing the pending record without reformatting it. Throws
    `format_error` if there is no pending record.
   */
  template <typename Commit> void retry(Commit&& commit) {
    if (committing_)
      FMT_THROW(format_error("reentrant record_buffer commit"));
    if (!pending_)
      FMT_THROW(format_error("record_buffer has no record to retry"));
    do_commit(commit);
  }
};

using record_buffer = basic_record_buffer<char>;

FMT_END_NAMESPACE

#endif  // FMT_RECORD_BUFFER_H_
