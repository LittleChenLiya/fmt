# Record Buffer

`fmt::record_buffer` buffers a complete formatted record before submitting it
to a caller-provided commit callback (sink). The record is formatted in full
first and only handed to the sink when formatting succeeds completely.

The facility lives in the independent header `<fmt/record_buffer.h>` and does
not change the semantics of `fmt::print` or any other existing API.

## Overview

~~~cpp
#include <fmt/record_buffer.h>

fmt::record_buffer buf(1024);  // capacity in encoded bytes
buf.format("answer={}", 42);
buf.commit([](fmt::string_view record) {
  sink_write(record.data(), record.size());  // may throw
});
~~~

- `format` formats into the buffer; compile-time and runtime format strings
  (`fmt::runtime`) are supported.
- `vformat` accepts a runtime format string and dynamic arguments
  (`fmt::dynamic_format_arg_store`).
- The capacity limits the **encoded byte length** of the record, so for
  `char` buffers the limit is on UTF-8 bytes (e.g. `中` counts as 3
  bytes). A record of exactly the capacity fits; one byte more does not.
- After a successful `commit` the buffer is cleared and can be reused.
- `fmt::thread_record_buffer()` returns a buffer local to the current
  thread. Each thread gets its own buffer; buffers are never shared between
  threads.

## Exception Guarantees

### Formatting

- If the encoded record exceeds the capacity, `format`/`vformat` return
  `false`, the buffer is cleared and **no record becomes pending**. The sink
  cannot be called for such a record.
- If a format error occurs (for example an invalid format string) or a
  user-defined formatter throws, the exception propagates to the caller and
  the buffer is cleared. Any prefix already written is discarded; the buffer
  never exposes a partially formatted record and the sink is never called.
- `commit` returns `false` without invoking the sink when there is no
  pending record.

### Commit

- If the sink throws, the exception propagates to the caller, the record is
  kept and `pending()` stays `true`. The record can be resubmitted by
  calling `commit` again.
- **Only a sink call that returns normally consumes the record.** After such
  a call the buffer is cleared and becomes reusable.
- Retrying a failed `commit` only re-invokes the sink; the formatter is
  **not** called again.
- Calling `commit` (or `format`) on the same buffer from within a sink is
  rejected with `std::logic_error`; the record stays pending.

Capacity enforcement uses exception handling internally, consistent with the
rest of {fmt}; with `FMT_USE_EXCEPTIONS=0` an over-capacity record triggers
the usual `FMT_THROW` failure mode instead of returning `false`.

## Demo

The example below formats a record containing UTF-8 text, simulates one
transient sink failure and retries. It prints the sink call count and the
full committed record. Build and run it from the repository root in well
under 90 seconds:

~~~sh
c++ -std=c++11 -I include -DFMT_HEADER_ONLY example/record-buffer.cc -o record-buffer-demo
./record-buffer-demo
~~~

Expected output:

~~~
commit failed (simulated transient sink failure), retrying
sink calls: 2
record: id=7 msg=hello utf8=中文
~~~

On Windows with MinGW the equivalent command is:

~~~sh
g++ -std=c++11 -I include -DFMT_HEADER_ONLY example/record-buffer.cc -o record-buffer-demo.exe
./record-buffer-demo.exe
~~~
