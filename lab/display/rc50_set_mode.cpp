#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>

int wmain(int argc,wchar_t** argv){
    if(argc!=3){std::fwprintf(stderr,L"usage: rc50_set_mode WIDTH HEIGHT\n");return 2;}
    const DWORD wantW=(DWORD)_wtoi(argv[1]),wantH=(DWORD)_wtoi(argv[2]);
    if(!wantW||!wantH)return 3;
    DEVMODEW chosen{};bool found=false;
    for(DWORD i=0;;++i){
        DEVMODEW dm{};dm.dmSize=sizeof(dm);
        if(!EnumDisplaySettingsW(nullptr,i,&dm))break;
        if(dm.dmPelsWidth==wantW&&dm.dmPelsHeight==wantH){chosen=dm;found=true;break;}
    }
    if(!found){std::printf("MODE_SWITCH=FAIL reason=mode_not_found want=%lux%lu\n",(unsigned long)wantW,(unsigned long)wantH);return 4;}
    LONG test=ChangeDisplaySettingsW(&chosen,CDS_TEST);
    std::printf("MODE_TEST rc=%ld want=%lux%lu hz=%lu\n",test,(unsigned long)wantW,(unsigned long)wantH,(unsigned long)chosen.dmDisplayFrequency);
    if(test!=DISP_CHANGE_SUCCESSFUL)return 5;
    LONG rc=ChangeDisplaySettingsW(&chosen,0);
    Sleep(1000);
    DEVMODEW cur{};cur.dmSize=sizeof(cur);EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&cur);
    std::printf("MODE_SWITCH rc=%ld current=%lux%lu hz=%lu\n",rc,(unsigned long)cur.dmPelsWidth,(unsigned long)cur.dmPelsHeight,(unsigned long)cur.dmDisplayFrequency);
    if(rc!=DISP_CHANGE_SUCCESSFUL||cur.dmPelsWidth!=wantW||cur.dmPelsHeight!=wantH)return 6;
    std::printf("MODE_SWITCH=PASS current=%lux%lu\n",(unsigned long)cur.dmPelsWidth,(unsigned long)cur.dmPelsHeight);
    return 0;
}
