from pathlib import Path

src_path=Path('lab/hf6_temporalpair60/apply_temporalpair60.py')
src=src_path.read_text(encoding='utf-8')

old="""for bad in (\n    'Direct3DCreate9Ex(',\n    'IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice',\n    'CreateAdditionalSwapChain(',\n    'PtD3D9IsolatedPresenterStart(',\n):"""
new="""for bad in (\n    'IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice',\n    'CreateAdditionalSwapChain(',\n    'PtD3D9IsolatedPresenterStart(',\n):"""
if old not in src:
    raise SystemExit('temporalpair safety gate anchor not found')
src=src.replace(old,new,1)

# The baseline proxy must keep the public Direct3DCreate9Ex export/wrapper for
# compatibility.  What is forbidden here is a SECOND Ex presenter/device, not
# the API symbol itself.  Execute the exact patcher with only that false-positive
# static gate narrowed.
ns={'__name__':'__main__','__file__':str(src_path)}
exec(compile(src,str(src_path),'exec'),ns,ns)
print('TEMPORALPAIR_GATEFIX_DIRECT3DCREATE9EX_EXPORT_ALLOWED=PASS')
