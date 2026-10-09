#!/usr/bin/env python3
from pathlib import Path

p = Path('compat/x86_d3d9/ptar_gw16i_hud_d3d9.h')
s = p.read_text(encoding='utf-8')

anchor = '''static void PtGw16FeedbackPanel(\n'''
if s.count(anchor) != 1:
    raise SystemExit('LOCK_FAIL feedback panel anchor')

helper = r'''static void PtGw16DiagnosticCase(
    IDirect3DDevice9* dev,
    LONG x,LONG y,
    D3DCOLOR bg,
    D3DCOLOR frame)
{
    if(!dev) return;

    // Explicit framed two-cell popup for the CTRL+F1/F5 diagnostic UI.
    // 440x96 keeps the validated D3D11 feedback footprint, but unlike the
    // earlier monolithic dark slab the two rows are visibly boxed/cased.
    PtGw16Panel(dev,x,y,x+440,y+96,frame);
    PtGw16Panel(dev,x+4,y+4,x+436,y+44,bg);
    PtGw16Panel(dev,x+4,y+52,x+436,y+92,bg);
}

'''
s = s.replace(anchor, helper + anchor, 1)

old_panel = '''    PtGw16Panel(\n        dev,\n        originX,originY,\n        originX+440,originY+96,\n        bg);\n\n    PTARGw16RectBatch batch={};\n    const LONG x=originX+16;\n    const LONG y=originY+16;\n'''
new_panel = '''    const bool diagnosticCase=(type>=19 && type<=28);\n    if(diagnosticCase)\n    {\n        const D3DCOLOR frame=D3DCOLOR_XRGB(86,98,116);\n        PtGw16DiagnosticCase(dev,originX,originY,bg,frame);\n    }\n    else\n    {\n        PtGw16Panel(\n            dev,\n            originX,originY,\n            originX+440,originY+96,\n            bg);\n    }\n\n    PTARGw16RectBatch batch={};\n    const LONG x=originX+16;\n    const LONG y=originY+14;\n    const LONG y2=originY+62;\n'''
if s.count(old_panel) != 1:
    raise SystemExit('LOCK_FAIL monolithic panel block')
s = s.replace(old_panel, new_panel, 1)

old_cases = '''        case 19:\n            PtGw16Text(dev,&batch,x,y,"C+F5 PRE FG",fg);\n            break;\n        case 20:\n            PtGw16Text(dev,&batch,x,y,"C+F5 FG ACTIVE",fg);\n            break;\n        case 21:\n            PtGw16Text(dev,&batch,x,y,"C+F5 POST FG",fg);\n            break;\n        case 22:\n            PtGw16Text(dev,&batch,x,y,"C+F5 MEASURE",fg);\n            break;\n        case 23:\n            PtGw16Text(dev,&batch,x,y,"C+F5 DONE",fg);\n            break;\n        case 24:\n            PtGw16Text(dev,&batch,x,y,"C+F1 READY",fg);\n            break;\n        case 25:\n            PtGw16Text(dev,&batch,x,y,"C+F1 MEASURE",fg);\n            break;\n        case 26:\n            PtGw16Text(dev,&batch,x,y,"C+F1 DONE",fg);\n            break;\n        case 27:\n            PtGw16Text(dev,&batch,x,y,"C+F1 ERROR",fg);\n            break;\n        case 28:\n            PtGw16Text(dev,&batch,x,y,"C+F5 ERROR",fg);\n            break;\n'''
new_cases = '''        case 19:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F5",fg);\n            PtGw16Text(dev,&batch,x,y2,"PRE FG",fg);\n            break;\n        case 20:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F5",fg);\n            PtGw16Text(dev,&batch,x,y2,"FG ACTIVE",fg);\n            break;\n        case 21:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F5",fg);\n            PtGw16Text(dev,&batch,x,y2,"POST FG",fg);\n            break;\n        case 22:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F5",fg);\n            PtGw16Text(dev,&batch,x,y2,"MEASURE",fg);\n            break;\n        case 23:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F5",fg);\n            PtGw16Text(dev,&batch,x,y2,"DONE",fg);\n            break;\n        case 24:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F1",fg);\n            PtGw16Text(dev,&batch,x,y2,a==0?"READY 60":"READY 120",fg);\n            break;\n        case 25:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F1",fg);\n            PtGw16Text(dev,&batch,x,y2,a==0?"MEASURE 60":"MEASURE 120",fg);\n            break;\n        case 26:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F1",fg);\n            PtGw16Text(dev,&batch,x,y2,"DONE",fg);\n            break;\n        case 27:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F1",fg);\n            PtGw16Text(dev,&batch,x,y2,"ERROR",fg);\n            break;\n        case 28:\n            PtGw16Text(dev,&batch,x,y,"CTRL+F5",fg);\n            PtGw16Text(dev,&batch,x,y2,"ERROR",fg);\n            break;\n'''
if s.count(old_cases) != 1:
    raise SystemExit('LOCK_FAIL diagnostic cases block')
s = s.replace(old_cases, new_cases, 1)

# Tight post-patch gates. Legacy activation/status cases must stay present.
for tok in (
    'PtGw16DiagnosticCase(',
    'x+4,y+4,x+436,y+44',
    'x+4,y+52,x+436,y+92',
    'D3DCOLOR_XRGB(86,98,116)',
    '"CTRL+F5"',
    '"CTRL+F1"',
    '"READY 60"',
    '"READY 120"',
    'case 12:',
    'case 13:',
    '"NV ON"',
    '"NV OFF"',
):
    if tok not in s:
        raise SystemExit('POST_FAIL '+tok)

p.write_text(s, encoding='utf-8', newline='\n')
print('D3D9_CASE_POPUP_PATCH=PASS')
print('FOOTPRINT=440x96')
print('OUTER_FRAME=RGB_86_98_116')
print('CELLS=2')
print('CELL1=4,4..436,44')
print('CELL2=4,52..436,92')
print('LEGACY_TYPES_4_18=UNCHANGED_BY_PATCH')
