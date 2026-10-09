#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>

#pragma pack(push,4)
struct Event{volatile LONG sequence;DWORD markerSerial;DWORD generated;LONGLONG qpc;DWORD realPresents;DWORD generatedPresents;DWORD resyncs;DWORD lateSkips;};
struct State{DWORD magic;DWORD version;DWORD structSize;DWORD capacity;volatile LONGLONG qpcFrequency;volatile LONG writeSerial;DWORD reserved;Event events[256];};
#pragma pack(pop)

int wmain(int argc,wchar_t** argv)
{
    if(argc<4){fwprintf(stderr,L"usage: harness <d3d9.dll> <mixed|real> <ms>\n");return 2;}
    const wchar_t* dll=argv[1]; bool mixed=_wcsicmp(argv[2],L"mixed")==0; int totalMs=_wtoi(argv[3]); if(totalMs<1000)totalMs=1000;
    HMODULE h=LoadLibraryW(dll); if(!h){fwprintf(stderr,L"LoadLibrary failed %lu\n",GetLastError());return 3;}
    State* s=(State*)GetProcAddress(h,"PTAR_D3D9_DIAG_STATE"); if(!s){fprintf(stderr,"export absent\n");return 4;}
    LARGE_INTEGER f={};QueryPerformanceFrequency(&f);s->qpcFrequency=f.QuadPart;
    DWORD r=0,g=0; ULONGLONG start=GetTickCount64(); unsigned marker=0;
    while((int)(GetTickCount64()-start)<totalMs)
    {
        bool gen=mixed && ((marker&1u)==0u);
        if(gen)++g;else ++r;
        LARGE_INTEGER q={};QueryPerformanceCounter(&q);
        LONG serial=InterlockedIncrement(&s->writeSerial); unsigned slot=((unsigned)(serial-1))&255u;Event* e=&s->events[slot];
        InterlockedExchange(&e->sequence,-serial);MemoryBarrier();e->markerSerial=marker&4095u;e->generated=gen?1u:0u;e->qpc=q.QuadPart;e->realPresents=r;e->generatedPresents=g;e->resyncs=0;e->lateSkips=0;MemoryBarrier();InterlockedExchange(&e->sequence,serial);
        marker++;Sleep(16);
    }
    wprintf(L"HARNESS_DONE mixed=%d real=%lu gen=%lu serial=%ld\n",mixed?1:0,r,g,s->writeSerial);
    return 0;
}
