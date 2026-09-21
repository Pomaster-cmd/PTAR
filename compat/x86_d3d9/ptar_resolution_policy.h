#pragma once

#include <windows.h>

struct PTARResolutionPolicy
{
    bool loaded;
    bool enabled;
    bool universalSpatialPresenter;
    UINT renderW;
    UINT renderH;
    UINT outputW;
    UINT outputH;
};

static PTARResolutionPolicy g_ptarResolutionPolicy={
    false,true,true,1280u,720u,1920u,1080u
};

static void PtResolutionLoadConfig(HMODULE selfModule)
{
    if(g_ptarResolutionPolicy.loaded)
        return;

    g_ptarResolutionPolicy.loaded=true;

    wchar_t dllPath[MAX_PATH]={0};
    DWORD n=GetModuleFileNameW(selfModule,dllPath,_countof(dllPath));
    if(!n || n>=_countof(dllPath))
        return;

    wchar_t* slash=wcsrchr(dllPath,L'\\');
    if(!slash)
        return;
    *(slash+1)=0;

    wchar_t iniPath[MAX_PATH]={0};
    wcscpy_s(iniPath,dllPath);
    wcscat_s(iniPath,L"win81_nis.ini");

    g_ptarResolutionPolicy.enabled=
        GetPrivateProfileIntW(L"WIN81_NIS",L"Enabled",1,iniPath)!=0;
    g_ptarResolutionPolicy.universalSpatialPresenter=
        GetPrivateProfileIntW(
            L"WIN81_NIS",L"UniversalSpatialPresenter",1,iniPath)!=0;

    const UINT renderW=(UINT)GetPrivateProfileIntW(
        L"WIN81_NIS",L"RenderWidth",1280,iniPath);
    const UINT renderH=(UINT)GetPrivateProfileIntW(
        L"WIN81_NIS",L"RenderHeight",720,iniPath);
    const UINT outputW=(UINT)GetPrivateProfileIntW(
        L"WIN81_NIS",L"OutputWidth",1920,iniPath);
    const UINT outputH=(UINT)GetPrivateProfileIntW(
        L"WIN81_NIS",L"OutputHeight",1080,iniPath);

    if(renderW>0) g_ptarResolutionPolicy.renderW=renderW;
    if(renderH>0) g_ptarResolutionPolicy.renderH=renderH;
    if(outputW>0) g_ptarResolutionPolicy.outputW=outputW;
    if(outputH>0) g_ptarResolutionPolicy.outputH=outputH;
}

static bool PtResolutionSpatialRequested(UINT requestedW,UINT requestedH)
{
    if(!g_ptarResolutionPolicy.enabled ||
       !g_ptarResolutionPolicy.universalSpatialPresenter)
        return false;

    return requestedW==g_ptarResolutionPolicy.renderW &&
           requestedH==g_ptarResolutionPolicy.renderH &&
           g_ptarResolutionPolicy.outputW>0 &&
           g_ptarResolutionPolicy.outputH>0;
}

static UINT PtResolutionRequestedOutputW(UINT requestedW)
{
    return PtResolutionSpatialRequested(
        requestedW,g_ptarResolutionPolicy.renderH)?
            g_ptarResolutionPolicy.outputW:requestedW;
}

static bool PtResolutionExactScale15(
    UINT sourceW,UINT sourceH,UINT outputW,UINT outputH)
{
    if(!sourceW || !sourceH || !outputW || !outputH)
        return false;

    return
        ((unsigned long long)outputW*2ull==
         (unsigned long long)sourceW*3ull) &&
        ((unsigned long long)outputH*2ull==
         (unsigned long long)sourceH*3ull);
}
