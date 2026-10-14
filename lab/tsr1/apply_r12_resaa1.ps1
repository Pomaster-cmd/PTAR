$ErrorActionPreference='Stop'

$path='lab/tsr1/ptar_tsr1_resolve_ps.hlsl'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($text.IndexOf('PTAR R11 PLSR1 PHASELATTICE1',[StringComparison]::Ordinal) -lt 0)
{
    throw 'R12 requires R11 PLSR1 source baseline'
}
if($text.IndexOf('PTAR R12 RESAA1',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R12 patch already applied'
}

$anchor='    float4 resolved=lerp(r7,latticeSample,latticeBlend);'
$pos=$text.IndexOf($anchor,[StringComparison]::Ordinal)
if($pos -lt 0){throw 'R12 resolved anchor missing'}
$insertPos=$pos+$anchor.Length

$aa=@'

    // PTAR R12 RESAA1:
    // Perceived resolution is limited not only by missing microdetail but also
    // by 720p staircase geometry.  Do NOT run a generic FXAA blur here: that
    // would destroy the 1-3px structures PLSR1 is trying to recover.  Instead
    // estimate a coherent source edge from the already-fetched current/history
    // 2x2 neighborhoods and apply a small luminance-only coverage correction.
    // No extra texture read and no extra pass.
    float hl11=Luma(h11);
    float hl21=Luma(h21);
    float hl12=Luma(h12);
    float hl22=Luma(h22);

    float cgx=0.5f*((l21-l11)+(l22-l12));
    float cgy=0.5f*((l12-l11)+(l22-l21));
    float hgx=0.5f*((hl21-hl11)+(hl22-hl12));
    float hgy=0.5f*((hl12-hl11)+(hl22-hl21));

    float cax=abs(cgx);
    float cay=abs(cgy);
    float edgeMag=max(cax,cay);
    float edgeStrength=saturate((edgeMag-0.020f)*12.5f);

    // Axis-aligned edges need little AA.  A second gradient component means a
    // slanted edge, exactly where the 720p source exposes visible stair steps.
    float slope=min(cax,cay)/max(edgeMag,0.0005f);
    float stairOrientation=saturate(slope*3.0f);

    // Reprojected-history gradient agreement rejects moving/disoccluded texture
    // and makes the coverage estimate temporally stable without feeding any FG
    // generated frame back into the TSR history.
    float gradDiff=abs(cgx-hgx)+abs(cgy-hgy);
    float gradScale=cax+cay+abs(hgx)+abs(hgy)+0.020f;
    float edgeAgreement=saturate(1.0f-2.0f*gradDiff/gradScale);
    float historyAA=trust*saturate(phaseDiversity);

    // Bilinear is used only as a local coverage estimate.  The correction is
    // luminance-only so chroma and tangential PLSR microstructure stay intact.
    float aaReferenceLuma=lerp(
        Luma(currentBilinear),
        Luma(historyBilinear),
        0.35f*historyAA);
    float aaDelta=aaReferenceLuma-Luma(resolved);
    float aaLimit=0.020f+0.18f*lrange;
    aaDelta=clamp(aaDelta,-aaLimit,aaLimit);

    // Texture/detail protection is deliberately strong: RESAA1 should remove
    // staircase geometry, not turn chair weave, small text or foliage into a
    // softer image.  Slanted coherent edges receive the strongest correction.
    float aaEdge=edgeStrength*lerp(0.22f,1.00f,stairOrientation);
    float aaStable=lerp(0.70f,1.00f,edgeAgreement*historyAA);
    float aaDetailGate=lerp(1.00f,0.30f,detailProtect);
    float aaStrength=0.32f*aaEdge*aaStable*aaDetailGate;
    resolved.rgb+=aaDelta*aaStrength;
'@

$text=$text.Insert($insertPos,$aa)
$text=$text.Replace('// PTAR R11 PLSR1 PHASELATTICE1 over R10 - phase-lattice super-resolution for exact 720p -> 1080p.','// PTAR R12 RESAA1 over R11 PLSR1 - phase-lattice SR plus selective resolution anti-aliasing for exact 720p -> 1080p.')

foreach($token in @(
    'PTAR R12 RESAA1',
    'float stairOrientation=saturate(slope*3.0f)',
    'float edgeAgreement=saturate(1.0f-2.0f*gradDiff/gradScale)',
    'float aaDetailGate=lerp(1.00f,0.30f,detailProtect)',
    'float aaStrength=0.32f*aaEdge*aaStable*aaDetailGate',
    'resolved.rgb+=aaDelta*aaStrength'))
{
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R12 final token missing: $token"
    }
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R12_RESAA1_PATCH=PASS'
Write-Host 'R12_AA_METHOD=SELECTIVE_LUMA_COVERAGE_EDGE_RECONSTRUCTION'
Write-Host 'R12_GENERIC_FXAA=NO'
Write-Host 'R12_EXTRA_TEXTURE_READS_OVER_R11=0'
Write-Host 'R12_EXTRA_PASSES_OVER_R11=0'
Write-Host 'R12_FG_SCOPE=UNCHANGED_R11_FG_OFF_ONLY'
