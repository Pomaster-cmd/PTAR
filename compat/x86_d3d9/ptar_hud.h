#pragma once

#include <windows.h>
#include <d3d9.h>
#include "ptar_runtime_metrics.h"
#include "ptar_capture.h"

// State/input side of the GW16I HUD contract. Rendering is performed by the
// exact production-HUD shader port in ptar_gw16i_hud_ps.hlsl and
// ptar_gw16i_feedback_ps.hlsl.

static bool g_ptarHudVisible=true;
static bool g_ptarHudUseMoe=true;
static bool g_ptarHudFgEnabled=false;
static bool g_ptarHudPrevF6=false;
static bool g_ptarHudPrevF7=false;
static bool g_ptarHudPrevF8=false;
static bool g_ptarHudPrevF9=false;
static bool g_ptarHudPrevF10=false;
static bool g_ptarHudPrevF11=false;
static bool g_ptarHudPrevF12=false;
static int g_ptarHudFgProfile=2;

static LARGE_INTEGER g_ptarHudFreq={0};
static PTARRollingRate g_ptarHudRealRate={};
static LONGLONG g_ptarHudLastFpsLogQpc=0;

static int g_ptarHudFeedbackType=0;
static int g_ptarHudFeedbackA=0;
static int g_ptarHudFeedbackB=0;
static int g_ptarHudFeedbackC=0;
static LONGLONG g_ptarHudFeedbackUntilQpc=0;

static int g_ptarHudStateNotice=0;
static LONGLONG g_ptarHudStateUntilQpc=0;

static bool g_ptarHudStatusLogPending=false;
static bool g_ptarHudMarkerEnabled=true;
static bool g_ptarHudConfigLoaded=false;
static unsigned long g_ptarHudCaptureOrdinal=0;

static void PtHudInitClock()
{
    if(g_ptarHudFreq.QuadPart==0)
    {
        QueryPerformanceFrequency(&g_ptarHudFreq);
        if(g_ptarHudFreq.QuadPart<=0)
            g_ptarHudFreq.QuadPart=1;
        PtRollingRateReset(&g_ptarHudRealRate);
    }
}

static LONGLONG PtHudNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static void PtHudLoadConfig(HMODULE selfModule)
{
    if(g_ptarHudConfigLoaded)
        return;
    g_ptarHudConfigLoaded=true;

    wchar_t dllPath[MAX_PATH]={0};
    DWORD n=GetModuleFileNameW(selfModule,dllPath,MAX_PATH);
    if(!n || n>=MAX_PATH)
        return;

    wchar_t* slash=wcsrchr(dllPath,L'\\');
    if(!slash)
        return;
    *(slash+1)=0;

    wchar_t iniPath[MAX_PATH]={0};
    wcscpy_s(iniPath,dllPath);
    wcscat_s(iniPath,L"win81_nis.ini");

    g_ptarHudVisible=
        GetPrivateProfileIntW(L"WIN81_NIS",L"Overlay",1,iniPath)!=0;
    g_ptarHudMarkerEnabled=
        GetPrivateProfileIntW(L"WIN81_NIS",L"VBlankDiagnostics",1,iniPath)!=0;

    int profile=GetPrivateProfileIntW(
        L"WIN81_NIS",L"FrameGenerationQuality",2,iniPath);
    if(profile<0||profile>3) profile=2;
    g_ptarHudFgProfile=profile;
}

static void PtHudFeedback(
    int type,
    int a,
    int b,
    int c)
{
    PtHudInitClock();
    g_ptarHudFeedbackType=type;
    g_ptarHudFeedbackA=a;
    g_ptarHudFeedbackB=b;
    g_ptarHudFeedbackC=c;
    g_ptarHudFeedbackUntilQpc=
        PtHudNow()+g_ptarHudFreq.QuadPart*2;
}

static bool PtHudFeedbackActive()
{
    PtHudInitClock();
    if(g_ptarHudFeedbackType==0)
        return false;
    if(PtHudNow()>=g_ptarHudFeedbackUntilQpc)
    {
        g_ptarHudFeedbackType=0;
        return false;
    }
    return true;
}

static int PtHudFeedbackType()
{
    return PtHudFeedbackActive()?g_ptarHudFeedbackType:0;
}

static int PtHudFeedbackArgA(){return g_ptarHudFeedbackA;}
static int PtHudFeedbackArgB(){return g_ptarHudFeedbackB;}
static int PtHudFeedbackArgC(){return g_ptarHudFeedbackC;}

static void PtHudStateFeedback(int state)
{
    PtHudInitClock();
    g_ptarHudStateNotice=state;
    g_ptarHudStateUntilQpc=
        PtHudNow()+g_ptarHudFreq.QuadPart*2;
}

static int PtHudStateNotice()
{
    PtHudInitClock();
    if(PtHudNow()>=g_ptarHudStateUntilQpc)
        return 0;
    return g_ptarHudStateNotice;
}

static void PtHudRecordRealFrame()
{
    PtHudInitClock();
    PtRollingRateRecord(&g_ptarHudRealRate);
}

static bool PtHudFeedbackIsProfileMenu()
{
    return PtHudFeedbackActive() && g_ptarHudFeedbackType==8;
}

static void PtHudAdvanceProfile()
{
    if(g_ptarHudFgEnabled)
    {
        // Production GW16G QUALITYSAFE1 contract: never cross the ME tier
        // while FG is active.
        if(g_ptarHudFgProfile<=1)
            g_ptarHudFgProfile=(g_ptarHudFgProfile==0)?1:0;
        else
            g_ptarHudFgProfile=(g_ptarHudFgProfile==2)?3:2;
    }
    else
    {
        g_ptarHudFgProfile=(g_ptarHudFgProfile+1)&3;
    }
}

static void PtHudUpdateInput()
{
    const bool f6=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
    const bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
    const bool f8=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    const bool f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    const bool f10=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    const bool f11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    const bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
    const bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;

    if(f6 && !g_ptarHudPrevF6)
    {
        if(ctrl)
        {
            g_ptarHudFgEnabled=!g_ptarHudFgEnabled;
            PtFgPacerReset();

            // Exact GW16 feedback vocabulary: NV ON / NV OFF + F8 STATUS.
            PtHudFeedback(g_ptarHudFgEnabled?12:13,0,0,0);

            PtDiagLogA(
                "HOTKEY CTRL+F6 FrameGeneration=%s",
                g_ptarHudFgEnabled?"ON":"OFF");
        }
        else
        {
            g_ptarHudUseMoe=!g_ptarHudUseMoe;
            PtHudFeedback(7,g_ptarHudUseMoe?2:1,0,0);
            PtDiagLogA(
                "HOTKEY F6 ManualFilter=%s",
                g_ptarHudUseMoe?"PTAR_MOE":"BILINEAR_REF");
        }
    }

    if(f7 && !g_ptarHudPrevF7 && !ctrl)
    {
        // Benchmark backend is not yet ported to D3D9. Preserve the production
        // HUD vocabulary instead of inventing another message.
        PtHudFeedback(4,0,0,0); // F7 BUSY
        PtDiagLogA("HOTKEY F7 Benchmark requested");
    }

    if(f8 && !g_ptarHudPrevF8)
    {
        if(ctrl)
        {
            // Production quality-menu semantics: first press is show-only;
            // another press while the name is visible selects the next legal
            // profile and refreshes the notice.
            if(PtHudFeedbackIsProfileMenu())
                PtHudAdvanceProfile();

            PtHudFeedback(8,g_ptarHudFgProfile,0,0);
            PtDiagLogA(
                "HOTKEY CTRL+F8 FGProfile=%d fg=%s",
                g_ptarHudFgProfile,
                g_ptarHudFgEnabled?"ON":"OFF");
        }
        else
        {
            PtHudFeedback(5,0,0,0); // exact F8 OK feedback
            g_ptarHudStatusLogPending=true;
            PtDiagLogA("HOTKEY STATUS: USR runtime status");
        }
    }

    if(f9 && !g_ptarHudPrevF9)
    {
        if(ctrl)
        {
            PtDiagLogA("HOTKEY CTRL+F9 VideoRecord requested");
        }
        else
        {
            PtCaptureRequest();
            PtDiagLogA("HOTKEY F9 Capture requested");
        }
    }

    if(f10 && !g_ptarHudPrevF10 && !ctrl)
    {
        if(PtIsoPresenterAvailable())
        {
            const bool enable=!PtIsoPresenterIsActive();
            PtIsoPresenterSetEnabled(enable);

            // Production HudParams state encoding:
            // 1 = USR OFF / F10 ENABLE
            // 2 = USR ON  / F10 DISABLE
            PtHudStateFeedback(enable?2:1);

            PtDiagLogA(
                "HOTKEY F10 TogglePresenter=%s",
                enable?"ON":"OFF");
        }
        else
        {
            PtDiagLogA(
                "HOTKEY F10 TogglePresenter unavailable");
        }
    }

    if(f11 && !g_ptarHudPrevF11 && ctrl)
    {
        g_ptarHudVisible=!g_ptarHudVisible;
        // Production HudParams state encoding: 3=HUD ON, 4=HUD OFF.
        PtHudStateFeedback(g_ptarHudVisible?3:4);
        PtDiagLogA(
            "HOTKEY CTRL+F11 ToggleHUD=%s",
            g_ptarHudVisible?"ON":"OFF");
    }

    if(f12 && !g_ptarHudPrevF12 && !ctrl)
    {
        g_ptarHudUseMoe=!g_ptarHudUseMoe;
        PtHudFeedback(7,g_ptarHudUseMoe?2:1,0,0);
        PtDiagLogA(
            "HOTKEY F12 FilterNext=%s",
            g_ptarHudUseMoe?"PTAR_MOE":"BILINEAR_REF");
    }

    g_ptarHudPrevF6=f6;
    g_ptarHudPrevF7=f7;
    g_ptarHudPrevF8=f8;
    g_ptarHudPrevF9=f9;
    g_ptarHudPrevF10=f10;
    g_ptarHudPrevF11=f11;
    g_ptarHudPrevF12=f12;
}

static void PtHudFrameTick()
{
    PtHudUpdateInput();
    PtHudRecordRealFrame();
}

static bool PtHudUseMoe(){return g_ptarHudUseMoe;}
static bool PtHudFgEnabled(){return g_ptarHudFgEnabled;}
static bool PtHudVisible(){return g_ptarHudVisible;}
static bool PtHudMarkerEnabled(){return g_ptarHudMarkerEnabled;}
static int PtHudFgProfile(){return g_ptarHudFgProfile;}

static double PtHudRealFps()
{
    return PtRollingRateValue(&g_ptarHudRealRate);
}

static double PtHudDisplayFps(bool fgProducing)
{
    const double measured=PtFgPacerVisibleFps();
    if(measured>0.0)
        return measured;
    return fgProducing?0.0:PtHudRealFps();
}

static bool PtHudConsumeStatusLogPending()
{
    if(!g_ptarHudStatusLogPending)
        return false;
    g_ptarHudStatusLogPending=false;
    return true;
}

static void PtHudNotifyCaptureSaved()
{
    ++g_ptarHudCaptureOrdinal;
    PtHudFeedback(6,(int)(g_ptarHudCaptureOrdinal%1000ul),0,0);
}

static int PtHudFilterId(bool spatialActive,bool exactScale15)
{
    if(!spatialActive)
        return 5; // COPY 1X1
    if(!g_ptarHudUseMoe)
        return 1; // BILINEAR
    if(exactScale15)
        return 2; // PTAR X15
    return 3;     // PTAR universal D3D9 adaptation
}
