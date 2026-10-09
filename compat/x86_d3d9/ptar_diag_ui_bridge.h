#pragma once

#include <windows.h>

// Tiny fail-open bridge between the x64 diagnostic controllers (CTRL+F1/F5)
// and the D3D9 in-game HUD. The controllers atomically replace an INI file
// next to d3d9.dll. The x86 runtime polls it at a low rate and renders the
// notice in the actual D3D9 backbuffer, so fullscreen/exclusive games do not
// depend on an external WinForms window being composited above the game.

static wchar_t g_ptarDiagUiPath[MAX_PATH]={0};
static bool g_ptarDiagUiInitialized=false;
static DWORD g_ptarDiagUiSequence=0;
static int g_ptarDiagUiType=0;
static int g_ptarDiagUiA=0;
static int g_ptarDiagUiB=0;
static int g_ptarDiagUiC=0;
static ULONGLONG g_ptarDiagUiUntilMs=0;
static ULONGLONG g_ptarDiagUiNextPollMs=0;

static void PtDiagUiInit(HMODULE selfModule)
{
    if(g_ptarDiagUiInitialized)
        return;
    g_ptarDiagUiInitialized=true;

    wchar_t dllPath[MAX_PATH]={0};
    DWORD n=GetModuleFileNameW(selfModule,dllPath,MAX_PATH);
    if(!n || n>=MAX_PATH)
        return;

    wchar_t* slash=wcsrchr(dllPath,L'\\');
    if(!slash)
        return;
    *(slash+1)=0;

    wcscpy_s(g_ptarDiagUiPath,dllPath);
    wcscat_s(g_ptarDiagUiPath,L"PTAR_D3D9_DIAG_UI.ini");
}

static void PtDiagUiTick()
{
    if(!g_ptarDiagUiInitialized || !g_ptarDiagUiPath[0])
        return;

    const ULONGLONG now=GetTickCount64();
    if(now<g_ptarDiagUiNextPollMs)
        return;
    g_ptarDiagUiNextPollMs=now+50ull;

    const DWORD seq=(DWORD)GetPrivateProfileIntW(
        L"PTAR_DIAG_UI",L"Sequence",0,g_ptarDiagUiPath);
    if(seq!=0 && seq!=g_ptarDiagUiSequence)
    {
        const int type=GetPrivateProfileIntW(
            L"PTAR_DIAG_UI",L"Type",0,g_ptarDiagUiPath);
        int duration=GetPrivateProfileIntW(
            L"PTAR_DIAG_UI",L"DurationMs",3500,g_ptarDiagUiPath);
        if(duration<250) duration=250;
        if(duration>15000) duration=15000;

        g_ptarDiagUiSequence=seq;
        g_ptarDiagUiType=type;
        g_ptarDiagUiA=GetPrivateProfileIntW(
            L"PTAR_DIAG_UI",L"A",0,g_ptarDiagUiPath);
        g_ptarDiagUiB=GetPrivateProfileIntW(
            L"PTAR_DIAG_UI",L"B",0,g_ptarDiagUiPath);
        g_ptarDiagUiC=GetPrivateProfileIntW(
            L"PTAR_DIAG_UI",L"C",0,g_ptarDiagUiPath);
        g_ptarDiagUiUntilMs=now+(ULONGLONG)duration;
    }

    if(g_ptarDiagUiType!=0 && now>=g_ptarDiagUiUntilMs)
        g_ptarDiagUiType=0;
}

static bool PtDiagUiActive()
{
    PtDiagUiTick();
    return g_ptarDiagUiType!=0;
}

static int PtDiagUiType()
{
    PtDiagUiTick();
    return g_ptarDiagUiType;
}

static int PtDiagUiArgA(){return g_ptarDiagUiA;}
static int PtDiagUiArgB(){return g_ptarDiagUiB;}
static int PtDiagUiArgC(){return g_ptarDiagUiC;}
