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

    void*** baseObject=reinterpret_cast<void***>(swapchain_);
    void*** derivedObject=reinterpret_cast<void***>(swapchain1_);
    baseOriginalVtable_=*baseObject;
    derivedOriginalVtable_=*derivedObject;
    if(!baseOriginalVtable_||!derivedOriginalVtable_){ uninstall(); return false; }

    const bool distinct=(reinterpret_cast<void*>(swapchain_)!=reinterpret_cast<void*>(swapchain1_));
    InterlockedExchange(&distinctInterfaces_,distinct?1:0);
    if(distinct) add(distinctInterfaceInstalls_);

    origGetDescBase_=reinterpret_cast<FnGetDesc>(baseOriginalVtable_[kSlotGetDesc]);
    origResizeBuffersBase_=reinterpret_cast<FnResizeBuffers>(baseOriginalVtable_[kSlotResizeBuffers]);
    origGetDescDerived_=reinterpret_cast<FnGetDesc>(derivedOriginalVtable_[kSlotGetDesc]);
    origResizeBuffersDerived_=reinterpret_cast<FnResizeBuffers>(derivedOriginalVtable_[kSlotResizeBuffers]);
    origGetDesc1_=reinterpret_cast<FnGetDesc1>(derivedOriginalVtable_[kSlotGetDesc1]);
    if(!origGetDescBase_||!origResizeBuffersBase_||!origGetDescDerived_||!origResizeBuffersDerived_||!origGetDesc1_){ uninstall(); return false; }

    if(!distinct){
        std::memcpy(derivedShadowVtable_,derivedOriginalVtable_,sizeof(derivedShadowVtable_));
        derivedShadowVtable_[kSlotGetDesc]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc);
        derivedShadowVtable_[kSlotResizeBuffers]=reinterpret_cast<void*>(&SwapchainHooks::hook_resize_buffers);
        derivedShadowVtable_[kSlotGetDesc1]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc1);
        void* prior=InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(swapchain1_),derivedShadowVtable_,derivedOriginalVtable_);
        if(prior!=derivedOriginalVtable_){ uninstall(); return false; }
    } else {
        std::memcpy(baseShadowVtable_,baseOriginalVtable_,sizeof(baseShadowVtable_));
        std::memcpy(derivedShadowVtable_,derivedOriginalVtable_,sizeof(derivedShadowVtable_));
        baseShadowVtable_[kSlotGetDesc]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc);
        baseShadowVtable_[kSlotResizeBuffers]=reinterpret_cast<void*>(&SwapchainHooks::hook_resize_buffers);
        derivedShadowVtable_[kSlotGetDesc]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc);
        derivedShadowVtable_[kSlotResizeBuffers]=reinterpret_cast<void*>(&SwapchainHooks::hook_resize_buffers);
        derivedShadowVtable_[kSlotGetDesc1]=reinterpret_cast<void*>(&SwapchainHooks::hook_get_desc1);

        void* priorBase=InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(swapchain_),baseShadowVtable_,baseOriginalVtable_);
        if(priorBase!=baseOriginalVtable_){ uninstall(); return false; }
        void* priorDerived=InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(swapchain1_),derivedShadowVtable_,derivedOriginalVtable_);
        if(priorDerived!=derivedOriginalVtable_){
            InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(swapchain_),baseOriginalVtable_,baseShadowVtable_);
            uninstall(); return false;
        }
    }
    InterlockedExchange(&installed_,1);
    return true;
}

void SwapchainHooks::uninstall() noexcept {
    InterlockedExchange(&installed_,0);
    const bool distinct=InterlockedCompareExchange(&distinctInterfaces_,0,0)!=0;
    if(swapchain1_ && derivedOriginalVtable_){
        InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(swapchain1_),derivedOriginalVtable_,derivedShadowVtable_);
    }
    if(distinct && swapchain_ && baseOriginalVtable_){
        InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(swapchain_),baseOriginalVtable_,baseShadowVtable_);
    }
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_swapOwner),nullptr,this);
    baseOriginalVtable_=nullptr;derivedOriginalVtable_=nullptr;
    origGetDescBase_=nullptr;origResizeBuffersBase_=nullptr;origGetDescDerived_=nullptr;origResizeBuffersDerived_=nullptr;origGetDesc1_=nullptr;
    InterlockedExchange(&distinctInterfaces_,0);
    if(swapchain1_){ swapchain1_->Release(); swapchain1_=nullptr; }
    if(swapchain_){ swapchain_->Release(); swapchain_=nullptr; }
}

bool SwapchainHooks::installed() const noexcept {
    return InterlockedCompareExchange(const_cast<volatile LONG*>(&installed_),0,0)!=0;
}
bool SwapchainHooks::distinct_interfaces() const noexcept {
    return InterlockedCompareExchange(const_cast<volatile LONG*>(&distinctInterfaces_),0,0)!=0;
}

CallerDomain SwapchainHooks::caller_domain(const void* returnAddress) const noexcept {
    return classify_caller_module(returnAddress,gameModule_,runtimeModule_,sidecarModule_);
}

SwapchainHooks::FnGetDesc SwapchainHooks::get_desc_original_for(IDXGISwapChain* sc) const noexcept {
    if(distinct_interfaces() && swapchain1_ && sc==static_cast<IDXGISwapChain*>(swapchain1_)) return origGetDescDerived_;
    return origGetDescBase_;
}
SwapchainHooks::FnResizeBuffers SwapchainHooks::resize_original_for(IDXGISwapChain* sc) const noexcept {
    if(distinct_interfaces() && swapchain1_ && sc==static_cast<IDXGISwapChain*>(swapchain1_)) return origResizeBuffersDerived_;
    return origResizeBuffersBase_;
}

HRESULT SwapchainHooks::physical_desc(DXGI_SWAP_CHAIN_DESC* desc) const noexcept {
    if(!desc||!swapchain_||!origGetDescBase_) return E_POINTER;
    return origGetDescBase_(swapchain_,desc);
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
    s.distinctInterfaceInstalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&distinctInterfaceInstalls_),0,0);
    return s;
}

HRESULT STDMETHODCALLTYPE SwapchainHooks::hook_get_desc(IDXGISwapChain* sc,DXGI_SWAP_CHAIN_DESC* desc){
    SwapchainHooks* self=swap_owner();
    if(!self) return E_FAIL;
    FnGetDesc original=self->get_desc_original_for(sc);
    if(!original)return E_FAIL;
    const CallerDomain domain=self->caller_domain(_ReturnAddress());
    const HRESULT hr=original(sc,desc);
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
    if(!self) return E_FAIL;
    FnResizeBuffers original=self->resize_original_for(sc);
    if(!original)return E_FAIL;
    const CallerDomain domain=self->caller_domain(_ReturnAddress());
    const ResizeDecision d=self->policy_.map_resize(width,height,domain);
    self->add(self->resizeCalls_);
    if(d.remapped) self->add(self->resizeRemapped_);
    const HRESULT hr=original(sc,count,d.width,d.height,format,flags);
    if(SUCCEEDED(hr)){
        self->add(self->resizeSucceeded_);
        if(self->onResize_) self->onResize_(self->onResizeUser_,sc);
    }
    return hr;
}

} // namespace ptar_rc41
