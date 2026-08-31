#!/usr/bin/env python3
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
ps=(ROOT/"automation/windows/PTAR_AutoSetupAndValidate.ps1").read_text(encoding="utf-8")
bat=(ROOT/"RUN_PTAR_AUTO.bat").read_text(encoding="utf-8")
checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond: raise AssertionError(name)

ck("BITS fallback","Start-BitsTransfer" in ps)
ck("bitsadmin fallback","bitsadmin.exe" in ps)
ck("WebClient TLS12 fallback","SecurityProtocolType]::Tls12" in ps or "SecurityProtocol = 3072" in ps)
ck("certutil fallback","certutil.exe" in ps and "-urlcache" in ps)
ck("BITS before WebClient",ps.find("Try-DownloadWithBits $Url") < ps.find("Try-DownloadWithWebClient $Url"))
ck("historical preserved-download log retained","Existing download preserved" in ps)
ck("Authenticode retained","Get-AuthenticodeSignature" in ps)
ck("Microsoft signer retained",'$subject -notmatch "Microsoft"' in ps)
ck("candidate engine suffixes",".bitsadmin" in ps and ".webclient" in ps and ".certutil" in ps)
ck("candidate reuse","Preserved candidate exists" in ps)
ck("64-bit strong crypto",r"HKLM:\SOFTWARE\Microsoft\.NETFramework\v4.0.30319" in ps)
ck("32-bit strong crypto",r"HKLM:\SOFTWARE\Wow6432Node\Microsoft\.NETFramework\v4.0.30319" in ps)
ck("SchUseStrongCrypto","SchUseStrongCrypto" in ps)
ck("SystemDefaultTlsVersions","SystemDefaultTlsVersions" in ps)
ck("Schannel TLS12 client",r"SCHANNEL\Protocols\TLS 1.2\Client" in ps)
ck("TLS backup file","TLS_REGISTRY_STATE_BEFORE_REPAIR.json" in ps)
ck("exit code 61","$finalExit = 61" in ps)
ck("BAT code61 handling",'if "%RC%"=="61"' in bat)
ck("no automatic reboot","Restart-Computer" not in ps and not re.search(r'(?im)^\s*shutdown\b',ps))

for label,text in [("PS",ps),("BAT",bat)]:
    ck(label+" no Remove-Item","Remove-Item" not in text)
    ck(label+" no del",not re.search(r'(?im)^\s*del\b',text))
    ck(label+" no rmdir",not re.search(r'(?im)^\s*(rd|rmdir)\b',text))
    ck(label+" no format",not re.search(r'(?im)^\s*format\b',text))

print(f"{sum(p for _,p in checks)}/{len(checks)} PASS")
for n,p in checks:
    print(("PASS" if p else "FAIL")+" - "+n)
