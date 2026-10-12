from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
proxy=ROOT/'compat/x86_d3d9/ptar_d3d9_proxy.cpp'
p=proxy.read_text(encoding='utf-8')

# Applied only after TEMPORALPAIR60 + F7BENCH1 + SF5PARITY1 reconstruction.
assert 'DrawSf5UnormPassRect' in p
assert 'static HRESULT RenderFGMotionAndIntermediate' in p

# 1) Add only the three full-quality spatial bytecode headers.
old='#include "ptar_universal_ps_bytecode.h"\n#include "ptar_bilinear_ps_bytecode.h"'
new=(
    '#include "ptar_universal_ps_bytecode.h"\n'
    '#include "ptar_qstage4diag2_d3d9_ps_bytecode.h"\n'
    '#include "ptar_spec5_d3d9_ps_bytecode.h"\n'
    '#include "ptar_hq900_d3d9_ps_bytecode.h"\n'
    '#include "ptar_bilinear_ps_bytecode.h"')
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# 2) Extend only the spatial shader inventory.
old='''    IDirect3DPixelShader9* shader;\n    IDirect3DPixelShader9* universalShader;\n    IDirect3DPixelShader9* bilinearShader;'''
new='''    IDirect3DPixelShader9* shader;\n    IDirect3DPixelShader9* universalShader;\n    IDirect3DPixelShader9* qstage4Shader;\n    IDirect3DPixelShader9* spec5Shader;\n    IDirect3DPixelShader9* hq900Shader;\n    IDirect3DPixelShader9* bilinearShader;'''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# 3) Release new spatial objects without touching FG resources.
old='''    if(g_ptar.bilinearShader){g_ptar.bilinearShader->Release();g_ptar.bilinearShader=0;}\n    if(g_ptar.universalShader){g_ptar.universalShader->Release();g_ptar.universalShader=0;}\n    if(g_ptar.shader){g_ptar.shader->Release();g_ptar.shader=0;}'''
new='''    if(g_ptar.bilinearShader){g_ptar.bilinearShader->Release();g_ptar.bilinearShader=0;}\n    if(g_ptar.hq900Shader){g_ptar.hq900Shader->Release();g_ptar.hq900Shader=0;}\n    if(g_ptar.spec5Shader){g_ptar.spec5Shader->Release();g_ptar.spec5Shader=0;}\n    if(g_ptar.qstage4Shader){g_ptar.qstage4Shader->Release();g_ptar.qstage4Shader=0;}\n    if(g_ptar.universalShader){g_ptar.universalShader->Release();g_ptar.universalShader=0;}\n    if(g_ptar.shader){g_ptar.shader->Release();g_ptar.shader=0;}'''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# 4) Create advanced shaders as independent optional quality resources. Each one
# can fail-open without affecting the already validated SF5/HF6 runtime.
anchor='''    PtDiagStage("Initialize_CreateBilinearShader");\n    hr=dev->CreatePixelShader((const DWORD*)g_ptarBilinearPs,&g_ptar.bilinearShader);'''
insert='''    PtDiagStage("Initialize_CreateFullQualitySpatialShaders");\n    HRESULT q4Hr=dev->CreatePixelShader(\n        (const DWORD*)g_ptarQstage4Diag2Ps,&g_ptar.qstage4Shader);\n    PtDiagLogA(\n        "INIT_CreateQSTAGE4DIAG2 hr=0x%08lX ptr=%p",\n        (unsigned long)q4Hr,g_ptar.qstage4Shader);\n    if(FAILED(q4Hr)) g_ptar.qstage4Shader=0;\n\n    HRESULT spec5Hr=dev->CreatePixelShader(\n        (const DWORD*)g_ptarSpec5Ps,&g_ptar.spec5Shader);\n    PtDiagLogA(\n        "INIT_CreateSPEC5 hr=0x%08lX ptr=%p",\n        (unsigned long)spec5Hr,g_ptar.spec5Shader);\n    if(FAILED(spec5Hr)) g_ptar.spec5Shader=0;\n\n    HRESULT hq900Hr=dev->CreatePixelShader(\n        (const DWORD*)g_ptarHq900Ps,&g_ptar.hq900Shader);\n    PtDiagLogA(\n        "INIT_CreateHQ900 hr=0x%08lX ptr=%p",\n        (unsigned long)hq900Hr,g_ptar.hq900Shader);\n    if(FAILED(hq900Hr)) g_ptar.hq900Shader=0;\n\n'''+anchor
assert p.count(anchor)==1, p.count(anchor)
p=p.replace(anchor,insert,1)

# 5) Replace only the spatial selector/body. SF5PARITY1 remains the sole colour
# state wrapper, preserving its restore-before-HF6 isolation boundary.
start=p.index('static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)')
end=p.index('static HRESULT RenderFGMotionAndIntermediate',start)
old_block=p[start:end]
assert 'DrawSf5UnormPassRect' in old_block

new_block=r'''static bool PtSpatialExactQ720(
    UINT sourceW,UINT sourceH,
    const PTARPresentationRect& fit)
{
    return
        sourceW==1280u && sourceH==720u &&
        fit.width==1920u && fit.height==1080u;
}

static bool PtSpatialHq900Eligible(
    UINT sourceW,UINT sourceH,
    const PTARPresentationRect& fit)
{
    if(!sourceW || !sourceH || !fit.width || !fit.height)
        return false;

    const unsigned long long inputPixels=
        (unsigned long long)sourceW*(unsigned long long)sourceH;
    const unsigned long long outputPixels=
        (unsigned long long)fit.width*(unsigned long long)fit.height;
    if(inputPixels>1440000ull ||
       fit.width>1920u || fit.height>1080u ||
       outputPixels>2073600ull)
        return false;

    // D3D11 HQ900 domain: strictly above 1.0 and at/below 1.5 on both axes.
    const unsigned long long sx1000000=
        ((unsigned long long)fit.width*1000000ull)/(unsigned long long)sourceW;
    const unsigned long long sy1000000=
        ((unsigned long long)fit.height*1000000ull)/(unsigned long long)sourceH;
    return
        sx1000000>1000000ull && sx1000000<=1500000ull &&
        sy1000000>1000000ull && sy1000000<=1500000ull;
}

static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)
{
    if(!g_ptar.spatialActive)
    {
        PtDiagLogA(
            "SPATIAL_PASS mode=NATIVE_1X1 src=%ux%u out=%ux%u",
            g_ptar.sourceW,g_ptar.sourceH,
            g_ptar.outputW,g_ptar.outputH);
        return DrawFullscreenPassRect(
            dev,
            g_ptar.currentRealSurface,
            g_ptar.outputW,
            g_ptar.outputH,
            0,0,g_ptar.outputW,g_ptar.outputH,
            false,
            g_ptar.bilinearShader,
            g_ptar.sourceTexture,
            0,0,0);
    }

    const PTARPresentationRect fit=PtResolutionAspectFit(
        g_ptar.sourceW,g_ptar.sourceH,
        g_ptar.outputW,g_ptar.outputH);

    const bool exact15=PtResolutionExactScale15(
        g_ptar.sourceW,g_ptar.sourceH,
        fit.width,fit.height);
    const bool exactQ720=PtSpatialExactQ720(
        g_ptar.sourceW,g_ptar.sourceH,fit);
    const bool hq900=
        !exact15 && PtSpatialHq900Eligible(
            g_ptar.sourceW,g_ptar.sourceH,fit);

    IDirect3DPixelShader9* spatialShader=g_ptar.bilinearShader;
    const char* spatialMode="BILINEAR";

    if(PtHudUseMoe())
    {
        if(exactQ720 && g_ptar.spec5Shader)
        {
            // Inquisitor Q720 SPEC5: fixed Sharp2.88 source specialization,
            // NativeFit40 plus selective Detail25, no runtime c1 dependency.
            spatialShader=g_ptar.spec5Shader;
            spatialMode="SPEC5_Q720_NATIVEFIT40_DETAIL25";
        }
        else if(exact15 && g_ptar.qstage4Shader)
        {
            // Adopted D3D11 exact-x1.5 path: K130/N90 + 2-tap diagonal residual.
            spatialShader=g_ptar.qstage4Shader;
            spatialMode="QSTAGE4DIAG2_K130_N90";
        }
        else if(hq900 && g_ptar.hq900Shader)
        {
            // D3D11 HQ900 companion path: specialized Sharp0.96 branch.
            spatialShader=g_ptar.hq900Shader;
            spatialMode="HQ900_SHARP96";
        }
        else if(exact15 && g_ptar.shader)
        {
            // Quality shader creation failure retains the field-proven SF5 exact
            // path rather than affecting FG/presentation availability.
            spatialShader=g_ptar.shader;
            spatialMode="SF5_EXACT_FAILOPEN";
        }
        else
        {
            spatialShader=g_ptar.bilinearShader;
            spatialMode="BILINEAR_DOMAIN_FAILOPEN";
        }
    }

    float sizes[4]={
        (float)g_ptar.sourceW,
        (float)g_ptar.sourceH,
        (float)fit.width,
        (float)fit.height};

    PtDiagLogA(
        "SPATIAL_PASS mode=%s src=%ux%u fit=%u,%u %ux%u target=%ux%u q4=%s spec5=%s hq900=%s",
        spatialMode,
        g_ptar.sourceW,g_ptar.sourceH,
        fit.x,fit.y,fit.width,fit.height,
        g_ptar.outputW,g_ptar.outputH,
        g_ptar.qstage4Shader?"YES":"NO",
        g_ptar.spec5Shader?"YES":"NO",
        g_ptar.hq900Shader?"YES":"NO");

    return DrawSf5UnormPassRect(
        dev,
        g_ptar.currentRealSurface,
        g_ptar.outputW,
        g_ptar.outputH,
        fit.x,fit.y,fit.width,fit.height,
        true,
        spatialShader,
        g_ptar.sourceTexture,
        sizes);
}

'''
p=p[:start]+new_block+p[end:]

# Hard scope/integration gates.
for marker in (
    '#include "ptar_qstage4diag2_d3d9_ps_bytecode.h"',
    '#include "ptar_spec5_d3d9_ps_bytecode.h"',
    '#include "ptar_hq900_d3d9_ps_bytecode.h"'):
    assert p.count(marker)==1, marker
assert 'ptar_spec5_hq900_d3d9_ps_bytecode.h' not in p
assert p.count('DrawSf5UnormPassRect(')==2
assert 'SPEC5_Q720_NATIVEFIT40_DETAIL25' in p
assert 'QSTAGE4DIAG2_K130_N90' in p
assert 'HQ900_SHARP96' in p
assert 'BILINEAR_DOMAIN_FAILOPEN' in p
assert 'SetPixelShaderConstantF(1' not in new_block
assert 'static HRESULT RenderFGMotionAndIntermediate' in p
assert 'Critical isolation boundary: restore the exact game state BEFORE HF6.' in p

proxy.write_text(p,encoding='utf-8')
print('FULLQUALITY1_PATCH=PASS')
print('FULLQUALITY1_SCOPE=SPATIAL_SHADER_INVENTORY_AND_SELECTOR_ONLY')
print('FULLQUALITY1_Q720=SPEC5_SPECIALIZED_NATIVEFIT40_DETAIL25')
print('FULLQUALITY1_EXACT15=QSTAGE4DIAG2_K130_N90')
print('FULLQUALITY1_HQ900=SPECIALIZED_SHARP0P96')
print('FULLQUALITY1_DYNAMIC_C1=NONE')
print('FULLQUALITY1_OUT_OF_DOMAIN=BILINEAR_FAILOPEN')
print('FULLQUALITY1_HF6_CHANGE=NONE')
print('FULLQUALITY1_TEMPORAL_CHANGE=NONE')
print('FULLQUALITY1_F7_CHANGE=NONE')
