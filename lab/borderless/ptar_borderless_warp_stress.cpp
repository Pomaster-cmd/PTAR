#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")

static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcW(h,m,w,l);}
static bool size_is(HWND h,UINT w,UINT hh){RECT r{};return GetClientRect(h,&r)&&UINT(r.right-r.left)==w&&UINT(r.bottom-r.top)==hh;}
static bool bb_size(IDXGISwapChain*s,UINT&w,UINT&h){ID3D11Texture2D*t=nullptr;HRESULT hr=s->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&t);if(FAILED(hr)||!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);t->Release();w=d.Width;h=d.Height;return true;}
static IDXGISwapChain* make_sc(IDXGIFactory*f,ID3D11Device*d,HWND h,UINT w,UINT hh){DXGI_SWAP_CHAIN_DESC x{};x.BufferDesc.Width=w;x.BufferDesc.Height=hh;x.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;x.SampleDesc.Count=1;x.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT|DXGI_USAGE_SHADER_INPUT;x.BufferCount=1;x.OutputWindow=h;x.Windowed=TRUE;x.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;IDXGISwapChain*s=nullptr;return SUCCEEDED(f->CreateSwapChain(d,&x,&s))?s:nullptr;}
static int die(const char*s,int c,HRESULT hr=S_OK){std::printf("FAIL code=%d %s hr=0x%08lx\n",c,s,(unsigned long)hr);return c;}

int main(){
 HINSTANCE hi=GetModuleHandleW(nullptr);WNDCLASSW a{};a.hInstance=hi;a.lpfnWndProc=GameProc;a.lpszClassName=L"PTARWarpGame";WNDCLASSW b{};b.hInstance=hi;b.lpfnWndProc=PresenterProc;b.lpszClassName=L"PTARWarpPresenter";if((!RegisterClassW(&a)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&b)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;
 HWND game=CreateWindowExW(0,a.lpszClassName,L"game",WS_POPUP,0,0,640,360,nullptr,nullptr,hi,nullptr);if(!game)return 11;HWND presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,b.lpszClassName,L"presenter",WS_POPUP,0,0,960,540,game,nullptr,hi,nullptr);if(!presenter)return 12;ShowWindow(game,SW_SHOW);ShowWindow(presenter,SW_SHOWNOACTIVATE);pump();
 ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;D3D_FEATURE_LEVEL fl{};D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&d,&fl,&c);if(FAILED(hr))return die("D3D11CreateDevice WARP",20,hr);IDXGIDevice*xd=nullptr;IDXGIAdapter*ad=nullptr;IDXGIFactory*f=nullptr;if(FAILED(d->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(xd->GetAdapter(&ad))||FAILED(ad->GetParent(__uuidof(IDXGIFactory),(void**)&f)))return 21;
 IDXGISwapChain*gs=make_sc(f,d,game,640,360),*ps=make_sc(f,d,presenter,960,540);if(!gs||!ps)return 22;hr=f->MakeWindowAssociation(game,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER);if(FAILED(hr))return die("MakeWindowAssociation",23,hr);BOOL fs=TRUE;if(FAILED(gs->GetFullscreenState(&fs,nullptr))||fs)return 24;if(FAILED(ps->GetFullscreenState(&fs,nullptr))||fs)return 25;
 std::mt19937 rng(0x57415250u);std::vector<POINT>r={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900}},o={{640,360},{960,540},{1024,768},{1280,720},{1366,768},{1600,900},{1920,1080}};unsigned long long gameResizes=0,outResizes=0,presents=0;
 for(unsigned i=0;i<12000;++i){
   if((rng()&1u)==0){POINT q=r[rng()%r.size()];SetWindowPos(game,nullptr,0,0,q.x,q.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();if(!size_is(game,q.x,q.y))return 30;c->OMSetRenderTargets(0,nullptr,nullptr);c->ClearState();c->Flush();hr=gs->ResizeBuffers(0,q.x,q.y,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr)){std::printf("GAME_RESIZE_FAIL i=%u size=%ldx%ld\n",i,q.x,q.y);return die("game ResizeBuffers",31,hr);}UINT bw=0,bh=0;if(!bb_size(gs,bw,bh)||bw!=UINT(q.x)||bh!=UINT(q.y)){std::printf("GAME_BB_MISMATCH i=%u expected=%ldx%ld actual=%ux%u\n",i,q.x,q.y,bw,bh);return 32;}++gameResizes;
   }else{POINT q=o[rng()%o.size()];UINT gw=0,gh=0;if(!bb_size(gs,gw,gh))return 33;SetWindowPos(presenter,nullptr,0,0,q.x,q.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();c->OMSetRenderTargets(0,nullptr,nullptr);c->ClearState();c->Flush();hr=ps->ResizeBuffers(0,q.x,q.y,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr)){std::printf("OUT_RESIZE_FAIL i=%u size=%ldx%ld\n",i,q.x,q.y);return die("presenter ResizeBuffers",34,hr);}UINT bw=0,bh=0;if(!bb_size(ps,bw,bh)||bw!=UINT(q.x)||bh!=UINT(q.y))return 35;UINT gw2=0,gh2=0;if(!bb_size(gs,gw2,gh2)||gw2!=gw||gh2!=gh)return 36;++outResizes;}
   // The authoritative PTAR path presents the native presenter. The game swapchain remains windowed/render-only.
   hr=ps->Present(0,DXGI_PRESENT_TEST);if(FAILED(hr))return die("presenter DXGI_PRESENT_TEST",37,hr);++presents;
 }
 DWORD s=DWORD(GetWindowLongPtrW(presenter,GWL_STYLE)),e=DWORD(GetWindowLongPtrW(presenter,GWL_EXSTYLE));if(!(s&WS_POPUP)||(s&(WS_CAPTION|WS_THICKFRAME))||!(e&WS_EX_NOACTIVATE)||!(e&WS_EX_TOOLWINDOW)||(e&WS_EX_TRANSPARENT))return 40;if(GetWindow(presenter,GW_OWNER)!=game)return 41;
 std::printf("PASS PTAR_BORDERLESS_WARP_STRESS\n");std::printf("game_resizes=%llu presenter_resizes=%llu presenter_present_tests=%llu\n",gameResizes,outResizes,presents);std::printf("swapchains=windowed game_client_tracks_render presenter_client_tracks_output output_changes_never_resize_game_backbuffer\n");
 ps->Release();gs->Release();f->Release();ad->Release();xd->Release();c->ClearState();c->Release();d->Release();DestroyWindow(presenter);DestroyWindow(game);return 0;
}
