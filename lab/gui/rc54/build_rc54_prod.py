from pathlib import Path
import sys

src = Path('lab/raster/rc41/ptar_rc41_prod.cpp')
out = Path(sys.argv[1] if len(sys.argv) > 1 else 'lab/gui/rc54/ptar_rc41_prod_rc54.cpp')
text = src.read_text(encoding='utf-8')

include_anchor = '#include "ptar_rc41_swapchain_hooks.h"\n'
if text.count(include_anchor) != 1:
    raise SystemExit('RC54: production include anchor mismatch')
text = text.replace(include_anchor, include_anchor + '#include "ptar_rc54_gui_callsite_probe.h"\n', 1)

anchor = '''    if(!game||!runtimeModule||!g_self){backbuffer->Release();log_line("FAIL RC41 module identity unavailable; fail-open");return -33;}\n\n'''
if text.count(anchor) != 1:
    raise SystemExit('RC54: production configure anchor mismatch')
probe = '''    if(!game||!runtimeModule||!g_self){backbuffer->Release();log_line("FAIL RC41 module identity unavailable; fail-open");return -33;}\n\n    if(!ptar_rc54::global_gui_callsite_probe().configure(game,g_self,contract)){\n        log_line("WARN RC54 GUI callsite probe unavailable; RC52 behavior preserved");\n    } else {\n        log_line("ACTIVE_RC54_GUI_CALLSITE_PROBE observe-only");\n    }\n\n'''
text = text.replace(anchor, probe, 1)

if 'ACTIVE_RC54_GUI_CALLSITE_PROBE observe-only' not in text:
    raise SystemExit('RC54: probe activation sentinel missing')
if 'g_swapchainHooks.configure(contract,game,runtimeModule,g_self,refresh_primary,nullptr)' not in text:
    raise SystemExit('RC54: inherited swapchain configuration missing')
if 'ACTIVE_RC41_LOGICAL_NATIVE_SUBRASTER logical/physical' not in text:
    raise SystemExit('RC54: inherited RC41 activation missing')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(text, encoding='utf-8', newline='\n')
print(f'RC54_PROD_GENERATED={out}')
print('RC54_BASELINE=RC41 production + RC52 context hooks + observation-only swapchain probe')
