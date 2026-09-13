#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace ptar_lab {

struct Geometry {
    RECT output{};       // physical/output coordinate space
    UINT render_w = 0;   // logical game client/backbuffer
    UINT render_h = 0;

    RECT aspect_fit() const noexcept {
        if (!render_w || !render_h || output.right <= output.left || output.bottom <= output.top) return RECT{0,0,0,0};
        const LONG ow = output.right - output.left;
        const LONG oh = output.bottom - output.top;
        const double s = (std::min)(double(ow) / double(render_w), double(oh) / double(render_h));
        const LONG w = LONG(std::floor(double(render_w) * s + 0.5));
        const LONG h = LONG(std::floor(double(render_h) * s + 0.5));
        const LONG x = output.left + (ow - w) / 2;
        const LONG y = output.top + (oh - h) / 2;
        return RECT{x,y,x+w,y+h};
    }

    POINT physical_to_logical(POINT p) const noexcept {
        const RECT r = aspect_fit();
        if (r.right <= r.left || r.bottom <= r.top || !render_w || !render_h) return POINT{0,0};
        p.x = (std::max)(r.left, (std::min)(r.right - 1, p.x));
        p.y = (std::max)(r.top,  (std::min)(r.bottom - 1, p.y));
        const double u = double(p.x - r.left) / double(r.right - r.left);
        const double v = double(p.y - r.top)  / double(r.bottom - r.top);
        LONG x = LONG(u * double(render_w));
        LONG y = LONG(v * double(render_h));
        x = (std::max)(0L, (std::min)(LONG(render_w) - 1, x));
        y = (std::max)(0L, (std::min)(LONG(render_h) - 1, y));
        return POINT{x,y};
    }

    POINT logical_to_physical(POINT p) const noexcept {
        const RECT r = aspect_fit();
        if (r.right <= r.left || r.bottom <= r.top || !render_w || !render_h) return POINT{output.left, output.top};
        p.x = (std::max)(0L, (std::min)(LONG(render_w) - 1, p.x));
        p.y = (std::max)(0L, (std::min)(LONG(render_h) - 1, p.y));
        LONG x = r.left + LONG((double(p.x) + 0.5) * double(r.right - r.left) / double(render_w));
        LONG y = r.top  + LONG((double(p.y) + 0.5) * double(r.bottom - r.top) / double(render_h));
        x = (std::max)(r.left, (std::min)(r.right - 1, x));
        y = (std::max)(r.top,  (std::min)(r.bottom - 1, y));
        return POINT{x,y};
    }

    // Game virtual screen coordinates use the output monitor origin as the
    // low-resolution game client's origin. This keeps ClientToScreen / ScreenToClient
    // semantics internally coherent while the native presenter owns the physical area.
    RECT logical_screen_clip_to_physical(RECT vr) const noexcept {
        const RECT fit = aspect_fit();
        const LONG ox = output.left, oy = output.top;
        LONG l = (std::max)(0L, (std::min)(LONG(render_w), vr.left   - ox));
        LONG t = (std::max)(0L, (std::min)(LONG(render_h), vr.top    - oy));
        LONG rr= (std::max)(0L, (std::min)(LONG(render_w), vr.right  - ox));
        LONG b = (std::max)(0L, (std::min)(LONG(render_h), vr.bottom - oy));
        const LONG fw = fit.right - fit.left, fh = fit.bottom - fit.top;
        return RECT{
            fit.left + LONG(std::floor(double(l ) * fw / render_w + 0.5)),
            fit.top  + LONG(std::floor(double(t ) * fh / render_h + 0.5)),
            fit.left + LONG(std::floor(double(rr) * fw / render_w + 0.5)),
            fit.top  + LONG(std::floor(double(b ) * fh / render_h + 0.5))
        };
    }

    RECT physical_clip_to_logical_screen(RECT pr) const noexcept {
        const RECT fit = aspect_fit();
        const LONG fw = fit.right - fit.left, fh = fit.bottom - fit.top;
        auto ix = [&](LONG x) {
            x=(std::max)(fit.left,(std::min)(fit.right,x));
            return output.left + LONG(std::floor(double(x-fit.left)*render_w/fw + 0.5));
        };
        auto iy = [&](LONG y) {
            y=(std::max)(fit.top,(std::min)(fit.bottom,y));
            return output.top + LONG(std::floor(double(y-fit.top)*render_h/fh + 0.5));
        };
        return RECT{ix(pr.left),iy(pr.top),ix(pr.right),iy(pr.bottom)};
    }
};

inline bool rect_equal(const RECT& a,const RECT& b) noexcept {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}

} // namespace ptar_lab
