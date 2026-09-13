#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <cstdint>
#include "ptar_rc41_contract.h"

namespace ptar_rc41 {

enum class CallerDomain : uint32_t {
    Unknown=0,
    Game=1,
    Runtime=2,
    Sidecar=3
};

struct ResizeDecision {
    UINT width=0;
    UINT height=0;
    bool remapped=false;
};

class SwapchainPolicy {
public:
    explicit SwapchainPolicy(const Contract& contract) noexcept : contract_(contract) {}

    bool valid() const noexcept { return contract_.valid(); }
    bool should_virtualize(CallerDomain domain) const noexcept {
        return valid() && domain==CallerDomain::Game;
    }

    void virtualize_desc(DXGI_SWAP_CHAIN_DESC& desc,CallerDomain domain) const noexcept {
        if(!should_virtualize(domain)) return;
        desc.BufferDesc.Width=contract_.logical.w;
        desc.BufferDesc.Height=contract_.logical.h;
    }

    void virtualize_desc1(DXGI_SWAP_CHAIN_DESC1& desc,CallerDomain domain) const noexcept {
        if(!should_virtualize(domain)) return;
        desc.Width=contract_.logical.w;
        desc.Height=contract_.logical.h;
    }

    ResizeDecision map_resize(UINT width,UINT height,CallerDomain domain) const noexcept {
        ResizeDecision d{width,height,false};
        if(!should_virtualize(domain)) return d;
        // Exact x1.5 contract only. A game-side native logical request or DXGI 0x0
        // window-derived request must not turn the physical render swapchain native.
        if((width==contract_.logical.w && height==contract_.logical.h) || (width==0 && height==0)){
            d.width=contract_.physical.w;
            d.height=contract_.physical.h;
            d.remapped=true;
        }
        return d;
    }

    const Contract& contract() const noexcept { return contract_; }

private:
    Contract contract_{};
};

CallerDomain classify_caller_module(const void* returnAddress,HMODULE gameModule,HMODULE runtimeModule,HMODULE sidecarModule) noexcept;

} // namespace ptar_rc41
