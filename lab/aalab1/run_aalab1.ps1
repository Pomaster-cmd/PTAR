$ErrorActionPreference='Stop'

# Compile every candidate and audit SM3 budgets.
.\lab\aalab1\compile_aalab_variants.ps1
if($LASTEXITCODE){throw 'AALAB variant compile failed'}

# Prepare the checked-in synthetic corpus source for MSVC parsing.
python .\lab\aalab1\prepare_quality_source.py
if($LASTEXITCODE){throw 'quality source prep failed'}

# Native-reference quality corpus.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\aalab1\aalab_quality_gpu.cpp /link d3d9.lib user32.lib /OUT:artifact\aalab_quality_gpu.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'quality harness compile failed'}
Push-Location artifact
& .\aalab_quality_gpu.exe 2>&1 | Tee-Object AALAB_QUALITY_LOG.txt
$qualityRc=$LASTEXITCODE
Pop-Location
if($qualityRc){throw "quality harness failed rc=$qualityRc"}
if((Get-Content artifact\AALAB_QUALITY_LOG.txt -Raw) -notmatch 'AALAB_QUALITY_GATE=PASS'){throw 'quality gate marker missing'}

# Synchronized relative D3D9 cost proxy.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\aalab1\aalab_perf_gpu.cpp /link d3d9.lib user32.lib /OUT:artifact\aalab_perf_gpu.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'perf harness compile failed'}
Push-Location artifact
& .\aalab_perf_gpu.exe 2>&1 | Tee-Object AALAB_PERF_LOG.txt
$perfRc=$LASTEXITCODE
Pop-Location
if($perfRc){throw "perf harness failed rc=$perfRc"}
if((Get-Content artifact\AALAB_PERF_LOG.txt -Raw) -notmatch 'AALAB_PERF_GATE=PASS'){throw 'perf gate marker missing'}

# Combine quality, performance and compiler budgets into a conservative ranking.
python .\lab\aalab1\analyze_aalab1.py 2>&1 | Tee-Object artifact\AALAB_DECISION_LOG.txt
if($LASTEXITCODE){throw 'decision analysis failed'}
if((Get-Content artifact\AALAB_DECISION_LOG.txt -Raw) -notmatch 'AALAB_DECISION_GATE=PASS'){throw 'decision gate marker missing'}

@(
 'AALAB1_STATUS=PASS',
 'PRODUCTION_RUNTIME_CHANGED=NO',
 'HF6_CHANGED=NO',
 'TEMPORALPAIR60_CHANGED=NO',
 'F7_CHANGED=NO',
 'FULLQUALITY1_CHANGED=NO',
 'VARIANTS=BASE_B08_B12_BADAPT20_AA0_15_AA2_15_AA2B_AA4B_AA2B_STRONG') | Set-Content artifact\AALAB1_STATUS.txt -Encoding ascii

Write-Host 'AALAB1_FULL_MATRIX=PASS'
