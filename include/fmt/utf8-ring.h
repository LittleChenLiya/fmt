// Formatting library for C++ - fixed-capacity UTF-8 ring buffer
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_UTF8_RING_H_
#define FMT_UTF8_RING_H_

#include <cstddef>   // std::size_t, std::ptrdiff_t
#include <iterator>  // std::output_iterator_tag
#include <string>
#include <vector>

#include "core.h"

FMT_BEGIN_NAMESPACE

/// A fixed-capacity ring buffer that keeps the most recent complete UTF-8
/// code points written to it and reports the number of discarded bytes.
///
/// Bytes are appended through a fmt output iterator (see `out`), e.g.:
///
///     auto ring = fmt::utf8_ring_buffer(5);
///     fmt::format_to(ring.out(), "A猫BC");
///     // ring.str() == "猫BC", ring.discarded() == 1
///
/// When the buffer is full the oldest complete code points are evicted, so
/// the stored contents never cut a multi-byte character in half. A code
/// point larger than the capacity is dropped entirely and counted. Newlines
/// and other bytes are ordinary content. Formatting through `out` invokes
/// each formatter exactly once, like any other `fmt::format_to` call.
class utf8_ring_buffer {
 public:
  explicit utf8_ring_buffer(size_t capacity)
      : storage_(capacity), capacity_(capacity) {}

  // The buffer cannot be copied. It can be moved, but iterators obtained
  // from `out` before the move are left dangling.
  utf8_ring_buffer(const utf8_ring_buffer&) = delete;
  auto operator=(const utf8_ring_buffer&) -> utf8_ring_buffer& = delete;
  utf8_ring_buffer(utf8_ring_buffer&&) = default;
  auto operator=(utf8_ring_buffer&&) -> utf8_ring_buffer& = default;

  ~utf8_ring_buffer() {
    // A truncated trailing code point cannot be stored; count it as
    // discarded.
    discarded_ += pending_used_;
  }

  /// An output iterator that appends bytes to a `utf8_ring_buffer`.
  class iterator {
   public:
    using iterator_category = std::output_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = void;

    auto operator=(char c) -> iterator& {
      buffer_->push(c);
      return *this;
    }
    auto operator*() -> iterator& { return *this; }
    auto operator++() -> iterator& { return *this; }
    auto operator++(int) -> iterator { return *this; }

   private:
    friend class utf8_ring_buffer;
    explicit iterator(utf8_ring_buffer* buffer) : buffer_(buffer) {}
    utf8_ring_buffer* buffer_;
  };

  /// Returns an output iterator that writes into this buffer.
  auto out() -> iterator { return iterator(this); }

  /// The number of bytes discarded so far: evicted code points, code points
  /// larger than the capacity, and truncated trailing bytes.
  auto discarded() const -> size_t { return discarded_; }

  auto capacity() const -> size_t { return capacity_; }
  auto size() const -> size_t { return size_; }
  auto empty() const -> bool { return size_ == 0; }

  /// Returns the current contents, oldest code point first.
  auto str() const -> std::string {
    auto result = std::string();
    result.reserve(size_);
    for (size_t i = 0; i < size_; ++i)
      result.push_back(storage_[(start_ + i) % capacity_]);
    return result;
  }

 private:
  static auto code_point_length(unsigned char lead) -> size_t {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 1;  // Invalid lead byte: treat as a single byte.
  }

  void push(char c) {
    if (pending_expected_ == 0) {
      pending_[0] = c;
      size_t length = code_point_length(static_cast<unsigned char>(c));
      if (length == 1) {
        commit(pending_, 1);
      } else {
        pending_expected_ = length;
        pending_used_ = 1;
      }
      return;
    }
    pending_[pending_used_++] = c;
    if (pending_used_ == pending_expected_) {
      commit(pending_, pending_expected_);
      pending_expected_ = 0;
      pending_used_ = 0;
    }
  }

  void commit(const char* code_point, size_t length) {
    if (length > capacity_) {
      // A code point larger than the capacity never fits: drop it whole.
      discarded_ += length;
      return;
    }
    while (size_ + length > capacity_) evict_oldest();
    for (size_t i = 0; i < length; ++i) {
      storage_[(start_ + size_) % capacity_] = code_point[i];
      ++size_;
    }
  }

  void evict_oldest() {
    size_t length =
        code_point_length(static_cast<unsigned char>(storage_[start_]));
    start_ = (start_ + length) % capacity_;
    size_ -= length;
    discarded_ += length;
  }

  std::vector<char> storage_;
  size_t capacity_ = 0;
  size_t start_ = 0;
  size_t size_ = 0;
  size_t discarded_ = 0;
  char pending_[4] = {};
  size_t pending_expected_ = 0;
  size_t pending_used_ = 0;
};

FMT_END_NAMESPACE

#endif  // FMT_UTF8_RING_H_
