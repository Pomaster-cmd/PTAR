#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <algorithm>

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static HBRUSH g_black=nullptr;

struct RegBackup{bool existed=false;DWORD value=0;};
static void backup_pref(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,n=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,reinterpret_cast<BYTE*>(&b.value),&n)==ERROR_SUCCESS&&t==REG_DWORD&&n==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&v),sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void restore_pref(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&b.value),sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,g_black);EndPaint(h,&ps);return 0;}if(m==WM_CLOSE){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return b.x>a.x&&b.y>a.y;}
struct Pixels{unsigned n=0,expected=0,black=0;unsigned ar=0,ag=0,ab=0;};
static Pixels sample_rect(const RECT& r,BYTE er,BYTE eg,BYTE eb){Pixels s{};HDC dc=GetDC(nullptr);if(!dc)return s;for(int yy=1;yy<=5;++yy)for(int xx=1;xx<=5;++xx){int x=r.left+(r.right-r.left)*xx/6,y=r.top+(r.bottom-r.top)*yy/6;COLORREF c=GetPixel(dc,x,y);if(c==CLR_INVALID)continue;BYTE rr=GetRValue(c),gg=GetGValue(c),bb=GetBValue(c);++s.n;s.ar+=rr;s.ag+=gg;s.ab+=bb;if(abs((int)rr-er)<=60&&abs((int)gg-eg)<=60&&abs((int)bb-eb)<=60)++s.expected;if(rr<20&&gg<20&&bb<20)++s.black;}ReleaseDC(nullptr,dc);return s;}
static BOOL CALLBACK enum_child(HWND h,LPARAM p){FILE* f=reinterpret_cast<FILE*>(p);wchar_t cls[128]{},txt[128]{};GetClassNameW(h,cls,127);GetWindowTextW(h,txt,127);RECT r{};GetWindowRect(h,&r);fwprintf(f,L"CHILD hwnd=%p parent=%p owner=%p vis=%d style=0x%llx ex=0x%llx rect=%ld,%ld,%ld,%ld class=%ls text=%ls\n",h,GetParent(h),GetWindow(h,GW_OWNER),IsWindowVisible(h),(unsigned long long)GetWindowLongPtrW(h,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(h,GWL_EXSTYLE),r.left,r.top,r.right,r.bottom,cls,txt);return TRUE;}
static BOOL CALLBACK enum_top(HWND h,LPARAM p){DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;FILE* f=reinterpret_cast<FILE*>(p);wchar_t cls[128]{},txt[128]{};GetClassNameW(h,cls,127);GetWindowTextW(h,txt,127);RECT r{};GetWindowRect(h,&r);fwprintf(f,L"TOP hwnd=%p parent=%p owner=%p vis=%d style=0x%llx ex=0x%llx rect=%ld,%ld,%ld,%ld class=%ls text=%ls\n",h,GetParent(h),GetWindow(h,GW_OWNER),IsWindowVisible(h),(unsigned long long)GetWindowLongPtrW(h,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(h,GWL_EXSTYLE),r.left,r.top,r.right,r.bottom,cls,txt);EnumChildWindows(h,enum_child,p);return TRUE;}
template<class T>static void rel(T*& p){if(p){p->Release();p=nullptr;}}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int){
    FILE* log=nullptr;_wfopen_s(&log,L"TRUE_RUNTIME_HOST.txt",L"wb");if(!log)return 90;
    fwprintf(log,L"TRUE_RUNTIME_HOST_BEGIN exe=Warhammer.exe pid=%lu\n",GetCurrentProcessId());fflush(log);
    RegBackup rb{};backup_pref(rb);if(!set_pref(0)){fwprintf(log,L"FAIL set WindowStyle=0\n");fclose(log);return 10;}
    DeleteFileW(L"win81_nis.log");DeleteFileW(L"ptar_borderless_rc38.log");DeleteFileW(L"ptar_rc41.log");DeleteFileW(L"ptar_rc41_bootstrap.log");
    g_black=CreateSolidBrush(RGB(0,0,0));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_TRUE_WARHAMMER_WINDOW";RegisterClassW(&wc);
    const DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,1280,720};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,80,55,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);if(!game){restore_pref(rb);fclose(log);return 11;}ShowWindow(game,SW_SHOW);UpdateWindow(game);SetForegroundWindow(game);pump();

    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferDesc.RefreshRate.Numerator=60;sd.BufferDesc.RefreshRate.Denominator=1;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=game;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);fwprintf(log,L"D3D_CREATE hr=0x%08X fl=0x%X swap=%p dev=%p ctx=%p\n",(unsigned)hr,(unsigned)fl,swap,dev,ctx);fflush(log);if(FAILED(hr)||!swap||!dev||!ctx){restore_pref(rb);fclose(log);return 12;}
    ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&bb)))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv))){restore_pref(rb);fclose(log);return 13;}D3D11_TEXTURE2D_DESC bd{};bb->GetDesc(&bd);fwprintf(log,L"ENGINE_BACKBUFFER observed=%ux%u\n",bd.Width,bd.Height);fflush(log);rel(bb);

    const float magenta[4]={1.0f,0.0f,0.75f,1.0f};unsigned bestExpected=0,bestN=0,bestBlack=9999;Pixels final{};RECT cr{};HRESULT last=S_OK;
    for(unsigned frame=0;frame<720;++frame){ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->ClearRenderTargetView(rtv,magenta);last=swap->Present(1,0);pump();if(FAILED(last)){fwprintf(log,L"FAIL Present frame=%u hr=0x%08X\n",frame,(unsigned)last);break;}if(frame>90 && frame%15==0 && client_screen(game,cr)){Pixels p=sample_rect(cr,255,0,191);if(p.expected>bestExpected){bestExpected=p.expected;bestN=p.n;bestBlack=p.black;}final=p;}if(frame%120==0){fwprintf(log,L"FRAME %u present=0x%08X client=%ld,%ld,%ld,%ld samples=%u expected=%u black=%u\n",frame,(unsigned)last,cr.left,cr.top,cr.right,cr.bottom,final.n,final.expected,final.black);fflush(log);}}
    if(client_screen(game,cr))final=sample_rect(cr,255,0,191);fwprintf(log,L"FINAL_PIXELS samples=%u expected=%u black=%u avg=%u,%u,%u best_expected=%u/%u best_black=%u\n",final.n,final.expected,final.black,final.n?final.ar/final.n:0,final.n?final.ag/final.n:0,final.n?final.ab/final.n:0,bestExpected,bestN,bestBlack);EnumWindows(enum_top,reinterpret_cast<LPARAM>(log));fflush(log);
    bool visible=SUCCEEDED(last)&&final.n>=20&&final.expected>=final.n*3/5&&final.black<=final.n/5;
    fwprintf(log,L"TRUE_P1U46_WINDOWED_VISIBLE_PIXELS=%ls\n",visible?L"PASS":L"FAIL");fflush(log);
    rel(rtv);rel(swap);rel(ctx);rel(dev);DestroyWindow(game);DeleteObject(g_black);restore_pref(rb);fclose(log);return visible?0:40;
}
