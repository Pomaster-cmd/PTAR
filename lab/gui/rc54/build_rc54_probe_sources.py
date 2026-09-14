from pathlib import Path
import re, sys

if len(sys.argv) != 4:
    raise SystemExit('usage: build_rc54_probe_sources.py RC52_CONTEXT_CPP OUT_CONTEXT_CPP OUT_SWAPCHAIN_CPP')

ctx_src = Path(sys.argv[1])
out_ctx = Path(sys.argv[2])
out_swap = Path(sys.argv[3])

ctx = ctx_src.read_text(encoding='utf-8')
swap = Path('lab/raster/rc41/ptar_rc41_swapchain_hooks.cpp').read_text(encoding='utf-8')

probe_helper = r'''
static volatile LONG g_rc54ProbeLines=0;
static void rc54_probe_line(const char* text) noexcept {
    if(!text) return;
    LONG n=InterlockedIncrement(&g_rc54ProbeLines);
    if(n>4096) return;
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&rc54_probe_line),&self) || !self) return;
    wchar_t path[MAX_PATH]{};
    if(!GetModuleFileNameW(self,path,MAX_PATH)) return;
    wchar_t* slash=wcsrchr(path,L'\\'); if(!slash) return;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ptar_rc54_gui_probe.log");
    HANDLE h=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{};GetLocalTime(&st);
    char line[896]{};
    int len=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,text);
    DWORD wr=0;WriteFile(h,line,(DWORD)len,&wr,nullptr);CloseHandle(h);
}
static uintptr_t rc54_game_rva(const void* p) noexcept {
    HMODULE game=GetModuleHandleW(nullptr);
    if(!game||!p) return 0;
    BYTE* base=reinterpret_cast<BYTE*>(game);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return 0;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return 0;
    uintptr_t v=reinterpret_cast<uintptr_t>(p),b=reinterpret_cast<uintptr_t>(base);
    if(v<b||v>=b+nt->OptionalHeader.SizeOfImage)return 0;
    return v-b;
}
'''

# Context instrumentation: behavior remains RC52-identical; only bounded traces are added.
if '#include <intrin.h>' not in ctx:
    ctx = ctx.replace('#include <cstring>\n', '#include <cstring>\n#include <intrin.h>\n', 1)
ctx = ctx.replace('namespace ptar_rc41 {\n', 'namespace ptar_rc41 {\n\n#pragma intrinsic(_ReturnAddress)\n' + probe_helper + '\n', 1)

vp_head = '''void STDMETHODCALLTYPE ContextHooks::hook_viewports(ID3D11DeviceContext* ctx,UINT count,const D3D11_VIEWPORT* viewports){\n    ContextHooks* self=owner_now();'''
vp_new = '''void STDMETHODCALLTYPE ContextHooks::hook_viewports(ID3D11DeviceContext* ctx,UINT count,const D3D11_VIEWPORT* viewports){\n    const void* rc54ra=_ReturnAddress();\n    ContextHooks* self=owner_now();'''
if ctx.count(vp_head)!=1: raise SystemExit('viewport head drift')
ctx=ctx.replace(vp_head,vp_new,1)

# In unbound pass-through path, trace the exact request.
unbound = '''        self->origViewports_(ctx,count,viewports);return;\n    }\n    if(viewports || count==0) self->cache_viewports(count,viewports,false);'''
unbound_new = '''        if(viewports && count){char q[512]{};wsprintfA(q,"RC54_VP rva=0x%llX bound=0 req=%.3fx%.3f out=%.3fx%.3f count=%u",(unsigned long long)rc54_game_rva(rc54ra),viewports[0].Width,viewports[0].Height,viewports[0].Width,viewports[0].Height,count);rc54_probe_line(q);}\n        self->origViewports_(ctx,count,viewports);return;\n    }\n    if(viewports || count==0) self->cache_viewports(count,viewports,false);'''
if ctx.count(unbound)!=1: raise SystemExit('unbound viewport drift')
ctx=ctx.replace(unbound,unbound_new,1)

mapped_tail = '''    self->add_counter(self->viewportMapped_);\n    self->origViewports_(ctx,count,mapped);\n}'''
mapped_tail_new = '''    self->add_counter(self->viewportMapped_);\n    {char q[512]{};wsprintfA(q,"RC54_VP rva=0x%llX bound=1 req=%.3fx%.3f out=%.3fx%.3f count=%u",(unsigned long long)rc54_game_rva(rc54ra),viewports[0].Width,viewports[0].Height,mapped[0].Width,mapped[0].Height,count);rc54_probe_line(q);}\n    self->origViewports_(ctx,count,mapped);\n}'''
if ctx.count(mapped_tail)!=1: raise SystemExit('mapped viewport drift')
ctx=ctx.replace(mapped_tail,mapped_tail_new,1)

sc_head = '''void STDMETHODCALLTYPE ContextHooks::hook_scissors(ID3D11DeviceContext* ctx,UINT count,const D3D11_RECT* rects){\n    ContextHooks* self=owner_now();'''
sc_new = '''void STDMETHODCALLTYPE ContextHooks::hook_scissors(ID3D11DeviceContext* ctx,UINT count,const D3D11_RECT* rects){\n    const void* rc54ra=_ReturnAddress();\n    ContextHooks* self=owner_now();'''
if ctx.count(sc_head)!=1: raise SystemExit('scissor head drift')
ctx=ctx.replace(sc_head,sc_new,1)

sc_unbound = '''        self->origScissors_(ctx,count,rects);return;\n    }\n    if(rects || count==0) self->cache_scissors(count,rects,false);'''
sc_unbound_new = '''        if(rects && count){char q[512]{};wsprintfA(q,"RC54_SC rva=0x%llX bound=0 req=%ld,%ld,%ld,%ld out=%ld,%ld,%ld,%ld count=%u",(unsigned long long)rc54_game_rva(rc54ra),rects[0].left,rects[0].top,rects[0].right,rects[0].bottom,rects[0].left,rects[0].top,rects[0].right,rects[0].bottom,count);rc54_probe_line(q);}\n        self->origScissors_(ctx,count,rects);return;\n    }\n    if(rects || count==0) self->cache_scissors(count,rects,false);'''
if ctx.count(sc_unbound)!=1: raise SystemExit('unbound scissor drift')
ctx=ctx.replace(sc_unbound,sc_unbound_new,1)

sc_tail = '''    self->add_counter(self->scissorMapped_);\n    self->origScissors_(ctx,count,mapped);\n}'''
sc_tail_new = '''    self->add_counter(self->scissorMapped_);\n    {char q[512]{};wsprintfA(q,"RC54_SC rva=0x%llX bound=1 req=%ld,%ld,%ld,%ld out=%ld,%ld,%ld,%ld count=%u",(unsigned long long)rc54_game_rva(rc54ra),rects[0].left,rects[0].top,rects[0].right,rects[0].bottom,mapped[0].left,mapped[0].top,mapped[0].right,mapped[0].bottom,count);rc54_probe_line(q);}\n    self->origScissors_(ctx,count,mapped);\n}'''
if ctx.count(sc_tail)!=1: raise SystemExit('mapped scissor drift')
ctx=ctx.replace(sc_tail,sc_tail_new,1)

out_ctx.parent.mkdir(parents=True,exist_ok=True)
out_ctx.write_text(ctx,encoding='utf-8',newline='\n')

# Swapchain instrumentation. No returned values or mapping decisions are changed.
swap = swap.replace('static SwapchainHooks* volatile g_swapOwner=nullptr;\n', 'static SwapchainHooks* volatile g_swapOwner=nullptr;\n' + probe_helper + '\n', 1)

old='''    const CallerDomain domain=self->caller_domain(_ReturnAddress());\n    const HRESULT hr=original(sc,desc);\n    self->add(self->getDescCalls_);\n    if(SUCCEEDED(hr)&&desc&&self->policy_.should_virtualize(domain)){\n        self->policy_.virtualize_desc(*desc,domain);\n        self->add(self->getDescVirtualized_);\n    }\n    return hr;'''
new='''    const void* rc54ra=_ReturnAddress();\n    const CallerDomain domain=self->caller_domain(rc54ra);\n    const HRESULT hr=original(sc,desc);\n    self->add(self->getDescCalls_);\n    UINT rawW=0,rawH=0;if(SUCCEEDED(hr)&&desc){rawW=desc->BufferDesc.Width;rawH=desc->BufferDesc.Height;}\n    if(SUCCEEDED(hr)&&desc&&self->policy_.should_virtualize(domain)){\n        self->policy_.virtualize_desc(*desc,domain);\n        self->add(self->getDescVirtualized_);\n    }\n    if(domain==CallerDomain::Game && SUCCEEDED(hr)&&desc){char q[512]{};wsprintfA(q,"RC54_DESC api=GetDesc rva=0x%llX raw=%ux%u returned=%ux%u",(unsigned long long)rc54_game_rva(rc54ra),rawW,rawH,desc->BufferDesc.Width,desc->BufferDesc.Height);rc54_probe_line(q);}\n    return hr;'''
if swap.count(old)!=1: raise SystemExit('GetDesc body drift')
swap=swap.replace(old,new,1)

old='''    const CallerDomain domain=self->caller_domain(_ReturnAddress());\n    const HRESULT hr=self->origGetDesc1_(sc,desc);\n    self->add(self->getDesc1Calls_);\n    if(SUCCEEDED(hr)&&desc&&self->policy_.should_virtualize(domain)){\n        self->policy_.virtualize_desc1(*desc,domain);\n        self->add(self->getDesc1Virtualized_);\n    }\n    return hr;'''
new='''    const void* rc54ra=_ReturnAddress();\n    const CallerDomain domain=self->caller_domain(rc54ra);\n    const HRESULT hr=self->origGetDesc1_(sc,desc);\n    self->add(self->getDesc1Calls_);\n    UINT rawW=0,rawH=0;if(SUCCEEDED(hr)&&desc){rawW=desc->Width;rawH=desc->Height;}\n    if(SUCCEEDED(hr)&&desc&&self->policy_.should_virtualize(domain)){\n        self->policy_.virtualize_desc1(*desc,domain);\n        self->add(self->getDesc1Virtualized_);\n    }\n    if(domain==CallerDomain::Game && SUCCEEDED(hr)&&desc){char q[512]{};wsprintfA(q,"RC54_DESC api=GetDesc1 rva=0x%llX raw=%ux%u returned=%ux%u",(unsigned long long)rc54_game_rva(rc54ra),rawW,rawH,desc->Width,desc->Height);rc54_probe_line(q);}\n    return hr;'''
if swap.count(old)!=1: raise SystemExit('GetDesc1 body drift')
swap=swap.replace(old,new,1)

old='''    const CallerDomain domain=self->caller_domain(_ReturnAddress());\n    const ResizeDecision d=self->policy_.map_resize(width,height,domain);'''
new='''    const void* rc54ra=_ReturnAddress();\n    const CallerDomain domain=self->caller_domain(rc54ra);\n    const ResizeDecision d=self->policy_.map_resize(width,height,domain);\n    if(domain==CallerDomain::Game){char q[512]{};wsprintfA(q,"RC54_RESIZE rva=0x%llX req=%ux%u mapped=%ux%u remap=%u",(unsigned long long)rc54_game_rva(rc54ra),width,height,d.width,d.height,d.remapped?1u:0u);rc54_probe_line(q);}'''
if swap.count(old)!=1: raise SystemExit('Resize body drift')
swap=swap.replace(old,new,1)

out_swap.parent.mkdir(parents=True,exist_ok=True)
out_swap.write_text(swap,encoding='utf-8',newline='\n')

print(f'RC54_CONTEXT={out_ctx}')
print(f'RC54_SWAPCHAIN={out_swap}')
print('RC54_POLICY=diagnostic-only; RC52 raster and logical/physical behavior unchanged')
