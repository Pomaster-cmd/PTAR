$ErrorActionPreference='Stop'
.\lab\fullquality1\compile_sm3_quality.ps1
if($LASTEXITCODE){throw 'SM3 compile gate failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\fullquality1\fullquality_gpu_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\fullquality_gpu_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'fullquality oracle compile failed'}
& artifact\fullquality_gpu_oracle.exe 2>&1 | Tee-Object artifact\FULLQUALITY_PIXEL_ORACLE.txt
if($LASTEXITCODE){throw 'fullquality pixel oracle failed'}
$r=Get-Content artifact\FULLQUALITY_PIXEL_ORACLE.txt -Raw
if($r -notmatch 'FULLQUALITY_PIXEL_PARITY=PASS'){throw 'pixel parity PASS marker missing'}
'FULLQUALITY1_PIXEL_ORACLE=PASS' | Set-Content artifact\FULLQUALITY1_ORACLE_STATUS.txt -Encoding ascii
Write-Host 'FULLQUALITY1_PIXEL_ORACLE=PASS'
