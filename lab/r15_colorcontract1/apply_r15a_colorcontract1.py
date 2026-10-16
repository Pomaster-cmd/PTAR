from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROXY = ROOT / 'compat' / 'x86_d3d9' / 'ptar_d3d9_proxy.cpp'

text = PROXY.read_text(encoding='utf-8').replace('\r\n', '\n')

if 'R15A COLOR-CONTRACT1' in text:
    raise SystemExit('R15A color contract already applied')

fn_anchor = 'static HRESULT DrawFullscreenPassRect(\n'
fn_start = text.find(fn_anchor)
if fn_start < 0:
    raise SystemExit('DrawFullscreenPassRect anchor missing')
fn_end = text.find('static HRESULT DrawFullscreenPass(\n', fn_start + len(fn_anchor))
if fn_end < 0:
    raise SystemExit('DrawFullscreenPassRect end anchor missing')

helper = '''// R15A COLOR-CONTRACT1\n// PTAR internal fullscreen shader passes operate in a deterministic numeric\n// UNORM domain. Game sRGB state is restored only at the outer Present state-\n// block boundary; it must never leak into spatial, motion or temporal math.\nstatic void PtR15AApplyNumericColorContract(IDirect3DDevice9* dev)\n{\n    if(!dev) return;\n    for(DWORD sampler=0;sampler<3;++sampler)\n        dev->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);\n    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);\n}\n\n'''

text = text[:fn_start] + helper + text[fn_start:]
fn_start += len(helper)
fn_end += len(helper)
segment = text[fn_start:fn_end]

loop_anchor = '''    for(DWORD sampler=0;sampler<3;++sampler)\n    {\n        dev->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);\n        dev->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);\n        dev->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);\n        dev->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);\n        dev->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);\n    }\n\n    if(clearTarget)\n'''
if segment.count(loop_anchor) != 1:
    raise SystemExit(f'DrawFullscreenPassRect sampler anchor count={segment.count(loop_anchor)}')

loop_replacement = '''    for(DWORD sampler=0;sampler<3;++sampler)\n    {\n        dev->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);\n        dev->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);\n        dev->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);\n        dev->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);\n        dev->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);\n    }\n\n    // Central R15A boundary: s0..s2 and the render target are numeric, never\n    // implicitly decoded/encoded through inherited D3D9 sRGB state.\n    PtR15AApplyNumericColorContract(dev);\n\n    if(clearTarget)\n'''
segment = segment.replace(loop_anchor, loop_replacement, 1)
text = text[:fn_start] + segment + text[fn_end:]

# Hard scope and semantic gates.
if text.count('R15A COLOR-CONTRACT1') != 1:
    raise SystemExit('R15A marker count mismatch')
if text.count('PtR15AApplyNumericColorContract(') != 2:
    raise SystemExit('R15A helper must have exactly definition + one call')
helper_start = text.find('static void PtR15AApplyNumericColorContract(')
helper_end = text.find('static HRESULT DrawFullscreenPassRect(', helper_start)
helper_text = text[helper_start:helper_end]
for token in (
    'D3DSAMP_SRGBTEXTURE,FALSE',
    'D3DRS_SRGBWRITEENABLE,FALSE',
    'sampler<3'):
    if token not in helper_text:
        raise SystemExit(f'R15A helper token missing: {token}')

PROXY.write_text(text, encoding='utf-8', newline='\n')
print('R15A_COLORCONTRACT1_PATCH=PASS')
print('R15A_INTERNAL_SRGB_SAMPLERS_0_2=FORCED_FALSE')
print('R15A_INTERNAL_SRGB_WRITE=FORCED_FALSE')
print('R15A_GAME_STATE_RESTORE_BOUNDARY=PRESENT_STATEBLOCK')
print('R15A_JITTER_CHANGE=NONE')
print('R15A_MOTION_ALGORITHM_CHANGE=NONE')
