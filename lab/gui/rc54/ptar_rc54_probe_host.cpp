#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "ptar_rc54_gui_callsite_probe.h"

using namespace ptar_rc54;
using namespace ptar_rc41;

static const void* probe_callsite() noexcept { return _ReturnAddress(); }

static bool read_all(const wchar_t* path,char* out,DWORD capacity) noexcept {
    if(!path||!out||capacity<2)return false;
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD got=0;const BOOL ok=ReadFile(h,out,capacity-1,&got,nullptr);CloseHandle(h);
    if(!ok)return false;out[got]=0;return true;
}

int main(){
    HMODULE self=GetModuleHandleW(nullptr);
    if(!self)return 10;
    wchar_t path[MAX_PATH]{};
    if(!GetModuleFileNameW(self,path,MAX_PATH))return 11;
    wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return 12;
    wcscpy_s(slash+1,MAX_PATH-size_t(slash+1-path),L"ptar_rc54_gui_callsites.log");
    DeleteFileW(path); // CI-owned diagnostic file only.

    GuiCallsiteProbe& probe=global_gui_callsite_probe();
    if(!probe.configure(self,self,Contract{{1920,1080},{1280,720}}))return 13;

    QueryObservation q{};
    q.kind=QueryKind::GetDesc;
    q.domain=CallerDomain::Game;
    q.returnAddress=probe_callsite();
    q.physicalW=1280;q.physicalH=720;
    q.reportedW=1920;q.reportedH=1080;
    q.virtualized=true;
    for(int i=0;i<16;++i)probe.observe(q);

    char data[16384]{};
    if(!read_all(path,data,sizeof(data)))return 14;
    if(!strstr(data,"RC54_GUI_CALLSITE_PROBE=ACTIVE"))return 15;
    if(!strstr(data,"policy=observe_only_no_behavior_change"))return 16;
    if(!strstr(data,"kind=GetDesc domain=Game"))return 17;
    if(!strstr(data,"physical=1280x720 reported=1920x1080 virtualized=1"))return 18;
    if(!strstr(data,"hits=8")||!strstr(data,"hits=16"))return 19;
    if(strstr(data,"hits=9"))return 20;
    std::puts("RC54_GUI_CALLSITE_PROBE_HOST=PASS sampling=1-8,powers-of-two behavior=observe-only");
    return 0;
}
