#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}

static bool SendChord(WORD vk)
{
    INPUT in[4]={};
    in[0].type=INPUT_KEYBOARD; in[0].ki.wVk=VK_CONTROL;
    in[1].type=INPUT_KEYBOARD; in[1].ki.wVk=vk;
    in[2].type=INPUT_KEYBOARD; in[2].ki.wVk=vk; in[2].ki.dwFlags=KEYEVENTF_KEYUP;
    in[3].type=INPUT_KEYBOARD; in[3].ki.wVk=VK_CONTROL; in[3].ki.dwFlags=KEYEVENTF_KEYUP;
    return SendInput(4,in,sizeof(INPUT))==4;
}

int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR cmd,int)
{
    bool exclusive=(cmd && wcsstr(cmd,L"--exclusive")!=0);
    int runMs=150000;
    const wchar_t* p=cmd? wcsstr(cmd,L"--ms=") : 0;
    if(p){int v=_wtoi(p+5); if(v>=5000)runMs=v;}

    WNDCLASSW wc={}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"PTAR_REAL_D3D9_E2E"; wc.hCursor=LoadCursor(0,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 10;
    DWORD style=exclusive?WS_POPUP:WS_OVERLAPPEDWINDOW;
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"PTAR D3D9 REAL PRESENT E2E",style,40,40,800,600,0,0,hi,0);
    if(!hwnd) return 11;
    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd); SetForegroundWindow(hwnd); SetFocus(hwnd);

    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){FILE* f=0;_wfopen_s(&f,L"PTAR_E2E_GAME_RESULT.txt",L"wb");if(f){fprintf(f,"DIRECT3D_CREATE9=FAIL\r\n");fclose(f);}return 12;}

    D3DDISPLAYMODE dm={}; d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&dm);
    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=800; pp.BackBufferHeight=600;
    pp.BackBufferFormat=exclusive?dm.Format:D3DFMT_X8R8G8B8;
    pp.BackBufferCount=1; pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=hwnd;
    pp.Windowed=exclusive?FALSE:TRUE; pp.EnableAutoDepthStencil=FALSE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    if(exclusive){pp.FullScreen_RefreshRateInHz=dm.RefreshRate;}

    IDirect3DDevice9* dev=0;
    D3DDEVTYPE used=D3DDEVTYPE_HAL;
    HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr))
    {
        used=D3DDEVTYPE_REF;
        hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    }
    if(FAILED(hr))
    {
        used=D3DDEVTYPE_NULLREF;
        hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_NULLREF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    }
    FILE* log=0; _wfopen_s(&log,L"PTAR_E2E_GAME_RESULT.txt",L"wb");
    if(FAILED(hr)||!dev)
    {
        if(log){fprintf(log,"CREATE_DEVICE=FAIL HR=0x%08lX EXCLUSIVE=%d\r\n",(unsigned long)hr,exclusive?1:0);fclose(log);}d3d->Release();return 13;
    }

    if(log){fprintf(log,"CREATE_DEVICE=PASS TYPE=%d EXCLUSIVE=%d\r\n",(int)used,exclusive?1:0);fflush(log);}
    ULONGLONG start=GetTickCount64(); bool fgSent=false; unsigned presents=0, failures=0;
    MSG msg={};
    while((int)(GetTickCount64()-start)<runMs)
    {
        while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)goto done;TranslateMessage(&msg);DispatchMessageW(&msg);}
        ULONGLONG elapsed=GetTickCount64()-start;
        if(!fgSent && elapsed>=26000)
        {
            SetForegroundWindow(hwnd); SetFocus(hwnd);
            if(SendChord(VK_F6)){fgSent=true;if(log){fprintf(log,"CTRL_F6_SENT_AT_MS=%llu\r\n",(unsigned long long)elapsed);fflush(log);}}
        }
        DWORD phase=(DWORD)((elapsed/250)%6);
        D3DCOLOR c=D3DCOLOR_XRGB(20+phase*25,40+phase*20,80+phase*15);
        dev->Clear(0,0,D3DCLEAR_TARGET,c,1.0f,0);
        hr=dev->Present(0,0,0,0);
        if(SUCCEEDED(hr))++presents; else ++failures;
        Sleep(16);
    }

done:
    if(log)
    {
        fprintf(log,"PRESENT_CALLS=%u\r\nPRESENT_FAILURES=%u\r\nFG_TOGGLE_SENT=%d\r\nRESULT=%s\r\n",presents,failures,fgSent?1:0,(presents>100&&failures==0&&fgSent)?"PASS":"FAIL");
        fclose(log);
    }
    dev->Release(); d3d->Release(); DestroyWindow(hwnd);
    return (presents>100&&failures==0&&fgSent)?0:14;
}
