# PTAR v0.10.2 — Windows 8.1 TLS/download hardening

The first real v0.10.1 Windows 8.1 run passed OS/GPU preflight and failed only
when .NET WebClient tried to download the official VS 2019 Build Tools
bootstrapper: Windows reported that it could not create an SSL/TLS secure
channel.

v0.10.2 tries four Windows download engines in this order:

1. PowerShell BITS;
2. bitsadmin.exe;
3. .NET WebClient forced to TLS 1.2;
4. certutil URL cache.

Every successful candidate still requires a valid Microsoft Authenticode
signature before it can be used.

Failed/partial engine files are preserved for diagnostics and never overwrite
the validated final installer.

Only if all engines fail, PTAR records existing .NET/SChannel registry values
and enables Microsoft's documented TLS 1.2 / strong-crypto settings. PTAR then
returns code 61 and asks for a manual Windows restart. It never restarts
Windows automatically.

No PTAR algorithm, shader, corpus, benchmark result or historical record changes
in this version.
