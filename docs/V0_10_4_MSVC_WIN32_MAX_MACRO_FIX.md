# PTAR v0.10.4 — MSVC / Win32 `max` macro correction

## Real Windows 8.1 result

The v0.10.3 run successfully reached native C++ compilation with:

- Windows 8.1 detected;
- GTX 960M detected;
- VS2019 Build Tools found;
- `cl.exe` imported and functional;
- D3DCompiler path selected.

MSVC then stopped at line 694:

`globalMax=std::max(globalMax,maxLsb);`

with:

- C2589
- C2062
- C2059

## Cause

`windows.h` historically exposes function-like `min`/`max` macros unless
`NOMINMAX` is defined before including it.

The preprocessor therefore sees `std::max(...)` before the C++ parser and the
macro expansion corrupts the expression. The compiler diagnostics observed on
the real Windows 8.1 machine are characteristic of this collision.

## Correction

Two defenses are used:

1. `#define NOMINMAX` before `#include <windows.h>`;
2. the affected expression is written as:

`globalMax=(std::max)(globalMax,maxLsb);`

The parenthesized function name is macro-safe even if a future header
accidentally exposes a `max` macro again.

## Secondary logging correction

The same real run showed FXC detected during preflight and then reported absent
after importing `vcvars64`.

FXC is optional in the D3DCompiler path, so this did not cause the build error.
v0.10.4 nevertheless preserves a valid preflight FXC path instead of rescanning
and producing contradictory logging.

No algorithm, shader coefficient, corpus, historical result or benchmark gate
has changed.
