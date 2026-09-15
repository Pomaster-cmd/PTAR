from pathlib import Path
import re
import sys

src = Path('lab/raster/rc41/ptar_rc41_swapchain_hooks.cpp')
out = Path(sys.argv[1] if len(sys.argv) > 1 else 'lab/gui/rc54/ptar_rc41_swapchain_hooks_rc54.cpp')
text = src.read_text(encoding='utf-8')

include_anchor = '#include "ptar_rc41_swapchain_hooks.h"\n'
if text.count(include_anchor) != 1:
    raise SystemExit('RC54: swapchain include anchor mismatch')
text = text.replace(include_anchor, include_anchor + '#include "ptar_rc54_gui_callsite_probe.h"\n', 1)

get_desc_pattern = r'''HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc\(IDXGISwapChain\* sc,DXGI_SWAP_CHAIN_DESC\* desc\)\{.*?\n\}\n\nHRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc1'''
get_desc_replacement = r'''HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc(IDXGISwapChain* sc,DXGI_SWAP_CHAIN_DESC* desc){
    SwapchainHooks* self=swap_owner();
    if(!self) return E_FAIL;
    FnGetDesc original=self->get_desc_original_for(sc);
    if(!original)return E_FAIL;
    const void* returnAddress=_ReturnAddress();
    const CallerDomain domain=self->caller_domain(returnAddress);
    const HRESULT hr=original(sc,desc);
    self->add(self->getDescCalls_);
    if(SUCCEEDED(hr)&&desc){
        const UINT physicalW=desc->BufferDesc.Width;
        const UINT physicalH=desc->BufferDesc.Height;
        const bool virtualized=self->policy_.should_virtualize(domain);
        if(virtualized){
            self->policy_.virtualize_desc(*desc,domain);
            self->add(self->getDescVirtualized_);
        }
        ptar_rc54::QueryObservation observation{};
        observation.kind=ptar_rc54::QueryKind::GetDesc;
        observation.domain=domain;
        observation.returnAddress=returnAddress;
        observation.physicalW=physicalW;
        observation.physicalH=physicalH;
        observation.reportedW=desc->BufferDesc.Width;
        observation.reportedH=desc->BufferDesc.Height;
        observation.virtualized=virtualized;
        ptar_rc54::global_gui_callsite_probe().observe(observation);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc1'''
text, count = re.subn(get_desc_pattern, get_desc_replacement, text, flags=re.S)
if count != 1:
    raise SystemExit(f'RC54: GetDesc replacement count={count}')

get_desc1_pattern = r'''HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc1\(IDXGISwapChain1\* sc,DXGI_SWAP_CHAIN_DESC1\* desc\)\{.*?\n\}\n\nHRESULT STDMETHODCALLTYPE SwapchainHooks::hook_resize_buffers'''
get_desc1_replacement = r'''HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc1(IDXGISwapChain1* sc,DXGI_SWAP_CHAIN_DESC1* desc){
    SwapchainHooks* self=swap_owner();
    if(!self||!self->origGetDesc1_) return E_FAIL;
    const void* returnAddress=_ReturnAddress();
    const CallerDomain domain=self->caller_domain(returnAddress);
    const HRESULT hr=self->origGetDesc1_(sc,desc);
    self->add(self->getDesc1Calls_);
    if(SUCCEEDED(hr)&&desc){
        const UINT physicalW=desc->Width;
        const UINT physicalH=desc->Height;
        const bool virtualized=self->policy_.should_virtualize(domain);
        if(virtualized){
            self->policy_.virtualize_desc1(*desc,domain);
            self->add(self->getDesc1Virtualized_);
        }
        ptar_rc54::QueryObservation observation{};
        observation.kind=ptar_rc54::QueryKind::GetDesc1;
        observation.domain=domain;
        observation.returnAddress=returnAddress;
        observation.physicalW=physicalW;
        observation.physicalH=physicalH;
        observation.reportedW=desc->Width;
        observation.reportedH=desc->Height;
        observation.virtualized=virtualized;
        ptar_rc54::global_gui_callsite_probe().observe(observation);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_resize_buffers'''
text, count = re.subn(get_desc1_pattern, get_desc1_replacement, text, flags=re.S)
if count != 1:
    raise SystemExit(f'RC54: GetDesc1 replacement count={count}')

sentinels = [
    'policy=observe_only_no_behavior_change',
    'global_gui_callsite_probe().observe(observation)',
    'observation.kind=ptar_rc54::QueryKind::GetDesc;',
    'observation.kind=ptar_rc54::QueryKind::GetDesc1;',
    'const bool virtualized=self->policy_.should_virtualize(domain);',
]
# The first sentinel lives in the probe source, not this generated file.
for token in sentinels[1:]:
    if token not in text:
        raise SystemExit(f'RC54: missing generated sentinel: {token}')

# Safety gate: ResizeBuffers implementation must remain byte-for-byte inherited.
base_resize = src.read_text(encoding='utf-8').split('HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_resize_buffers',1)[1]
new_resize = text.split('HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_resize_buffers',1)[1]
if new_resize != base_resize:
    raise SystemExit('RC54: ResizeBuffers tail changed unexpectedly')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(text, encoding='utf-8', newline='\n')
print(f'RC54_SWAPCHAIN_HOOKS_GENERATED={out}')
print('RC54_BEHAVIOR=unchanged; GetDesc/GetDesc1 observations only')
