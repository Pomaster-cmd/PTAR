#!/usr/bin/env python3
from pathlib import Path
import re, json

ROOT=Path(__file__).resolve().parents[1]
ps=(ROOT/"automation/windows/PTAR_AutoSetupAndValidate.ps1").read_text(encoding="utf-8")
bat=(ROOT/"RUN_PTAR_AUTO.bat").read_text(encoding="utf-8")
build=(ROOT/"build/windows/BUILD_AND_RUN_K185_HARDWARE_VALIDATION.bat").read_text(encoding="utf-8")
doc=(ROOT/"docs/AUTOMATIC_WINDOWS81_SETUP_AND_TEST.md").read_text(encoding="utf-8")

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond:
        raise AssertionError(name)

# Official endpoint lock.
ck("VS2019 official endpoint", "https://aka.ms/vs/16/release/vs_buildtools.exe" in ps)
ck("Windows 8.1 SDK official endpoint", "https://go.microsoft.com/fwlink/p/?LinkId=323507" in ps)

# Safety.
for label,text in [("PowerShell",ps),("root BAT",bat)]:
    ck(label+" no Remove-Item", "Remove-Item" not in text)
    ck(label+" no del command", not re.search(r'(?im)^\s*del\b',text))
    ck(label+" no rmdir command", not re.search(r'(?im)^\s*(rd|rmdir)\b',text))
    ck(label+" no format command", not re.search(r'(?im)^\s*format\b',text))
    ck(label+" no shutdown/reboot", not re.search(r'(?im)\b(shutdown|Restart-Computer)\b',text))

ck("UAC uses RunAs", "-Verb RunAs" in ps)
ck("signature verification present", "Get-AuthenticodeSignature" in ps)
ck("requires valid signature", '$sig.Status -ne "Valid"' in ps)
ck("requires Microsoft signer", '$subject -notmatch "Microsoft"' in ps)
ck("TLS12 attempt", "Tls12" in ps)

# Visual Studio automation.
ck("VS quiet", '"--quiet"' in ps)
ck("VS wait", '"--wait"' in ps)
ck("VS norestart", '"--norestart"' in ps)
ck("VCTools workload", "Microsoft.VisualStudio.Workload.VCTools" in ps)
ck("VS 16 version range", '"[16.0,17.0)"' in ps)

# SDK/FXC.
ck("prefers Windows Kits 8.1 FXC", r"Windows Kits\8.1\bin\x64\fxc.exe" in ps)
ck("SDK unattended attempt", '@("/quiet", "/norestart")' in ps)
ck("SDK interactive fallback documented", "interactively as a fallback" in ps)

# Idempotent / append-only style.
ck("unique automation run", "run_" in ps and "Get-Random" in ps)
ck("preserves existing downloads", "Existing download preserved" in ps)
ck("hardware runner invoked", "BUILD_AND_RUN_K185_HARDWARE_VALIDATION.bat" in ps)
ck("opens result folder", "Start-Process explorer.exe" in ps)

# Windows 8.1 compilation hardening.
ck("static CRT /MT", "/MT" in build)
ck("explicit subsystem 6.03", "/SUBSYSTEM:CONSOLE,6.03" in build)
ck("WINVER 0x0603", "/DWINVER=0x0603" in build)
ck("_WIN32_WINNT 0x0603", "/D_WIN32_WINNT=0x0603" in build)

# Documentation mentions unavoidable boundaries.
ck("doc says no automatic reboot", "never restarts the machine automatically" in doc)
ck("doc says no UAC bypass", "does not bypass UAC" in doc)

print(f"{sum(x[1] for x in checks)}/{len(checks)} PASS")
for n,p in checks:
    print(("PASS" if p else "FAIL")+" - "+n)
