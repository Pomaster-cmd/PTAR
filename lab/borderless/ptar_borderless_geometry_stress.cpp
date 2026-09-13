#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include "include/ptar_borderless_geometry.h"

using ptar_lab::Geometry;

static unsigned edge_error(LONG a,LONG b){return unsigned(a>b?a-b:b-a);}

int main(){
    std::mt19937 rng(0x50544152u);
    const std::vector<POINT> renders={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900},{1920,1080}};
    const std::vector<RECT> outputs={
        {0,0,1024,768},{0,0,1366,768},{0,0,1600,900},{0,0,1920,1080},{0,0,2560,1440},
        {-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0},{-3440,100,-1520,1180}
    };

    unsigned long long point_cases=0,clip_cases=0,letterbox_cases=0;
    unsigned point_fail=0,clip_fail=0,max_point_error=0,max_clip_error=0;

    for(unsigned i=0;i<1500000;++i){
        const POINT rs=renders[rng()%renders.size()];
        const RECT out=outputs[rng()%outputs.size()];
        Geometry g{out,UINT(rs.x),UINT(rs.y)};
        const RECT fit=g.aspect_fit();
        if((fit.right-fit.left)!=(out.right-out.left) || (fit.bottom-fit.top)!=(out.bottom-out.top)) ++letterbox_cases;

        POINT lp{LONG(rng()%g.render_w),LONG(rng()%g.render_h)};
        POINT pp=g.logical_to_physical(lp);
        POINT back=g.physical_to_logical(pp);
        unsigned e=(std::max)(edge_error(lp.x,back.x),edge_error(lp.y,back.y));
        max_point_error=(std::max)(max_point_error,e);
        if(e>1) ++point_fail;
        ++point_cases;

        LONG l=LONG(rng()%(g.render_w+1)),r=LONG(rng()%(g.render_w+1));if(l>r)std::swap(l,r);
        LONG t=LONG(rng()%(g.render_h+1)),b=LONG(rng()%(g.render_h+1));if(t>b)std::swap(t,b);
        RECT vr{out.left+l,out.top+t,out.left+r,out.top+b};
        RECT pr=g.logical_screen_clip_to_physical(vr);
        RECT vb=g.physical_clip_to_logical_screen(pr);
        unsigned ce=(std::max)({edge_error(vr.left,vb.left),edge_error(vr.top,vb.top),edge_error(vr.right,vb.right),edge_error(vr.bottom,vb.bottom)});
        max_clip_error=(std::max)(max_clip_error,ce);
        if(ce>1) ++clip_fail;
        ++clip_cases;
    }

    if(point_fail||clip_fail){
        std::printf("FAIL point_fail=%u clip_fail=%u max_point_error=%u max_clip_error=%u\n",point_fail,clip_fail,max_point_error,max_clip_error);
        return 2;
    }
    std::printf("PASS PTAR_BORDERLESS_GEOMETRY_STRESS\n");
    std::printf("point_cases=%llu clip_cases=%llu letterbox_cases=%llu max_point_error=%u max_clip_error=%u\n",point_cases,clip_cases,letterbox_cases,max_point_error,max_clip_error);
    std::printf("negative_monitor_origins=covered mixed_aspect_ratios=covered\n");
    return 0;
}
