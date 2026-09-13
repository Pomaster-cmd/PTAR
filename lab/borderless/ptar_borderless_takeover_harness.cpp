#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static HWND g_game = nullptr;
static HWND g_presenter = nullptr;
static UINT g_gameW = 640, g_gameH = 360;
static UINT g_outW = 960, g_outH = 540;
static LONG g_gameSizeMsgs = 0;
static LONG g_lastLogicalX = -1, g_lastLogicalY = -1;
static const UINT WM_PTAR_LOGICAL_MOUSE = WM_APP + 0x41;

static void pump_messages(){ MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);} }
static RECT aspect_fit(UINT sw,UINT sh,UINT ow,UINT oh){const double s=(std::min)(double(ow)/sw,double(oh)/sh);const LONG w=LONG(sw*s+.5),h=LONG(sh*s+.5),x=(LONG(ow)-w)/2,y=(LONG(oh)-h)/2;RECT r={x,y,x+w,y+h};return r;}
static POINT map_output_to_game(LONG x,LONG y){RECT r=aspect_fit(g_gameW,g_gameH,g_outW,g_outH);x=(std::max)(r.left,(std::min)(r.right-1,x));y=(std::max)(r.top,(std::min)(r.bottom-1,y));LONG gx=LONG(double(x-r.left)*g_gameW/(r.right-r.left)),gy=LONG(double(y-r.top)*g_gameH/(r.bottom-r.top));gx=(std::max)(0L,(std::min)(LONG(g_gameW)-1,gx));gy=(std::max)(0L,(std::min)(LONG(g_gameH)-1,gy));return {gx,gy};}
static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){case WM_GETMINMAXINFO:{MINMAXINFO*mm=reinterpret_cast<MINMAXINFO*>(l);mm->ptMaxTrackSize.x=8192;mm->ptMaxTrackSize.y=8192;return 0;}case WM_SIZE:++g_gameSizeMsgs;break;case WM_PTAR_LOGICAL_MOUSE:g_lastLogicalX=GET_X_LPARAM(l);g_lastLogicalY=GET_Y_LPARAM(l);return 0;}return DefWindowProcW(h,m,w,l);}
static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){case WM_MOUSEACTIVATE:return MA_NOACTIVATE;case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_RBUTTONDOWN:case WM_RBUTTONUP:{POINT p=map_output_to_game(GET_X_LPARAM(l),GET_Y_LPARAM(l));SendMessageW(g_game,WM_PTAR_LOGICAL_MOUSE,w,MAKELPARAM(p.x,p.y));return 0;}}return DefWindowProcW(h,m,w,l);}
static bool set_client_size(HWND h,UINT cw,UINT ch){RECT r={0,0,LONG(cw),LONG(ch)};DWORD s=DWORD(GetWindowLongPtrW(h,GWL_STYLE)),e=DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE));if(!AdjustWindowRectEx(&r,s,FALSE,e))return false;return !!SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);}
static bool get_client_size(HWND h,UINT&w,UINT&hh){RECT r{};if(!GetClientRect(h,&r))return false;w=UINT(r.right-r.left);hh=UINT(r.bottom-r.top);return true;}
static bool verify_swap_size(IDXGISwapChain*sc,UINT w,UINT h){ID3D11Texture2D*t=nullptr;if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&t))||!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);t->Release();return d.Width==w&&d.Height==h;}
static IDXGISwapChain* make_swapchain(IDXGIFactory*f,ID3D11Device*d,HWND h,UINT w,UINT hh){DXGI_SWAP_CHAIN_DESC x{};x.BufferDesc.Width=w;x.BufferDesc.Height=hh;x.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;x.SampleDesc.Count=1;x.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT|DXGI_USAGE_SHADER_INPUT;x.BufferCount=1;x.OutputWindow=h;x.Windowed=TRUE;x.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;IDXGISwapChain*s=nullptr;if(FAILED(f->CreateSwapChain(d,&x,&s)))return nullptr;return s;}
static int fail(const char*what,int c){std::printf("FAIL code=%d %s\n",c,what);return c;}

int main(){
 HINSTANCE hi=GetModuleHandleW(nullptr); WNDCLASSW wc{};wc.hInstance=hi;wc.lpfnWndProc=GameProc;wc.lpszClassName=L"PTARLabGame";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 10; WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARLabPresenter";if(!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 11;
 g_game=CreateWindowExW(0,wc.lpszClassName,L"PTAR synthetic game",WS_OVERLAPPEDWINDOW,10,10,640,360,nullptr,nullptr,hi,nullptr);if(!g_game)return 12;if(!set_client_size(g_game,g_gameW,g_gameH))return 13;ShowWindow(g_game,SW_SHOW);pump_messages();
 const DWORD pex=WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;g_presenter=CreateWindowExW(pex,pc.lpszClassName,L"PTAR borderless presenter",WS_POPUP,0,0,LONG(g_outW),LONG(g_outH),g_game,nullptr,hi,nullptr);if(!g_presenter)return 14;ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump_messages();
 DWORD style=DWORD(GetWindowLongPtrW(g_presenter,GWL_STYLE)),ex=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));if(!(style&WS_POPUP)||(style&(WS_CAPTION|WS_THICKFRAME)))return fail("presenter style",15);if(!(ex&WS_EX_NOACTIVATE)||!(ex&WS_EX_TOOLWINDOW))return fail("presenter exstyle",16);if(ex&WS_EX_TRANSPARENT)return fail("presenter transparent forbidden",17);if(GetWindow(g_presenter,GW_OWNER)!=g_game)return fail("owner",18);if(SendMessageW(g_presenter,WM_MOUSEACTIVATE,(WPARAM)g_game,MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))!=MA_NOACTIVATE)return fail("mouseactivate",19);
 ID3D11Device*dev=nullptr;ID3D11DeviceContext*ctx=nullptr;D3D_FEATURE_LEVEL fl{};D3D_FEATURE_LEVEL req[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,req,ARRAYSIZE(req),D3D11_SDK_VERSION,&dev,&fl,&ctx)))return fail("warp",20);IDXGIDevice*dx=nullptr;IDXGIAdapter*ad=nullptr;IDXGIFactory*f=nullptr;if(FAILED(dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&dx))||FAILED(dx->GetAdapter(&ad))||FAILED(ad->GetParent(__uuidof(IDXGIFactory),(void**)&f)))return 21;
 IDXGISwapChain*gs=make_swapchain(f,dev,g_game,g_gameW,g_gameH),*os=make_swapchain(f,dev,g_presenter,g_outW,g_outH);if(!gs||!os)return fail("swapchains",22);if(FAILED(f->MakeWindowAssociation(g_game,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER)))return fail("association",23);BOOL fs=TRUE;if(FAILED(gs->GetFullscreenState(&fs,nullptr))||fs)return fail("game fullscreen",24);if(FAILED(os->GetFullscreenState(&fs,nullptr))||fs)return fail("out fullscreen",25);if(!verify_swap_size(gs,g_gameW,g_gameH)||!verify_swap_size(os,g_outW,g_outH))return 26;
 std::mt19937 rng(0x50544152u);std::vector<POINT>renders={{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800}},outputs={{960,540},{1280,720},{1366,768},{1600,900},{1920,1080}};unsigned resizeFailures=0,outputLeaks=0,mouseFailures=0;unsigned long long checks=0;
 for(unsigned i=0;i<20000;++i){if((rng()&1u)==0){POINT p=renders[rng()%renders.size()];g_gameW=UINT(p.x);g_gameH=UINT(p.y);LONG before=g_gameSizeMsgs;if(!set_client_size(g_game,g_gameW,g_gameH))return fail("set game client",30);pump_messages();HRESULT hr=gs->ResizeBuffers(0,g_gameW,g_gameH,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))++resizeFailures;UINT cw=0,ch=0;if(!get_client_size(g_game,cw,ch)||cw!=g_gameW||ch!=g_gameH){RECT wr{};GetWindowRect(g_game,&wr);std::printf("MISMATCH i=%u expected=%ux%u actual=%ux%u window=%ldx%ld style=0x%08lx ex=0x%08lx screen=%dx%d\n",i,g_gameW,g_gameH,cw,ch,wr.right-wr.left,wr.bottom-wr.top,GetWindowLongW(g_game,GWL_STYLE),GetWindowLongW(g_game,GWL_EXSTYLE),GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));return 31;}if(!verify_swap_size(gs,g_gameW,g_gameH))return 32;if(g_gameSizeMsgs<before)return 33;checks+=7;}else{POINT p=outputs[rng()%outputs.size()];LONG before=g_gameSizeMsgs;UINT a=0,b=0;get_client_size(g_game,a,b);g_outW=UINT(p.x);g_outH=UINT(p.y);if(!SetWindowPos(g_presenter,nullptr,0,0,g_outW,g_outH,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE))return 34;pump_messages();HRESULT hr=os->ResizeBuffers(0,g_outW,g_outH,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))++resizeFailures;UINT c=0,d=0;get_client_size(g_game,c,d);if(a!=c||b!=d||g_gameSizeMsgs!=before)++outputLeaks;if(!verify_swap_size(os,g_outW,g_outH))return 35;checks+=7;}for(int k=0;k<16;++k){LONG px=LONG(rng()%g_outW),py=LONG(rng()%g_outH);POINT e=map_output_to_game(px,py);g_lastLogicalX=g_lastLogicalY=-1;SendMessageW(g_presenter,WM_MOUSEMOVE,0,MAKELPARAM(px,py));if(g_lastLogicalX!=e.x||g_lastLogicalY!=e.y)++mouseFailures;if(g_lastLogicalX<0||g_lastLogicalY<0||g_lastLogicalX>=LONG(g_gameW)||g_lastLogicalY>=LONG(g_gameH))++mouseFailures;checks+=4;}}
 UINT cw=0,ch=0;get_client_size(g_game,cw,ch);if(cw!=g_gameW||ch!=g_gameH)return 40;if(!verify_swap_size(gs,g_gameW,g_gameH))return 41;if(resizeFailures||outputLeaks||mouseFailures){std::printf("FAIL resizeFailures=%u outputLeaks=%u mouseFailures=%u\n",resizeFailures,outputLeaks,mouseFailures);return 42;}
 std::printf("PASS PTAR_BORDERLESS_TAKEOVER_LAB\ntransitions=20000 pointer_probes=320000 checks=%llu\nresize_failures=0 output_only_game_resize_leaks=0 mouse_mapping_failures=0\ninvariant=game_client_equals_game_backbuffer; presenter_client_equals_native_output\npresenter=WS_POPUP+NOACTIVATE+TOOLWINDOW+owned+NOT_TRANSPARENT\ndxgi=both_windowed+NO_WINDOW_CHANGES+NO_ALT_ENTER\n",checks);
 os->Release();gs->Release();f->Release();ad->Release();dx->Release();ctx->ClearState();ctx->Release();dev->Release();DestroyWindow(g_presenter);DestroyWindow(g_game);return 0;
}
