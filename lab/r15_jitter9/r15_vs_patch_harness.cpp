#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "ptar_vs_jitter_patch.h"
#include "r15_test_vs2_bytecode.h"
#include "r15_test_vs3_bytecode.h"

static bool ContainsText(ID3DBlob* blob,const char* token)
{
    if(!blob || !token) return false;
    const char* p=(const char*)blob->GetBufferPointer();
    const SIZE_T n=blob->GetBufferSize();
    if(!p || !n) return false;
    std::string s(p,p+n);
    return s.find(token)!=std::string::npos;
}

static bool CheckOne(
    IDirect3DDevice9* dev,
    const DWORD* bytecode,
    const char* label,
    UINT expectedMajor)
{
    std::vector<DWORD> patched;
    PtVsJitterPatchInfo info={};
    if(!PtPatchVertexShaderForJitter(bytecode,&patched,&info))
    {
        std::printf("%s_PATCH=FAIL\n",label);
        return false;
    }

    if(!info.patched || info.shaderMajor!=expectedMajor ||
       info.positionWrites==0 || info.tempRegNum>=32 ||
       info.dwordCountAfter!=info.dwordCountBefore+8)
    {
        std::printf(
            "%s_METADATA=FAIL major=%u writes=%u temp=%u before=%u after=%u\n",
            label,info.shaderMajor,info.positionWrites,info.tempRegNum,
            info.dwordCountBefore,info.dwordCountAfter);
        return false;
    }

    ID3DBlob* disasm=0;
    HRESULT hr=D3DDisassemble(
        patched.data(),patched.size()*sizeof(DWORD),
        0,0,&disasm);
    if(FAILED(hr) || !disasm)
    {
        std::printf("%s_DISASSEMBLE=FAIL hr=0x%08lX\n",label,(unsigned long)hr);
        return false;
    }

    const bool hasMad=ContainsText(disasm,"mad");
    const bool hasC255=ContainsText(disasm,"c255");
    const bool hasMov=ContainsText(disasm,"mov");
    if(!hasMad || !hasC255 || !hasMov)
    {
        std::printf(
            "%s_EPILOGUE=FAIL mad=%d c255=%d mov=%d\n",
            label,hasMad?1:0,hasC255?1:0,hasMov?1:0);
        disasm->Release();
        return false;
    }

    IDirect3DVertexShader9* shader=0;
    hr=dev->CreateVertexShader(patched.data(),&shader);
    if(FAILED(hr) || !shader)
    {
        std::printf("%s_CREATE=FAIL hr=0x%08lX\n",label,(unsigned long)hr);
        disasm->Release();
        return false;
    }

    const float jitter[4]={
        2.0f*(-0.5f)/1280.0f,
        -2.0f*(-1.0f/6.0f)/720.0f,
        0.0f,0.0f};
    hr=dev->SetVertexShaderConstantF(255,jitter,1);
    if(FAILED(hr))
    {
        std::printf("%s_C255=FAIL hr=0x%08lX\n",label,(unsigned long)hr);
        shader->Release();
        disasm->Release();
        return false;
    }

    std::printf(
        "%s_PATCH=PASS major=%u minor=%u posType=%u posReg=%u temp=r%u writes=%u dwords=%u_to_%u\n",
        label,
        info.shaderMajor,info.shaderMinor,
        info.positionRegType,info.positionRegNum,
        info.tempRegNum,info.positionWrites,
        info.dwordCountBefore,info.dwordCountAfter);

    shader->Release();
    disasm->Release();
    return true;
}

int main()
{
    HWND hwnd=CreateWindowExW(
        0,L"STATIC",L"PTAR_R15_JITTER_PATCH_HARNESS",
        WS_OVERLAPPEDWINDOW,
        0,0,64,64,0,0,GetModuleHandleW(0),0);
    if(!hwnd)
    {
        std::printf("R15_WINDOW=FAIL gle=%lu\n",(unsigned long)GetLastError());
        return 2;
    }

    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d)
    {
        std::printf("R15_D3D9=FAIL\n");
        DestroyWindow(hwnd);
        return 3;
    }

    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed=TRUE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=64;
    pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.hDeviceWindow=hwnd;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &pp,&dev);
    if(FAILED(hr) || !dev)
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp,&dev);
    }
    if(FAILED(hr) || !dev)
    {
        std::printf("R15_DEVICE=FAIL hr=0x%08lX\n",(unsigned long)hr);
        d3d->Release();
        DestroyWindow(hwnd);
        return 4;
    }

    D3DCAPS9 caps={};
    hr=dev->GetDeviceCaps(&caps);
    if(FAILED(hr) || caps.MaxVertexShaderConst<256)
    {
        std::printf(
            "R15_CAPS=FAIL hr=0x%08lX maxVsConst=%lu\n",
            (unsigned long)hr,(unsigned long)caps.MaxVertexShaderConst);
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return 5;
    }

    std::printf(
        "R15_DEVICE=PASS vs=0x%08lX maxVsConst=%lu\n",
        (unsigned long)caps.VertexShaderVersion,
        (unsigned long)caps.MaxVertexShaderConst);

    const bool ok2=CheckOne(dev,(const DWORD*)g_r15TestVs2,"R15_VS2",2);
    const bool ok3=CheckOne(dev,(const DWORD*)g_r15TestVs3,"R15_VS3",3);

    dev->Release();
    d3d->Release();
    DestroyWindow(hwnd);

    if(!ok2 || !ok3)
        return 6;

    std::printf("R15_VERTEX_JITTER_PATCH_ALL=PASS\n");
    return 0;
}
