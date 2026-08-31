# PTAR v0.10.5 — MSVC wide-string conversion correction

## Real Windows 8.1 result

v0.10.4 successfully passed the Win32 `max` macro site fixed in the previous
release. MSVC then stopped later in the validator with:

- C2220 because warnings are treated as errors;
- C4244 `wchar_t -> char`, possible loss of data.

The template traces point to two PTAR source lines:

- case ID conversion;
- GPU adapter-name conversion.

The old code used range constructors such as:

`std::string id8(id.begin(), id.end());`

This does not perform text encoding. It simply narrows each `wchar_t` to `char`,
which is incorrect and legitimately rejected by `/W4 /WX`.

## Correction

v0.10.5 adds one explicit Windows conversion function:

`WideToUtf8(const std::wstring&, std::string&)`

implemented with:

`WideCharToMultiByte(CP_UTF8, ...)`

The function:
- checks that the input length fits the Win32 `int` API contract;
- queries the exact required UTF-8 byte count;
- allocates the exact output size;
- verifies the number of bytes written;
- reports failure instead of silently truncating data.

Both the case ID and GPU adapter name now use this helper.

## Scope

This is a native validation-tool correction only.

Unchanged:
- EDGE-NG v03 K185;
- HLSL;
- D3D11 / Shader Model 5 target;
- corpus and historical records;
- GPU parity/timing protocol;
- non-destructive automation rules.
