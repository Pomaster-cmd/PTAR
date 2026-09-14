#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include "ptar_rc41_swapchain_policy.h"

namespace ptar_rc41 {

struct SwapchainHookStats {
    uint64_t getDescCalls=0;
    uint64_t getDescVirtualized=0;
    uint64_t getDesc1Calls=0;
    uint64_t getDesc1Virtualized=0;
    uint64_t resizeCalls=0;
    uint64_t resizeRemapped=0;
    uint64_t resizeSucceeded=0;
    uint64_t distinctInterfaceInstalls=0;
};

using ResizeSuccessFn = void (*)(void* user,IDXGISwapChain* swapchain) noexcept;

class SwapchainHooks {
public:
    SwapchainHooks() noexcept;
    ~SwapchainHooks();
    SwapchainHooks(const SwapchainHooks&) = delete;
    SwapchainHooks& operator=(const SwapchainHooks&) = delete;

    bool configure(const Contract& contract,HMODULE gameModule,HMODULE runtimeModule,HMODULE sidecarModule,
                   ResizeSuccessFn onResize=nullptr,void* onResizeUser=nullptr) noexcept;
    bool install(IDXGISwapChain* swapchain) noexcept;
    void uninstall() noexcept;
    bool installed() const noexcept;
    bool distinct_interfaces() const noexcept;
    SwapchainHookStats stats() const noexcept;
    HRESULT physical_desc(DXGI_SWAP_CHAIN_DESC* desc) const noexcept;
    HRESULT physical_desc1(DXGI_SWAP_CHAIN_DESC1* desc) const noexcept;

private:
    static constexpr size_t kBaseVtableSlots=18;    // IDXGISwapChain.
    static constexpr size_t kDerivedVtableSlots=29; // IDXGISwapChain1 on Win8/8.1.
    static constexpr size_t kSlotGetDesc=12;
    static constexpr size_t kSlotResizeBuffers=13;
    static constexpr size_t kSlotGetDesc1=18;

    using FnGetDesc=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,DXGI_SWAP_CHAIN_DESC*);
    using FnResizeBuffers=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
    using FnGetDesc1=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*,DXGI_SWAP_CHAIN_DESC1*);

    static HRESULT STDMETHODCALLTYPE hook_get_desc(IDXGISwapChain*,DXGI_SWAP_CHAIN_DESC*);
    static HRESULT STDMETHODCALLTYPE hook_resize_buffers(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
    static HRESULT STDMETHODCALLTYPE hook_get_desc1(IDXGISwapChain1*,DXGI_SWAP_CHAIN_DESC1*);

    CallerDomain caller_domain(const void* returnAddress) const noexcept;
    void add(volatile LONG64& counter) noexcept;
    FnGetDesc get_desc_original_for(IDXGISwapChain* sc) const noexcept;
    FnResizeBuffers resize_original_for(IDXGISwapChain* sc) const noexcept;

    Contract contract_{};
    HMODULE gameModule_=nullptr,runtimeModule_=nullptr,sidecarModule_=nullptr;
    SwapchainPolicy policy_{Contract{}};
    IDXGISwapChain* swapchain_=nullptr;
    IDXGISwapChain1* swapchain1_=nullptr;
    void** baseOriginalVtable_=nullptr;
    void** derivedOriginalVtable_=nullptr;
    void* baseShadowVtable_[kBaseVtableSlots]{};
    void* derivedShadowVtable_[kDerivedVtableSlots]{};
    FnGetDesc origGetDescBase_=nullptr;
    FnResizeBuffers origResizeBuffersBase_=nullptr;
    FnGetDesc origGetDescDerived_=nullptr;
    FnResizeBuffers origResizeBuffersDerived_=nullptr;
    FnGetDesc1 origGetDesc1_=nullptr;
    ResizeSuccessFn onResize_=nullptr;
    void* onResizeUser_=nullptr;
    volatile LONG installed_=0;
    volatile LONG distinctInterfaces_=0;
    volatile LONG64 getDescCalls_=0,getDescVirtualized_=0,getDesc1Calls_=0,getDesc1Virtualized_=0;
    volatile LONG64 resizeCalls_=0,resizeRemapped_=0,resizeSucceeded_=0,distinctInterfaceInstalls_=0;
};

} // namespace ptar_rc41
