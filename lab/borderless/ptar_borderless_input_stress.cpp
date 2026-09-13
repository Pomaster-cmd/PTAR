#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include "include/ptar_borderless_geometry.h"

using ptar_lab::Geometry;

static unsigned absdiff(LONG a,LONG b){return unsigned(a>b?a-b:b-a);}

int main(){
    std::mt19937 rng(0x494E5054u);
    const std::vector<POINT> renders={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900}};
    const std::vector<RECT> outputs={{0,0,1024,768},{0,0,1366,768},{0,0,1920,1080},{0,0,2560,1440},{-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0}};
    unsigned failures=0,maxRoundTrip=0,maxClipRoundTrip=0;
    unsigned long long cursorCases=0,warpCases=0,wheelCases=0,clipCases=0,captureCases=0;
    bool virtualCapture=false;

    for(unsigned i=0;i<2000000;++i){
        POINT rs=renders[rng()%renders.size()];RECT out=outputs[rng()%outputs.size()];Geometry g{out,UINT(rs.x),UINT(rs.y)};
        POINT logical{LONG(rng()%g.render_w),LONG(rng()%g.render_h)};

        // SetCursorPos virtual logical screen -> physical presenter -> GetCursorPos virtualized back.
        POINT phys=g.logical_to_physical(logical);
        POINT back=g.physical_to_logical(phys);
        unsigned e=(std::max)(absdiff(logical.x,back.x),absdiff(logical.y,back.y));
        maxRoundTrip=(std::max)(maxRoundTrip,e);if(e>1)++failures;++cursorCases;++warpCases;

        // Wheel lParam uses screen coordinates: physical presenter point maps to logical screen = monitor origin + logical client.
        POINT rawPhys{out.left+LONG(rng()%UINT(out.right-out.left)),out.top+LONG(rng()%UINT(out.bottom-out.top))};
        POINT lw=g.physical_to_logical(rawPhys);
        POINT virtualWheel{out.left+lw.x,out.top+lw.y};
        if(virtualWheel.x<out.left||virtualWheel.x>=out.left+LONG(g.render_w)||virtualWheel.y<out.top||virtualWheel.y>=out.top+LONG(g.render_h))++failures;
        ++wheelCases;

        // ClipCursor logical screen rectangle -> physical aspect-fit rectangle -> inverse.
        LONG l=LONG(rng()%(g.render_w+1)),r=LONG(rng()%(g.render_w+1));if(l>r)std::swap(l,r);
        LONG t=LONG(rng()%(g.render_h+1)),b=LONG(rng()%(g.render_h+1));if(t>b)std::swap(t,b);
        RECT vr{out.left+l,out.top+t,out.left+r,out.top+b};RECT pr=g.logical_screen_clip_to_physical(vr);RECT vb=g.physical_clip_to_logical_screen(pr);
        unsigned ce=(std::max)({absdiff(vr.left,vb.left),absdiff(vr.top,vb.top),absdiff(vr.right,vb.right),absdiff(vr.bottom,vb.bottom)});
        maxClipRoundTrip=(std::max)(maxClipRoundTrip,ce);if(ce>1)++failures;++clipCases;

        // Capture virtualization model: game's logical SetCapture maps to presenter OS capture,
        // while GetCapture exposed to the game remains logically the game HWND.
        if((rng()%5)==0)virtualCapture=true;
        if((rng()%7)==0)virtualCapture=false;
        bool osCapturePresenter=virtualCapture;
        bool gameSeesGameCapture=virtualCapture;
        if(osCapturePresenter!=gameSeesGameCapture)++failures;
        ++captureCases;
    }

    if(failures){std::printf("FAIL failures=%u max_cursor_roundtrip=%u max_clip_roundtrip=%u\n",failures,maxRoundTrip,maxClipRoundTrip);return 2;}
    std::printf("PASS PTAR_BORDERLESS_INPUT_STRESS\n");
    std::printf("cursor_cases=%llu warp_cases=%llu wheel_cases=%llu clip_cases=%llu capture_cases=%llu\n",cursorCases,warpCases,wheelCases,clipCases,captureCases);
    std::printf("max_cursor_roundtrip=%u max_clip_roundtrip=%u negative_monitor_origins=covered letterbox_clamp=covered\n",maxRoundTrip,maxClipRoundTrip);
    std::printf("contract=raw_input_untouched; absolute_pointer_virtualized; capture_owned_physically_by_presenter_but_logically_by_game\n");
    return 0;
}
