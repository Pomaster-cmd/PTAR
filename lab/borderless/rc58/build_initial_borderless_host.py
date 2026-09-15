from pathlib import Path

SRC = Path('lab/borderless/rc58/ptar_rc58_mode_bridge_host.cpp')
OUT = Path('lab/borderless/rc58/ptar_rc58_initial_borderless_host.cpp')

src = SRC.read_text(encoding='utf-8')
src = src.replace('L"RC58_MODE_BRIDGE_STRESS.txt"', 'L"RC58_INITIAL_BORDERLESS_STRESS.txt"', 1)
old = 'if(!set_pref(0)){fclose(ev);return 10;}'
new = 'if(!set_pref(1)){fclose(ev);return 10;}'
if old not in src:
    raise RuntimeError('initial preference anchor missing')
src = src.replace(old, new, 1)

start = 'if(!wait_state(d,q,true,15000)){restore(rb);fclose(ev);return 17;}'
end = 'constexpr unsigned cycles=500;'
a = src.find(start)
b = src.find(end, a)
if a < 0 or b < 0:
    raise RuntimeError('startup block anchors missing')

repl = r'''if(!wait_state(d,q,false,15000)){restore(rb);fclose(ev);return 17;}
for(unsigned i=0;i<240;++i)if(!d.frame()){restore(rb);fclose(ev);return 18;}
Geometry base{},pg{};BridgeState bs{};
if(!geom(presenter,pg)||pg.cw!=1920||pg.ch!=1080||!query(q,bs)||bs.windowed||!bs.usrActive||bs.restoreFailures!=0){
    fwprintf(ev,L"START_BORDERLESS=FAIL presenter=%ux%u windowed=%u usr=%u restores=%llu\n",pg.cw,pg.ch,bs.windowed,bs.usrActive,bs.restoreFailures);restore(rb);fclose(ev);return 19;
}
fwprintf(ev,L"START_BORDERLESS=PASS presents=240 presenter=%ux%u restores=%llu\n",pg.cw,pg.ch,bs.restoreFailures);fflush(ev);
if(!set_pref(0)||!wait_state(d,q,true,10000)){restore(rb);fclose(ev);return 20;}
for(unsigned i=0;i<60;++i)if(!d.frame()){restore(rb);fclose(ev);return 21;}
if(!geom(game,base)||!game_windowed(base)||base.cw!=1280||base.ch!=720||!query(q,bs)||!bs.windowed||bs.usrActive||bs.restoreFailures!=0){
    fwprintf(ev,L"BORDERLESS_TO_WINDOWED=FAIL game=%ux%u restores=%llu\n",base.cw,base.ch,bs.restoreFailures);restore(rb);fclose(ev);return 22;
}
fwprintf(ev,L"BORDERLESS_TO_WINDOWED=PASS game=%ux%u\n",base.cw,base.ch);fflush(ev);
if(!set_pref(1)||!wait_state(d,q,false,10000)){restore(rb);fclose(ev);return 23;}
for(unsigned i=0;i<120;++i)if(!d.frame()){restore(rb);fclose(ev);return 24;}
if(!geom(presenter,pg)||pg.cw!=1920||pg.ch!=1080||!query(q,bs)||bs.windowed||!bs.usrActive||bs.restoreFailures!=0){restore(rb);fclose(ev);return 25;}
if(!set_pref(0)||!wait_state(d,q,true,10000)){restore(rb);fclose(ev);return 26;}
for(unsigned i=0;i<60;++i)if(!d.frame()){restore(rb);fclose(ev);return 27;}
Geometry afterPref{};
if(!geom(game,afterPref)||!same(afterPref,base)||afterPref.cw!=1280||afterPref.ch!=720||!query(q,bs)||bs.restoreFailures!=0){
    fwprintf(ev,L"INITIAL_BORDERLESS_ROUNDTRIP=FAIL game=%ux%u restores=%llu\n",afterPref.cw,afterPref.ch,bs.restoreFailures);restore(rb);fclose(ev);return 28;
}
fwprintf(ev,L"INITIAL_BORDERLESS_ROUNDTRIP=PASS\n");fflush(ev);
'''

src = src[:a] + repl + src[b:]
OUT.write_text(src, encoding='utf-8', newline='\n')
print('RC58_INITIAL_BORDERLESS_HOST_GENERATED=PASS')
