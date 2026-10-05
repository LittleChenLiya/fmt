# Explicit Digit-Symbol Profiles

`fmt/digit-profile.h` provides an explicit, locale-independent way to format
numbers with a custom set of digit symbols. A `fmt::digit_profile` supplies
the ten decimal digits, the plus and minus signs, the decimal point and the
group separator as UTF-8 strings. The profile is an immutable value object:
it never touches the global locale, and the normal number formatter is
unchanged.

A value is wrapped with `fmt::profiled(profile, value)`. It is first
formatted by the ordinary fmt formatter with the requested precision and
rounding (so rounding results are identical to plain fmt), then the ASCII
symbols in the result are mapped onto the profile symbols and group
separators are inserted. Width and alignment are computed from the final
display column count (one column per Unicode code point), not from the
UTF-8 byte count.

Invalid symbol sets (empty/duplicate/non-UTF-8 digit symbols, a decimal
point equal to the group separator, negative group size, ...) are rejected
with `fmt::format_error` before any output is produced.

## Supported format specs

Only the documented decimal subset is supported:

    [[fill]align][sign]["0"][width]["." precision][type]

- align: `<`, `>`, `^` (default: right)
- sign: `+`, `-`, space
- `0`: zero-pads after the sign when no explicit alignment is given;
  grouping is applied after zero-padding
- type: `d` for integers; `f`, `F`, `e`, `E`, `g`, `G` for floating-point
  numbers (or omitted)

Anything else (`#`, `L`, `n`, hex/octal/binary types, dynamic width or
precision) is rejected with `fmt::format_error`.

## Profile examples

```cpp
#include "fmt/digit-profile.h"
#include "fmt/format.h"

// Built-in profile with plain ASCII symbols (the default):
auto ascii = fmt::digit_profile::ascii();

// Built-in profile with Arabic-Indic digits U+0660..U+0669, the Arabic
// decimal separator U+066B and the Arabic thousands separator U+066C:
auto arabic = fmt::digit_profile::arabic_indic();

// 1234.5 is grouped and padded to 10 display columns (7 columns of
// Arabic-Indic output plus 3 spaces), not to 10 bytes:
std::string s = fmt::format("{:10}", fmt::profiled(arabic, 1234.5));
// s == "   ١٬٢٣٤٫٥"

// A fully custom profile, e.g. Devanagari digits with no grouping:
fmt::digit_profile devanagari;
const char* d[10] = {"०", "१", "२", "३", "४", "५", "६", "७", "८", "९"};
for (int i = 0; i < 10; ++i) devanagari.digits[i] = d[i];
devanagari.group_size = 0;
fmt::print("{:.1f}\n", fmt::profiled(devanagari, 2.75));  // २.८
```

Profiles are plain values copied into each `fmt::profiled` view, so two
threads formatting with different profiles concurrently never interfere:

```cpp
std::thread t1([&] { fmt::print("{}\n", fmt::profiled(arabic, 1234.5)); });
std::thread t2([&] { fmt::print("{}\n", fmt::profiled(ascii, 1234.5)); });
```

## Building and running the tests

The tests live in `test/digit-profile-test.cc` and are registered with
CMake as the `digit-profile-test` target. To build just this test with a
single job and run it:

```sh
cmake -S . -B build -DFMT_TEST=ON
cmake --build build --target digit-profile-test -- -j1
cd build && ctest -R digit-profile-test --output-on-failure
```

Alternatively, compile the test directly without CMake:

```sh
g++ -std=c++11 -I include -I test -I test/gtest \
    test/digit-profile-test.cc test/test-main.cc \
    test/gtest/gmock-gtest-all.cc src/format.cc \
    -o digit-profile-test -pthread
./digit-profile-test
```
