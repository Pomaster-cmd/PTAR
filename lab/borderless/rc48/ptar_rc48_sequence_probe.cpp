#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <algorithm>

using AttachFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(void*);
struct ModeState{
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};
static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static const wchar_t* kSync=L"PTAR_RC46_WINDOWSTYLE_SYNC_20260914";
static unsigned long long g_dispatched=0;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static bool pump_one(){MSG m{};if(!PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){Sleep(0);return false;}TranslateMessage(&m);DispatchMessageW(&m);++g_dispatched;return true;}
static unsigned service_queue(unsigned maxMessages){unsigned n=0;while(n<maxMessages&&pump_one())++n;return n;}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q&&q(&s)==0;}
static bool wait_mode(QueryFn q,bool b,unsigned timeout,unsigned& elapsed,ModeState& last,unsigned& qfails,unsigned& installed0,unsigned& wrongmode){DWORD st=GetTickCount();qfails=installed0=wrongmode=0;for(;;){ModeState s{};if(!query(q,s)){++qfails;}else{last=s;if(!s.installed)++installed0;else if((s.borderlessActive!=0)==b){elapsed=GetTickCount()-st;return true;}else ++wrongmode;}if(GetTickCount()-st>=timeout)break;pump_one();}elapsed=GetTickCount()-st;ModeState final{};if(query(q,final)){last=final;if(final.installed&&((final.borderlessActive!=0)==b))return true;}return false;}
static int fail(int rc,const char* tag,QueryFn q,HWND g,HWND p,unsigned elapsed=0,unsigned qf=0,unsigned i0=0,unsigned wm=0){ModeState s{};bool qok=query(q,s);RECT gr{},pr{};if(g)GetWindowRect(g,&gr);if(p)GetWindowRect(p,&pr);std::printf("RC48_SEQUENCE=FAIL rc=%d tag=%s qok=%d installed=%u mode=%u pref=%ld elapsed=%u qfails=%u installed0=%u wrongmode=%u dispatched=%llu gstyle=0x%llx pstyle=0x%llx parent=%p owner=%p grect=%ld,%ld,%ld,%ld prect=%ld,%ld,%ld,%ld\n",rc,tag,qok?1:0,s.installed,s.borderlessActive,s.windowStylePreference,elapsed,qf,i0,wm,g_dispatched,(unsigned long long)(g?GetWindowLongPtrW(g,GWL_STYLE):0),(unsigned long long)(p?GetWindowLongPtrW(p,GWL_STYLE):0),p?GetParent(p):nullptr,p?GetWindow(p,GW_OWNER):nullptr,gr.left,gr.top,gr.right,gr.bottom,pr.left,pr.top,pr.right,pr.bottom);std::fflush(stdout);return rc;}
int wmain(){
    if(!set_pref(0))return fail(10,"initial-pref",nullptr,nullptr,nullptr);
    HINSTANCE hi=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=hi;wc.lpszClassName=L"PTAR_RC48_SEQUENCE";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 11;
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,8,8,nullptr,nullptr,hi,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi);DestroyWindow(probe);UINT ow=UINT(mi.rcMonitor.right-mi.rcMonitor.left),oh=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);UINT rw=std::min<UINT>(1280,std::max<UINT>(320,ow*2/3)),rh=std::min<UINT>(720,std::max<UINT>(180,oh*2/3));RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);HWND g=CreateWindowExW(0,wc.lpszClassName,L"game",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,60,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,hi,nullptr);HWND p=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,0,0,ow,oh,g,nullptr,hi,nullptr);if(!g||!p)return fail(12,"windows",nullptr,g,p);
    HMODULE m=LoadLibraryW(L"ptar_borderless.dll");if(!m)return fail(13,"load",nullptr,g,p);auto a=(AttachFn)GetProcAddress(m,"PTAR_BorderlessAttachStable");auto q=(QueryFn)GetProcAddress(m,"PTAR_BorderlessQueryMode");if(!a||!q)return fail(14,"exports",q,g,p);if(a(g,p,rw,rh,ow,oh)!=0)return fail(15,"attach",q,g,p);
    unsigned e=0,qf=0,i0=0,wm=0;ModeState s{};if(!wait_mode(q,false,5000,e,s,qf,i0,wm))return fail(16,"initial-windowed",q,g,p,e,qf,i0,wm);
    UINT sync=RegisterWindowMessageW(kSync);if(!sync)return fail(17,"sync",q,g,p);
    constexpr unsigned kPairs=2500;unsigned long long servicedDuringDirect=0;
    for(unsigned i=0;i<kPairs;++i){
        SendMessageW(g,sync,1,0);if(!wait_mode(q,true,1000,e,s,qf,i0,wm))return fail(20,"direct-borderless",q,g,p,e,qf,i0,wm);
        SendMessageW(g,sync,0,0);if(!wait_mode(q,false,1000,e,s,qf,i0,wm))return fail(21,"direct-windowed",q,g,p,e,qf,i0,wm);
        servicedDuringDirect+=service_queue(16);
    }
    const unsigned residual=service_queue(256);
    ModeState before{};query(q,before);std::printf("RC48_SEQUENCE_DIRECT_PASS pairs=%u toB=%llu toW=%llu pref=%ld serviced=%llu residual=%u\n",kPairs,before.transitionsToBorderless,before.transitionsToWindowed,before.windowStylePreference,servicedDuringDirect,residual);std::fflush(stdout);
    DWORD st=GetTickCount();if(!set_pref(1))return fail(30,"set-pref-1",q,g,p);if(!wait_mode(q,true,3000,e,s,qf,i0,wm))return fail(31,"registry-borderless",q,g,p,e,qf,i0,wm);unsigned regB=GetTickCount()-st;
    service_queue(32);
    st=GetTickCount();if(!set_pref(0))return fail(32,"set-pref-0",q,g,p);if(!wait_mode(q,false,3000,e,s,qf,i0,wm))return fail(33,"registry-windowed",q,g,p,e,qf,i0,wm);unsigned regW=GetTickCount()-st;
    std::printf("RC48_SEQUENCE=PASS direct_pairs=%u direct_transitions=%u registry_borderless_ms=%u registry_windowed_ms=%u final_pref=%ld final_mode=%u dispatched=%llu\n",kPairs,kPairs*2u,regB,regW,s.windowStylePreference,s.borderlessActive,g_dispatched);std::fflush(stdout);return 0;
}
