#pragma once

// PTAR X86/D3D9 diagnostic layer.
// Intentionally limited to Win32 + statically-linked CRT functions so it can
// start logging before the real D3D9 runtime or PTAR resources are initialized.
//
// Production/field default is deliberately silent: diagnostics are enabled
// only when PTAR_D3D9_DIAGNOSTICS.ON exists beside d3d9.dll. This keeps the hot
// Present/pacing path free from per-frame file I/O while preserving the same
// crash logging layer for explicit forensic runs.

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>

static HMODULE g_ptdiagSelf=0;
static wchar_t g_ptdiagLogPath[MAX_PATH]={0};
static bool g_ptdiagEnabled=false;
static const char* volatile g_ptdiagStage="PRE_INIT";
static volatile LONG g_ptdiagStageSeq=0;
static volatile LONG g_ptdiagSeriousExceptionCount=0;
static PVOID g_ptdiagVectoredHandle=0;
static LPTOP_LEVEL_EXCEPTION_FILTER g_ptdiagPreviousUnhandled=0;

static void PtDiagRawWrite(const char* text)
{
    if(!g_ptdiagEnabled || !text || !*text || !g_ptdiagLogPath[0]) return;

    HANDLE h=CreateFileW(
        g_ptdiagLogPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        0,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        0);
    if(h==INVALID_HANDLE_VALUE) return;

    DWORD written=0;
    const DWORD len=(DWORD)strlen(text);
    if(len) WriteFile(h,text,len,&written,0);
    CloseHandle(h);
}

static void PtDiagLogA(const char* fmt,...)
{
    if(!g_ptdiagEnabled || !fmt) return;

    char body[1536]={0};
    va_list ap;
    va_start(ap,fmt);
    _vsnprintf_s(body,sizeof(body),_TRUNCATE,fmt,ap);
    va_end(ap);

    SYSTEMTIME st={};
    GetLocalTime(&st);

    char line[2048]={0};
    const char* stage=g_ptdiagStage ? g_ptdiagStage : "<null>";
    _snprintf_s(
        line,sizeof(line),_TRUNCATE,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u tid=%lu stage=%s | %s\r\n",
        (unsigned)st.wYear,(unsigned)st.wMonth,(unsigned)st.wDay,
        (unsigned)st.wHour,(unsigned)st.wMinute,(unsigned)st.wSecond,
        (unsigned)st.wMilliseconds,(unsigned long)GetCurrentThreadId(),
        stage,body);
    PtDiagRawWrite(line);
}

static void PtDiagVLogW(const wchar_t* fmt,va_list ap)
{
    if(!g_ptdiagEnabled || !fmt) return;
    wchar_t wide[1024]={0};
    _vsnwprintf_s(wide,_countof(wide),_TRUNCATE,fmt,ap);

    char utf8[3072]={0};
    const int n=WideCharToMultiByte(
        CP_UTF8,0,wide,-1,utf8,(int)sizeof(utf8),0,0);
    if(n>0) PtDiagLogA("%s",utf8);
}

static void PtDiagStage(const char* stage)
{
    if(!stage) stage="<null>";
    g_ptdiagStage=stage;
    if(!g_ptdiagEnabled) return;
    const LONG seq=InterlockedIncrement(&g_ptdiagStageSeq);
    PtDiagLogA("STAGE seq=%ld",seq);
}

static bool PtDiagIsSeriousException(DWORD code)
{
    switch(code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
            return true;
        default:
            return false;
    }
}

static void PtDiagLogException(const char* kind,EXCEPTION_POINTERS* ep)
{
    if(!g_ptdiagEnabled || !ep || !ep->ExceptionRecord) return;

    const DWORD code=ep->ExceptionRecord->ExceptionCode;
    if(!PtDiagIsSeriousException(code)) return;

    const LONG n=InterlockedIncrement(&g_ptdiagSeriousExceptionCount);
    if(n>32) return;

    void* address=ep->ExceptionRecord->ExceptionAddress;
    MEMORY_BASIC_INFORMATION mbi={};
    char module[MAX_PATH]={0};
    DWORD_PTR moduleOffset=0;

    if(address &&
       VirtualQuery(address,&mbi,sizeof(mbi))==sizeof(mbi) &&
       mbi.AllocationBase)
    {
        GetModuleFileNameA(
            (HMODULE)mbi.AllocationBase,module,(DWORD)_countof(module));
        moduleOffset=(DWORD_PTR)address-(DWORD_PTR)mbi.AllocationBase;
    }
    if(!module[0]) strcpy_s(module,"<unknown>");

    PtDiagLogA(
        "EXCEPTION kind=%s count=%ld code=0x%08lX flags=0x%08lX "
        "address=%p module=%s module_offset=0x%08lX params=%lu",
        kind?kind:"<null>",n,
        (unsigned long)code,
        (unsigned long)ep->ExceptionRecord->ExceptionFlags,
        address,module,(unsigned long)moduleOffset,
        (unsigned long)ep->ExceptionRecord->NumberParameters);

    if(code==EXCEPTION_ACCESS_VIOLATION &&
       ep->ExceptionRecord->NumberParameters>=2)
    {
        const ULONG_PTR operation=ep->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR target=ep->ExceptionRecord->ExceptionInformation[1];
        const char* op=(operation==0)?"READ":(operation==1)?"WRITE":
                       (operation==8)?"EXECUTE":"OTHER";
        PtDiagLogA(
            "ACCESS_VIOLATION operation=%s(%lu) target=0x%08lX",
            op,(unsigned long)operation,(unsigned long)target);
    }

#if defined(_M_IX86)
    if(ep->ContextRecord)
    {
        const CONTEXT* c=ep->ContextRecord;
        PtDiagLogA(
            "REGISTERS EIP=%08lX ESP=%08lX EBP=%08lX "
            "EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX "
            "ESI=%08lX EDI=%08lX EFLAGS=%08lX",
            (unsigned long)c->Eip,(unsigned long)c->Esp,
            (unsigned long)c->Ebp,(unsigned long)c->Eax,
            (unsigned long)c->Ebx,(unsigned long)c->Ecx,
            (unsigned long)c->Edx,(unsigned long)c->Esi,
            (unsigned long)c->Edi,(unsigned long)c->EFlags);
    }
#endif
}

static LONG CALLBACK PtDiagVectoredHandler(EXCEPTION_POINTERS* ep)
{
    PtDiagLogException("VEH_FIRST_CHANCE",ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI PtDiagUnhandledFilter(EXCEPTION_POINTERS* ep)
{
    PtDiagLogException("UNHANDLED",ep);
    if(g_ptdiagPreviousUnhandled &&
       g_ptdiagPreviousUnhandled!=PtDiagUnhandledFilter)
        return g_ptdiagPreviousUnhandled(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void PtDiagInit(HMODULE self)
{
    g_ptdiagSelf=self;
    g_ptdiagStage="DLL_ATTACH";
    g_ptdiagEnabled=false;
    g_ptdiagLogPath[0]=0;

    wchar_t baseDir[MAX_PATH]={0};
    if(GetModuleFileNameW(self,baseDir,(DWORD)_countof(baseDir)))
    {
        wchar_t* last=0;
        for(wchar_t* p=baseDir;*p;++p)
            if(*p==L'\\' || *p==L'/') last=p;
        if(last) *(last+1)=0;

        wchar_t flagPath[MAX_PATH]={0};
        wcscpy_s(flagPath,baseDir);
        if(wcslen(flagPath)+wcslen(L"PTAR_D3D9_DIAGNOSTICS.ON")+1<_countof(flagPath))
        {
            wcscat_s(flagPath,L"PTAR_D3D9_DIAGNOSTICS.ON");
            const DWORD attr=GetFileAttributesW(flagPath);
            g_ptdiagEnabled=(attr!=INVALID_FILE_ATTRIBUTES) &&
                            ((attr&FILE_ATTRIBUTE_DIRECTORY)==0);
        }

        if(g_ptdiagEnabled)
        {
            wchar_t logPath[MAX_PATH]={0};
            wcscpy_s(logPath,baseDir);
            if(wcslen(logPath)+wcslen(L"PTAR_X86_D3D9.log")+1<_countof(logPath))
            {
                wcscat_s(logPath,L"PTAR_X86_D3D9.log");
                wcsncpy_s(g_ptdiagLogPath,logPath,_TRUNCATE);
            }
        }
    }

    // Keep default field execution silent and free of diagnostic handlers.
    if(!g_ptdiagEnabled)
        return;

    PtDiagRawWrite(
        "\r\n============================================================\r\n"
        "PTAR X86/D3D9 CRASH DIAGNOSTICS START\r\n"
        "============================================================\r\n");

    PtDiagLogA(
        "DIAG_INIT build=PTAR_X86_D3D9_LEGACY_REPAIR_V1 "
        "pid=%lu self=%p",
        (unsigned long)GetCurrentProcessId(),self);

    g_ptdiagVectoredHandle=AddVectoredExceptionHandler(
        1,PtDiagVectoredHandler);
    g_ptdiagPreviousUnhandled=SetUnhandledExceptionFilter(
        PtDiagUnhandledFilter);

    PtDiagLogA(
        "DIAG_HANDLERS veh=%p previous_unhandled=%p",
        g_ptdiagVectoredHandle,(void*)g_ptdiagPreviousUnhandled);
}
