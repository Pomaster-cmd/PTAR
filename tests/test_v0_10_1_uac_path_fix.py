#!/usr/bin/env python3
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
ps=(ROOT/"automation/windows/PTAR_AutoSetupAndValidate.ps1").read_text(encoding="utf-8")
bat=(ROOT/"RUN_PTAR_AUTO.bat").read_text(encoding="utf-8")
verify=(ROOT/"automation/windows/VERIFY_TOOLCHAIN_ONLY.bat").read_text(encoding="utf-8")

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond:
        raise AssertionError(name)

ck("root BAT does not pass ProjectRoot", "-ProjectRoot" not in bat)
ck("verify BAT does not pass ProjectRoot", "-ProjectRoot" not in verify)
ck("root derives from PSScriptRoot", 'Join-Path $PSScriptRoot "..\\.."' in ps)
ck("root resolution uses LiteralPath", "Get-Item -LiteralPath $rootCandidate" in ps)
ck("UAC child argument list has no ProjectRoot", '"-ProjectRoot"' not in ps and "'-ProjectRoot'" not in ps)
ck("UAC uses argument array", "$elevatedArgs = @(" in ps)
ck("root failure exits 91", "exit 91" in ps)
ck("UAC failure exits 93", "exit 93" in ps)
ck("BAT refuses false success wording", "This is NOT a successful run." in bat)

for label,text in [("PowerShell",ps),("root BAT",bat),("verify BAT",verify)]:
    ck(label+" no Remove-Item", "Remove-Item" not in text)
    ck(label+" no del", not re.search(r'(?im)^\s*del\b', text))
    ck(label+" no rmdir", not re.search(r'(?im)^\s*(rd|rmdir)\b', text))
    ck(label+" no reboot", not re.search(r'(?im)\b(shutdown|Restart-Computer)\b', text))

print(f"{sum(x[1] for x in checks)}/{len(checks)} PASS")
for name,passed in checks:
    print(("PASS" if passed else "FAIL")+" - "+name)
