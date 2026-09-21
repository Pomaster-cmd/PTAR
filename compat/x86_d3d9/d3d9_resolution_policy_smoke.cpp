#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#pragma warning(disable:4505)
#include "ptar_resolution_policy.h"

static int Check(
    const char* name,
    UINT requestW,UINT requestH,
    UINT resolvedW,UINT resolvedH,
    bool expectSpatial,
    UINT expectSourceW,UINT expectSourceH,
    UINT expectOutputW,UINT expectOutputH)
{
    PTARResolutionPlan plan={};
    PtResolutionBuildPlan(requestW,requestH,&plan);

    UINT sourceW=0,sourceH=0,outputW=0,outputH=0;
    const bool spatial=PtResolutionFinalizePlan(
        &plan,resolvedW,resolvedH,
        &sourceW,&sourceH,&outputW,&outputH);

    std::printf(
        "%s request=%ux%u planned=%ux%u resolved=%ux%u source=%ux%u output=%ux%u spatial=%d\n",
        name,
        requestW,requestH,
        plan.deviceW,plan.deviceH,
        resolvedW,resolvedH,
        sourceW,sourceH,
        outputW,outputH,
        spatial?1:0);

    if(spatial!=expectSpatial ||
       sourceW!=expectSourceW || sourceH!=expectSourceH ||
       outputW!=expectOutputW || outputH!=expectOutputH)
    {
        std::printf("FAIL %s\n",name);
        return 1;
    }
    return 0;
}

int main()
{
    g_ptarResolutionPolicy.loaded=true;
    g_ptarResolutionPolicy.enabled=true;
    g_ptarResolutionPolicy.universalSpatialPresenter=true;
    g_ptarResolutionPolicy.renderW=1280;
    g_ptarResolutionPolicy.renderH=720;
    g_ptarResolutionPolicy.outputW=1920;
    g_ptarResolutionPolicy.outputH=1080;

    if(Check(
        "SPATIAL_720_TO_1080",
        1280,720,1920,1080,
        true,1280,720,1920,1080)) return 10;

    if(Check(
        "NATIVE_1080_FG_ONLY",
        1920,1080,1920,1080,
        false,1920,1080,1920,1080)) return 11;

    if(Check(
        "NATIVE_900",
        1600,900,1600,900,
        false,1600,900,1600,900)) return 12;

    if(Check(
        "NATIVE_ODD_GEOMETRY",
        1365,767,1365,767,
        false,1365,767,1365,767)) return 13;

    // D3D9 windowed mode permits zero requested dimensions; D3D resolves them
    // from the client area. PTAR must remain active after that resolution is
    // known rather than treating 0x0 as a backend-disable condition.
    if(Check(
        "WINDOWED_ZERO_REQUEST_RESOLVED_NATIVE",
        0,0,1366,768,
        false,1366,768,1366,768)) return 14;

    // If a requested spatial presentation does not resolve to the configured
    // 1.5x output, fail soft to native 1:1 rather than disabling PTAR/FG.
    if(Check(
        "SPATIAL_NEGOTIATION_FAILSOFT",
        1280,720,1600,900,
        false,1600,900,1600,900)) return 15;

    g_ptarResolutionPolicy.universalSpatialPresenter=false;
    if(Check(
        "SPATIAL_DISABLED_NATIVE_720",
        1280,720,1280,720,
        false,1280,720,1280,720)) return 16;

    g_ptarResolutionPolicy.universalSpatialPresenter=true;
    g_ptarResolutionPolicy.enabled=false;
    if(Check(
        "PTAR_CONFIG_DISABLED_NATIVE_PLAN",
        1280,720,1280,720,
        false,1280,720,1280,720)) return 17;

    std::printf("D3D9_RESOLUTION_POLICY_SMOKE=PASS\n");
    return 0;
}
