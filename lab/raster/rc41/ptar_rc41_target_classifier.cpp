#include "ptar_rc41_target_classifier.h"

namespace ptar_rc41 {

TargetClassifier::TargetClassifier() noexcept : lock_(SRWLOCK_INIT), primaryIdentity_(nullptr) {}

TargetClassifier::~TargetClassifier(){ clear_primary(); }

void TargetClassifier::clear_primary() noexcept {
    AcquireSRWLockExclusive(&lock_);
    primaryIdentity_=nullptr;
    ReleaseSRWLockExclusive(&lock_);
}

bool TargetClassifier::set_primary_resource(ID3D11Resource* resource) noexcept {
    IUnknown* identity=nullptr;
    if(resource){
        if(FAILED(resource->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity))) || !identity) return false;
    }
    // Store identity value only. We intentionally do NOT retain a COM reference:
    // a persistent AddRef on the swap-chain backbuffer would make ResizeBuffers fail.
    AcquireSRWLockExclusive(&lock_);
    primaryIdentity_=identity;
    ReleaseSRWLockExclusive(&lock_);
    if(identity) identity->Release();
    return resource ? identity!=nullptr : true;
}

bool TargetClassifier::is_primary(ID3D11RenderTargetView* rtv) const noexcept {
    if(!rtv) return false;
    ID3D11Resource* resource=nullptr;
    rtv->GetResource(&resource);
    if(!resource) return false;
    IUnknown* identity=nullptr;
    const HRESULT hr=resource->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity));
    resource->Release();
    if(FAILED(hr)||!identity) return false;

    bool same=false;
    AcquireSRWLockShared(&lock_);
    same=(primaryIdentity_ && identity==primaryIdentity_);
    ReleaseSRWLockShared(&lock_);
    identity->Release();
    return same;
}

bool TargetClassifier::any_primary(UINT count, ID3D11RenderTargetView* const* rtvs) const noexcept {
    if(!count || !rtvs || count==D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) return false;
    for(UINT i=0;i<count;++i){ if(is_primary(rtvs[i])) return true; }
    return false;
}

} // namespace ptar_rc41
