#pragma once

#include <windows.h>
#include <d3d9.h>
#include <cstdio>

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
static LARGE_INTEGER g_ptarHudLast={0};
static double g_ptarHudFps=0.0;

static void PtHudInitClock()
{
    if(g_ptarHudFreq.QuadPart==0)
    {
        QueryPerformanceFrequency(&g_ptarHudFreq);
        QueryPerformanceCounter(&g_ptarHudLast);
    }
}

static void PtHudUpdateFps()
{
    PtHudInitClock();
    LARGE_INTEGER now={0};
    QueryPerformanceCounter(&now);
    if(g_ptarHudLast.QuadPart!=0 && g_ptarHudFreq.QuadPart>0)
    {
        const double dt=(double)(now.QuadPart-g_ptarHudLast.QuadPart)/
                        (double)g_ptarHudFreq.QuadPart;
        if(dt>0.00001 && dt<1.0)
        {
            const double inst=1.0/dt;
            g_ptarHudFps=(g_ptarHudFps<=0.0)?inst:(g_ptarHudFps*0.90+inst*0.10);
        }
    }
    g_ptarHudLast=now;
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

    // Keep the production GW16I shortcut contract. Chords are isolated from
    // their plain-key actions exactly as in the reference runtime.
    if(f6 && !g_ptarHudPrevF6)
    {
        if(ctrl)
        {
            g_ptarHudFgEnabled=!g_ptarHudFgEnabled;
            PtFgPacerReset();
            PtDiagLogA(
                "HOTKEY CTRL+F6 FrameGeneration=%s",
                g_ptarHudFgEnabled?"ON":"OFF");
        }
        else
        {
            g_ptarHudUseMoe=!g_ptarHudUseMoe;
            PtDiagLogA(
                "HOTKEY F6 ManualFilter=%s",
                g_ptarHudUseMoe?"PTAR_MOE":"BILINEAR_REF");
        }
    }

    if(f7 && !g_ptarHudPrevF7 && !ctrl)
        PtDiagLogA("HOTKEY F7 Benchmark requested");

    if(f8 && !g_ptarHudPrevF8)
    {
        if(ctrl)
        {
            if(g_ptarHudFgEnabled)
            {
                // Match production same-ME-tier live selection. The D3D9
                // production port currently uses the /2 high-quality tier,
                // therefore live cycling is QUALITY <-> CONSERVATIVE.
                g_ptarHudFgProfile=(g_ptarHudFgProfile==2)?3:2;
            }
            else
            {
                g_ptarHudFgProfile=(g_ptarHudFgProfile+1)&3;
            }

            PtDiagLogA(
                "HOTKEY CTRL+F8 FGProfile=%d fg=%s",
                g_ptarHudFgProfile,
                g_ptarHudFgEnabled?"ON":"OFF");
        }
        else
        {
            // Plain F8 is Status in the production contract. It must never
            // toggle HUD visibility; CTRL+F11 owns that function.
            g_ptarHudVisible=true;
            PtDiagLogA(
                "HOTKEY F8 Status mode=%s fg=%s profile=%d",
                g_ptarHudUseMoe?"PTAR_MOE":"BILINEAR_REF",
                g_ptarHudFgEnabled?"ON":"OFF",
                g_ptarHudFgProfile);
        }
    }

    if(f9 && !g_ptarHudPrevF9)
    {
        if(ctrl)
            PtDiagLogA("HOTKEY CTRL+F9 VideoRecord requested");
        else
            PtDiagLogA("HOTKEY F9 Capture requested");
    }

    if(f10 && !g_ptarHudPrevF10 && !ctrl)
        PtDiagLogA("HOTKEY F10 TogglePresenter requested");

    if(f11 && !g_ptarHudPrevF11 && ctrl)
    {
        g_ptarHudVisible=!g_ptarHudVisible;
        PtDiagLogA(
            "HOTKEY CTRL+F11 ToggleHUD=%s",
            g_ptarHudVisible?"ON":"OFF");
    }

    if(f12 && !g_ptarHudPrevF12 && !ctrl)
    {
        g_ptarHudUseMoe=!g_ptarHudUseMoe;
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
    PtHudUpdateFps();
}

static bool PtHudUseMoe()
{
    return g_ptarHudUseMoe;
}

static bool PtHudFgEnabled()
{
    return g_ptarHudFgEnabled;
}

static double PtHudRealFps()
{
    return g_ptarHudFps;
}

static double PtHudDisplayFps(bool fgProducing)
{
    const double measured=PtFgPacerVisibleFps();
    if(measured>0.0)
        return measured;
    return g_ptarHudFps*(fgProducing?2.0:1.0);
}

static const BYTE* PtHudGlyph(char c)
{
    static const BYTE blank[7]={0,0,0,0,0,0,0};

    static const BYTE A[7]={14,17,17,31,17,17,17};
    static const BYTE B[7]={30,17,17,30,17,17,30};
    static const BYTE C[7]={14,17,16,16,16,17,14};
    static const BYTE D[7]={30,17,17,17,17,17,30};
    static const BYTE E[7]={31,16,16,30,16,16,31};
    static const BYTE F[7]={31,16,16,30,16,16,16};
    static const BYTE G[7]={14,17,16,23,17,17,14};
    static const BYTE H[7]={17,17,17,31,17,17,17};
    static const BYTE I[7]={31,4,4,4,4,4,31};
    static const BYTE J[7]={1,1,1,1,17,17,14};
    static const BYTE K[7]={17,18,20,24,20,18,17};
    static const BYTE L[7]={16,16,16,16,16,16,31};
    static const BYTE M[7]={17,27,21,21,17,17,17};
    static const BYTE N[7]={17,25,21,19,17,17,17};
    static const BYTE O[7]={14,17,17,17,17,17,14};
    static const BYTE P[7]={30,17,17,30,16,16,16};
    static const BYTE Q[7]={14,17,17,17,21,18,13};
    static const BYTE R[7]={30,17,17,30,20,18,17};
    static const BYTE S[7]={15,16,16,14,1,1,30};
    static const BYTE T[7]={31,4,4,4,4,4,4};
    static const BYTE U[7]={17,17,17,17,17,17,14};
    static const BYTE V[7]={17,17,17,17,17,10,4};
    static const BYTE W[7]={17,17,17,21,21,21,10};
    static const BYTE X[7]={17,17,10,4,10,17,17};
    static const BYTE Y[7]={17,17,10,4,4,4,4};
    static const BYTE Z[7]={31,1,2,4,8,16,31};

    static const BYTE N0[7]={14,17,19,21,25,17,14};
    static const BYTE N1[7]={4,12,4,4,4,4,14};
    static const BYTE N2[7]={14,17,1,2,4,8,31};
    static const BYTE N3[7]={30,1,1,14,1,1,30};
    static const BYTE N4[7]={2,6,10,18,31,2,2};
    static const BYTE N5[7]={31,16,16,30,1,1,30};
    static const BYTE N6[7]={14,16,16,30,17,17,14};
    static const BYTE N7[7]={31,1,2,4,8,8,8};
    static const BYTE N8[7]={14,17,17,14,17,17,14};
    static const BYTE N9[7]={14,17,17,15,1,1,14};

    static const BYTE colon[7]={0,4,4,0,4,4,0};
    static const BYTE slash[7]={1,1,2,4,8,16,16};
    static const BYTE dash[7]={0,0,0,31,0,0,0};
    static const BYTE dot[7]={0,0,0,0,0,12,12};
    static const BYTE gt[7]={16,8,4,2,4,8,16};

    if(c>='a' && c<='z') c=(char)(c-'a'+'A');

    switch(c)
    {
        case 'A': return A; case 'B': return B; case 'C': return C;
        case 'D': return D; case 'E': return E; case 'F': return F;
        case 'G': return G; case 'H': return H; case 'I': return I;
        case 'J': return J; case 'K': return K; case 'L': return L;
        case 'M': return M; case 'N': return N; case 'O': return O;
        case 'P': return P; case 'Q': return Q; case 'R': return R;
        case 'S': return S; case 'T': return T; case 'U': return U;
        case 'V': return V; case 'W': return W; case 'X': return X;
        case 'Y': return Y; case 'Z': return Z;

        case '0': return N0; case '1': return N1; case '2': return N2;
        case '3': return N3; case '4': return N4; case '5': return N5;
        case '6': return N6; case '7': return N7; case '8': return N8;
        case '9': return N9;

        case ':': return colon;
        case '/': return slash;
        case '-': return dash;
        case '.': return dot;
        case '>': return gt;
        default: return blank;
    }
}

static void PtHudDrawLine(
    IDirect3DDevice9* dev,
    int x,
    int y,
    int scale,
    const char* text,
    D3DCOLOR color)
{
    if(!dev || !text || scale<1) return;

    D3DRECT rects[1024];
    DWORD count=0;

    for(int ci=0;text[ci] && count<_countof(rects);++ci)
    {
        const BYTE* glyph=PtHudGlyph(text[ci]);
        const int baseX=x+ci*(6*scale);

        for(int row=0;row<7 && count<_countof(rects);++row)
        {
            const BYTE bits=glyph[row];
            for(int col=0;col<5 && count<_countof(rects);++col)
            {
                if(bits&(1<<(4-col)))
                {
                    D3DRECT& r=rects[count++];
                    r.x1=baseX+col*scale;
                    r.y1=y+row*scale;
                    r.x2=r.x1+scale;
                    r.y2=r.y1+scale;
                }
            }
        }
    }

    if(count)
        dev->Clear(count,rects,D3DCLEAR_TARGET,color,1.0f,0);
}

static void PtHudDraw(
    IDirect3DDevice9* dev,
    UINT sourceW,
    UINT sourceH,
    UINT outputW,
    UINT outputH,
    bool fgProducing)
{
    if(!g_ptarHudVisible || !dev) return;

    const int x=14;
    const int y=14;
    const int scale=2;
    const int lineStep=17;

    D3DRECT bg={8,8,700,160};
    dev->Clear(
        1,&bg,D3DCLEAR_TARGET,
        D3DCOLOR_XRGB(8,8,8),
        1.0f,0);

    char line[160]={0};

    PtHudDrawLine(
        dev,x,y,scale,
        "PTAR X86 D3D9 PRODPORT",
        D3DCOLOR_XRGB(240,240,240));

    _snprintf_s(
        line,sizeof(line),_TRUNCATE,
        "MODE: %s",
        g_ptarHudUseMoe?"PTAR MOE":"BILINEAR REF");
    PtHudDrawLine(
        dev,x,y+lineStep,scale,line,
        g_ptarHudUseMoe?
            D3DCOLOR_XRGB(70,255,120):
            D3DCOLOR_XRGB(255,210,70));

    _snprintf_s(
        line,sizeof(line),_TRUNCATE,
        "SRC: %uX%u > OUT: %uX%u",
        sourceW,sourceH,outputW,outputH);
    PtHudDrawLine(
        dev,x,y+lineStep*2,scale,line,
        D3DCOLOR_XRGB(220,220,220));

    _snprintf_s(
        line,sizeof(line),_TRUNCATE,
        "REAL FPS: %.1f  DISPLAY FPS: %.1f",
        PtHudRealFps(),PtHudDisplayFps(fgProducing));
    PtHudDrawLine(
        dev,x,y+lineStep*3,scale,line,
        D3DCOLOR_XRGB(220,220,220));

    _snprintf_s(
        line,sizeof(line),_TRUNCATE,
        "FG: %s  PROFILE: %d  TARGET: 60",
        g_ptarHudFgEnabled?"ON":"OFF",
        g_ptarHudFgProfile);
    PtHudDrawLine(
        dev,x,y+lineStep*4,scale,line,
        g_ptarHudFgEnabled?
            D3DCOLOR_XRGB(90,235,255):
            D3DCOLOR_XRGB(190,190,190));

    _snprintf_s(
        line,sizeof(line),_TRUNCATE,
        "REAL: %lu  GEN: %lu  LATE SKIP: %lu",
        PtFgPacerRealCount(),
        PtFgPacerGeneratedCount(),
        PtFgPacerLateSkipCount());
    PtHudDrawLine(
        dev,x,y+lineStep*5,scale,line,
        D3DCOLOR_XRGB(210,210,210));

    PtHudDrawLine(
        dev,x,y+lineStep*6,scale,
        "F6 FILTER  CTRL+F6 FG  CTRL+F8 PROFILE",
        D3DCOLOR_XRGB(180,220,255));

    PtHudDrawLine(
        dev,x,y+lineStep*7,scale,
        g_ptarHudVisible?
            (fgProducing?"F8 STATUS  CTRL+F11 HUD  F12 NEXT":"F8 STATUS  CTRL+F11 HUD  F12 NEXT"):
            "CTRL+F11 HUD",
        D3DCOLOR_XRGB(210,210,210));
}
