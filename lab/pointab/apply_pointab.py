from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
hud=ROOT/'compat/x86_d3d9/ptar_hud.h'
proxy=ROOT/'compat/x86_d3d9/ptar_d3d9_proxy.cpp'

h=hud.read_text(encoding='utf-8')

old='''static bool g_ptarHudVisible=true;\nstatic bool g_ptarHudUseMoe=true;\nstatic bool g_ptarHudFgEnabled=false;'''
new='''static bool g_ptarHudVisible=true;\n// Manual spatial filter mode: 0=POINT raw-720 reference, 1=BILINEAR reference, 2=PTAR.\n// Default remains PTAR. F6 is a direct PTAR<->POINT A/B toggle; F12 cycles all 3.\nstatic int g_ptarHudFilterMode=2;\nstatic bool g_ptarHudFgEnabled=false;'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

old='''        else\n        {\n            g_ptarHudUseMoe=!g_ptarHudUseMoe;\n            PtHudFeedback(7,g_ptarHudUseMoe?2:1,0,0);\n            PtDiagLogA(\n                "HOTKEY F6 ManualFilter=%s",\n                g_ptarHudUseMoe?"PTAR_MOE":"BILINEAR_REF");\n        }'''
new='''        else\n        {\n            // Direct visual A/B requested for D3D9 quality work:\n            // PTAR reconstruction <-> raw 720p source enlarged with POINT.\n            g_ptarHudFilterMode=(g_ptarHudFilterMode==0)?2:0;\n            PtHudFeedback(7,g_ptarHudFilterMode,0,0);\n            PtDiagLogA(\n                "HOTKEY F6 ManualFilter=%s",\n                g_ptarHudFilterMode==0?"POINT_RAW720_REF":"PTAR");\n        }'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

old='''    if(f12 && !g_ptarHudPrevF12 && !ctrl)\n    {\n        g_ptarHudUseMoe=!g_ptarHudUseMoe;\n        PtHudFeedback(7,g_ptarHudUseMoe?2:1,0,0);\n        PtDiagLogA(\n            "HOTKEY F12 FilterNext=%s",\n            g_ptarHudUseMoe?"PTAR_MOE":"BILINEAR_REF");\n    }'''
new='''    if(f12 && !g_ptarHudPrevF12 && !ctrl)\n    {\n        // Preserve the broader manual filter comparison: PTAR -> POINT -> BILINEAR -> PTAR.\n        g_ptarHudFilterMode=(g_ptarHudFilterMode+1)%3;\n        PtHudFeedback(7,g_ptarHudFilterMode,0,0);\n        PtDiagLogA(\n            "HOTKEY F12 FilterNext=%s",\n            g_ptarHudFilterMode==0?"POINT_RAW720_REF":\n            g_ptarHudFilterMode==1?"BILINEAR_REF":"PTAR");\n    }'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

old='''static bool PtHudUseMoe(){return g_ptarHudUseMoe;}\nstatic bool PtHudFgEnabled(){return g_ptarHudFgEnabled;}'''
new='''static bool PtHudUseMoe(){return g_ptarHudFilterMode==2;}\nstatic bool PtHudPointRef(){return g_ptarHudFilterMode==0;}\nstatic bool PtHudFgEnabled(){return g_ptarHudFgEnabled;}'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

old='''    if(!spatialActive)\n        return 5; // COPY 1X1\n    if(!g_ptarHudUseMoe)\n        return 1; // BILINEAR\n    if(exactScale15)\n        return 2; // PTAR X15'''
new='''    if(!spatialActive)\n        return 5; // COPY 1X1\n    if(g_ptarHudFilterMode==0)\n        return 0; // POINT raw source reference\n    if(g_ptarHudFilterMode==1)\n        return 1; // BILINEAR\n    if(exactScale15)\n        return 2; // PTAR X15'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

assert 'g_ptarHudUseMoe' not in h
assert 'PtHudPointRef(){return g_ptarHudFilterMode==0;}' in h
assert 'POINT_RAW720_REF' in h
hud.write_text(h,encoding='utf-8')

p=proxy.read_text(encoding='utf-8')
anchor='''    IDirect3DPixelShader9* spatialShader=g_ptar.bilinearShader;\n    const char* spatialMode="BILINEAR";\n\n    if(PtHudUseMoe())'''
insert='''    // POINT A/B reference: raw source pixels are enlarged to the same fitted\n    // output rectangle with nearest-neighbour filtering. No PTAR spatial\n    // reconstruction or bilinear smoothing is applied. FG/presenter/HUD remain\n    // unchanged so F6 isolates the spatial reconstruction itself.\n    if(PtHudPointRef())\n    {\n        RECT srcRect={0,0,(LONG)g_ptar.sourceW,(LONG)g_ptar.sourceH};\n        RECT dstRect={\n            (LONG)fit.x,(LONG)fit.y,\n            (LONG)(fit.x+fit.width),(LONG)(fit.y+fit.height)};\n        PtDiagLogA(\n            "SPATIAL_PASS mode=POINT_RAW720_REF src=%ux%u fit=%u,%u %ux%u target=%ux%u",\n            g_ptar.sourceW,g_ptar.sourceH,\n            fit.x,fit.y,fit.width,fit.height,\n            g_ptar.outputW,g_ptar.outputH);\n        HRESULT pointHr=dev->StretchRect(\n            g_ptar.sourceSurface,&srcRect,\n            g_ptar.currentRealSurface,&dstRect,\n            D3DTEXF_POINT);\n        if(SUCCEEDED(pointHr))\n            return pointHr;\n\n        // Fail open to the existing bilinear reference if a driver rejects\n        // point StretchRect. Never fall through to PTAR while HUD says POINT.\n        PtDiagLogA(\n            "POINT_RAW720_STRETCHRECT_FAIL hr=0x%08lX -> BILINEAR_REF",\n            (unsigned long)pointHr);\n    }\n\n    IDirect3DPixelShader9* spatialShader=g_ptar.bilinearShader;\n    const char* spatialMode="BILINEAR";\n\n    if(PtHudUseMoe())'''
assert p.count(anchor)==1, p.count(anchor)
p=p.replace(anchor,insert,1)
assert p.count('D3DTEXF_POINT')==1
assert p.count('POINT_RAW720_REF')>=1
assert p.count('POINT_RAW720_STRETCHRECT_FAIL')==1
assert p.index('POINT_RAW720_REF') < p.index('static HRESULT RenderFGMotionAndIntermediate')
proxy.write_text(p,encoding='utf-8')

print('POINTAB_PATCH=PASS')
print('POINTAB_F6=PTAR_POINT_DIRECT_TOGGLE')
print('POINTAB_F12=PTAR_POINT_BILINEAR_CYCLE')
print('POINTAB_POINT_FILTER=D3DTEXF_POINT')
print('POINTAB_FG_PRESENTER_UNCHANGED=BY_SCOPE')
