#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <cmath>
#include "ptar_vs_jitter_patch.h"

struct Vertex { float x,y,z,w; };
struct Bounds { int minX,maxX,minY,maxY,count; };

static bool LoadBytes(const char* path,std::vector<DWORD>& out)
{
    std::ifstream f(path,std::ios::binary);
    if(!f) return false;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    if(b.empty() || (b.size()&3u)) return false;
    out.resize(b.size()/4u);
    memcpy(&out[0],&b[0],b.size());
    return true;
}

static Bounds Scan(IDirect3DSurface9* s)
{
    Bounds b={999999,-1,999999,-1,0};
    D3DSURFACE_DESC d={};
    s->GetDesc(&d);
    D3DLOCKED_RECT lr={};
    HRESULT hr=s->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr))
    {
        std::printf("JITTER9_LOCK_FAIL hr=0x%08lx\n",(unsigned long)hr);
        return b;
    }
    for(UINT y=0;y<d.Height;++y)
    {
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+(size_t)y*(size_t)lr.Pitch);
        for(UINT x=0;x<d.Width;++x)
        {
            if((row[x]&0x00ffffffu)!=0)
            {
                const int ix=(int)x, iy=(int)y;
                if(ix<b.minX) b.minX=ix;
                if(ix>b.maxX) b.maxX=ix;
                if(iy<b.minY) b.minY=iy;
                if(iy>b.maxY) b.maxY=iy;
                ++b.count;
            }
        }
    }
    s->UnlockRect();
    return b;
}

static bool HrOk(const char* stage,HRESULT hr)
{
    if(FAILED(hr))
    {
        std::printf("JITTER9_%s_FAIL hr=0x%08lx\n",stage,(unsigned long)hr);
        return false;
    }
    return true;
}

static bool RenderProbe(
    IDirect3DDevice9* dev,IDirect3DSurface9* rt,IDirect3DSurface9* sys,
    IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,
    IDirect3DVertexDeclaration9* decl,UINT jitterReg,float jx,float jy,Bounds& out)
{
    const UINT W=64,H=64;
    if(!HrOk("SET_RT",dev->SetRenderTarget(0,rt))) return false;
    if(!HrOk("SET_DS_NULL",dev->SetDepthStencilSurface(0))) return false;
    D3DVIEWPORT9 vp={0,0,W,H,0.0f,1.0f};
    if(!HrOk("SET_VIEWPORT",dev->SetViewport(&vp))) return false;
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    if(!HrOk("CLEAR",dev->Clear(0,0,D3DCLEAR_TARGET,0xff000000u,1.0f,0))) return false;
    if(!HrOk("SET_DECL",dev->SetVertexDeclaration(decl))) return false;
    if(!HrOk("SET_VS",dev->SetVertexShader(vs))) return false;
    if(!HrOk("SET_PS",dev->SetPixelShader(ps))) return false;
    float c[4]={jx,jy,0.0f,0.0f};
    if(!HrOk("SET_JITTER_CONST",dev->SetVertexShaderConstantF(jitterReg,c,1))) return false;

    const Vertex q[4]={
        {-0.25f,-0.25f,0.5f,1.0f},
        { 0.25f,-0.25f,0.5f,1.0f},
        {-0.25f, 0.25f,0.5f,1.0f},
        { 0.25f, 0.25f,0.5f,1.0f}
    };
    if(!HrOk("BEGIN_SCENE",dev->BeginScene())) return false;
    HRESULT drawHr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));
    HRESULT endHr=dev->EndScene();
    if(!HrOk("DRAW",drawHr) || !HrOk("END_SCENE",endHr)) return false;
    if(!HrOk("GET_RT_DATA",dev->GetRenderTargetData(rt,sys))) return false;
    out=Scan(sys);
    if(out.count<=0)
    {
        std::printf("JITTER9_SCAN_EMPTY\n");
        return false;
    }
    return true;
}

int main(int argc,char** argv)
{
    if(argc!=3)
    {
        std::printf("usage: jitter_vs_patch_gpu original_vs.cso probe_ps.cso\n");
        return 2;
    }

    std::vector<DWORD> vsCode,psCode;
    if(!LoadBytes(argv[1],vsCode) || !LoadBytes(argv[2],psCode))
    {
        std::printf("JITTER9_LOAD=FAIL\n");
        return 3;
    }

    PtJitterPatchedVs patched;
    if(!PtPatchVs3ForClipJitter(&vsCode[0],&patched) || !patched.patched)
    {
        std::printf("JITTER9_BYTECODE_PATCH=FAIL\n");
        return 4;
    }
    std::printf("JITTER9_BYTECODE_PATCH=PASS pos=o%u const=c%u dwords=%u\n",
        patched.positionOutputRegister,patched.constantRegister,(unsigned)patched.code.size());
    {
        std::ofstream dump("artifact\\jitter_vs_patched.cso",std::ios::binary);
        dump.write((const char*)&patched.code[0],(std::streamsize)(patched.code.size()*sizeof(DWORD)));
    }

    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d) return 5;
    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed=TRUE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=GetDesktopWindow();
    pp.BackBufferWidth=64;
    pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,pp.hDeviceWindow,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&dev);
    if(FAILED(hr) || !dev)
    {
        std::printf("JITTER9_DEVICE=FAIL hr=0x%08lx\n",(unsigned long)hr);
        d3d->Release();
        return 6;
    }

    D3DCAPS9 caps={};
    dev->GetDeviceCaps(&caps);
    std::printf("JITTER9_DEVICE_VS=0x%08lx MAXCONST=%lu\n",(unsigned long)caps.VertexShaderVersion,(unsigned long)caps.MaxVertexShaderConst);

    IDirect3DVertexShader9* vs=0;
    IDirect3DPixelShader9* ps=0;
    IDirect3DVertexDeclaration9* decl=0;
    IDirect3DSurface9* rt=0;
    IDirect3DSurface9* sys=0;
    D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        D3DDECL_END()
    };
    bool ok=true;
    hr=dev->CreateVertexShader(&patched.code[0],&vs);
    std::printf("JITTER9_CREATE_PATCHED_VS hr=0x%08lx ptr=%p\n",(unsigned long)hr,vs);
    if(FAILED(hr) || !vs) ok=false;
    if(ok)
    {
        hr=dev->CreatePixelShader(&psCode[0],&ps);
        std::printf("JITTER9_CREATE_PS hr=0x%08lx ptr=%p\n",(unsigned long)hr,ps);
        if(FAILED(hr) || !ps) ok=false;
    }
    if(ok)
    {
        hr=dev->CreateVertexDeclaration(elements,&decl);
        std::printf("JITTER9_CREATE_DECL hr=0x%08lx ptr=%p\n",(unsigned long)hr,decl);
        if(FAILED(hr) || !decl) ok=false;
    }
    if(ok)
    {
        hr=dev->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,0);
        std::printf("JITTER9_CREATE_RT hr=0x%08lx ptr=%p\n",(unsigned long)hr,rt);
        if(FAILED(hr) || !rt) ok=false;
    }
    if(ok)
    {
        hr=dev->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,0);
        std::printf("JITTER9_CREATE_SYS hr=0x%08lx ptr=%p\n",(unsigned long)hr,sys);
        if(FAILED(hr) || !sys) ok=false;
    }

    Bounds zero={},shifted={};
    if(ok) ok=RenderProbe(dev,rt,sys,vs,ps,decl,patched.constantRegister,0.0f,0.0f,zero);
    if(ok) ok=RenderProbe(dev,rt,sys,vs,ps,decl,patched.constantRegister,2.0f/64.0f,-2.0f/64.0f,shifted);

    if(ok)
    {
        const double zx=0.5*(zero.minX+zero.maxX);
        const double zy=0.5*(zero.minY+zero.maxY);
        const double sx=0.5*(shifted.minX+shifted.maxX);
        const double sy=0.5*(shifted.minY+shifted.maxY);
        const double dx=sx-zx,dy=sy-zy;
        std::printf("JITTER9_ZERO_BOUNDS=%d,%d..%d,%d pixels=%d\n",zero.minX,zero.minY,zero.maxX,zero.maxY,zero.count);
        std::printf("JITTER9_SHIFT_BOUNDS=%d,%d..%d,%d pixels=%d\n",shifted.minX,shifted.minY,shifted.maxX,shifted.maxY,shifted.count);
        std::printf("JITTER9_MEASURED_SHIFT=%.3f,%.3f expected=1.000,1.000\n",dx,dy);
        ok=std::fabs(dx-1.0)<=0.10 && std::fabs(dy-1.0)<=0.10;
    }

    std::printf("JITTER9_VS_GPU_SHIFT=%s\n",ok?"PASS":"FAIL");
    if(sys) sys->Release();
    if(rt) rt->Release();
    if(decl) decl->Release();
    if(ps) ps->Release();
    if(vs) vs->Release();
    dev->Release();
    d3d->Release();
    return ok?0:7;
}
