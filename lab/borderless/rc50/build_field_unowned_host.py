from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
src = ROOT / 'lab' / 'borderless' / 'rc48' / 'ptar_rc48_true_runtime_host.cpp'
out = Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).with_name('ptar_rc50_field_unowned_host.cpp')
s = src.read_text(encoding='utf-8')

# Insert a helper for the real P1U46 presenter created by the reconstructed runtime.
anchor = 'template<class T>static void rel(T*& p){if(p){p->Release();p=nullptr;}}\n'
helper = anchor + r'''
static HWND wait_p1u46_presenter(unsigned timeoutMs=8000){
    DWORD st=GetTickCount();
    do{
        HWND h=FindWindowW(L"Win81USRPresenterV041",nullptr);
        if(h&&IsWindow(h))return h;
        pump();Sleep(10);
    }while(GetTickCount()-st<timeoutMs);
    return nullptr;
}
'''
if anchor not in s: raise SystemExit('field host guard: helper anchor missing')
s=s.replace(anchor,helper,1)

# After the exact runtime has created its swapchain/presenter and before frame testing,
# force the topology observed on the user's Win8.1 field machine: top-level + unowned.
anchor = '    const float magenta[4]={1.0f,0.0f,0.75f,1.0f};'
field_block = r'''    HWND presenter=wait_p1u46_presenter();
    if(!presenter){fwprintf(log,L"FAIL presenter not found\n");restore_pref(rb);fclose(log);return 14;}
    LONG_PTR ps=GetWindowLongPtrW(presenter,GWL_STYLE),pex=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    HWND owner0=GetWindow(presenter,GW_OWNER),parent0=GetParent(presenter);
    fwprintf(log,L"PRESENTER_BEFORE_FIELD_EMULATION hwnd=%p owner=%p parent=%p style=0x%llX ex=0x%llX\n",presenter,owner0,parent0,(unsigned long long)ps,(unsigned long long)pex);fflush(log);
    if(ps&WS_CHILD){fwprintf(log,L"FAIL presenter unexpectedly WS_CHILD before field emulation\n");restore_pref(rb);fclose(log);return 15;}
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(presenter,GWLP_HWNDPARENT,0);
    SetWindowPos(presenter,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    pump();Sleep(150);pump();
    HWND owner1=GetWindow(presenter,GW_OWNER),parent1=GetParent(presenter);
    ps=GetWindowLongPtrW(presenter,GWL_STYLE);pex=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    fwprintf(log,L"PRESENTER_FIELD_EMULATED hwnd=%p owner=%p parent=%p style=0x%llX ex=0x%llX gle=%lu\n",presenter,owner1,parent1,(unsigned long long)ps,(unsigned long long)pex,GetLastError());fflush(log);
    if((ps&WS_CHILD)||owner1!=nullptr){fwprintf(log,L"FAIL forced-unowned topology did not stick before stress\n");restore_pref(rb);fclose(log);return 16;}

''' + anchor
if anchor not in s: raise SystemExit('field host guard: render-loop anchor missing')
s=s.replace(anchor,field_block,1)

old = 'last=swap->Present(1,0);pump();if(FAILED(last))'
new = '''last=swap->Present(1,0);pump();
        // Hold the exact field invariant throughout the run. This prevents modern
        // Windows/P1U46 owner behavior from accidentally rescuing a candidate that
        // would still fail on the user's Win8.1 unowned presenter topology.
        if(GetWindow(presenter,GW_OWNER)!=nullptr){
            SetWindowLongPtrW(presenter,GWLP_HWNDPARENT,0);
            SetWindowPos(presenter,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pump();
        }
        // Reproduce the field pressure: the opaque game window repeatedly wins a
        // top-level z-order transaction. The candidate must make the independent
        // P1U46 presenter visible again without owner/parent/WS_CHILD mutation.
        SetWindowPos(game,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        pump();
        if(FAILED(last))'''
if old not in s: raise SystemExit('field host guard: present loop missing')
s=s.replace(old,new,1)

s=s.replace('TRUE_RUNTIME_HOST_BEGIN','FIELD_UNOWNED_HOST_BEGIN',1)
s=s.replace('TRUE_P1U46_WINDOWED_VISIBLE_PIXELS=','FIELD_UNOWNED_VISIBLE_PIXELS=',1)
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(s,encoding='utf-8',newline='\n')
print(f'RC50_FIELD_UNOWNED_HOST_GENERATED={out}')
