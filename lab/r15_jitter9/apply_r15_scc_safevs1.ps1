$ErrorActionPreference='Stop'
$path='lab/r15_jitter9/ptar_vs_jitter_patch.h'
$utf8=New-Object System.Text.UTF8Encoding($false)
$p=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

function R1([string]$t,[string]$o,[string]$n,[string]$label)
{
    $a=$t.IndexOf($o,[StringComparison]::Ordinal)
    if($a -lt 0){throw "R15 SCC SAFEVS1 missing anchor: $label"}
    if($t.IndexOf($o,$a+$o.Length,[StringComparison]::Ordinal) -ge 0){throw "R15 SCC SAFEVS1 non-unique anchor: $label"}
    return $t.Substring(0,$a)+$n+$t.Substring($a+$o.Length)
}

if($p.IndexOf('R15_SCC_SAFE_VS1',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 SCC SAFEVS1 already applied' }

$p=R1 $p '// PTAR R15 TRUE720 JITTER9' @'
// PTAR R15 TRUE720 JITTER9
// R15_SCC_SAFE_VS1
// Field hardening after SCC black-screen/AppHang: reject shaders with relative
// register addressing, preserve reads of the position output through the
// scratch temp, and patch only shaders with exactly one position write.
'@ 'marker'

$p=R1 $p @'
    bool c255Used=false;
    bool hasCallOrRet=false;
    bool malformed=false;
'@ @'
    bool c255Used=false;
    bool hasCallOrRet=false;
    bool hasRelativeAddressing=false;
    bool malformed=false;
'@ 'relative-address state'

$p=R1 $p @'
                const DWORD t=original[i+p];
                if(!PtShaderTokenIsRegister(t)) continue;
                const UINT rt=PtShaderRegType(t);
'@ @'
                const DWORD t=original[i+p];
                if(!PtShaderTokenIsRegister(t)) continue;
                // D3D9 source-token bit 13 is relative addressing. A shader
                // that can index the constant bank dynamically can reach c255
                // without containing an explicit c255 token, so stealing c255
                // for jitter is unsafe. Fail open to the original shader.
                if((t & 0x00002000u)!=0) hasRelativeAddressing=true;
                const UINT rt=PtShaderRegType(t);
'@ 'relative-address detection'

$p=R1 $p @'
    if(malformed || !endIndex || !positionFound || c255Used || hasCallOrRet)
        return false;
'@ @'
    if(malformed || !endIndex || !positionFound || c255Used ||
       hasCallOrRet || hasRelativeAddressing)
        return false;
'@ 'relative-address rejection'

$oldSecond=@'
    UINT positionWrites=0;

    // Second pass: redirect every executable write to POSITION into our temp.
    for(UINT i=1;i<endIndex;)
    {
        DWORD inst=(*patched)[i];
        const UINT opcode=inst & D3DSI_OPCODE_MASK;
        if(opcode==D3DSIO_COMMENT)
        {
            const UINT n=(inst & D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT;
            i+=1+n;
            continue;
        }
        const UINT n=(inst & D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(!n) return false;

        if(opcode!=D3DSIO_DCL && PtShaderOpcodeHasDestination(opcode) && n>=1)
        {
            DWORD& dst=(*patched)[i+1];
            if(PtShaderTokenIsRegister(dst) &&
               PtShaderRegType(dst)==positionType &&
               (dst & D3DSP_REGNUM_MASK)==positionNum)
            {
                dst &= ~(D3DSP_REGTYPE_MASK|D3DSP_REGTYPE_MASK2|D3DSP_REGNUM_MASK);
                dst |= PtShaderRegTypeBits(D3DSPR_TEMP) | ((UINT)temp & D3DSP_REGNUM_MASK);
                ++positionWrites;
            }
        }
        i+=1+n;
    }

    if(!positionWrites)
        return false;
'@
$newSecond=@'
    UINT positionWrites=0;
    UINT positionReads=0;

    // Second pass: redirect the POSITION value to our scratch temp. This must
    // include reads as well as writes: SCC shaders are allowed to consume their
    // already-computed clip position later in the program. Redirecting only the
    // write leaves the original oPos/oN undefined until the epilogue.
    for(UINT i=1;i<endIndex;)
    {
        DWORD inst=(*patched)[i];
        const UINT opcode=inst & D3DSI_OPCODE_MASK;
        if(opcode==D3DSIO_COMMENT)
        {
            const UINT n=(inst & D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT;
            i+=1+n;
            continue;
        }
        const UINT n=(inst & D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(!n) return false;

        const bool hasDestination=(opcode!=D3DSIO_DCL && PtShaderOpcodeHasDestination(opcode) && n>=1);
        UINT sourceStart=1;
        if(hasDestination)
        {
            DWORD& dst=(*patched)[i+1];
            if(PtShaderTokenIsRegister(dst) &&
               PtShaderRegType(dst)==positionType &&
               (dst & D3DSP_REGNUM_MASK)==positionNum)
            {
                dst &= ~(D3DSP_REGTYPE_MASK|D3DSP_REGTYPE_MASK2|D3DSP_REGNUM_MASK);
                dst |= PtShaderRegTypeBits(D3DSPR_TEMP) | ((UINT)temp & D3DSP_REGNUM_MASK);
                ++positionWrites;
            }
            sourceStart=2;
        }

        if(opcode!=D3DSIO_DCL && opcode!=D3DSIO_DEF &&
           opcode!=D3DSIO_DEFI && opcode!=D3DSIO_DEFB)
        {
            for(UINT q=sourceStart;q<=n;++q)
            {
                DWORD& src=(*patched)[i+q];
                if(PtShaderTokenIsRegister(src) &&
                   PtShaderRegType(src)==positionType &&
                   (src & D3DSP_REGNUM_MASK)==positionNum)
                {
                    src &= ~(D3DSP_REGTYPE_MASK|D3DSP_REGTYPE_MASK2|D3DSP_REGNUM_MASK);
                    src |= PtShaderRegTypeBits(D3DSPR_TEMP) | ((UINT)temp & D3DSP_REGNUM_MASK);
                    ++positionReads;
                }
            }
        }
        i+=1+n;
    }

    // Field fail-open policy: ambiguous multi-write/control-flow position
    // programs stay byte-for-byte original until a dedicated SCC-safe rewriter
    // exists for them. One-write shaders cover the low-risk geometry domain.
    if(positionWrites!=1)
        return false;
'@
$p=R1 $p $oldSecond $newSecond 'position read/write preservation'

# Keep the variable live in optimized harness builds and make the generated
# header self-auditing without changing the public metadata struct.
$p=R1 $p @'
    // Replace END with the epilogue and append a new END.
    patched->resize(endIndex);
'@ @'
    // positionReads is intentionally diagnostic-only; all discovered reads
    // have already been redirected to the scratch temp above.
    (void)positionReads;

    // Replace END with the epilogue and append a new END.
    patched->resize(endIndex);
'@ 'position read audit'

foreach($token in @(
    'R15_SCC_SAFE_VS1',
    'hasRelativeAddressing',
    'positionReads',
    'positionWrites!=1',
    '0x00002000u'))
{
    if($p.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    { throw "R15 SCC SAFEVS1 final token missing: $token" }
}

[IO.File]::WriteAllText((Resolve-Path $path),$p,$utf8)
Write-Host 'R15_SCC_SAFE_VS1=PASS'
Write-Host 'R15_SCC_RELATIVE_ADDRESSING=REJECT_TO_ORIGINAL'
Write-Host 'R15_SCC_POSITION_READS=REDIRECTED_TO_TEMP'
Write-Host 'R15_SCC_POSITION_WRITES=EXACTLY_ONE_ONLY'
