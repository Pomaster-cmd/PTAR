#pragma once

#include <windows.h>
#include <d3d9.h>
#include <vector>

// PTAR R15 TRUE720 JITTER9
//
// The 720p -> 1080p ratio is exactly 2:3.  Three deliberate raster phases on
// each axis can therefore expose the complete 1080p sample lattice over nine
// real frames.  The runtime needs a game-independent way to move the raster
// lattice by a fractional source pixel.  D3D9 viewport coordinates are integer,
// so R15 patches programmable vertex shaders instead.
//
// The patch is deliberately conservative:
//   * vs_2_0 and vs_3_0 only;
//   * c255 must be unused by the original shader;
//   * shaders containing CALL/RET are left untouched in this first lab stage;
//   * every write to the position output is redirected to one otherwise-unused
//     temporary register;
//   * immediately before END, clip position is emitted as
//       xy = temp.xy + temp.w * c255.xy
//       zw = temp.zw
//
// c255.xy therefore contains the clip-space shift.  For a source-space jitter
// (jx,jy) measured in source pixels:
//   c255.x =  2*jx/sourceWidth
//   c255.y = -2*jy/sourceHeight
// The minus sign on Y follows the D3D viewport transform.

struct PtVsJitterPatchInfo
{
    bool patched;
    UINT shaderMajor;
    UINT shaderMinor;
    UINT positionRegType;
    UINT positionRegNum;
    UINT tempRegNum;
    UINT positionWrites;
    UINT dwordCountBefore;
    UINT dwordCountAfter;
};

static UINT PtShaderRegType(DWORD token)
{
    return
        ((token & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT) |
        ((token & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2);
}

static DWORD PtShaderRegTypeBits(UINT type)
{
    return
        ((type << D3DSP_REGTYPE_SHIFT) & D3DSP_REGTYPE_MASK) |
        ((type << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2);
}

static DWORD PtShaderDstToken(UINT type,UINT number,DWORD writeMask)
{
    return 0x80000000u |
        PtShaderRegTypeBits(type) |
        (number & D3DSP_REGNUM_MASK) |
        writeMask;
}

static DWORD PtShaderSrcToken(UINT type,UINT number,DWORD swizzle)
{
    return 0x80000000u |
        PtShaderRegTypeBits(type) |
        (number & D3DSP_REGNUM_MASK) |
        swizzle;
}

static bool PtShaderOpcodeHasDestination(UINT opcode)
{
    switch(opcode)
    {
    case D3DSIO_MOV:
    case D3DSIO_ADD:
    case D3DSIO_SUB:
    case D3DSIO_MAD:
    case D3DSIO_MUL:
    case D3DSIO_RCP:
    case D3DSIO_RSQ:
    case D3DSIO_DP3:
    case D3DSIO_DP4:
    case D3DSIO_MIN:
    case D3DSIO_MAX:
    case D3DSIO_SLT:
    case D3DSIO_SGE:
    case D3DSIO_EXP:
    case D3DSIO_LOG:
    case D3DSIO_LIT:
    case D3DSIO_DST:
    case D3DSIO_LRP:
    case D3DSIO_FRC:
    case D3DSIO_M4x4:
    case D3DSIO_M4x3:
    case D3DSIO_M3x4:
    case D3DSIO_M3x3:
    case D3DSIO_M3x2:
    case D3DSIO_POW:
    case D3DSIO_CRS:
    case D3DSIO_SGN:
    case D3DSIO_ABS:
    case D3DSIO_NRM:
    case D3DSIO_SINCOS:
    case D3DSIO_MOVA:
    case D3DSIO_EXPP:
    case D3DSIO_LOGP:
    case D3DSIO_CMP:
    case D3DSIO_DP2ADD:
    case D3DSIO_SETP:
    case D3DSIO_TEXLDL:
        return true;
    default:
        return false;
    }
}

static bool PtShaderTokenIsRegister(DWORD token)
{
    return (token & 0x80000000u)!=0;
}

static bool PtPatchVertexShaderForJitter(
    const DWORD* original,
    std::vector<DWORD>* patched,
    PtVsJitterPatchInfo* info)
{
    if(info)
        ZeroMemory(info,sizeof(*info));
    if(!original || !patched)
        return false;

    const DWORD version=original[0];
    if((version & 0xFFFF0000u)!=0xFFFE0000u)
        return false;

    const UINT major=D3DSHADER_VERSION_MAJOR(version);
    const UINT minor=D3DSHADER_VERSION_MINOR(version);
    if(major!=2 && major!=3)
        return false;

    const UINT maxTemps=(major>=3)?32u:12u;
    bool tempUsed[32]={false};
    bool c255Used=false;
    bool hasCallOrRet=false;
    bool malformed=false;
    bool positionFound=(major==2);
    UINT positionType=(major==2)?(UINT)D3DSPR_RASTOUT:(UINT)D3DSPR_OUTPUT;
    UINT positionNum=(major==2)?(UINT)D3DSRO_POSITION:0u;
    UINT endIndex=0;
    UINT dwordCount=1;

    // First pass: locate END, POSITION output, used temporaries and c255.
    for(UINT i=1;i<65536;)
    {
        const DWORD inst=original[i];
        const UINT opcode=inst & D3DSI_OPCODE_MASK;
        if(opcode==D3DSIO_END)
        {
            endIndex=i;
            dwordCount=i+1;
            break;
        }
        if(opcode==D3DSIO_COMMENT)
        {
            const UINT n=(inst & D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT;
            i+=1+n;
            continue;
        }

        const UINT n=(inst & D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(!n || i+1+n>=65536)
        {
            malformed=true;
            break;
        }

        if(opcode==D3DSIO_CALL || opcode==D3DSIO_CALLNZ || opcode==D3DSIO_RET)
            hasCallOrRet=true;

        if(opcode==D3DSIO_DCL && major==3 && n>=2)
        {
            const DWORD semantic=original[i+1];
            const DWORD dst=original[i+2];
            const UINT usage=(semantic & D3DSP_DCL_USAGE_MASK)>>D3DSP_DCL_USAGE_SHIFT;
            const UINT usageIndex=(semantic & D3DSP_DCL_USAGEINDEX_MASK)>>D3DSP_DCL_USAGEINDEX_SHIFT;
            if(usage==D3DDECLUSAGE_POSITION && usageIndex==0 &&
               PtShaderTokenIsRegister(dst) && PtShaderRegType(dst)==D3DSPR_OUTPUT)
            {
                if(positionFound && positionNum!=(dst & D3DSP_REGNUM_MASK))
                    return false;
                positionFound=true;
                positionType=D3DSPR_OUTPUT;
                positionNum=dst & D3DSP_REGNUM_MASK;
            }
            if(PtShaderTokenIsRegister(dst))
            {
                const UINT rt=PtShaderRegType(dst);
                const UINT rn=dst & D3DSP_REGNUM_MASK;
                if(rt==D3DSPR_TEMP && rn<32) tempUsed[rn]=true;
                if(rt==D3DSPR_CONST && rn==255) c255Used=true;
            }
        }
        else if(opcode==D3DSIO_DEF || opcode==D3DSIO_DEFI || opcode==D3DSIO_DEFB)
        {
            // Only the first payload DWORD is a register; the rest are immediate data.
            if(n>=1)
            {
                const DWORD t=original[i+1];
                if(PtShaderTokenIsRegister(t))
                {
                    const UINT rt=PtShaderRegType(t);
                    const UINT rn=t & D3DSP_REGNUM_MASK;
                    if(rt==D3DSPR_TEMP && rn<32) tempUsed[rn]=true;
                    if(rt==D3DSPR_CONST && rn==255) c255Used=true;
                }
            }
        }
        else
        {
            for(UINT p=1;p<=n;++p)
            {
                const DWORD t=original[i+p];
                if(!PtShaderTokenIsRegister(t)) continue;
                const UINT rt=PtShaderRegType(t);
                const UINT rn=t & D3DSP_REGNUM_MASK;
                if(rt==D3DSPR_TEMP && rn<32) tempUsed[rn]=true;
                if(rt==D3DSPR_CONST && rn==255) c255Used=true;
            }
        }
        i+=1+n;
    }

    if(malformed || !endIndex || !positionFound || c255Used || hasCallOrRet)
        return false;

    int temp=-1;
    for(int r=(int)maxTemps-1;r>=0;--r)
    {
        if(!tempUsed[r])
        {
            temp=r;
            break;
        }
    }
    if(temp<0)
        return false;

    patched->assign(original,original+dwordCount);
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

    // Replace END with the epilogue and append a new END.
    patched->resize(endIndex);

    const DWORD tempNoSwizzle=PtShaderSrcToken(D3DSPR_TEMP,(UINT)temp,D3DSP_NOSWIZZLE);
    const DWORD tempWWWW=PtShaderSrcToken(
        D3DSPR_TEMP,(UINT)temp,
        D3DVS_X_W|D3DVS_Y_W|D3DVS_Z_W|D3DVS_W_W);
    const DWORD jitter=PtShaderSrcToken(D3DSPR_CONST,255,D3DSP_NOSWIZZLE);

    patched->push_back(D3DSIO_MAD | (4u<<D3DSI_INSTLENGTH_SHIFT));
    patched->push_back(PtShaderDstToken(positionType,positionNum,D3DSP_WRITEMASK_0|D3DSP_WRITEMASK_1));
    patched->push_back(tempWWWW);
    patched->push_back(jitter);
    patched->push_back(tempNoSwizzle);

    patched->push_back(D3DSIO_MOV | (2u<<D3DSI_INSTLENGTH_SHIFT));
    patched->push_back(PtShaderDstToken(positionType,positionNum,D3DSP_WRITEMASK_2|D3DSP_WRITEMASK_3));
    patched->push_back(tempNoSwizzle);

    patched->push_back(D3DSIO_END);

    if(info)
    {
        info->patched=true;
        info->shaderMajor=major;
        info->shaderMinor=minor;
        info->positionRegType=positionType;
        info->positionRegNum=positionNum;
        info->tempRegNum=(UINT)temp;
        info->positionWrites=positionWrites;
        info->dwordCountBefore=dwordCount;
        info->dwordCountAfter=(UINT)patched->size();
    }
    return true;
}
