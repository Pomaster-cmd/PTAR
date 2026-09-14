#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "ptar_rc41_swapchain_hooks.h"

using namespace ptar_rc41;

struct FakeState;
struct RawIface { void** vtable; FakeState* state; bool derived; };
struct FakeState {
    LONG refs=1;
    RawIface base{},derived{};
    void* baseVtable[18]{};
    void* derivedVtable[29]{};
    UINT baseResizeW=0,baseResizeH=0,derivedResizeW=0,derivedResizeH=0;
    UINT baseDescCalls=0,derivedDescCalls=0,desc1Calls=0;
};

static RawIface* raw(void* self){return reinterpret_cast<RawIface*>(self);}
static HRESULT STDMETHODCALLTYPE fq(void* self,REFIID iid,void** out){
    if(!out)return E_POINTER;*out=nullptr;RawIface* r=raw(self);FakeState* s=r->state;
    if(IsEqualIID(iid,__uuidof(IUnknown))||IsEqualIID(iid,__uuidof(IDXGIObject))||IsEqualIID(iid,__uuidof(IDXGIDeviceSubObject))||IsEqualIID(iid,__uuidof(IDXGISwapChain))){*out=&s->base;InterlockedIncrement(&s->refs);return S_OK;}
    if(IsEqualIID(iid,__uuidof(IDXGISwapChain1))){*out=&s->derived;InterlockedIncrement(&s->refs);return S_OK;}
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE fa(void* self){return ULONG(InterlockedIncrement(&raw(self)->state->refs));}
static ULONG STDMETHODCALLTYPE fr(void* self){return ULONG(InterlockedDecrement(&raw(self)->state->refs));}
static HRESULT STDMETHODCALLTYPE base_desc(IDXGISwapChain* self,DXGI_SWAP_CHAIN_DESC* d){
    if(!d)return E_POINTER;std::memset(d,0,sizeof(*d));d->BufferDesc.Width=1280;d->BufferDesc.Height=720;++raw(self)->state->baseDescCalls;return S_OK;
}
static HRESULT STDMETHODCALLTYPE derived_desc(IDXGISwapChain* self,DXGI_SWAP_CHAIN_DESC* d){
    if(!d)return E_POINTER;std::memset(d,0,sizeof(*d));d->BufferDesc.Width=1280;d->BufferDesc.Height=720;++raw(self)->state->derivedDescCalls;return S_OK;
}
static HRESULT STDMETHODCALLTYPE base_resize(IDXGISwapChain* self,UINT,UINT w,UINT h,DXGI_FORMAT,UINT){FakeState* s=raw(self)->state;s->baseResizeW=w;s->baseResizeH=h;return S_OK;}
static HRESULT STDMETHODCALLTYPE derived_resize(IDXGISwapChain* self,UINT,UINT w,UINT h,DXGI_FORMAT,UINT){FakeState* s=raw(self)->state;s->derivedResizeW=w;s->derivedResizeH=h;return S_OK;}
static HRESULT STDMETHODCALLTYPE derived_desc1(IDXGISwapChain1* self,DXGI_SWAP_CHAIN_DESC1* d){
    if(!d)return E_POINTER;std::memset(d,0,sizeof(*d));d->Width=1280;d->Height=720;++raw(self)->state->desc1Calls;return S_OK;
}
static HRESULT STDMETHODCALLTYPE unused_hresult(){return E_NOTIMPL;}

int main(){
    FakeState s{};
    for(auto& p:s.baseVtable)p=reinterpret_cast<void*>(&unused_hresult);
    for(auto& p:s.derivedVtable)p=reinterpret_cast<void*>(&unused_hresult);
    s.base={s.baseVtable,&s,false};s.derived={s.derivedVtable,&s,true};
    s.baseVtable[0]=reinterpret_cast<void*>(&fq);s.baseVtable[1]=reinterpret_cast<void*>(&fa);s.baseVtable[2]=reinterpret_cast<void*>(&fr);
    s.baseVtable[12]=reinterpret_cast<void*>(&base_desc);s.baseVtable[13]=reinterpret_cast<void*>(&base_resize);
    s.derivedVtable[0]=reinterpret_cast<void*>(&fq);s.derivedVtable[1]=reinterpret_cast<void*>(&fa);s.derivedVtable[2]=reinterpret_cast<void*>(&fr);
    s.derivedVtable[12]=reinterpret_cast<void*>(&derived_desc);s.derivedVtable[13]=reinterpret_cast<void*>(&derived_resize);s.derivedVtable[18]=reinterpret_cast<void*>(&derived_desc1);

    auto* base=reinterpret_cast<IDXGISwapChain*>(&s.base);
    auto* derived=reinterpret_cast<IDXGISwapChain1*>(&s.derived);
    void** baseBefore=s.base.vtable;void** derivedBefore=s.derived.vtable;
    HMODULE game=GetModuleHandleW(nullptr),runtime=GetModuleHandleW(L"kernel32.dll"),sidecar=GetModuleHandleW(L"user32.dll");
    SwapchainHooks hooks;
    if(!hooks.configure(Contract{{1920,1080},{1280,720}},game,runtime,sidecar))return 10;
    if(!hooks.install(base)||!hooks.installed()||!hooks.distinct_interfaces())return 11;
    if(s.base.vtable==baseBefore||s.derived.vtable==derivedBefore)return 12;

    DXGI_SWAP_CHAIN_DESC bd{};if(FAILED(base->GetDesc(&bd))||bd.BufferDesc.Width!=1920||bd.BufferDesc.Height!=1080||s.baseDescCalls!=1)return 13;
    DXGI_SWAP_CHAIN_DESC dd{};if(FAILED(derived->GetDesc(&dd))||dd.BufferDesc.Width!=1920||dd.BufferDesc.Height!=1080||s.derivedDescCalls!=1)return 14;
    DXGI_SWAP_CHAIN_DESC1 d1{};if(FAILED(derived->GetDesc1(&d1))||d1.Width!=1920||d1.Height!=1080||s.desc1Calls!=1)return 15;
    if(FAILED(base->ResizeBuffers(2,1920,1080,DXGI_FORMAT_UNKNOWN,0))||s.baseResizeW!=1280||s.baseResizeH!=720)return 16;
    if(FAILED(derived->ResizeBuffers(2,0,0,DXGI_FORMAT_UNKNOWN,0))||s.derivedResizeW!=1280||s.derivedResizeH!=720)return 17;

    DXGI_SWAP_CHAIN_DESC physical{};if(FAILED(hooks.physical_desc(&physical))||physical.BufferDesc.Width!=1280||physical.BufferDesc.Height!=720)return 18;
    DXGI_SWAP_CHAIN_DESC1 physical1{};if(FAILED(hooks.physical_desc1(&physical1))||physical1.Width!=1280||physical1.Height!=720)return 19;
    SwapchainHookStats st=hooks.stats();if(st.getDescVirtualized!=2||st.getDesc1Virtualized!=1||st.resizeRemapped!=2||st.distinctInterfaceInstalls!=1)return 20;

    hooks.uninstall();if(hooks.installed()||s.base.vtable!=baseBefore||s.derived.vtable!=derivedBefore)return 21;
    DXGI_SWAP_CHAIN_DESC restored{};if(FAILED(base->GetDesc(&restored))||restored.BufferDesc.Width!=1280||restored.BufferDesc.Height!=720)return 22;
    std::cout<<"RC41_DUAL_INTERFACE=PASS base_ptr_distinct=1 base_getdesc=PASS derived_getdesc=PASS getdesc1=PASS resize_base=1280x720 resize_derived=1280x720 physical_bypass=PASS restore_both=PASS refs="<<s.refs<<"\n";
    return 0;
}
