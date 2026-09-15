from pathlib import Path

SRC=Path('lab/borderless/rc58/ptar_rc58_mode_bridge_host.cpp')
OUT=Path('lab/borderless/rc59/generated/ptar_rc59_popup_host.cpp')
s=SRC.read_text(encoding='utf-8')
s=s.replace('RC58','RC59').replace('rc58','rc59')
s=s.replace('if(!set_pref(0)){fclose(ev);return 10;}','if(!set_pref(1)){fclose(ev);return 10;}',1)
# Hosted Windows runner is only 1024px wide. The real field monitor is 1920x1080;
# permit a >1024 top-level tracking width so SetWindowPos can exercise the exact
# 1280x720 client geometry instead of being clipped by the runner's default max-track size.
old_proc='static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}'
new_proc='static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_GETMINMAXINFO&&l){MINMAXINFO* mm=(MINMAXINFO*)l;mm->ptMaxTrackSize.x=4096;mm->ptMaxTrackSize.y=2160;}if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}'
if old_proc not in s: raise RuntimeError('host Proc anchor missing')
s=s.replace(old_proc,new_proc,1)
old='DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,1280,720};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,35,25,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);'
new='DWORD style=WS_POPUP|WS_VISIBLE;RECT wr{0,0,1920,1080};HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,0,0,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);'
if old not in s: raise RuntimeError('window construction anchor missing')
s=s.replace(old,new,1)
start='if(!wait_state(d,q,true,15000)){restore(rb);fclose(ev);return 17;}'
end='constexpr unsigned cycles=500;'
a=s.find(start);b=s.find(end,a)
if a<0 or b<0: raise RuntimeError('startup test block anchors missing')
repl=r'''if(!wait_state(d,q,false,15000)){restore(rb);fclose(ev);return 17;}
for(unsigned i=0;i<240;++i)if(!d.frame()){restore(rb);fclose(ev);return 18;}
Geometry base{},pg{};BridgeState bs{};
if(!geom(game,base)||!game_windowed(base)||base.cw!=1280||base.ch!=720||!geom(presenter,pg)||pg.cw!=1920||pg.ch!=1080||!query(q,bs)||bs.windowed||!bs.usrActive||bs.restoreFailures!=0){
 fwprintf(ev,L"START_BORDERLESS_POPUP_RECOVERY=FAIL game=%ux%u style=%llx presenter=%ux%u windowed=%u usr=%u restores=%llu\n",base.cw,base.ch,(unsigned long long)base.style,pg.cw,pg.ch,bs.windowed,bs.usrActive,bs.restoreFailures);restore(rb);fclose(ev);return 19;
}
fwprintf(ev,L"START_BORDERLESS_POPUP_RECOVERY=PASS game=%ux%u presenter=%ux%u restores=%llu\n",base.cw,base.ch,pg.cw,pg.ch,bs.restoreFailures);fflush(ev);
if(!set_pref(0)||!wait_state(d,q,true,10000)){restore(rb);fclose(ev);return 20;}for(unsigned i=0;i<60;++i)if(!d.frame()){restore(rb);fclose(ev);return 21;}Geometry win{};if(!geom(game,win)||!same(win,base)||!game_windowed(win)||win.cw!=1280||win.ch!=720){restore(rb);fclose(ev);return 22;}fwprintf(ev,L"POPUP_RECOVERY_TO_WINDOWED=PASS game=%ux%u\n",win.cw,win.ch);fflush(ev);
if(!set_pref(1)||!wait_state(d,q,false,10000)){restore(rb);fclose(ev);return 23;}for(unsigned i=0;i<120;++i)if(!d.frame()){restore(rb);fclose(ev);return 24;}Geometry bor{};if(!geom(game,bor)||!same(bor,base)||!game_windowed(bor)||!geom(presenter,pg)||pg.cw!=1920||pg.ch!=1080||!query(q,bs)||bs.restoreFailures!=0){restore(rb);fclose(ev);return 25;}fwprintf(ev,L"WINDOWED_TO_BORDERLESS_GAME_HWND_STABLE=PASS\n");fflush(ev);
if(!set_pref(0)||!wait_state(d,q,true,10000)){restore(rb);fclose(ev);return 26;}for(unsigned i=0;i<60;++i)if(!d.frame()){restore(rb);fclose(ev);return 27;}Geometry afterPref{};if(!geom(game,afterPref)||!same(afterPref,base)||!query(q,bs)||bs.restoreFailures!=0){restore(rb);fclose(ev);return 28;}fwprintf(ev,L"POPUP_RECOVERY_PREF_ROUNDTRIP=PASS\n");fflush(ev);
'''
s=s[:a]+repl+s[b:]
old_b='if(!geom(presenter,pg)||pg.cw!=1920||pg.ch!=1080){fwprintf(ev,L"FAIL cycle=%u presenter=%ux%u\\n",i,pg.cw,pg.ch);restore(rb);fclose(ev);return 32;}'
new_b='Geometry gb{};if(!geom(presenter,pg)||pg.cw!=1920||pg.ch!=1080||!geom(game,gb)||!same(gb,base)||!game_windowed(gb)){fwprintf(ev,L"FAIL cycle=%u borderless game=%ux%u presenter=%ux%u\\n",i,gb.cw,gb.ch,pg.cw,pg.ch);restore(rb);fclose(ev);return 32;}'
if old_b not in s: raise RuntimeError('borderless cycle anchor missing')
s=s.replace(old_b,new_b,1)
OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_text(s,encoding='utf-8',newline='\n')
print('RC59_POPUP_HOST_SOURCE=PASS')
