#pragma once

#include <windows.h>
#include <d3d9.h>
#include <cstdio>

static volatile LONG g_ptarCapturePending=0;

static void PtCaptureRequest()
{
    InterlockedExchange(&g_ptarCapturePending,1);
}

static bool PtCaptureConsumeRequest()
{
    return InterlockedExchange(&g_ptarCapturePending,0)!=0;
}

#pragma pack(push,1)
struct PTARBmpFileHeader
{
    WORD type;
    DWORD size;
    WORD reserved1;
    WORD reserved2;
    DWORD offBits;
};

struct PTARBmpInfoHeader
{
    DWORD size;
    LONG width;
    LONG height;
    WORD planes;
    WORD bitCount;
    DWORD compression;
    DWORD sizeImage;
    LONG xPelsPerMeter;
    LONG yPelsPerMeter;
    DWORD clrUsed;
    DWORD clrImportant;
};
#pragma pack(pop)

static bool PtCaptureDecodePixel(
    D3DFORMAT format,
    const BYTE* src,
    BYTE* b,
    BYTE* g,
    BYTE* r)
{
    if(!src || !b || !g || !r)
        return false;

    if(format==D3DFMT_A8R8G8B8 || format==D3DFMT_X8R8G8B8)
    {
        *b=src[0];
        *g=src[1];
        *r=src[2];
        return true;
    }

    if(format==D3DFMT_R5G6B5)
    {
        const WORD v=*(const WORD*)src;
        const unsigned rv=(v>>11)&31u;
        const unsigned gv=(v>>5)&63u;
        const unsigned bv=v&31u;
        *r=(BYTE)((rv*255u+15u)/31u);
        *g=(BYTE)((gv*255u+31u)/63u);
        *b=(BYTE)((bv*255u+15u)/31u);
        return true;
    }

    if(format==D3DFMT_X1R5G5B5 || format==D3DFMT_A1R5G5B5)
    {
        const WORD v=*(const WORD*)src;
        const unsigned rv=(v>>10)&31u;
        const unsigned gv=(v>>5)&31u;
        const unsigned bv=v&31u;
        *r=(BYTE)((rv*255u+15u)/31u);
        *g=(BYTE)((gv*255u+15u)/31u);
        *b=(BYTE)((bv*255u+15u)/31u);
        return true;
    }

    if(format==D3DFMT_A2R10G10B10)
    {
        const DWORD v=*(const DWORD*)src;
        const unsigned rv=(v>>20)&1023u;
        const unsigned gv=(v>>10)&1023u;
        const unsigned bv=v&1023u;
        *r=(BYTE)((rv*255u+511u)/1023u);
        *g=(BYTE)((gv*255u+511u)/1023u);
        *b=(BYTE)((bv*255u+511u)/1023u);
        return true;
    }

    return false;
}

static UINT PtCaptureBytesPerPixel(D3DFORMAT format)
{
    if(format==D3DFMT_A8R8G8B8 ||
       format==D3DFMT_X8R8G8B8 ||
       format==D3DFMT_A2R10G10B10)
        return 4u;

    if(format==D3DFMT_R5G6B5 ||
       format==D3DFMT_X1R5G5B5 ||
       format==D3DFMT_A1R5G5B5)
        return 2u;

    return 0u;
}

static bool PtCaptureBuildPath(
    HMODULE module,
    wchar_t* pathOut,
    size_t pathCount)
{
    if(!module || !pathOut || pathCount<32u)
        return false;

    wchar_t dir[MAX_PATH]={0};
    DWORD n=GetModuleFileNameW(module,dir,MAX_PATH);
    if(!n || n>=MAX_PATH)
        return false;

    wchar_t* slash=wcsrchr(dir,L'\\');
    if(!slash)
        return false;
    *(slash+1)=0;

    for(unsigned int i=1;i<=9999u;++i)
    {
        wchar_t candidate[MAX_PATH]={0};
        _snwprintf_s(
            candidate,MAX_PATH,_TRUNCATE,
            L"%swin81_nis_capture_%u.bmp",
            dir,i);

        if(GetFileAttributesW(candidate)==INVALID_FILE_ATTRIBUTES)
        {
            wcsncpy_s(pathOut,pathCount,candidate,_TRUNCATE);
            return true;
        }
    }

    return false;
}

static HRESULT PtCaptureSavePostOverlayBmp(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* finalBackBuffer,
    HMODULE module,
    wchar_t* savedPath,
    size_t savedPathCount)
{
    if(savedPath && savedPathCount)
        savedPath[0]=0;

    if(!dev || !finalBackBuffer || !module)
        return D3DERR_INVALIDCALL;

    D3DSURFACE_DESC desc={};
    HRESULT hr=finalBackBuffer->GetDesc(&desc);
    if(FAILED(hr) || !desc.Width || !desc.Height)
        return FAILED(hr)?hr:E_FAIL;

    const UINT bytesPerPixel=PtCaptureBytesPerPixel(desc.Format);
    if(!bytesPerPixel)
    {
        PtDiagLogA(
            "F9_CAPTURE_UNSUPPORTED_FORMAT format=%u",
            (unsigned)desc.Format);
        return D3DERR_NOTAVAILABLE;
    }

    IDirect3DSurface9* staging=0;
    hr=dev->CreateOffscreenPlainSurface(
        desc.Width,desc.Height,desc.Format,
        D3DPOOL_SYSTEMMEM,&staging,0);
    if(FAILED(hr) || !staging)
    {
        PtDiagLogA(
            "F9_CAPTURE_STAGING_CREATE_FAIL hr=0x%08lX",
            (unsigned long)hr);
        return FAILED(hr)?hr:E_FAIL;
    }

    hr=dev->GetRenderTargetData(finalBackBuffer,staging);
    if(FAILED(hr))
    {
        PtDiagLogA(
            "F9_CAPTURE_READBACK_FAIL hr=0x%08lX",
            (unsigned long)hr);
        staging->Release();
        return hr;
    }

    D3DLOCKED_RECT locked={};
    hr=staging->LockRect(&locked,0,D3DLOCK_READONLY);
    if(FAILED(hr))
    {
        staging->Release();
        return hr;
    }

    wchar_t path[MAX_PATH]={0};
    if(!PtCaptureBuildPath(module,path,MAX_PATH))
    {
        staging->UnlockRect();
        staging->Release();
        return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    }

    HANDLE file=CreateFileW(
        path,GENERIC_WRITE,0,0,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE)
    {
        const DWORD gle=GetLastError();
        staging->UnlockRect();
        staging->Release();
        return HRESULT_FROM_WIN32(gle);
    }

    const DWORD rowBytes=desc.Width*3u;
    const DWORD rowStride=(rowBytes+3u)&~3u;
    const DWORD imageBytes=rowStride*desc.Height;

    PTARBmpFileHeader fileHeader={};
    fileHeader.type=0x4D42u;
    fileHeader.offBits=
        (DWORD)(sizeof(PTARBmpFileHeader)+sizeof(PTARBmpInfoHeader));
    fileHeader.size=fileHeader.offBits+imageBytes;

    PTARBmpInfoHeader infoHeader={};
    infoHeader.size=sizeof(PTARBmpInfoHeader);
    infoHeader.width=(LONG)desc.Width;
    infoHeader.height=(LONG)desc.Height;
    infoHeader.planes=1;
    infoHeader.bitCount=24;
    infoHeader.compression=0;
    infoHeader.sizeImage=imageBytes;

    bool ok=true;
    DWORD written=0;
    if(!WriteFile(
        file,&fileHeader,sizeof(fileHeader),&written,0) ||
       written!=sizeof(fileHeader))
        ok=false;

    if(ok &&
       (!WriteFile(
           file,&infoHeader,sizeof(infoHeader),&written,0) ||
        written!=sizeof(infoHeader)))
        ok=false;

    BYTE* row=(BYTE*)HeapAlloc(GetProcessHeap(),0,rowStride);
    if(!row)
        ok=false;

    if(ok)
    {
        for(LONG y=(LONG)desc.Height-1;y>=0 && ok;--y)
        {
            ZeroMemory(row,rowStride);
            const BYTE* srcRow=
                (const BYTE*)locked.pBits+
                (size_t)y*(size_t)locked.Pitch;

            for(UINT x=0;x<desc.Width;++x)
            {
                BYTE b=0,g=0,r=0;
                if(!PtCaptureDecodePixel(
                    desc.Format,
                    srcRow+(size_t)x*bytesPerPixel,
                    &b,&g,&r))
                {
                    ok=false;
                    break;
                }

                BYTE* dst=row+x*3u;
                dst[0]=b;
                dst[1]=g;
                dst[2]=r;
            }

            if(ok &&
               (!WriteFile(file,row,rowStride,&written,0) ||
                written!=rowStride))
                ok=false;
        }
    }

    if(row)
        HeapFree(GetProcessHeap(),0,row);

    CloseHandle(file);
    staging->UnlockRect();
    staging->Release();

    if(!ok)
    {
        DeleteFileW(path);
        return E_FAIL;
    }

    if(savedPath && savedPathCount)
        wcsncpy_s(savedPath,savedPathCount,path,_TRUNCATE);

    return S_OK;
}
