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
       !g_ptarResolutionPolicy.universalSpatialPresenter ||
       !requestedW || !requestedH ||
       !g_ptarResolutionPolicy.outputW ||
       !g_ptarResolutionPolicy.outputH)
        return false;

    // Match the production USR contract: the game owns its render resolution.
    // Any render domain that fits below the configured/native output can use
    // PTAR spatial reconstruction. Exact native output is the 1:1 fast path.
    return
        requestedW<=g_ptarResolutionPolicy.outputW &&
        requestedH<=g_ptarResolutionPolicy.outputH &&
        (requestedW<g_ptarResolutionPolicy.outputW ||
         requestedH<g_ptarResolutionPolicy.outputH);
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

struct PTARPresentationRect
{
    UINT x;
    UINT y;
    UINT width;
    UINT height;
};

static PTARPresentationRect PtResolutionAspectFit(
    UINT sourceW,UINT sourceH,
    UINT outputW,UINT outputH)
{
    PTARPresentationRect r={0,0,outputW,outputH};
    if(!sourceW || !sourceH || !outputW || !outputH)
        return r;

    const unsigned long long lhs=
        (unsigned long long)outputW*(unsigned long long)sourceH;
    const unsigned long long rhs=
        (unsigned long long)outputH*(unsigned long long)sourceW;

    if(lhs<=rhs)
    {
        r.width=outputW;
        r.height=(UINT)(
            ((unsigned long long)sourceH*(unsigned long long)outputW)/
            (unsigned long long)sourceW);
    }
    else
    {
        r.height=outputH;
        r.width=(UINT)(
            ((unsigned long long)sourceW*(unsigned long long)outputH)/
            (unsigned long long)sourceH);
    }

    if(!r.width) r.width=1;
    if(!r.height) r.height=1;
    r.x=(outputW-r.width)/2u;
    r.y=(outputH-r.height)/2u;
    return r;
}

struct PTARResolutionPlan
{
    bool spatialRequested;
    UINT requestedW;
    UINT requestedH;
    UINT deviceW;
    UINT deviceH;
};

static void PtResolutionBuildPlan(
    UINT requestedW,UINT requestedH,
    PTARResolutionPlan* plan)
{
    if(!plan)
        return;

    plan->requestedW=requestedW;
    plan->requestedH=requestedH;
    plan->spatialRequested=
        PtResolutionSpatialRequested(requestedW,requestedH);

    plan->deviceW=plan->spatialRequested?
        g_ptarResolutionPolicy.outputW:requestedW;
    plan->deviceH=plan->spatialRequested?
        g_ptarResolutionPolicy.outputH:requestedH;
}

static bool PtResolutionFinalizePlan(
    const PTARResolutionPlan* plan,
    UINT resolvedDeviceW,UINT resolvedDeviceH,
    UINT* sourceW,UINT* sourceH,
    UINT* outputW,UINT* outputH)
{
    if(!plan || !sourceW || !sourceH || !outputW || !outputH ||
       !resolvedDeviceW || !resolvedDeviceH)
        return false;

    *outputW=resolvedDeviceW;
    *outputH=resolvedDeviceH;

    const bool spatialActive=
        plan->spatialRequested &&
        plan->requestedW>0 &&
        plan->requestedH>0 &&
        resolvedDeviceW==g_ptarResolutionPolicy.outputW &&
        resolvedDeviceH==g_ptarResolutionPolicy.outputH;

    if(spatialActive)
    {
        *sourceW=plan->requestedW;
        *sourceH=plan->requestedH;
    }
    else
    {
        // Native 1:1 mode: keep the backend/presenter/FG active while spatial
        // reconstruction is bypassed.
        *sourceW=resolvedDeviceW;
        *sourceH=resolvedDeviceH;
    }

    return spatialActive;
}
