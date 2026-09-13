#include "ptar_rc41_swapchain_hooks.h"
#include <cstring>
#include <intrin.h>

namespace ptar_rc41 {

#pragma intrinsic(_ReturnAddress)
static SwapchainHooks* volatile g_swapOwner=nullptr;

static SwapchainHooks* swap_owner() noexcept {
    return reinterpret_cast<SwapchainHooks*>(InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_swapOwner),nullptr,nullptr));
}

SwapchainHooks::SwapchainHooks() noexcept = default;
SwapchainHooks::~SwapchainHooks(){ uninstall(); }

void SwapchainHooks::add(volatile LONG64& counter) noexcept { InterlockedIncrement64(&counter); }

bool SwapchainHooks::configure(const Contract& contract,HMODULE gameModule,HMODULE runtimeModule,HMODULE sidecarModule,
                               ResizeSuccessFn onResize,void* onResizeUser) noexcept {
    if(InterlockedCompareExchange(&installed_,0,0) || !contract.valid() || !gameModule || !runtimeModule || !sidecarModule) return false;
    contract_=contract;
    policy_=SwapchainPolicy(contract);
    gameModule_=gameModule;
    runtimeModule_=runtimeModule;
    sidecarModule_=sidecarModule;
    onResize_=onResize;
    onResizeUser_=onResizeUser;
    return true;
}

bool SwapchainHooks::install(IDXGISwapChain* swapchain) noexcept {
    if(!swapchain || !policy_.valid() || InterlockedCompareExchange(&installed_,0,0)) return false;
    if(InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_swapOwner),this,nullptr)!=nullptr) return false;

    IDXGISwapChain1* sc1=nullptr;
    if(FAILED(swapchain->QueryInterface(__uuidof(IDXGISwapChain1),reinterpret_cast<void**>(&sc1))) || !sc1){
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_swapOwner),nullptr,this);
        return false;
    }
    swapchain->AddRef();
    swapchain_=swapchain;
    swapchain1_=sc1;

    void*** objectVtable=reinterpret_cast<void***>(sc1);
    originalVtable_=*objectVtable;
    if(!originalVtable_){ uninstall(); return false; }
    std::memcpy(shadowVtable_,originalVtable_,sizeof(shadowVtable_));
    origGetDesc_=reinterpret_cast<FnGetDesc>(originalVtable_[kSlotGetDesc]);
    origResizeBuffers_=reinterpret_cast<FnResizeBuffers>(originalVtable_[kSlotResizeBuffers]);
    origGetDesc1_=reinterpret_cast<FnGetDesc1>(originalVtable_[kSlotGetDesc1]);
    if(!origGetDesc_||!origResizeBuffers_||!origGetDesc1_){ uninstall(); return false; }

    shadowVtable_[kSlotGetDesc]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc);
    shadowVtable_[kSlotResizeBuffers]=reinterpret_cast<void*>(&SwapchainHooks::hook_resize_buffers);
    shadowVtable_[kSlotGetDesc1]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc1);

    void* prior=InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(sc1),shadowVtable_,originalVtable_);
    if(prior!=originalVtable_){ uninstall(); return false; }
    InterlockedExchange(&installed_,1);
    return true;
}

void SwapchainHooks::uninstall() noexcept {
    InterlockedExchange(&installed_,0);
    if(swapchain1_ && originalVtable_){
        InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(swapchain1_),originalVtable_,shadowVtable_);
    }
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_swapOwner),nullptr,this);
    originalVtable_=nullptr;
    origGetDesc_=nullptr;origResizeBuffers_=nullptr;origGetDesc1_=nullptr;
    if(swapchain1_){ swapchain1_->Release(); swapchain1_=nullptr; }
    if(swapchain_){ swapchain_->Release(); swapchain_=nullptr; }
}

bool SwapchainHooks::installed() const noexcept {
    return InterlockedCompareExchange(const_cast<volatile LONG*>(&installed_),0,0)!=0;
}

CallerDomain SwapchainHooks::caller_domain(const void* returnAddress) const noexcept {
    return classify_caller_module(returnAddress,gameModule_,runtimeModule_,sidecarModule_);
}

HRESULT SwapchainHooks::physical_desc(DXGI_SWAP_CHAIN_DESC* desc) const noexcept {
    if(!desc||!swapchain_||!origGetDesc_) return E_POINTER;
    return origGetDesc_(swapchain_,desc);
}
HRESULT SwapchainHooks::physical_desc1(DXGI_SWAP_CHAIN_DESC1* desc) const noexcept {
    if(!desc||!swapchain1_||!origGetDesc1_) return E_POINTER;
    return origGetDesc1_(swapchain1_,desc);
}

SwapchainHookStats SwapchainHooks::stats() const noexcept {
    SwapchainHookStats s{};
    s.getDescCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&getDescCalls_),0,0);
    s.getDescVirtualized=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&getDescVirtualized_),0,0);
    s.getDesc1Calls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&getDesc1Calls_),0,0);
    s.getDesc1Virtualized=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&getDesc1Virtualized_),0,0);
    s.resizeCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&resizeCalls_),0,0);
    s.resizeRemapped=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&resizeRemapped_),0,0);
    s.resizeSucceeded=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&resizeSucceeded_),0,0);
    return s;
}

HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc(IDXGISwapChain* sc,DXGI_SWAP_CHAIN_DESC* desc){
    SwapchainHooks* self=swap_owner();
    if(!self||!self->origGetDesc_) return E_FAIL;
    const CallerDomain domain=self->caller_domain(_ReturnAddress());
    const HRESULT hr=self->origGetDesc_(sc,desc);
    self->add(self->getDescCalls_);
    if(SUCCEEDED(hr)&&desc&&self->policy_.should_virtualize(domain)){
        self->policy_.virtualize_desc(*desc,domain);
        self->add(self->getDescVirtualized_);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc1(IDXGISwapChain1* sc,DXGI_SWAP_CHAIN_DESC1* desc){
    SwapchainHooks* self=swap_owner();
    if(!self||!self->origGetDesc1_) return E_FAIL;
    const CallerDomain domain=self->caller_domain(_ReturnAddress());
    const HRESULT hr=self->origGetDesc1_(sc,desc);
    self->add(self->getDesc1Calls_);
    if(SUCCEEDED(hr)&&desc&&self->policy_.should_virtualize(domain)){
        self->policy_.virtualize_desc1(*desc,domain);
        self->add(self->getDesc1Virtualized_);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_resize_buffers(IDXGISwapChain* sc,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags){
    SwapchainHooks* self=swap_owner();
    if(!self||!self->origResizeBuffers_) return E_FAIL;
    const CallerDomain domain=self->caller_domain(_ReturnAddress());
    const ResizeDecision d=self->policy_.map_resize(width,height,domain);
    self->add(self->resizeCalls_);
    if(d.remapped) self->add(self->resizeRemapped_);
    const HRESULT hr=self->origResizeBuffers_(sc,count,d.width,d.height,format,flags);
    if(SUCCEEDED(hr)){
        self->add(self->resizeSucceeded_);
        if(self->onResize_) self->onResize_(self->onResizeUser_,sc);
    }
    return hr;
}

} // namespace ptar_rc41
