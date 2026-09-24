# Record Buffers

`fmt/record-buffer.h` provides `fmt::record_buffer`, a buffer that
formats a complete record and commits it through a caller-provided callback:

```cc
#include "fmt/record-buffer.h"

auto buf = fmt::record_buffer(1024);  // capacity in encoded bytes
buf.format([](fmt::string_view record) {
  // Sink receives the complete record.
  std::fwrite(record.data(), 1, record.size(), stdout);
}, "answer={}", 42);
```

A record is always formatted in full before the commit callback runs, so the
callback never observes a partial record. The capacity limits the number of
encoded bytes of a single record; the length is checked after formatting and
records that exceed it are rejected. Both compile-time format strings
(`format`) and runtime strings with type-erased arguments such as
`fmt::dynamic_format_arg_store` (`vformat`) are supported. After a
successful commit the buffer state is reused for the next record.

## Exception Guarantees

- If formatting throws (a format error or a failing custom formatter) or the
  encoded record exceeds the capacity, the commit callback is **not**
  executed, not even once, and no record is left pending.
- If the commit callback throws, the record is kept and can be committed
  later with `retry` without re-invoking any formatter. Only a successful
  return from the callback consumes the record.
- While a record is pending, `format`/`vformat` are rejected with
  `fmt::format_error`; use `retry` to commit it or `discard` to drop it.
- Calling `format`, `vformat`, `retry` or `discard` from inside the
  commit callback (reentrancy) is rejected with `fmt::format_error`.

## Threading

A `record_buffer` is not thread-safe and instances must not be shared
between threads; give each thread its own buffer. The interface does not
change the semantics of `fmt::print` or any other existing API.

## Demo

`support/record-buffer-demo.cc` prints the number of sink invocations and
the full records, including a simulated commit failure followed by a retry.
It builds and runs in a few seconds:

```sh
c++ -std=c++11 -DFMT_HEADER_ONLY -I include \
    support/record-buffer-demo.cc -o record-buffer-demo
./record-buffer-demo
```

