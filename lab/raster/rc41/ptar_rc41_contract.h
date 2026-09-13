#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ptar_rc41 {

struct SizeU { uint32_t w=0, h=0; };
struct ViewportF { float x=0, y=0, w=0, h=0, minDepth=0, maxDepth=1; };
struct RectI { int32_t l=0, t=0, r=0, b=0; };

struct Contract {
    SizeU logical{};
    SizeU physical{};

    bool valid() const noexcept {
        return logical.w && logical.h && physical.w && physical.h &&
               physical.w <= logical.w && physical.h <= logical.h;
    }
    double sx() const noexcept { return valid() ? double(physical.w)/double(logical.w) : 1.0; }
    double sy() const noexcept { return valid() ? double(physical.h)/double(logical.h) : 1.0; }
};

inline ViewportF map_viewport(const Contract& c, const ViewportF& v, bool primaryFamilyBound) noexcept {
    if (!primaryFamilyBound || !c.valid()) return v;
    const double sx=c.sx(), sy=c.sy();
    ViewportF o=v;
    o.x=float(double(v.x)*sx);
    o.y=float(double(v.y)*sy);
    o.w=float(double(v.w)*sx);
    o.h=float(double(v.h)*sy);
    return o;
}

inline RectI map_scissor(const Contract& c, const RectI& r, bool primaryFamilyBound) noexcept {
    if (!primaryFamilyBound || !c.valid()) return r;
    const double sx=c.sx(), sy=c.sy();
    RectI o{};
    // Conservative coverage: never clip a logical pixel because of fractional sub-raster edges.
    o.l=(int32_t)std::floor(double(r.l)*sx);
    o.t=(int32_t)std::floor(double(r.t)*sy);
    o.r=(int32_t)std::ceil (double(r.r)*sx);
    o.b=(int32_t)std::ceil (double(r.b)*sy);
    o.l=std::max<int32_t>(0,std::min<int32_t>(o.l,(int32_t)c.physical.w));
    o.t=std::max<int32_t>(0,std::min<int32_t>(o.t,(int32_t)c.physical.h));
    o.r=std::max<int32_t>(o.l,std::min<int32_t>(o.r,(int32_t)c.physical.w));
    o.b=std::max<int32_t>(o.t,std::min<int32_t>(o.b,(int32_t)c.physical.h));
    return o;
}

inline ViewportF unmap_viewport(const Contract& c, const ViewportF& v, bool primaryFamilyBound) noexcept {
    if (!primaryFamilyBound || !c.valid()) return v;
    const double sx=c.sx(), sy=c.sy();
    ViewportF o=v;
    o.x=float(double(v.x)/sx);
    o.y=float(double(v.y)/sy);
    o.w=float(double(v.w)/sx);
    o.h=float(double(v.h)/sy);
    return o;
}

inline RectI unmap_scissor_cover(const Contract& c, const RectI& r, bool primaryFamilyBound) noexcept {
    if (!primaryFamilyBound || !c.valid()) return r;
    const double sx=c.sx(), sy=c.sy();
    RectI o{};
    o.l=(int32_t)std::floor(double(r.l)/sx);
    o.t=(int32_t)std::floor(double(r.t)/sy);
    o.r=(int32_t)std::ceil (double(r.r)/sx);
    o.b=(int32_t)std::ceil (double(r.b)/sy);
    return o;
}

} // namespace ptar_rc41
