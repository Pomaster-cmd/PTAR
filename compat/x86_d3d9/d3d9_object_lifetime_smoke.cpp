#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Create9)(UINT);

static int Exercise(IDirect3D9* d3d,int index)
{
    if(!d3d) return 10;

    const UINT adapters=d3d->GetAdapterCount();
    std::printf("OBJECT=%d PTR=%p ADAPTERS=%u\n",index,d3d,adapters);

    if(adapters>0)
    {
        D3DDISPLAYMODE mode={};
        const HRESULT hr=d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&mode);
        std::printf(
            "OBJECT=%d DISPLAY hr=0x%08lX %ux%u fmt=%u refresh=%u\n",
            index,(unsigned long)hr,mode.Width,mode.Height,
            (unsigned)mode.Format,mode.RefreshRate);
    }

    D3DADAPTER_IDENTIFIER9 ident={};
    const HRESULT idhr=d3d->GetAdapterIdentifier(
        D3DADAPTER_DEFAULT,0,&ident);
    std::printf(
        "OBJECT=%d IDENT hr=0x%08lX desc=%s\n",
        index,(unsigned long)idhr,
        SUCCEEDED(idhr)?ident.Description:"<unavailable>");

    return 0;
}

int main()
{
    HMODULE proxy=LoadLibraryW(L"d3d9.dll");
    if(!proxy)
    {
        std::printf("FAIL LoadLibrary gle=%lu\n",(unsigned long)GetLastError());
        return 2;
    }

    PFN_Create9 create9=(PFN_Create9)GetProcAddress(proxy,"Direct3DCreate9");
    if(!create9)
    {
        std::printf("FAIL GetProcAddress gle=%lu\n",(unsigned long)GetLastError());
        return 3;
    }

    // Sequential create/use/release cycles: matches the field trace that
    // exposed the truncated-vtable lifetime crash.
    for(int i=0;i<12;++i)
    {
        IDirect3D9* d3d=create9(D3D_SDK_VERSION);
        if(!d3d)
        {
            std::printf("FAIL create sequential i=%d\n",i);
            return 10+i;
        }
        const int rc=Exercise(d3d,i);
        if(rc) return rc;
        const ULONG refs=d3d->Release();
        std::printf("OBJECT=%d RELEASE refs=%lu\n",i,(unsigned long)refs);
    }

    // Several objects alive simultaneously, all sharing the system vtable.
    IDirect3D9* live[4]={};
    for(int i=0;i<4;++i)
    {
        live[i]=create9(D3D_SDK_VERSION);
        if(!live[i])
        {
            std::printf("FAIL create live i=%d\n",i);
            return 30+i;
        }
        if(Exercise(live[i],100+i)) return 40+i;
    }
    for(int i=3;i>=0;--i)
    {
        const ULONG refs=live[i]->Release();
        std::printf("LIVE=%d RELEASE refs=%lu\n",i,(unsigned long)refs);
    }

    FreeLibrary(proxy);
    std::printf("D3D9_OBJECT_LIFETIME_SMOKE=PASS\n");
    return 0;
}
