#pragma once
#include <windows.h>
#include <d3d9.h>
#include <vector>

struct PtJitterPatchedVs
{
    std::vector<DWORD> code;
    UINT constantRegister;
    UINT positionOutputRegister;
    bool patched;
    PtJitterPatchedVs():constantRegister(0),positionOutputRegister(0),patched(false){}
};

static UINT PtShaderRegType(DWORD token)
{
    return ((token&D3DSP_REGTYPE_MASK)>>D3DSP_REGTYPE_SHIFT) |
           ((token&D3DSP_REGTYPE_MASK2)>>D3DSP_REGTYPE_SHIFT2);
}

static DWORD PtShaderRegBits(UINT type,UINT number)
{
    return 0x80000000u |
           (number&D3DSP_REGNUM_MASK) |
           ((type&0x7u)<<D3DSP_REGTYPE_SHIFT) |
           ((type&0x18u)<<D3DSP_REGTYPE_SHIFT2);
}

static DWORD PtVsDest(UINT type,UINT number,DWORD mask)
{
    return PtShaderRegBits(type,number)|mask;
}

static DWORD PtVsSource(UINT type,UINT number,DWORD swizzle)
{
    return PtShaderRegBits(type,number)|swizzle;
}

// Conservative runtime patcher for VS 3.0 only.
//
// It appends:
//   mad oPosition.xy, oPosition.wwww, cJitter, oPosition
// immediately before END. The injected constant is selected from the highest
// unused c-register. Shaders using relative addressing are deliberately skipped
// in R15/JITTER9 phase 1; silently corrupting an indexed constant array is worse
// than leaving one draw unjittered.
static bool PtPatchVs3ForClipJitter(const DWORD* input,PtJitterPatchedVs* result)
{
    if(!input || !result) return false;
    *result=PtJitterPatchedVs();

    if(input[0]!=D3DVS_VERSION(3,0))
        return false;

    bool constUsed[256]={false};
    bool relativeAddressing=false;
    int positionOutput=-1;
    size_t endIndex=0;

    const size_t kMaxTokens=65536;
    size_t i=1;
    while(i<kMaxTokens)
    {
        const DWORD inst=input[i];
        const UINT opcode=inst&D3DSI_OPCODE_MASK;
        if(opcode==D3DSIO_END)
        {
            endIndex=i;
            break;
        }

        if(opcode==D3DSIO_COMMENT)
        {
            const UINT commentDwords=(inst&D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT;
            i+=1u+commentDwords;
            continue;
        }

        const UINT length=(inst&D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(length==0 || i+length>=kMaxTokens)
            return false;

        if(opcode==D3DSIO_DCL && length>=2)
        {
            const DWORD semantic=input[i+1];
            const DWORD dst=input[i+2];
            const UINT usage=semantic&0x1fu;
            if(usage==D3DDECLUSAGE_POSITION && PtShaderRegType(dst)==D3DSPR_OUTPUT)
                positionOutput=(int)(dst&D3DSP_REGNUM_MASK);
        }

        if(opcode==D3DSIO_DEF && length>=5)
        {
            const DWORD dst=input[i+1];
            if(PtShaderRegType(dst)==D3DSPR_CONST)
            {
                const UINT n=dst&D3DSP_REGNUM_MASK;
                if(n<256) constUsed[n]=true;
            }
        }
        else if(opcode!=D3DSIO_DCL && opcode!=D3DSIO_DEFI && opcode!=D3DSIO_DEFB)
        {
            for(UINT k=1;k<=length;++k)
            {
                const DWORD p=input[i+k];
                if((p&0x80000000u)==0) continue;
                const UINT type=PtShaderRegType(p);
                if(type==D3DSPR_CONST)
                {
                    const UINT n=p&D3DSP_REGNUM_MASK;
                    if(n<256) constUsed[n]=true;
                }
                if((p&D3DSHADER_ADDRESSMODE_MASK)!=0)
                    relativeAddressing=true;
            }
        }

        i+=1u+length;
    }

    if(!endIndex || positionOutput<0 || relativeAddressing)
        return false;

    int jitterConst=-1;
    for(int c=255;c>=224;--c)
    {
        if(!constUsed[c])
        {
            jitterConst=c;
            break;
        }
    }
    if(jitterConst<0)
        return false;

    result->code.assign(input,input+endIndex);

    // Shader-model 2+ instruction length excludes the instruction token.
    result->code.push_back((DWORD)D3DSIO_MAD|(4u<<D3DSI_INSTLENGTH_SHIFT));
    result->code.push_back(PtVsDest(D3DSPR_OUTPUT,(UINT)positionOutput,D3DSP_WRITEMASK_0|D3DSP_WRITEMASK_1));
    result->code.push_back(PtVsSource(D3DSPR_OUTPUT,(UINT)positionOutput,0x00ff0000u)); // .wwww
    result->code.push_back(PtVsSource(D3DSPR_CONST,(UINT)jitterConst,0x00e40000u));      // .xyzw
    result->code.push_back(PtVsSource(D3DSPR_OUTPUT,(UINT)positionOutput,0x00e40000u)); // .xyzw
    result->code.push_back((DWORD)D3DSIO_END);

    result->constantRegister=(UINT)jitterConst;
    result->positionOutputRegister=(UINT)positionOutput;
    result->patched=true;
    return true;
}
