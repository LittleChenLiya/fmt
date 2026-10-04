# UTF-8 ring buffer output iterator

`fmt/utf8-ring.h` provides `fmt::utf8_ring_buffer`, a fixed-capacity ring
buffer with a fmt output iterator. It keeps only the most recent complete
UTF-8 code points and reports how many bytes were discarded.

```cpp
#include "fmt/utf8-ring.h"
#include "fmt/format.h"

auto ring = fmt::utf8_ring_buffer(5);
fmt::format_to(ring.out(), "A猫BC");
// ring.str() == "猫BC"   (newest complete code points)
// ring.discarded() == 1  ('A' was evicted)
```

Semantics:

* A multi-byte code point is never split: when space runs out, the oldest
  complete code points are evicted first, so the contents are never
  truncated in the middle of a character.
* A code point larger than the capacity is dropped entirely and its bytes
  are added to `discarded()`.
* Newlines and all other bytes are ordinary content.
* Formatting through `out()` is a regular `fmt::format_to` call: each
  formatter is invoked exactly once. The existing `fmt::format_to` is
  unchanged.

## Building and running the tests (single job)

From the repository root:

```
cmake -S . -B build -DFMT_TEST=ON
cmake --build build --target utf8-ring-test -j1
./build/bin/utf8-ring-test        # or ./build/test/utf8-ring-test
```

A minimal smoke test that does not require CMake or gtest:

```
g++ -std=c++11 -I include test/utf8-ring-smoke.cc src/format.cc -o utf8-ring-smoke
./utf8-ring-smoke
```
