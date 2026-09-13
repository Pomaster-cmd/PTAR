#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")

static HWND g_game=nullptr,g_presenter=nullptr;
static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcW(h,m,w,l);}
static IDXGISwapChain* make_sc(IDXGIFactory*f,ID3D11Device*d,HWND h,UINT w,UINT hh){DXGI_SWAP_CHAIN_DESC x{};x.BufferDesc.Width=w;x.BufferDesc.Height=hh;x.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;x.SampleDesc.Count=1;x.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;x.BufferCount=1;x.OutputWindow=h;x.Windowed=TRUE;x.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;IDXGISwapChain*s=nullptr;return SUCCEEDED(f->CreateSwapChain(d,&x,&s))?s:nullptr;}

int main(){
 HINSTANCE hi=GetModuleHandleW(nullptr);WNDCLASSW a{};a.hInstance=hi;a.lpfnWndProc=GameProc;a.lpszClassName=L"PTAROccGame";WNDCLASSW b{};b.hInstance=hi;b.lpfnWndProc=PresenterProc;b.lpszClassName=L"PTAROccPresenter";if((!RegisterClassW(&a)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&b)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;
 g_game=CreateWindowExW(0,a.lpszClassName,L"game",WS_POPUP,0,0,640,360,nullptr,nullptr,hi,nullptr);if(!g_game)return 11;g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,b.lpszClassName,L"presenter",WS_POPUP,0,0,960,540,g_game,nullptr,hi,nullptr);if(!g_presenter)return 12;ShowWindow(g_game,SW_SHOW);SetActiveWindow(g_game);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();
 if(GetWindow(g_presenter,GW_OWNER)!=g_game)return 13;if(SendMessageW(g_presenter,WM_MOUSEACTIVATE,(WPARAM)g_game,MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))!=MA_NOACTIVATE)return 14;if(GetActiveWindow()!=g_game)return 15;
 // Physical mouse capture can belong to the presenter while PTAR exposes the game HWND logically.
 if(SetCapture(g_presenter)!=nullptr && GetCapture()!=g_presenter)return 16;if(GetCapture()!=g_presenter)return 17;ReleaseCapture();if(GetCapture()!=nullptr)return 18;
 ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;D3D_FEATURE_LEVEL fl{};D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&d,&fl,&c);if(FAILED(hr))return 20;IDXGIDevice*xd=nullptr;IDXGIAdapter*ad=nullptr;IDXGIFactory*f=nullptr;if(FAILED(d->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(xd->GetAdapter(&ad))||FAILED(ad->GetParent(__uuidof(IDXGIFactory),(void**)&f)))return 21;IDXGISwapChain*gs=make_sc(f,d,g_game,640,360),*ps=make_sc(f,d,g_presenter,960,540);if(!gs||!ps)return 22;if(FAILED(f->MakeWindowAssociation(g_game,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER)))return 23;
 unsigned long long gameOK=0,gameOccluded=0,gameOtherStatus=0,gameErrors=0,presenterOK=0,presenterOccluded=0,presenterErrors=0,proxySuccess=0,focusChecks=0,captureChecks=0,minRestore=0;
 for(unsigned i=0;i<2000;++i){
   if((i%200)==0 && i){ShowWindow(g_game,SW_MINIMIZE);pump();ShowWindow(g_game,SW_RESTORE);SetActiveWindow(g_game);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();++minRestore;}
   HRESULT gh=gs->Present(0,0);if(gh==S_OK)++gameOK;else if(gh==DXGI_STATUS_OCCLUDED)++gameOccluded;else if(SUCCEEDED(gh))++gameOtherStatus;else ++gameErrors;
   HRESULT ph=ps->Present(0,0);if(ph==S_OK)++presenterOK;else if(ph==DXGI_STATUS_OCCLUDED)++presenterOccluded;else ++presenterErrors;
   // Candidate proxy contract: the game-visible Present result follows the native presenter,
   // not occlusion of the hidden low-resolution render HWND.
   HRESULT proxy=SUCCEEDED(ph)?S_OK:ph;if(SUCCEEDED(proxy))++proxySuccess;
   if((i%13)==0){SetActiveWindow(g_game);LRESULT ma=SendMessageW(g_presenter,WM_MOUSEACTIVATE,(WPARAM)g_game,MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));if(ma!=MA_NOACTIVATE||GetActiveWindow()!=g_game)return 30;++focusChecks;}
   if((i%17)==0){SetCapture(g_presenter);if(GetCapture()!=g_presenter)return 31;ReleaseCapture();if(GetCapture()!=nullptr)return 32;++captureChecks;}
 }
 if(gameErrors||presenterErrors||proxySuccess!=2000)return 40;
 std::printf("PASS PTAR_BORDERLESS_OCCLUSION_FOCUS_STRESS\n");
 std::printf("cycles=2000 game_present_ok=%llu game_present_occluded=%llu game_other_status=%llu presenter_ok=%llu presenter_occluded=%llu proxy_success=%llu\n",gameOK,gameOccluded,gameOtherStatus,presenterOK,presenterOccluded,proxySuccess);
 std::printf("focus_noactivate_checks=%llu physical_presenter_capture_checks=%llu minimize_restore_cycles=%llu\n",focusChecks,captureChecks,minRestore);
 std::printf("contract=render_HWND_may_be_occluded; native_presenter_result_is_authoritative; keyboard_focus_stays_game; physical_mouse_capture_can_be_presenter\n");
 ps->Release();gs->Release();f->Release();ad->Release();xd->Release();c->Release();d->Release();return 0;
}
