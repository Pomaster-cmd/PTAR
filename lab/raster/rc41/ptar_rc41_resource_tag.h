#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>

namespace ptar_rc41 {

// Private-data marker carried by resources/views that belong to the logical-native
// composition family but are physically allocated at the low-resolution raster size.
// The marker is owned by the D3D11 object itself, so classification needs no persistent
// COM reference and cannot block ResizeBuffers/resource destruction.
struct ResourceFamilyTag {
    uint32_t magic=0x31344352u; // "RC41" little-endian
    uint32_t version=1;
    uint32_t logicalW=0,logicalH=0;
    uint32_t physicalW=0,physicalH=0;
};

inline const GUID& rc41_family_tag_guid() noexcept {
    static const GUID g={0x52314350u,0x5441u,0x4101u,{0x9a,0x7c,0x52,0x43,0x34,0x31,0x11,0x5e}};
    return g;
}

inline bool valid_family_tag(const ResourceFamilyTag& t) noexcept {
    return t.magic==0x31344352u && t.version==1u && t.logicalW && t.logicalH && t.physicalW && t.physicalH;
}

inline bool set_family_tag(ID3D11DeviceChild* child,const ResourceFamilyTag& tag) noexcept {
    if(!child || !valid_family_tag(tag)) return false;
    return SUCCEEDED(child->SetPrivateData(rc41_family_tag_guid(),sizeof(tag),&tag));
}

inline bool get_family_tag(ID3D11DeviceChild* child,ResourceFamilyTag* out=nullptr) noexcept {
    if(!child) return false;
    ResourceFamilyTag tag{};UINT size=sizeof(tag);
    if(FAILED(child->GetPrivateData(rc41_family_tag_guid(),&size,&tag)) || size!=sizeof(tag) || !valid_family_tag(tag)) return false;
    if(out) *out=tag;
    return true;
}

inline bool resource_has_family_tag(ID3D11Resource* resource,ResourceFamilyTag* out=nullptr) noexcept {
    return get_family_tag(resource,out);
}

inline bool view_or_resource_has_family_tag(ID3D11View* view,ResourceFamilyTag* out=nullptr) noexcept {
    if(!view) return false;
    ResourceFamilyTag tag{};
    if(get_family_tag(view,&tag)){ if(out) *out=tag; return true; }
    ID3D11Resource* resource=nullptr;view->GetResource(&resource);
    if(!resource) return false;
    const bool ok=resource_has_family_tag(resource,&tag);
    resource->Release();
    if(ok && out) *out=tag;
    return ok;
}

} // namespace ptar_rc41
