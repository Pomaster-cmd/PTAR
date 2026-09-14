#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <set>
#include <utility>

int main(){
    DEVMODEW dm{};dm.dmSize=sizeof(dm);
    std::set<std::pair<DWORD,DWORD>> seen;
    unsigned total=0, exact=0;
    for(DWORD i=0;EnumDisplaySettingsW(nullptr,i,&dm);++i){
        ++total;
        const auto wh=std::make_pair(dm.dmPelsWidth,dm.dmPelsHeight);
        if(seen.insert(wh).second){
            const bool x15=(dm.dmPelsWidth%3u==0u && dm.dmPelsHeight%3u==0u);
            std::printf("MODE %lux%lu %lubpp %luHz exact_x1p5=%u render=%lux%lu\n",
                (unsigned long)dm.dmPelsWidth,(unsigned long)dm.dmPelsHeight,
                (unsigned long)dm.dmBitsPerPel,(unsigned long)dm.dmDisplayFrequency,
                x15?1u:0u,
                x15?(unsigned long)(dm.dmPelsWidth*2u/3u):0ul,
                x15?(unsigned long)(dm.dmPelsHeight*2u/3u):0ul);
            if(x15)++exact;
        }
        dm={};dm.dmSize=sizeof(dm);
    }
    DEVMODEW cur{};cur.dmSize=sizeof(cur);EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&cur);
    std::printf("CURRENT %lux%lu\n",(unsigned long)cur.dmPelsWidth,(unsigned long)cur.dmPelsHeight);
    std::printf("DISPLAY_MODE_PROBE total=%u unique=%zu exact_x1p5=%u\n",total,seen.size(),exact);
    return 0;
}
