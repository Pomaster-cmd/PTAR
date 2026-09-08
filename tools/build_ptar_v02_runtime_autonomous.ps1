param(
    [string]$OutRoot = 'lab_results\runtime_v02'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw "$Label missing: $Path" }
    if ((Get-Item $Path).Length -le 0) { throw "$Label is empty: $Path" }
}

function Sha256([string]$Path) {
    return (Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$repo = (Get-Location).Path
$root = Join-Path $repo $OutRoot
$bundle = Join-Path $root 'PTAR_NG_MOE_V02_LAB18_WIN81_X64'
$shaders = Join-Path $bundle 'shaders'
$bin = Join-Path $bundle 'bin'
$evid = Join-Path $bundle 'build_evidence'
$gen = Join-Path $root 'generated'
New-Item -ItemType Directory -Force $root,$bundle,$shaders,$bin,$evid,$gen | Out-Null

$fxc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -match '\\x64\\fxc\.exe$' } |
       Sort-Object FullName -Descending |
       Select-Object -First 1
if (-not $fxc) { throw 'fxc.exe not found on Windows runner' }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
Require-File $vswhere 'vswhere.exe'
$vcvars = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat' | Select-Object -First 1
if (-not $vcvars) { throw 'vcvars64.bat not found' }

$oldBundle = Join-Path $repo 'runtime_autonomous\PTAR_NG_MOE_V01_WIN81_X64'
Copy-Item (Join-Path $oldBundle 'shaders\ptar_vs.cso') $shaders
Copy-Item (Join-Path $oldBundle 'shaders\ptar_moe_ng_v01_sf5_ps.cso') $shaders
Copy-Item (Join-Path $oldBundle 'shaders\ptar_k185_control_ps.cso') $shaders

$lab18Src = Join-Path $repo 'src\hlsl\moe_ng_v02\ptar_moe_ng_v02_lab18_admission_ps.hlsl'
$lab18Cso = Join-Path $shaders 'ptar_moe_ng_v02_lab18_admission_ps.cso'
$lab18Asm = Join-Path $evid 'ptar_moe_ng_v02_lab18_admission_ps.asm'
& $fxc.FullName /nologo /T ps_5_0 /E main /O3 /WX /Fo $lab18Cso /Fc $lab18Asm $lab18Src
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Require-File $lab18Cso 'LAB18 CSO'
Require-File $lab18Asm 'LAB18 ASM'
$fxc.FullName | Set-Content -Encoding ascii (Join-Path $evid 'FXC_PATH.txt')

$asm = Get-Content $lab18Asm -Raw
$g = ([regex]::Matches($asm,'(?mi)^\s*gather4\w*\b')).Count
$s = ([regex]::Matches($asm,'(?mi)^\s*sample\w*\b')).Count
$u = ([regex]::Matches($asm,'(?mi)^\s*dcl_uav\w*\b')).Count
$inst = ([regex]::Matches($asm,'(?mi)^\s*(?!dcl_|def\b|ret\b|if\b|else\b|endif\b|loop\b|endloop\b|break\b)([a-z][a-z0-9_]*)\s')).Count
$audit = [pscustomobject]@{
    shader='LAB18'; gather4=$g; sample_ops=$s; uav_decl=$u;
    instruction_proxy=$inst; cso_bytes=(Get-Item $lab18Cso).Length; sha256=(Sha256 $lab18Cso)
}
$audit | Export-Csv -NoTypeInformation (Join-Path $evid 'LAB18_DXBC_AUDIT.csv')
$audit | Format-List | Out-String | Set-Content -Encoding ascii (Join-Path $evid 'LAB18_DXBC_AUDIT.txt')
if ($g -ne 1) { throw "LAB18 gather4 footprint changed: $g" }
if ($s -ne 4) { throw "LAB18 sample footprint changed: $s" }
if ($u -ne 0) { throw "LAB18 UAV footprint changed: $u" }

$vsPath = Join-Path $shaders 'ptar_vs.cso'
$v01Path = Join-Path $shaders 'ptar_moe_ng_v01_sf5_ps.cso'
$k185Path = Join-Path $shaders 'ptar_k185_control_ps.cso'
$hVS = Sha256 $vsPath
$h02 = Sha256 $lab18Cso
$h01 = Sha256 $v01Path
$hK = Sha256 $k185Path

$generatedHeader = Join-Path $gen 'PTARV02GeneratedHashes.h'
@"
#pragma once
#define PTAR_SHA_VS "$hVS"
#define PTAR_SHA_V02 "$h02"
#define PTAR_SHA_V01 "$h01"
#define PTAR_SHA_K185 "$hK"
"@ | Set-Content -Encoding ascii $generatedHeader

$provenance = [ordered]@{
    runtime_id='PTAR-NG-MoE-v02-LAB18-AUTONOMOUS-BENCH'
    target='Windows 8.1 x64 / D3D11 FL11_0'
    source_branch_base='SOURCE'
    source_base_commit='bdb26584d4bcc7aa9655ae27d48359e60f707c50'
    research_branch='lab/moe-ng-v02-native-detail'
    frozen_candidate_commit='bda10f16ad287eaac5fabba58e88aedc2651dd34'
    postfreeze_validation_commit='1f837599717e6504a2ddabf38cb707d5bcff0b37'
    frozen_shader_blob='27ab6720ffedfc8d12046c647b5ada680323d703'
    software_gate_run=34247986293
    runtime_compile_mode='PRECOMPILED_DXBC_ONLY_AT_RUNTIME'
    runtime_hlsl_compiler_required=$false
    runtime_visual_studio_required=$false
    runtime_windows_sdk_required=$false
    texture_contract='1 GatherGreen + 4 SampleLevel + 0 UAV'
    shaders=[ordered]@{
        'ptar_vs.cso'=[ordered]@{sha256=$hVS; bytes=(Get-Item $vsPath).Length}
        'ptar_moe_ng_v02_lab18_admission_ps.cso'=[ordered]@{sha256=$h02; bytes=(Get-Item $lab18Cso).Length}
        'ptar_moe_ng_v01_sf5_ps.cso'=[ordered]@{sha256=$h01; bytes=(Get-Item $v01Path).Length}
        'ptar_k185_control_ps.cso'=[ordered]@{sha256=$hK; bytes=(Get-Item $k185Path).Length}
    }
}
$provenance | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $bundle 'SHADER_PROVENANCE.json')

$hostSrc = Join-Path $repo 'runtime_autonomous\PTAR_NG_MOE_V02_LAB18_WIN81_X64\source\ptar_v02_runtime_bench.cpp'
$timerInc = Join-Path $oldBundle 'include'
$hostExe = Join-Path $bin 'ptar_v02_runtime_bench.exe'
$hostObj = Join-Path $gen 'ptar_v02_runtime_bench.obj'
$cmd = "call `"$vcvars`" && cl /nologo /EHsc /O2 /MT /W4 /WX /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /DUNICODE /D_UNICODE /I `"$timerInc`" /I `"$gen`" /Fo:`"$hostObj`" `"$hostSrc`" /Fe:`"$hostExe`" /link /SUBSYSTEM:CONSOLE,6.03 d3d11.lib dxgi.lib bcrypt.lib"
cmd.exe /d /s /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Require-File $hostExe 'Autonomous host'

$imports = Join-Path $evid 'IMPORTS_VALIDATED.txt'
$cmd2 = "call `"$vcvars`" && dumpbin /dependents `"$hostExe`" > `"$imports`""
cmd.exe /d /s /c $cmd2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$bad = Select-String -Path $imports -Pattern 'D3DCOMPILER|VCRUNTIME|MSVCP|UCRTBASE|API-MS-WIN-CRT' -CaseSensitive:$false
if ($bad) { $bad | ForEach-Object { $_.Line }; throw 'Forbidden runtime/toolchain import detected' }
(Sha256 $hostExe) | Set-Content -Encoding ascii (Join-Path $evid 'HOST_SHA256.txt')

Copy-Item (Join-Path $repo 'runtime_autonomous\PTAR_NG_MOE_V02_LAB18_WIN81_X64\RUN_AUTONOMOUS_ONLY.bat') $bundle
Copy-Item (Join-Path $repo 'runtime_autonomous\PTAR_NG_MOE_V02_LAB18_WIN81_X64\README_AUTONOMOUS.txt') $bundle

$smoke = Join-Path $evid 'WARP_SMOKE'
New-Item -ItemType Directory -Force $smoke | Out-Null
& $hostExe --bundle-root $bundle --out $smoke --warp --smoke *> (Join-Path $smoke 'console.txt')
$rc = $LASTEXITCODE
Get-Content (Join-Path $smoke 'console.txt')
if ($rc -ne 0) { exit $rc }
Require-File (Join-Path $smoke 'timing.csv') 'WARP smoke timing.csv'
Require-File (Join-Path $smoke 'timing_pairs.csv') 'WARP smoke timing_pairs.csv'
Require-File (Join-Path $smoke 'runtime_summary.txt') 'WARP smoke runtime_summary.txt'
if ((Import-Csv (Join-Path $smoke 'timing.csv')).Count -ne 3) { throw 'WARP smoke did not report all three shaders' }
if (-not (Select-String -Path (Join-Path $smoke 'runtime_summary.txt') -Pattern 'runtime_d3dcompiler_required=NO' -Quiet)) { throw 'Runtime summary missing no-D3DCompiler proof' }

$manifest = Join-Path $bundle 'BUNDLE_SHA256.txt'
$lines = Get-ChildItem $bundle -Recurse -File |
    Where-Object { $_.FullName -ne $manifest } |
    Sort-Object FullName |
    ForEach-Object {
        $rel=$_.FullName.Substring($bundle.Length+1).Replace('\','/')
        "$(Sha256 $_.FullName)  $rel"
    }
$lines | Set-Content -Encoding ascii $manifest
foreach($line in Get-Content $manifest) {
    if($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed manifest line: $line" }
    $expected=$Matches[1]
    $rel=$Matches[2].Replace('/','\')
    $p=Join-Path $bundle $rel
    Require-File $p "Manifest entry $rel"
    if((Sha256 $p) -ne $expected) { throw "Manifest mismatch: $rel" }
}

$zip = Join-Path $root 'PTAR_NG_MOE_V02_LAB18_WIN81_X64.zip'
if(Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $bundle -DestinationPath $zip -CompressionLevel Optimal
Require-File $zip 'Runtime ZIP'
(Sha256 $zip) | Set-Content -Encoding ascii (Join-Path $root 'PACKAGE_SHA256.txt')

Write-Host '[PASS] PTAR-NG MoE v02 LAB18 autonomous runtime package built and WARP-smoke validated.'
Write-Host "[PASS] ZIP: $zip"
