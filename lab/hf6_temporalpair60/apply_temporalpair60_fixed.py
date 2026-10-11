from pathlib import Path

core = Path('lab/hf6_temporalpair60/apply_temporalpair60.py')
text = core.read_text(encoding='utf-8')

# The base D3D9 proxy legitimately exports Direct3DCreate9Ex for proxy/API
# compatibility. That symbol is NOT evidence of the forbidden HF7+ isolated
# presenter. Remove only this false-positive safety-gate entry before executing
# the otherwise unchanged temporal patcher. The real forbidden objects remain
# gated: isolated D3D9Ex display device, additional swapchain and HF7 presenter.
needle = "    'Direct3DCreate9Ex(',\n"
if text.count(needle) != 1:
    raise SystemExit('temporal core false-positive gate anchor mismatch')
text = text.replace(needle, '', 1)

ns = {'__name__': '__main__', '__file__': str(core)}
exec(compile(text, str(core), 'exec'), ns, ns)
