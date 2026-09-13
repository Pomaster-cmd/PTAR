#include "ptar_rc41_contract.h"
#include <cmath>
#include <iostream>
using namespace ptar_rc41;

static bool near(double a,double b,double eps=1e-4){return std::abs(a-b)<=eps;}

int main(){
    const Contract c{{1920,1080},{1280,720}};
    const ViewportF logicalUi{300.0f,160.0f,520.0f,180.0f,0.0f,1.0f};
    const ViewportF rasterUi=map_viewport(c,logicalUi,true);
    const double upX=double(c.logical.w)/double(c.physical.w);
    const double upY=double(c.logical.h)/double(c.physical.h);
    const double rc41ScreenW=double(rasterUi.w)*upX;
    const double rc41ScreenH=double(rasterUi.h)*upY;
    const double rc40ScreenW=double(logicalUi.w)*upX;
    const double rc40ScreenH=double(logicalUi.h)*upY;

    if(!near(rc41ScreenW,logicalUi.w)||!near(rc41ScreenH,logicalUi.h)) return 10;
    if(!(rc40ScreenW>logicalUi.w*1.49 && rc40ScreenH>logicalUi.h*1.49)) return 11;

    const RectI logicalSc{301,161,821,341};
    const RectI rasterSc=map_scissor(c,logicalSc,true);
    const RectI recovered=unmap_scissor_cover(c,rasterSc,true);
    if(recovered.l>logicalSc.l || recovered.t>logicalSc.t || recovered.r<logicalSc.r || recovered.b<logicalSc.b) return 12;
    if(logicalSc.l-recovered.l>2 || logicalSc.t-recovered.t>2 || recovered.r-logicalSc.r>2 || recovered.b-logicalSc.b>2) return 13;

    std::cout<<"RC41_FRAMING_ORACLE=PASS logical_ui="<<logicalUi.w<<"x"<<logicalUi.h
             <<" raster_ui="<<rasterUi.w<<"x"<<rasterUi.h
             <<" final_ui="<<rc41ScreenW<<"x"<<rc41ScreenH
             <<" rc40_wrong_final="<<rc40ScreenW<<"x"<<rc40ScreenH<<"\n";
    return 0;
}
