#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellscalingapi.h>
#include <cstdio>
#include <random>
#include <array>
#include <algorithm>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shcore.lib")

static bool same_process_native_safe(PROCESS_DPI_AWARENESS awareness,unsigned targetScale,unsigned systemScale) noexcept {
    // Windows 8.1 has process-wide DPI awareness only. Do not mutate a game's
    // awareness after startup merely to make the PTAR presenter native-sized.
    switch(awareness){
    case PROCESS_PER_MONITOR_DPI_AWARE:
        return true;
    case PROCESS_SYSTEM_DPI_AWARE:
        return targetScale==systemScale;
    case PROCESS_DPI_UNAWARE:
    default:
        return targetScale==100u;
    }
}

static unsigned dpi_to_scale(UINT dpi) noexcept {
    return unsigned((unsigned long long(dpi)*100ull+48ull)/96ull);
}

int main(){
    const std::array<unsigned,12> scales={{100,120,125,140,150,160,175,180,200,225,250,300}};
    unsigned long long matrixCases=0;
    unsigned failures=0;

    for(unsigned target:scales){
        for(unsigned system:scales){
            const bool u=same_process_native_safe(PROCESS_DPI_UNAWARE,target,system);
            const bool s=same_process_native_safe(PROCESS_SYSTEM_DPI_AWARE,target,system);
            const bool p=same_process_native_safe(PROCESS_PER_MONITOR_DPI_AWARE,target,system);
            if(u!=(target==100u))++failures;
            if(s!=(target==system))++failures;
            if(!p)++failures;
            matrixCases+=3;
        }
    }

    std::mt19937 rng(0x44504938u);
    for(unsigned i=0;i<500000;++i){
        unsigned target=scales[rng()%scales.size()];
        unsigned system=scales[rng()%scales.size()];
        PROCESS_DPI_AWARENESS a=PROCESS_DPI_AWARENESS(rng()%3u);
        bool expected=(a==PROCESS_PER_MONITOR_DPI_AWARE)||
                      (a==PROCESS_SYSTEM_DPI_AWARE&&target==system)||
                      (a==PROCESS_DPI_UNAWARE&&target==100u);
        if(same_process_native_safe(a,target,system)!=expected)++failures;
        ++matrixCases;
    }

    PROCESS_DPI_AWARENESS runtimeAwareness=PROCESS_DPI_UNAWARE;
    HRESULT awarenessHr=GetProcessDpiAwareness(nullptr,&runtimeAwareness);
    POINT origin{0,0};
    HMONITOR mon=MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY);
    DEVICE_SCALE_FACTOR monScale=SCALE_100_PERCENT;
    HRESULT scaleHr=mon?GetScaleFactorForMonitor(mon,&monScale):E_FAIL;
    HDC dc=GetDC(nullptr);
    UINT systemDpi=dc?UINT(GetDeviceCaps(dc,LOGPIXELSX)):96u;
    if(dc)ReleaseDC(nullptr,dc);
    unsigned systemScale=dpi_to_scale(systemDpi);
    bool runtimeSafe=SUCCEEDED(awarenessHr)&&SUCCEEDED(scaleHr)&&same_process_native_safe(runtimeAwareness,unsigned(monScale),systemScale);

    // Compile/runtime presence of physical cursor APIs is part of the Win8.1 contract.
    POINT physicalCursor{};
    BOOL physicalCursorReadable=GetPhysicalCursorPos(&physicalCursor);

    if(failures){
        std::printf("FAIL policy_failures=%u matrix_cases=%llu\n",failures,matrixCases);
        return 20;
    }
    std::printf("PASS PTAR_BORDERLESS_DPI_POLICY\n");
    std::printf("matrix_cases=%llu policy_failures=0\n",matrixCases);
    std::printf("runtime_awareness_hr=0x%08lx awareness=%d monitor_scale=%u system_dpi=%u system_scale=%u same_process_native_safe=%s\n",
        (unsigned long)awarenessHr,int(runtimeAwareness),unsigned(monScale),systemDpi,systemScale,runtimeSafe?"YES":"NO");
    std::printf("GetPhysicalCursorPos=%s physical_cursor=%ld,%ld\n",physicalCursorReadable?"AVAILABLE":"UNAVAILABLE",physicalCursor.x,physicalCursor.y);
    std::printf("contract=never_change_game_DPI_awareness; Win8.1 same-process presenter only when native-pixel mapping is safe; otherwise fail-open before takeover\n");
    return 0;
}
