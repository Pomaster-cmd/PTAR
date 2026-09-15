from pathlib import Path

SRC=Path('lab/borderless/rc58/ptar_rc58_mode_bridge_host.cpp')
OUT=Path('lab/borderless/rc59/generated/ptar_rc59_windowed_host.cpp')
s=SRC.read_text(encoding='utf-8')
s=s.replace('RC58','RC59').replace('rc58','rc59')
old='static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}'
new='static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_GETMINMAXINFO&&l){MINMAXINFO* mm=(MINMAXINFO*)l;mm->ptMaxTrackSize.x=4096;mm->ptMaxTrackSize.y=2160;}if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}'
if old not in s: raise RuntimeError('host Proc anchor missing')
s=s.replace(old,new,1)
OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_text(s,encoding='utf-8',newline='\n')
print('RC59_WINDOWED_HOST_SOURCE=PASS')
