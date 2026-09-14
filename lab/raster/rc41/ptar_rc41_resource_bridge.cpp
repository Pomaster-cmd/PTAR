#include "ptar_rc41_resource_bridge.h"
#include "ptar_rc41_resource_tag.h"
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <d3d11_3.h>
#include <d3d11_4.h>
#include <cstring>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace ptar_rc41 {

static ResourceBridge* volatile g_resourceOwner=nullptr;

static ResourceBridge* resource_owner_now() noexcept {
    return reinterpret_cast<ResourceBridge*>(InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_resourceOwner),nullptr,nullptr));
}

template<class T>
static bool aliases_base_device(ID3D11Device* device) noexcept {
    T* p=nullptr;
    const HRESULT hr=device->QueryInterface(__uuidof(T),reinterpret_cast<void**>(&p));
    if(FAILED(hr)||!p) return false;
    const bool alias=reinterpret_cast<void*>(p)==reinterpret_cast<void*>(device);
    p->Release();
    return alias;
}

static size_t highest_aliased_device_slots(ID3D11Device* device,unsigned& version) noexcept {
    // Only extend the shadow when the newer interface aliases the base pointer.
    // If QueryInterface returns a distinct interface pointer, reading beyond the
    // 43-slot ID3D11Device vtable would itself be unsafe.
    if(aliases_base_device<ID3D11Device5>(device)){version=5;return 69;}
    if(aliases_base_device<ID3D11Device4>(device)){version=4;return 67;}
    if(aliases_base_device<ID3D11Device3>(device)){version=3;return 65;}
    if(aliases_base_device<ID3D11Device2>(device)){version=2;return 54;}
    if(aliases_base_device<ID3D11Device1>(device)){version=1;return 50;}
    version=0;return 43;
}

ResourceBridge::ResourceBridge() noexcept = default;
ResourceBridge::~ResourceBridge(){ uninstall(); }

void ResourceBridge::add(volatile LONG64& counter) noexcept { InterlockedIncrement64(&counter); }

CallerDomain ResourceBridge::caller_domain(const void* returnAddress) const noexcept {
    return classify_caller_module(returnAddress,gameModule_,runtimeModule_,sidecarModule_);
}

bool ResourceBridge::configure(const Contract& contract,HMODULE gameModule,HMODULE runtimeModule,HMODULE sidecarModule) noexcept {
    if(InterlockedCompareExchange(&installed_,0,0) || !contract.valid() || !gameModule || !runtimeModule || !sidecarModule) return false;
    contract_=contract;
    policy_=ResourcePolicy(contract);
    gameModule_=gameModule;runtimeModule_=runtimeModule;sidecarModule_=sidecarModule;
    return true;
}

bool ResourceBridge::install(ID3D11Device* device) noexcept {
    if(!device || !contract_.valid() || InterlockedCompareExchange(&installed_,0,0)) return false;

    unsigned detectedVersion=0;
    const size_t detectedSlots=highest_aliased_device_slots(device,detectedVersion);
    if(detectedSlots<kBaseVtableSlots || detectedSlots>kMaxVtableSlots) return false;

    if(InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_resourceOwner),this,nullptr)!=nullptr) return false;
    device->AddRef();device_=device;
    shadowSlots_=detectedSlots;
    deviceInterfaceVersion_=detectedVersion;

    void*** objectVtable=reinterpret_cast<void***>(device);
    originalVtable_=*objectVtable;
    if(!originalVtable_){
        device_->Release();device_=nullptr;shadowSlots_=kBaseVtableSlots;deviceInterfaceVersion_=0;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_resourceOwner),nullptr,this);
        return false;
    }
    std::memcpy(shadowVtable_,originalVtable_,shadowSlots_*sizeof(void*));
    origCreateTexture2D_=reinterpret_cast<FnCreateTexture2D>(originalVtable_[kSlotCreateTexture2D]);
    origCreateRTV_=reinterpret_cast<FnCreateRenderTargetView>(originalVtable_[kSlotCreateRenderTargetView]);
    origCreateDSV_=reinterpret_cast<FnCreateDepthStencilView>(originalVtable_[kSlotCreateDepthStencilView]);
    if(!origCreateTexture2D_||!origCreateRTV_||!origCreateDSV_){
        device_->Release();device_=nullptr;originalVtable_=nullptr;shadowSlots_=kBaseVtableSlots;deviceInterfaceVersion_=0;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_resourceOwner),nullptr,this);
        return false;
    }
    shadowVtable_[kSlotCreateTexture2D]=reinterpret_cast<void*>(&ResourceBridge::hook_create_texture2d);
    shadowVtable_[kSlotCreateRenderTargetView]=reinterpret_cast<void*>(&ResourceBridge::hook_create_rtv);
    shadowVtable_[kSlotCreateDepthStencilView]=reinterpret_cast<void*>(&ResourceBridge::hook_create_dsv);
    void* prior=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(device_),shadowVtable_,originalVtable_);
    if(prior!=originalVtable_){
        device_->Release();device_=nullptr;originalVtable_=nullptr;shadowSlots_=kBaseVtableSlots;deviceInterfaceVersion_=0;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_resourceOwner),nullptr,this);
        return false;
    }
    InterlockedExchange(&installed_,1);
    return true;
}

void ResourceBridge::uninstall() noexcept {
    if(!device_) return;
    InterlockedExchange(&installed_,0);
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(device_),originalVtable_,shadowVtable_);
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_resourceOwner),nullptr,this);
    ID3D11Device* old=device_;device_=nullptr;originalVtable_=nullptr;
    shadowSlots_=kBaseVtableSlots;deviceInterfaceVersion_=0;
    old->Release();
}

bool ResourceBridge::installed() const noexcept {
    return InterlockedCompareExchange(const_cast<volatile LONG*>(&installed_),0,0)!=0;
}

ResourceBridgeStats ResourceBridge::stats() const noexcept {
    ResourceBridgeStats s{};
    s.textureCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&textureCalls_),0,0);
    s.textureRemapped=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&textureRemapped_),0,0);
    s.textureFallbacks=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&textureFallbacks_),0,0);
    s.rtvCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&rtvCalls_),0,0);
    s.rtvTagged=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&rtvTagged_),0,0);
    s.dsvCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&dsvCalls_),0,0);
    s.dsvTagged=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&dsvTagged_),0,0);
    s.deviceInterfaceVersion=deviceInterfaceVersion_;
    s.shadowSlots=static_cast<uint32_t>(shadowSlots_);
    return s;
}

HRESULT STDMETHODCALLTYPE ResourceBridge::hook_create_texture2d(ID3D11Device* device,const D3D11_TEXTURE2D_DESC* desc,const D3D11_SUBRESOURCE_DATA* initialData,ID3D11Texture2D** out){
    ResourceBridge* self=resource_owner_now();
    if(!self || !self->origCreateTexture2D_) return E_FAIL;
    self->add(self->textureCalls_);
    if(!desc) return self->origCreateTexture2D_(device,desc,initialData,out);
    const Texture2DDecision d=self->policy_.map_texture2d(*desc,initialData,self->caller_domain(_ReturnAddress()));
    if(!d.remapped) return self->origCreateTexture2D_(device,desc,initialData,out);

    HRESULT hr=self->origCreateTexture2D_(device,&d.desc,initialData,out);
    if(SUCCEEDED(hr) && out && *out){
        ResourceFamilyTag tag{};tag.logicalW=self->contract_.logical.w;tag.logicalH=self->contract_.logical.h;
        tag.physicalW=self->contract_.physical.w;tag.physicalH=self->contract_.physical.h;
        if(set_family_tag(*out,tag)){
            self->add(self->textureRemapped_);
            return hr;
        }
        (*out)->Release();*out=nullptr;
        self->add(self->textureFallbacks_);
        return self->origCreateTexture2D_(device,desc,initialData,out);
    }
    if(FAILED(hr)){
        self->add(self->textureFallbacks_);
        return self->origCreateTexture2D_(device,desc,initialData,out);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE ResourceBridge::hook_create_rtv(ID3D11Device* device,ID3D11Resource* resource,const D3D11_RENDER_TARGET_VIEW_DESC* desc,ID3D11RenderTargetView** out){
    ResourceBridge* self=resource_owner_now();
    if(!self || !self->origCreateRTV_) return E_FAIL;
    self->add(self->rtvCalls_);
    const HRESULT hr=self->origCreateRTV_(device,resource,desc,out);
    if(SUCCEEDED(hr) && out && *out){
        ResourceFamilyTag tag{};
        if(resource_has_family_tag(resource,&tag) && set_family_tag(*out,tag)) self->add(self->rtvTagged_);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE ResourceBridge::hook_create_dsv(ID3D11Device* device,ID3D11Resource* resource,const D3D11_DEPTH_STENCIL_VIEW_DESC* desc,ID3D11DepthStencilView** out){
    ResourceBridge* self=resource_owner_now();
    if(!self || !self->origCreateDSV_) return E_FAIL;
    self->add(self->dsvCalls_);
    const HRESULT hr=self->origCreateDSV_(device,resource,desc,out);
    if(SUCCEEDED(hr) && out && *out){
        ResourceFamilyTag tag{};
        if(resource_has_family_tag(resource,&tag) && set_family_tag(*out,tag)) self->add(self->dsvTagged_);
    }
    return hr;
}

} // namespace ptar_rc41
