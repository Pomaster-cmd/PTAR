$ErrorActionPreference='Stop'

$path='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

function Insert-Before-One([string]$anchor,[string]$insert,[string]$label)
{
    $first=$script:text.IndexOf($anchor,[StringComparison]::Ordinal)
    if($first -lt 0){throw "R8 core anchor missing: $label"}
    $second=$script:text.IndexOf($anchor,$first+$anchor.Length,[StringComparison]::Ordinal)
    if($second -ge 0){throw "R8 core anchor not unique: $label"}
    $script:text=$script:text.Substring(0,$first)+$insert+$script:text.Substring($first)
}

if($text.IndexOf('ptar_tsr1_runtime.h',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R8 core appears to be already applied'
}

# Bytecode/runtime declarations. ptar_diag.h is stable across the fully
# qualified R3->R7 lineage, unlike the exact preceding include sequence.
Insert-Before-One @'
#include "ptar_diag.h"
'@ @'
#include "ptar_tsr1_motion_subpixel_ps_bytecode.h"
#include "ptar_tsr1_resolve_ps_bytecode.h"
#include "ptar_tsr1_runtime.h"
'@ 'TSR includes'

# Cleanup order varies slightly across lineage overlays. The state block release
# is unique; release TSR resources immediately before it.
Insert-Before-One @'
    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}
'@ @'
    PtTsr1Release();
'@ 'TSR release'

# The diagnostic marker around CreateStateBlock is not stable across all lineage
# overlays, but the actual state-block creation call is. Insert TSR setup directly
# before that unique semantic operation and leave surrounding instrumentation intact.
Insert-Before-One @'
    hr=dev->CreateStateBlock(D3DSBT_ALL,&g_ptar.stateBlock);
'@ @'
    if(spatialActive)
    {
        PtDiagStage("Initialize_CreateTSR1");
        HRESULT tsrHr=PtTsr1Initialize(
            dev,sourceW,sourceH,outputW,outputH,desc.Format);
        if(FAILED(tsrHr))
            PtDiagLogA(
                "TSR1_INIT_FAILOPEN hr=0x%08lX fallback=R7",
                (unsigned long)tsrHr);
    }

'@ 'TSR initialization'

Insert-Before-One @'
static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)
'@ @'
#include "ptar_tsr1_runtime.inl"

'@ 'TSR runtime implementation'

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R8_PROXY_CORE_PATCH=PASS'
Write-Host 'R8_PROXY_CORE_INIT_ANCHOR=CREATE_STATE_BLOCK'
