#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static volatile LONG g_calls=0;
extern "C" __declspec(dllexport) int WINAPI PTAR_RC41_AutoStart(HMODULE runtime){
    if(!runtime) return -99;
    LONG n=InterlockedIncrement(&g_calls);
    return n < 3 ? -21 : 0;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){ return TRUE; }
