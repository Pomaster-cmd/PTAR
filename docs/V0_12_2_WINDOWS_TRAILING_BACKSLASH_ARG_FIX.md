# PTAR v0.12.2 — Windows trailing-backslash CLI correction

## Real v0.12.1 result

The autonomous host:
- compiled successfully;
- passed the dependency/import audit;
- launched with `cl.exe` / `fxc.exe` hidden from PATH.

It then printed `Usage` and returned code 2 before GPU validation.

## Cause

`RUN_AUTONOMOUS_ONLY.bat` used:

`set "BUNDLE=%~dp0"`

`%~dp0` includes a trailing backslash. The value was passed as a quoted CRT
argument:

`--bundle-root "C:\...\PTAR_NG_MOE_V01_WIN81_X64\"`

A terminal backslash directly before a closing quote can change Windows C/C++
command-line quote parsing, causing the following `--out` token to be absorbed
or malformed.

## Correction

The batch now canonicalizes the directory itself:

`for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"`

This yields a fully-qualified bundle directory without the trailing separator
before it is quoted.

The script also logs the exact `bundle-root` and `out` values before launching
the autonomous host.

No shader, bytecode hash, MoE algorithm, parity gate, timing protocol, import
policy, or toolchain dependency changes.
