from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
proxy=ROOT/'compat/x86_d3d9/ptar_d3d9_proxy.cpp'
p=proxy.read_text(encoding='utf-8')

# This patch is intentionally applied AFTER TEMPORALPAIR60 + F7BENCH1 + SF5PARITY1.
assert 'DrawSf5UnormPassRect' in p
assert 'static HRESULT RenderFGMotionAndIntermediate' in p

# 1) Add only the two new spatial bytecode headers.
old='#include "ptar_universal_ps_bytecode.h"\n#include "ptar_bilinear_ps_bytecode.h"'
new='#include "ptar_universal_ps_bytecode.h"\n#include "ptar_qstage4diag2_d3d9_ps_bytecode.h"\n#include "ptar_spec5_hq900_d3d9_ps_bytecode.h"\n#include "ptar_bilinear_ps_bytecode.h"'
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# 2) Extend only the spatial shader inventory in PTARContext.
old='''    IDirect3DPixelShader9* shader;\n    IDirect3DPixelShader9* universalShader;\n    IDirect3DPixelShader9* bilinearShader;'''
new='''    IDirect3DPixelShader9* shader;\n    IDirect3DPixelShader9* universalShader;\n    IDirect3DPixelShader9* qstage4Shader;\n    IDirect3DPixelShader9* spec5Hq900Shader;\n    IDirect3DPixelShader9* bilinearShader;'''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# 3) Release new spatial objects without touching FG resources.
old='''    if(g_ptar.bilinearShader){g_ptar.bilinearShader->Release();g_ptar.bilinearShader=0;}\n    if(g_ptar.universalShader){g_ptar.universalShader->Release();g_ptar.universalShader=0;}\n    if(g_ptar.shader){g_ptar.shader->Release();g_ptar.shader=0;}'''
new='''    if(g_ptar.bilinearShader){g_ptar.bilinearShader->Release();g_ptar.bilinearShader=0;}\n    if(g_ptar.spec5Hq900Shader){g_ptar.spec5Hq900Shader->Release();g_ptar.spec5Hq900Shader=0;}\n    if(g_ptar.qstage4Shader){g_ptar.qstage4Shader->Release();g_ptar.qstage4Shader=0;}\n    if(g_ptar.universalShader){g_ptar.universalShader->Release();g_ptar.universalShader=0;}\n    if(g_ptar.shader){g_ptar.shader->Release();g_ptar.shader=0;}'''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# 4) Create advanced shaders as optional quality resources. Failure is fail-open;
# the validated runtime remains alive and the selector falls back safely.
anchor='''    PtDiagStage("Initialize_CreateBilinearShader");\n    hr=dev->CreatePixelShader((const DWORD*)g_ptarBilinearPs,&g_ptar.bilinearShader);'''
insert='''    PtDiagStage("Initialize_CreateFullQualitySpatialShaders");\n    HRESULT q4Hr=dev->CreatePixelShader(\n        (const DWORD*)g_ptarQstage4Diag2Ps,&g_ptar.qstage4Shader);\n    PtDiagLogA(\n        "INIT_CreateQSTAGE4DIAG2 hr=0x%08lX ptr=%p",\n        (unsigned long)q4Hr,g_ptar.qstage4Shader);\n    if(FAILED(q4Hr))\n        g_ptar.qstage4Shader=0;\n\n    HRESULT adaptiveHr=dev->CreatePixelShader(\n        (const DWORD*)g_ptarSpec5Hq900Ps,&g_ptar.spec5Hq900Shader);\n    PtDiagLogA(\n        "INIT_CreateSPEC5_HQ900 hr=0x%08lX ptr=%p",\n        (unsigned long)adaptiveHr,g_ptar.spec5Hq900Shader);\n    if(FAILED(adaptiveHr))\n        g_ptar.spec5Hq900Shader=0;\n\n'''+anchor
assert p.count(anchor)==1, p.count(anchor)
p=p.replace(anchor,insert,1)

# 5) Replace only the spatial selector/body. SF5PARITY1's colour-state wrapper
# remains the sole execution wrapper, so its restore-before-HF6 boundary survives.
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

    // D3D11 HQ900 contract: strictly above 1.0 and at/below 1.5 on both axes.
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
    float sharp=0.0f;

    if(PtHudUseMoe())
    {
        if(exactQ720 && g_ptar.spec5Hq900Shader)
        {
            // Inquisitor Q720 SPEC5: Sharp2.88 selects NativeFit40 + Detail25.
            spatialShader=g_ptar.spec5Hq900Shader;
            spatialMode="SPEC5_Q720_NATIVEFIT40_DETAIL25";
            sharp=2.88f;
        }
        else if(exact15 && g_ptar.qstage4Shader)
        {
            // Adopted D3D11 exact-x1.5 quality path.
            spatialShader=g_ptar.qstage4Shader;
            spatialMode="QSTAGE4DIAG2_K130_N90";
        }
        else if(hq900 && g_ptar.spec5Hq900Shader)
        {
            // D3D11 HQ900 companion path, nonexact 1.0<scale<=1.5.
            spatialShader=g_ptar.spec5Hq900Shader;
            spatialMode="HQ900_SHARP96";
            sharp=0.96f;
        }
        else if(exact15 && g_ptar.shader)
        {
            // Quality shader creation failed: retain the already field-proven SF5
            // exact path rather than aborting PTAR/FG. This is a safety fallback,
            // never the expected FULLQUALITY1 route.
            spatialShader=g_ptar.shader;
            spatialMode="SF5_EXACT_FAILOPEN";
        }
        else
        {
            // Match D3D11 HQ900 domain policy outside validated geometry.
            spatialShader=g_ptar.bilinearShader;
            spatialMode="BILINEAR_DOMAIN_FAILOPEN";
        }
    }

    float sizes[4]={
        (float)g_ptar.sourceW,
        (float)g_ptar.sourceH,
        (float)fit.width,
        (float)fit.height};
    float quality[4]={sharp,0.0f,0.0f,0.0f};

    // c1 is spatial-only. DrawFullscreenPassRect sets c0 but never writes c1;
    // HF6 uses its own c0 and no c1 dependency. SF5PARITY1 restores sRGB state
    // before RenderFGMotionAndIntermediate is entered.
    HRESULT c1Hr=dev->SetPixelShaderConstantF(1,quality,1);
    if(FAILED(c1Hr))
    {
        PtDiagLogA(
            "SPATIAL_QUALITY_CONSTANT_FAIL hr=0x%08lX mode=%s -> BILINEAR",
            (unsigned long)c1Hr,spatialMode);
        spatialShader=g_ptar.bilinearShader;
        spatialMode="BILINEAR_C1_FAILOPEN";
        sharp=0.0f;
    }

    PtDiagLogA(
        "SPATIAL_PASS mode=%s src=%ux%u fit=%u,%u %ux%u target=%ux%u sharp=%.2f q4=%s adaptive=%s",
        spatialMode,
        g_ptar.sourceW,g_ptar.sourceH,
        fit.x,fit.y,fit.width,fit.height,
        g_ptar.outputW,g_ptar.outputH,
        sharp,
        g_ptar.qstage4Shader?"YES":"NO",
        g_ptar.spec5Hq900Shader?"YES":"NO");

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
assert p.count('#include "ptar_qstage4diag2_d3d9_ps_bytecode.h"')==1
assert p.count('#include "ptar_spec5_hq900_d3d9_ps_bytecode.h"')==1
assert p.count('DrawSf5UnormPassRect(')==2  # definition + spatial call
assert 'SPEC5_Q720_NATIVEFIT40_DETAIL25' in p
assert 'QSTAGE4DIAG2_K130_N90' in p
assert 'HQ900_SHARP96' in p
assert 'BILINEAR_DOMAIN_FAILOPEN' in p
assert 'static HRESULT RenderFGMotionAndIntermediate' in p
assert 'Critical isolation boundary: restore the exact game state BEFORE HF6.' in p

proxy.write_text(p,encoding='utf-8')
print('FULLQUALITY1_PATCH=PASS')
print('FULLQUALITY1_SCOPE=SPATIAL_SHADER_INVENTORY_AND_SELECTOR_ONLY')
print('FULLQUALITY1_Q720=SPEC5_SHARP2P88_NATIVEFIT40_DETAIL25')
print('FULLQUALITY1_EXACT15=QSTAGE4DIAG2_K130_N90')
print('FULLQUALITY1_HQ900=NONEXACT_1_TO_1P5_SHARP0P96')
print('FULLQUALITY1_OUT_OF_DOMAIN=BILINEAR_FAILOPEN')
print('FULLQUALITY1_HF6_CHANGE=NONE')
print('FULLQUALITY1_TEMPORAL_CHANGE=NONE')
print('FULLQUALITY1_F7_CHANGE=NONE')
