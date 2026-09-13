#include "ptar_rc41_target_classifier.h"

namespace ptar_rc41 {

TargetClassifier::TargetClassifier() noexcept : lock_(SRWLOCK_INIT), primaryIdentity_(nullptr) {}

TargetClassifier::~TargetClassifier(){ clear_primary(); }

void TargetClassifier::clear_primary() noexcept {
    IUnknown* old=nullptr;
    AcquireSRWLockExclusive(&lock_);
    old=primaryIdentity_;
    primaryIdentity_=nullptr;
    ReleaseSRWLockExclusive(&lock_);
    if(old) old->Release();
}

bool TargetClassifier::set_primary_resource(ID3D11Resource* resource) noexcept {
    IUnknown* next=nullptr;
    if(resource){
        if(FAILED(resource->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&next))) || !next) return false;
    }
    IUnknown* old=nullptr;
    AcquireSRWLockExclusive(&lock_);
    old=primaryIdentity_;
    primaryIdentity_=next;
    ReleaseSRWLockExclusive(&lock_);
    if(old) old->Release();
    return resource ? next!=nullptr : true;
}

bool TargetClassifier::is_primary(ID3D11RenderTargetView* rtv) const noexcept {
    if(!rtv) return false;
    ID3D11Resource* resource=nullptr;
    rtv->GetResource(&resource);
    if(!resource) return false;
    IUnknown* id=nullptr;
    const HRESULT hr=resource->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&id));
    resource->Release();
    if(FAILED(hr)||!id) return false;

    bool same=false;
    AcquireSRWLockShared(&lock_);
    same=(primaryIdentity_ && id==primaryIdentity_);
    ReleaseSRWLockShared(&lock_);
    id->Release();
    return same;
}

bool TargetClassifier::any_primary(UINT count, ID3D11RenderTargetView* const* rtvs) const noexcept {
    if(!count || !rtvs || count==D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) return false;
    for(UINT i=0;i<count;++i){ if(is_primary(rtvs[i])) return true; }
    return false;
}

} // namespace ptar_rc41
