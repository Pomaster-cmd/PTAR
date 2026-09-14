#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "ptar_rc41_contract.h"
#include "ptar_rc41_resource_policy.h"

namespace ptar_rc41 {

struct ResourceBridgeStats {
    uint64_t textureCalls=0;
    uint64_t textureRemapped=0;
    uint64_t textureFallbacks=0;
    uint64_t rtvCalls=0;
    uint64_t rtvTagged=0;
    uint64_t dsvCalls=0;
    uint64_t dsvTagged=0;
};

class ResourceBridge {
public:
    ResourceBridge() noexcept;
    ~ResourceBridge();
    ResourceBridge(const ResourceBridge&) = delete;
    ResourceBridge& operator=(const ResourceBridge&) = delete;

    bool configure(const Contract& contract,HMODULE gameModule,HMODULE runtimeModule,HMODULE sidecarModule) noexcept;
    bool install(ID3D11Device* device) noexcept;
    void uninstall() noexcept;
    bool installed() const noexcept;
    ResourceBridgeStats stats() const noexcept;

private:
    static constexpr size_t kVtableSlots=43;
    static constexpr size_t kSlotCreateTexture2D=5;
    static constexpr size_t kSlotCreateRenderTargetView=9;
    static constexpr size_t kSlotCreateDepthStencilView=10;

    using FnCreateTexture2D=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
    using FnCreateRenderTargetView=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11Resource*,const D3D11_RENDER_TARGET_VIEW_DESC*,ID3D11RenderTargetView**);
    using FnCreateDepthStencilView=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11Resource*,const D3D11_DEPTH_STENCIL_VIEW_DESC*,ID3D11DepthStencilView**);

    static HRESULT STDMETHODCALLTYPE hook_create_texture2d(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
    static HRESULT STDMETHODCALLTYPE hook_create_rtv(ID3D11Device*,ID3D11Resource*,const D3D11_RENDER_TARGET_VIEW_DESC*,ID3D11RenderTargetView**);
    static HRESULT STDMETHODCALLTYPE hook_create_dsv(ID3D11Device*,ID3D11Resource*,const D3D11_DEPTH_STENCIL_VIEW_DESC*,ID3D11DepthStencilView**);

    CallerDomain caller_domain(const void* returnAddress) const noexcept;
    void add(volatile LONG64& counter) noexcept;

    Contract contract_{};
    ResourcePolicy policy_{Contract{}};
    HMODULE gameModule_=nullptr,runtimeModule_=nullptr,sidecarModule_=nullptr;
    ID3D11Device* device_=nullptr;
    void** originalVtable_=nullptr;
    void* shadowVtable_[kVtableSlots]{};
    FnCreateTexture2D origCreateTexture2D_=nullptr;
    FnCreateRenderTargetView origCreateRTV_=nullptr;
    FnCreateDepthStencilView origCreateDSV_=nullptr;
    volatile LONG installed_=0;
    volatile LONG64 textureCalls_=0,textureRemapped_=0,textureFallbacks_=0;
    volatile LONG64 rtvCalls_=0,rtvTagged_=0,dsvCalls_=0,dsvTagged_=0;
};

} // namespace ptar_rc41
