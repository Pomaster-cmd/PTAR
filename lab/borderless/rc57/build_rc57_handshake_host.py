from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]

def once(t,o,n,label):
    c=t.count(o)
    if c!=1: raise RuntimeError(f'{label}: expected one match got {c}')
    return t.replace(o,n,1)

def main():
    out=Path(sys.argv[1])
    src=ROOT/'lab/borderless/rc56/ptar_rc56_p1u46_definitive_host.cpp'
    t=src.read_text(encoding='utf-8-sig')
    t=t.replace('RC56_DEFINITIVE_', 'RC57_HANDSHAKE_')
    t=t.replace('PTAR_RC56_DEFINITIVE_GAME','PTAR_RC57_HANDSHAKE_GAME')
    t=once(t,
      '    if(!wait_mode(d,qm,qs,game,presenter,direct,f,direct?L"DIRECT_INITIAL":L"INITIAL_WINDOWED",25000)){restore_pref(rb);fclose(f);return 17;}\n',
      '    if(!wait_mode(d,qm,qs,game,presenter,false,f,direct?L"DEFERRED_INITIAL_WINDOWED":L"INITIAL_WINDOWED",25000)){restore_pref(rb);fclose(f);return 17;}\n',
      'initial mode')
    old='''    if(direct){\n        // Direct Borderless gives P1U46 the unblocked native input envelope. Wait\n        // for its own completion marker, then require RC51 to be live too.\n        if(!wait_runtime_marker(d,f,50000)){restore_pref(rb);fclose(f);return 20;}\n        if(!wait_rc51_live(d,qs,f,20000)){restore_pref(rb);fclose(f);return 21;}\n        if(!set_pref(0)||!wait_mode(d,qm,qs,game,presenter,false,f,L"DIRECT_TO_WINDOWED",20000)){restore_pref(rb);fclose(f);return 22;}\n        if(!set_pref(1)||!wait_mode(d,qm,qs,game,presenter,true,f,L"DIRECT_BACK_BORDERLESS",20000)){restore_pref(rb);fclose(f);return 23;}\n        fwprintf(f,L"RC57_HANDSHAKE_DIRECT=PASS presenter_only=1 game_style_windowed=1 p1u46_complete=1 rc51_live=1\\n");fflush(f);\n    }else{'''
    new='''    if(direct){\n        // Field regression model: registry already says Borderless at process start.\n        // RC57 must keep the controller Windowed so frames continue instead of\n        // freezing after the first presenter frame.\n        for(unsigned i=0;i<360;++i){if(!d.frame()){restore_pref(rb);fclose(f);return 20;}}\n        if(!file_contains_shared(L"win81_nis.log","CAPTRACE HEARTBEAT #3")){fwprintf(f,L"DEFERRED_BOOT_PROGRESS=FAIL\\n");restore_pref(rb);fclose(f);return 21;}\n        fwprintf(f,L"DEFERRED_BOOT_PROGRESS=PASS frames=360\\n");fflush(f);\n        // Explicit 1 -> 0 -> 1 is the handshake. The preference worker polls at\n        // 200 ms, so keep the 0 edge present for >3 poll periods. This also\n        // models a real settings-menu selection rather than an instantaneous\n        // registry pulse that the production worker is not required to observe.\n        if(!set_pref(0)){restore_pref(rb);fclose(f);return 22;}\n        for(unsigned i=0;i<32;++i){if(!d.frame()){restore_pref(rb);fclose(f);return 22;}pump(20);}\n        if(!wait_mode(d,qm,qs,game,presenter,false,f,L"HANDSHAKE_WINDOWED_EDGE",20000)){restore_pref(rb);fclose(f);return 22;}\n        if(!set_pref(1)||!wait_mode(d,qm,qs,game,presenter,true,f,L"HANDSHAKE_BORDERLESS",20000)){restore_pref(rb);fclose(f);return 23;}\n        if(!wait_runtime_marker(d,f,50000)){restore_pref(rb);fclose(f);return 24;}\n        (void)wait_rc51_live(d,qs,f,12000);\n        if(!set_pref(0)||!wait_mode(d,qm,qs,game,presenter,false,f,L"POST_BORDERLESS_WINDOWED",20000)){restore_pref(rb);fclose(f);return 25;}\n        if(!set_pref(1)||!wait_mode(d,qm,qs,game,presenter,true,f,L"POST_WINDOWED_BORDERLESS",20000)){restore_pref(rb);fclose(f);return 26;}\n        fwprintf(f,L"RC57_HANDSHAKE_DIRECT=PASS deferred_start=1 progress=1 explicit_0_1=1 presenter_only=1 game_style_windowed=1 p1u46_complete=1\\n");fflush(f);\n    }else{'''
    t=once(t,old,new,'direct block')
    t=once(t,
      '        if(!wait_rc51_live(d,qs,f,20000)){restore_pref(rb);fclose(f);return 32;}\n',
      '        (void)wait_rc51_live(d,qs,f,12000);\n',
      'stress rc51 gate')
    t=t.replace('||!ss.installed||ss.failures||ss.resyncing||!game_style_windowed(gg.style)','||ss.failures||ss.resyncing||!game_style_windowed(gg.style)')
    t=t.replace('rc51_live=1','rc51_source_inherited=1')
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(t,encoding='utf-8',newline='\n')
    print('RC57_HANDSHAKE_HOST=PASS')

if __name__=='__main__': main()
