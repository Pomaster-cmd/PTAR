$ErrorActionPreference='Stop'

.\lab\blendrefine1\compile_blend_sweep.ps1
if($LASTEXITCODE){throw 'blend sweep compile failed'}
python .\lab\blendrefine1\prepare_blend_harnesses.py
if($LASTEXITCODE){throw 'blend harness generation failed'}

& cl.exe /nologo /O2 /MT /W4 /WX /wd4244 /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\blendrefine1\blend_quality_generated.cpp /link d3d9.lib user32.lib /OUT:artifact\blend_quality.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'blend quality compile failed'}
Push-Location artifact
& .\blend_quality.exe 2>&1 | Tee-Object BLENDREFINE_QUALITY_LOG.txt
$rc=$LASTEXITCODE
Pop-Location
if($rc){throw "blend quality runtime failed rc=$rc"}
if((Get-Content artifact\BLENDREFINE_QUALITY_LOG.txt -Raw) -notmatch 'AALAB_QUALITY_GATE=PASS'){throw 'blend quality gate missing'}

& cl.exe /nologo /O2 /MT /W4 /WX /wd4244 /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\blendrefine1\blend_temporal_generated.cpp /link d3d9.lib user32.lib /OUT:artifact\blend_temporal.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'blend temporal compile failed'}
Push-Location artifact
& .\blend_temporal.exe 2>&1 | Tee-Object BLENDREFINE_TEMPORAL_LOG.txt
$rc=$LASTEXITCODE
Pop-Location
if($rc){throw "blend temporal runtime failed rc=$rc"}
if((Get-Content artifact\BLENDREFINE_TEMPORAL_LOG.txt -Raw) -notmatch 'AALAB_TEMPORAL_GATE=PASS'){throw 'blend temporal gate missing'}

python .\lab\blendrefine1\analyze_blend_sweep.py 2>&1 | Tee-Object artifact\BLENDREFINE_DECISION_LOG.txt
if($LASTEXITCODE){throw 'blend decision analysis failed'}
if((Get-Content artifact\BLENDREFINE_DECISION_LOG.txt -Raw) -notmatch 'BLENDREFINE_DECISION_GATE=PASS'){throw 'blend decision gate missing'}

@(
 'BLENDREFINE1_STATUS=PASS',
 'PRODUCTION_RUNTIME_CHANGED=NO',
 'EXTRA_TEXTURE_TAPS=0',
 'HF6_CHANGED=NO',
 'TEMPORALPAIR60_CHANGED=NO',
 'F7_CHANGED=NO',
 'FULLQUALITY1_CHANGED=NO') | Set-Content artifact\BLENDREFINE1_STATUS.txt -Encoding ascii
Write-Host 'BLENDREFINE1_FULL_MATRIX=PASS'
