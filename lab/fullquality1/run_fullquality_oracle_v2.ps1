$ErrorActionPreference='Stop'
.\lab\fullquality1\compile_sm3_quality.ps1
if($LASTEXITCODE){throw 'SM3 compile gate failed'}

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\fullquality1\fullquality_gpu_oracle_v2.cpp /link d3d9.lib user32.lib /OUT:artifact\fullquality_gpu_oracle_v2.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'fullquality oracle v2 compile failed'}

& artifact\fullquality_gpu_oracle_v2.exe 2>&1 | Tee-Object artifact\FULLQUALITY_PIXEL_ORACLE_V2.txt
$oracleRc=$LASTEXITCODE
"ORACLE_V2_RC=$oracleRc" | Tee-Object -Append artifact\FULLQUALITY_PIXEL_ORACLE_V2.txt
if($oracleRc){throw "fullquality pixel oracle v2 failed rc=$oracleRc"}

$r=Get-Content artifact\FULLQUALITY_PIXEL_ORACLE_V2.txt -Raw
foreach($m in @(
  'QSTAGE4_PARITY=PASS',
  'SPEC5_PARITY=PASS',
  'HQ900_PARITY=PASS',
  'FULLQUALITY_PIXEL_PARITY=PASS')){
  if($r -notmatch [regex]::Escape($m)){throw "missing oracle v2 gate $m"}
}
@(
 'FULLQUALITY1_PIXEL_ORACLE_V2=PASS',
 'QSTAGE4_REFERENCE=EXACT_D3D11_DXBC_SEMANTICS',
 'SPEC5_REFERENCE=INQUISITOR_D3D11_HLSL_EQUATIONS',
 'HQ900_REFERENCE=INQUISITOR_D3D11_HLSL_EQUATIONS') | Set-Content artifact\FULLQUALITY1_ORACLE_V2_STATUS.txt -Encoding ascii
Write-Host 'FULLQUALITY1_PIXEL_ORACLE_V2=PASS'
