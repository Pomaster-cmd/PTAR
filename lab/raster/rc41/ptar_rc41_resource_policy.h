#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "ptar_rc41_contract.h"
#include "ptar_rc41_swapchain_policy.h"

namespace ptar_rc41 {

enum class ResourceDecisionReason : uint32_t {
    Passthrough=0,
    WrongCaller=1,
    WrongGeometry=2,
    InitialData=3,
    UnsupportedUsage=4,
    CpuAccessible=5,
    NotRenderFamily=6,
    SharedOrGdi=7,
    RemapExactLogical=8
};

struct Texture2DDecision {
    D3D11_TEXTURE2D_DESC desc{};
    bool remapped=false;
    ResourceDecisionReason reason=ResourceDecisionReason::Passthrough;
};

class ResourcePolicy {
public:
    explicit ResourcePolicy(const Contract& contract) noexcept : contract_(contract) {}

    bool valid() const noexcept { return contract_.valid(); }

    Texture2DDecision map_texture2d(const D3D11_TEXTURE2D_DESC& in,const D3D11_SUBRESOURCE_DATA* initialData,CallerDomain domain) const noexcept {
        Texture2DDecision d{};d.desc=in;
        if(!valid()) return d;
        if(domain!=CallerDomain::Game){ d.reason=ResourceDecisionReason::WrongCaller; return d; }
        if(in.Width!=contract_.logical.w || in.Height!=contract_.logical.h){ d.reason=ResourceDecisionReason::WrongGeometry; return d; }
        if(initialData){ d.reason=ResourceDecisionReason::InitialData; return d; }
        if(in.Usage!=D3D11_USAGE_DEFAULT){ d.reason=ResourceDecisionReason::UnsupportedUsage; return d; }
        if(in.CPUAccessFlags!=0){ d.reason=ResourceDecisionReason::CpuAccessible; return d; }
        if((in.BindFlags&(D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL))==0){ d.reason=ResourceDecisionReason::NotRenderFamily; return d; }
        const UINT unsafeMisc=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
        if((in.MiscFlags&unsafeMisc)!=0){ d.reason=ResourceDecisionReason::SharedOrGdi; return d; }
        d.desc.Width=contract_.physical.w;
        d.desc.Height=contract_.physical.h;
        d.remapped=true;
        d.reason=ResourceDecisionReason::RemapExactLogical;
        return d;
    }

    const Contract& contract() const noexcept { return contract_; }

private:
    Contract contract_{};
};

} // namespace ptar_rc41
