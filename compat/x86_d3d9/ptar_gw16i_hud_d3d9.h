#pragma once

#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

// Exact D3D9 raster adaptation of the embedded GW16I HUD.
// The source D3D11 HUD is procedural HLSL. On the user's Win8.1/D3D9 device
// CreatePixelShader rejected the monolithic ps_3_0 translation with
// D3DERR_INVALIDCALL (0x8876086C). D3D9 therefore renders the same encoded
// 3x5 glyph masks/layout through Clear(rects), which is independent of shader
// instruction/capability limits and inherited game raster state.
//
// Coordinate equivalence with the source shader:
//   source HLSL: p = SV_Position / 2; glyph q = floor((p-o)/2)
//   D3D9 CPU port: glyph cell = 4 physical pixels, character stride = 16 px.

struct PTARGw16RectBatch
{
    D3DRECT rects[768];
    DWORD count;
};

static unsigned short PtGw16GlyphMask(int c)
{
    switch(c)
    {
        case 32:return 0;
        case 43:return 4680;
        case 45:return 448;
        case 46:return 8192;
        case 58:return 1040;
        case 88:return 23213;
        case 48:return 31599;
        case 49:return 29850;
        case 50:return 29671;
        case 51:return 31207;
        case 52:return 18925;
        case 53:return 31183;
        case 54:return 31695;
        case 55:return 9383;
        case 56:return 31727;
        case 57:return 31215;
        case 65:return 23530;
        case 66:return 15083;
        case 67:return 25166;
        case 68:return 15211;
        case 69:return 29391;
        case 70:return 4815;
        case 71:return 27470;
        case 72:return 23533;
        case 73:return 29847;
        case 75:return 23277;
        case 76:return 29257;
        case 77:return 23549;
        case 78:return 24573;
        case 79:return 31599;
        case 80:return 4843;
        case 81:return 20335;
        case 82:return 23275;
        case 83:return 14798;
        case 84:return 9367;
        case 85:return 31597;
        case 86:return 11117;
        case 87:return 24557;
        case 89:return 9389;
        default:return 0;
    }
}

static void PtGw16Flush(
    IDirect3DDevice9* dev,
    PTARGw16RectBatch* batch,
    D3DCOLOR color)
{
    if(!dev || !batch || !batch->count)
        return;

    dev->Clear(
        batch->count,
        batch->rects,
        D3DCLEAR_TARGET,
        color,
        1.0f,
        0);
    batch->count=0;
}

static void PtGw16PushRect(
    IDirect3DDevice9* dev,
    PTARGw16RectBatch* batch,
    LONG x1,LONG y1,LONG x2,LONG y2,
    D3DCOLOR color)
{
    if(!dev || !batch || x2<=x1 || y2<=y1)
        return;

    if(batch->count>=_countof(batch->rects))
        PtGw16Flush(dev,batch,color);

    D3DRECT& r=batch->rects[batch->count++];
    r.x1=x1; r.y1=y1; r.x2=x2; r.y2=y2;
}

static void PtGw16Glyph(
    IDirect3DDevice9* dev,
    PTARGw16RectBatch* batch,
    LONG x,LONG y,
    int c,
    D3DCOLOR color)
{
    const unsigned short mask=PtGw16GlyphMask(c);
    if(!mask) return;

    const LONG cell=4;
    for(int row=0;row<5;++row)
    {
        for(int col=0;col<3;++col)
        {
            const int bit=row*3+col;
            if((mask>>bit)&1)
            {
                PtGw16PushRect(
                    dev,batch,
                    x+col*cell,
                    y+row*cell,
                    x+(col+1)*cell,
                    y+(row+1)*cell,
                    color);
            }
        }
    }
}

static void PtGw16Text(
    IDirect3DDevice9* dev,
    PTARGw16RectBatch* batch,
    LONG x,LONG y,
    const char* text,
    D3DCOLOR color)
{
    if(!text) return;
    for(int i=0;text[i];++i)
        PtGw16Glyph(dev,batch,x+i*16,y,(unsigned char)text[i],color);
}

static void PtGw16Number(
    IDirect3DDevice9* dev,
    PTARGw16RectBatch* batch,
    LONG x,LONG y,
    unsigned long value,
    int width,
    D3DCOLOR color)
{
    if(width<1) return;
    if(width>4) width=4;

    unsigned long limit=1;
    for(int i=0;i<width;++i) limit*=10ul;
    if(value>=limit) value=limit-1ul;

    char digits[8]={0};
    _snprintf_s(
        digits,sizeof(digits),_TRUNCATE,
        "%lu",value);

    const int len=(int)strlen(digits);
    const int leading=width-len;

    // The production Dig3/Dig4 suppresses leading zeroes and keeps the least
    // significant digit on the final character cell.
    for(int i=0;i<len;++i)
        PtGw16Glyph(
            dev,batch,
            x+(leading+i)*16,y,
            digits[i],
            color);
}

static void PtGw16Panel(
    IDirect3DDevice9* dev,
    LONG x1,LONG y1,LONG x2,LONG y2,
    D3DCOLOR bg)
{
    if(!dev) return;
    D3DRECT r={x1,y1,x2,y2};
    dev->Clear(1,&r,D3DCLEAR_TARGET,bg,1.0f,0);
}

static const char* PtGw16FilterName(int id)
{
    switch(id)
    {
        case 0:return "POINT";
        case 1:return "BILINEAR";
        case 2:return "PTAR X15";
        case 3:return "PTAR";
        case 5:return "COPY 1X1";
        case 6:return "EDGE 25";
        case 7:return "EDGE 45";
        default:return "SAFE";
    }
}

static const char* PtGw16ProfileName(int id)
{
    switch(id)
    {
        case 0:return "LEGACY";
        case 1:return "BALANCED";
        case 2:return "QUALITY";
        default:return "CONSERVATIVE";
    }
}

static void PtGw16Marker(
    IDirect3DDevice9* dev,
    PTARGw16RectBatch* batch,
    LONG x,LONG y,
    unsigned long frameId,
    bool generated,
    D3DCOLOR fg)
{
    frameId%=4096ul;
    PtGw16Glyph(dev,batch,x,y,'M',fg);
    PtGw16Number(dev,batch,x+32,y,frameId%10000ul,4,fg);
    PtGw16Glyph(dev,batch,x+112,y,generated?'G':'R',fg);

    unsigned long parity=generated?1ul:0ul;
    for(int i=0;i<12;++i)
    {
        const unsigned long b0=(frameId>>i)&1ul;
        const unsigned long b1=(frameId>>(i+1))&1ul;
        const unsigned long bit=b0^b1;
        parity=(parity+bit)&1ul;
        if(bit)
            PtGw16PushRect(
                dev,batch,
                x+144+i*10,y,
                x+152+i*10,y+10,
                fg);
    }

    if(generated)
        PtGw16PushRect(dev,batch,x+264,y,x+272,y+10,fg);
    if(parity)
        PtGw16PushRect(dev,batch,x+274,y,x+282,y+10,fg);
    PtGw16PushRect(dev,batch,x+284,y,x+292,y+10,fg);
}

static void PtGw16StatePanel(
    IDirect3DDevice9* dev,
    int state,
    D3DCOLOR bg,
    D3DCOLOR fg)
{
    if(!dev || state==0)
        return;

    PtGw16Panel(dev,16,16,640,96,bg);

    PTARGw16RectBatch batch={};
    const bool hudState=state>=3;
    const bool enabled=(state==1 || state==4);

    PtGw16Text(
        dev,&batch,32,32,
        hudState?"HUD":"USR",fg);
    PtGw16Text(
        dev,&batch,96,32,
        enabled?"OFF":"ON",fg);

    // Production HudParams contract currently maps the DX9 HUD shortcut to
    // CTRL+F11. State=4 (HUD OFF) advertises ENABLE, state=3 advertises DISABLE.
    PtGw16Text(dev,&batch,32,64,"C+F11",fg);
    PtGw16Text(
        dev,&batch,128,64,
        enabled?"ENABLE":"DISABLE",fg);

    PtGw16Flush(dev,&batch,fg);
}

static void PtGw16FeedbackPanel(
    IDirect3DDevice9* dev,
    LONG originX,LONG originY,
    int type,
    int a,int b,int c,
    D3DCOLOR bg,
    D3DCOLOR fg)
{
    if(!dev || type==0)
        return;

    PtGw16Panel(
        dev,
        originX,originY,
        originX+440,originY+96,
        bg);

    PTARGw16RectBatch batch={};
    const LONG x=originX+16;
    const LONG y=originY+16;

    switch(type)
    {
        case 4:
            PtGw16Text(dev,&batch,x,y,"F7 BUSY",fg);
            break;
        case 5:
            PtGw16Text(dev,&batch,x,y,"F8 OK",fg);
            break;
        case 6:
            PtGw16Text(dev,&batch,x,y,"F9 CAP",fg);
            PtGw16Number(dev,&batch,x+112,y,(unsigned long)a,3,fg);
            PtGw16Text(dev,&batch,x+176,y,"SAVED",fg);
            break;
        case 7:
            PtGw16Text(dev,&batch,x,y,"F6 MODE",fg);
            PtGw16Text(
                dev,&batch,x+128,y,
                a==0?"POINT":
                a==1?"BILIN":
                a==2?"PTAR":"SAFE",
                fg);
            break;
        case 8:
            PtGw16Text(dev,&batch,x,y,PtGw16ProfileName(a),fg);
            break;
        case 12:
            PtGw16Text(dev,&batch,x,y,"NV ON",fg);
            PtGw16Text(dev,&batch,x,originY+48,"F8 STATUS",fg);
            break;
        case 13:
            PtGw16Text(dev,&batch,x,y,"NV OFF",fg);
            PtGw16Text(dev,&batch,x,originY+48,"F8 STATUS",fg);
            break;
        case 14:
            PtGw16Text(dev,&batch,x,y,"REC WAIT",fg);
            break;
        case 15:
            PtGw16Text(dev,&batch,x,y,"REC ON",fg);
            break;
        case 16:
            PtGw16Text(dev,&batch,x,y,"REC SAVE",fg);
            break;
        case 17:
            PtGw16Text(dev,&batch,x,y,"REC END",fg);
            break;
        case 18:
            PtGw16Text(dev,&batch,x,y,"REC ERR",fg);
            break;
        default:
            PtGw16Text(dev,&batch,x,y,"PTAR",fg);
            break;
    }

    PtGw16Flush(dev,&batch,fg);
}

static HRESULT PtGw16RenderExactHudD3D9(
    IDirect3DDevice9* dev,
    bool visible,
    int state,
    bool marker,
    unsigned long frameId,
    bool generated,
    double displayFps,
    UINT sourceW,
    UINT sourceH,
    int filterId,
    int feedbackType,
    int feedbackA,
    int feedbackB,
    int feedbackC)
{
    if(!dev)
        return D3DERR_INVALIDCALL;

    const D3DCOLOR bg=D3DCOLOR_XRGB(3,3,5);
    const D3DCOLOR fg=D3DCOLOR_XRGB(235,245,255);

    if(visible)
    {
        PtGw16Panel(dev,16,16,680,176,bg);

        PTARGw16RectBatch batch={};
        PtGw16Text(dev,&batch,32,32,"WIN81 USR V0.41",fg);

        PtGw16Text(dev,&batch,32,64,"FPS",fg);
        const unsigned long fps=
            displayFps<=0.0?0ul:
            (displayFps>=999.0?999ul:
             (unsigned long)(displayFps+0.5));
        PtGw16Number(dev,&batch,112,64,fps,3,fg);

        PtGw16Text(dev,&batch,32,96,"RES.",fg);
        PtGw16Number(dev,&batch,112,96,sourceW,4,fg);
        PtGw16Glyph(dev,&batch,180,96,'X',fg);
        PtGw16Number(dev,&batch,200,96,sourceH,4,fg);

        PtGw16Text(dev,&batch,32,128,"FILTER",fg);
        PtGw16Text(dev,&batch,160,128,PtGw16FilterName(filterId),fg);

        if(marker)
            PtGw16Marker(
                dev,&batch,
                32,160,
                frameId,
                generated,
                fg);

        PtGw16Flush(dev,&batch,fg);
    }
    else if(marker && state==0)
    {
        PtGw16Panel(dev,32,160,336,172,bg);
        PTARGw16RectBatch batch={};
        PtGw16Marker(
            dev,&batch,32,160,
            frameId,generated,fg);
        PtGw16Flush(dev,&batch,fg);
    }
    else if(state!=0)
    {
        PtGw16StatePanel(dev,state,bg,fg);
    }

    if(feedbackType!=0)
    {
        PtGw16FeedbackPanel(
            dev,
            16,
            visible?192:16,
            feedbackType,
            feedbackA,feedbackB,feedbackC,
            bg,fg);
    }

    return S_OK;
}
