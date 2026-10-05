/*
  Formatting library for C++ - UTF-8 ring buffer output iterator adapter

  Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef FMT_RING_BUFFER_H_
#define FMT_RING_BUFFER_H_

#include <cstring>  // std::memcpy, std::memmove
#include <memory>   // std::shared_ptr

#include "format.h"

FMT_BEGIN_NAMESPACE

/**
 * An output iterator adapter that writes complete UTF-8 code points to a
 * fixed-capacity ring buffer, keeping only the latest content and returning
 * the number of discarded bytes.
 *
 * Unlike a regular output iterator, this adapter never truncates a multi-byte
 * UTF-8 character. If a code point does not fit in the remaining capacity, the
 * oldest complete code points are discarded to make room. If a code point is
 * larger than the total capacity, it is discarded entirely and counted.
 *
 * Newline characters are treated as ordinary content.
 *
 * The iterator's state is shared across all copies, so passing the iterator
 * by value to `format_to` and then inspecting it afterwards works as expected.
 *
 * **Example**:
 *
 *     char buf[5];
 *     fmt::ring_buffer_iterator it(buf, sizeof(buf));
 *     fmt::format_to(it, "A{}BC", "猫");
 *     // buf contains "猫BC" (5 bytes, complete UTF-8)
 *     // it.discarded() == 1 (the byte of 'A')
 */
template <typename T = char>
class basic_ring_buffer_iterator {
 private:
  struct state {
    T* buffer;         ///< Pointer to the ring buffer storage.
    size_t capacity;   ///< Total capacity of the ring buffer (in bytes).
    size_t size;       ///< Current number of bytes in the ring buffer.
    size_t discarded;  ///< Total number of discarded bytes.

    static constexpr size_t max_utf8_length = 4;
    T utf8_buf[max_utf8_length];  ///< Accumulator for the current code point.
    size_t utf8_len;              ///< Accumulated length of the current code point.
    size_t utf8_expected;         ///< Expected length of the current code point.
  };
  std::shared_ptr<state> state_;

  /// Returns the byte length of the UTF-8 code point starting at `data`.
  static FMT_CONSTEXPR auto utf8_length(const T* data) -> size_t {
    unsigned char c = static_cast<unsigned char>(data[0]);
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  // Invalid leading byte; treat as a single byte.
  }

  /// Writes a complete UTF-8 code point to the ring buffer.
  void write_code_point(const T* cp, size_t len) {
    if (len > state_->capacity) {
      // The code point is larger than the total capacity; discard it.
      state_->discarded += len;
      return;
    }
    // Discard the oldest complete code points until there is enough room.
    while (state_->size + len > state_->capacity) {
      size_t oldest_len = utf8_length(state_->buffer);
      state_->size -= oldest_len;
      std::memmove(state_->buffer, state_->buffer + oldest_len, state_->size);
      state_->discarded += oldest_len;
    }
    // Append the new code point.
    std::memcpy(state_->buffer + state_->size, cp, len);
    state_->size += len;
  }

 public:
  using value_type = void;
  using difference_type = void;
  using pointer = void;
  using reference = void;
  using iterator_category = std::output_iterator_tag;

  /// Constructs a ring buffer iterator with the given storage and capacity.
  basic_ring_buffer_iterator(T* buffer, size_t capacity) noexcept
      : state_(std::make_shared<state>(
            state{buffer, capacity, 0, 0, {}, 0, 0})) {}

  basic_ring_buffer_iterator(const basic_ring_buffer_iterator&) = default;
  auto operator=(const basic_ring_buffer_iterator&)
      -> basic_ring_buffer_iterator& = default;

  FMT_CONSTEXPR auto operator*() noexcept -> basic_ring_buffer_iterator& {
    return *this;
  }
  FMT_CONSTEXPR auto operator++() noexcept -> basic_ring_buffer_iterator& {
    return *this;
  }
  FMT_CONSTEXPR auto operator++(int) noexcept -> basic_ring_buffer_iterator& {
    return *this;
  }

  /// Receives a single byte of output. Accumulates bytes until a complete
  /// UTF-8 code point is formed, then writes it to the ring buffer.
  FMT_CONSTEXPR auto operator=(T c) -> basic_ring_buffer_iterator& {
    state_->utf8_buf[state_->utf8_len++] = c;
    if (state_->utf8_len == 1) {
      // Determine the expected length from the leading byte.
      unsigned char uc = static_cast<unsigned char>(c);
      if ((uc & 0x80) == 0)
        state_->utf8_expected = 1;
      else if ((uc & 0xE0) == 0xC0)
        state_->utf8_expected = 2;
      else if ((uc & 0xF0) == 0xE0)
        state_->utf8_expected = 3;
      else if ((uc & 0xF8) == 0xF0)
        state_->utf8_expected = 4;
      else
        state_->utf8_expected = 1;  // Invalid leading byte; treat as a single byte.
    }
    if (state_->utf8_len == state_->utf8_expected) {
      write_code_point(state_->utf8_buf, state_->utf8_len);
      state_->utf8_len = 0;
    }
    return *this;
  }

  /// Returns the total number of discarded bytes.
  FMT_CONSTEXPR auto discarded() const noexcept -> size_t {
    return state_->discarded;
  }

  /// Returns the current number of bytes in the ring buffer.
  FMT_CONSTEXPR auto size() const noexcept -> size_t { return state_->size; }

  /// Returns the total capacity of the ring buffer (in bytes).
  FMT_CONSTEXPR auto capacity() const noexcept -> size_t {
    return state_->capacity;
  }

  /// Returns a pointer to the ring buffer data (not null-terminated).
  FMT_CONSTEXPR auto data() noexcept -> T* { return state_->buffer; }
  FMT_CONSTEXPR auto data() const noexcept -> const T* {
    return state_->buffer;
  }

  /// Resets the ring buffer to its initial state.
  FMT_CONSTEXPR void reset() noexcept {
    state_->size = 0;
    state_->discarded = 0;
    state_->utf8_len = 0;
    state_->utf8_expected = 0;
  }
};

using ring_buffer_iterator = basic_ring_buffer_iterator<char>;

FMT_END_NAMESPACE

#endif  // FMT_RING_BUFFER_H_
