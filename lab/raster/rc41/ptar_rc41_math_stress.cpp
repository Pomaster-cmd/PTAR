#include "ptar_rc41_contract.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>

using namespace ptar_rc41;

int main(){
    const Contract c{{1920,1080},{1280,720}};
    if(!c.valid()) return 2;
    std::mt19937_64 rng(0x52433431ULL);
    std::uniform_real_distribution<double> ux(0.0,1919.0), uy(0.0,1079.0);
    std::uniform_real_distribution<double> uw(0.25,1920.0), uh(0.25,1080.0);
    double maxErr=0.0;
    uint64_t checks=0;
    for(uint64_t i=0;i<1500000ULL;++i){
        double x=ux(rng), y=uy(rng);
        double w=std::min(uw(rng),1920.0-x), h=std::min(uh(rng),1080.0-y);
        ViewportF v{float(x),float(y),float(w),float(h),0.0f,1.0f};
        auto p=map_viewport(c,v,true);
        auto q=unmap_viewport(c,p,true);
        maxErr=std::max(maxErr,std::abs(double(q.x)-double(v.x))/1920.0);
        maxErr=std::max(maxErr,std::abs(double(q.y)-double(v.y))/1080.0);
        maxErr=std::max(maxErr,std::abs(double(q.w)-double(v.w))/1920.0);
        maxErr=std::max(maxErr,std::abs(double(q.h)-double(v.h))/1080.0);
        if(q.minDepth!=v.minDepth||q.maxDepth!=v.maxDepth) return 3;
        auto passthru=map_viewport(c,v,false);
        if(passthru.x!=v.x||passthru.y!=v.y||passthru.w!=v.w||passthru.h!=v.h) return 4;
        checks += 6;
    }
    if(maxErr>1.0e-6){ std::cerr<<"FAIL max_normalized_error="<<maxErr<<"\n"; return 5; }
    std::cout<<"RC41_MATH_STRESS=PASS cases=1500000 checks="<<checks
             <<" max_normalized_error="<<maxErr<<"\n";
    return 0;
}
