#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <stdint.h>

namespace ptar_bctl {

struct Geometry {
    RECT output{0,0,0,0};
    uint32_t renderW = 0;
    uint32_t renderH = 0;

    bool valid() const noexcept {
        return renderW && renderH && output.right > output.left && output.bottom > output.top;
    }

    RECT aspectFit() const noexcept {
        if (!valid()) return RECT{0,0,0,0};
        const int64_t ow = int64_t(output.right) - output.left;
        const int64_t oh = int64_t(output.bottom) - output.top;
        // Integer/rational fit: compare ow/renderW and oh/renderH without FP.
        int64_t fw=0, fh=0;
        if (ow * int64_t(renderH) <= oh * int64_t(renderW)) {
            fw = ow;
            fh = (ow * int64_t(renderH) + int64_t(renderW)/2) / int64_t(renderW);
        } else {
            fh = oh;
            fw = (oh * int64_t(renderW) + int64_t(renderH)/2) / int64_t(renderH);
        }
        const LONG x = output.left + LONG((ow-fw)/2);
        const LONG y = output.top  + LONG((oh-fh)/2);
        return RECT{x,y,x+LONG(fw),y+LONG(fh)};
    }

    POINT physicalScreenToLogicalClient(POINT p) const noexcept {
        const RECT r=aspectFit();
        const int64_t fw=int64_t(r.right)-r.left;
        const int64_t fh=int64_t(r.bottom)-r.top;
        if(fw<=0||fh<=0||!renderW||!renderH) return POINT{0,0};
        p.x=(std::max)(r.left,(std::min)(r.right-1,p.x));
        p.y=(std::max)(r.top,(std::min)(r.bottom-1,p.y));
        // Pixel-centre mapping, bounded to <=1 logical pixel on non-integer ratios.
        int64_t nx=(2*int64_t(p.x-r.left)+1)*int64_t(renderW);
        int64_t ny=(2*int64_t(p.y-r.top )+1)*int64_t(renderH);
        LONG x=LONG(nx/(2*fw));
        LONG y=LONG(ny/(2*fh));
        x=(std::max)(0L,(std::min)(LONG(renderW)-1,x));
        y=(std::max)(0L,(std::min)(LONG(renderH)-1,y));
        return POINT{x,y};
    }

    POINT logicalClientToPhysicalScreen(POINT p) const noexcept {
        const RECT r=aspectFit();
        const int64_t fw=int64_t(r.right)-r.left;
        const int64_t fh=int64_t(r.bottom)-r.top;
        if(fw<=0||fh<=0||!renderW||!renderH) return POINT{output.left,output.top};
        p.x=(std::max)(0L,(std::min)(LONG(renderW)-1,p.x));
        p.y=(std::max)(0L,(std::min)(LONG(renderH)-1,p.y));
        LONG x=r.left+LONG(((2*int64_t(p.x)+1)*fw)/(2*int64_t(renderW)));
        LONG y=r.top +LONG(((2*int64_t(p.y)+1)*fh)/(2*int64_t(renderH)));
        x=(std::max)(r.left,(std::min)(r.right-1,x));
        y=(std::max)(r.top,(std::min)(r.bottom-1,y));
        return POINT{x,y};
    }

    POINT physicalScreenToLogicalScreen(POINT p) const noexcept {
        POINT q=physicalScreenToLogicalClient(p);
        q.x+=output.left;
        q.y+=output.top;
        return q;
    }

    POINT logicalScreenToPhysicalScreen(POINT p) const noexcept {
        p.x-=output.left;
        p.y-=output.top;
        return logicalClientToPhysicalScreen(p);
    }

    RECT logicalScreenRectToPhysical(RECT q) const noexcept {
        const RECT fit=aspectFit();
        const int64_t fw=int64_t(fit.right)-fit.left;
        const int64_t fh=int64_t(fit.bottom)-fit.top;
        if(fw<=0||fh<=0||!renderW||!renderH) return RECT{0,0,0,0};
        LONG l=(std::max)(0L,(std::min)(LONG(renderW),q.left-output.left));
        LONG t=(std::max)(0L,(std::min)(LONG(renderH),q.top-output.top));
        LONG r=(std::max)(0L,(std::min)(LONG(renderW),q.right-output.left));
        LONG b=(std::max)(0L,(std::min)(LONG(renderH),q.bottom-output.top));
        return RECT{
            fit.left+LONG((int64_t(l)*fw+renderW/2)/renderW),
            fit.top +LONG((int64_t(t)*fh+renderH/2)/renderH),
            fit.left+LONG((int64_t(r)*fw+renderW/2)/renderW),
            fit.top +LONG((int64_t(b)*fh+renderH/2)/renderH)};
    }

    RECT physicalRectToLogicalScreen(RECT q) const noexcept {
        const RECT fit=aspectFit();
        const int64_t fw=int64_t(fit.right)-fit.left;
        const int64_t fh=int64_t(fit.bottom)-fit.top;
        if(fw<=0||fh<=0||!renderW||!renderH) return RECT{0,0,0,0};
        auto mx=[&](LONG x)->LONG{ x=(std::max)(fit.left,(std::min)(fit.right,x)); return output.left+LONG((int64_t(x-fit.left)*renderW+fw/2)/fw); };
        auto my=[&](LONG y)->LONG{ y=(std::max)(fit.top,(std::min)(fit.bottom,y)); return output.top +LONG((int64_t(y-fit.top )*renderH+fh/2)/fh); };
        return RECT{mx(q.left),my(q.top),mx(q.right),my(q.bottom)};
    }
};

} // namespace ptar_bctl
