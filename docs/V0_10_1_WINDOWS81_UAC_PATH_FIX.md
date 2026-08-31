# PTAR v0.10.1 — Windows 8.1 UAC path correction

## Real-machine failure in v0.10.0

The first Windows 8.1 run stopped before setup at:

`Resolve-Path $ProjectRoot`

with:

`Caractères non conformes dans le chemin d'accès.`

The v0.10.0 launcher passed `%~dp0` as `ProjectRoot`. `%~dp0` ends with a
backslash. v0.10.0 then relayed that value through a newly constructed quoted
UAC PowerShell command line. On the real Windows 8.1 / PowerShell 4 boundary,
the trailing backslash corrupted the command-line argument.

The failure happened before the normal orchestration try/catch and the parent
launcher consequently displayed a misleading success message.

## Correction

v0.10.1 no longer passes ProjectRoot in either launcher.

Both the normal and elevated PowerShell processes derive the project root from
the physical location of `PTAR_AutoSetupAndValidate.ps1` via `$PSScriptRoot`.

The UAC relaunch therefore contains no project directory argument at all.

Additional hardening:
- project-root resolution failure exits 91;
- UAC launch/cancellation failure exits 93;
- the outer BAT explicitly labels nonzero exit codes as failures;
- verify-only mode uses the same corrected bootstrap path.

Algorithms, shaders, corpora and prior benchmark results are unchanged.
