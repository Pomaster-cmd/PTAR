from pathlib import Path
import re, sys

OLD='0201c9ae7c1b02163a7960e26effebedd9f6d478987da63a19412af7b758cad3'
NEW='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'

def read(p): return p.read_text(encoding='utf-8-sig')
def write(p,s): p.parent.mkdir(parents=True,exist_ok=True); p.write_text(s,encoding='utf-8-sig',newline='')
def repl(s,a,b,label):
    if a not in s: raise RuntimeError('missing '+label)
    return s.replace(a,b,1)
def add_array(s,var,old,new):
    pat=r'(\$'+re.escape(var)+r'=@\([\s\S]*?\''+re.escape(old)+r"')([\s\S]*?\))"
    m=re.search(pat,s)
    if not m: raise RuntimeError('array '+var)
    block=m.group(0)
    if new in block: return s
    changed=block.replace("'"+old+"'", "'"+old+"',\n '"+new+"'",1)
    return s[:m.start()]+changed+s[m.end():]

def transform(src,out):
    out.mkdir(parents=True,exist_ok=True)

    p=src/'install.ps1'; s=read(p)
    s=repl(s,f"$ExpectedBorderless='{OLD}'",f"$ExpectedBorderless='{NEW}'",'install expected')
    s=add_array(s,'KnownBorderless',OLD,NEW)
    s=s.replace("package='PTAR_RC48_WINDOWED_CHILD_PRESENTER'","package='PTAR_RC51_PRESENTER_SWAPCHAIN_SYNC'")
    s=s.replace("F 'Payload RC48 hash mismatch.'","F 'Payload RC51 hash mismatch.'")
    s=s.replace("L 'INSTALL_RC48=PASS'","L 'INSTALL_RC51=PASS'")
    s=s.replace("L 'MODE=RC48_WINDOWED_CHILD_PRESENTER_WITH_RC42_RASTER'","L 'MODE=RC51_PRESENTER_SWAPCHAIN_SYNC_WITH_RC46_GEOMETRY_AND_RC42_RASTER'")
    write(out/p.name,s)

    p=src/'verify.ps1'; s=read(p)
    s=repl(s,f"$ExpectedBorderless='{OLD}'",f"$ExpectedBorderless='{NEW}'",'verify expected')
    s=s.replace('VERIFY_RC48_WINDOWED_CHILD_PRESENTER=PASS','VERIFY_RC51_PRESENTER_SWAPCHAIN_SYNC=PASS')
    write(out/p.name,s)

    p=src/'rollback.ps1'; s=add_array(read(p),'KnownBorder',OLD,NEW); s=s.replace('[PASS] RC48 retire;','[PASS] RC51 retire;'); write(out/p.name,s)
    p=src/'uninstall_overlay.ps1'; s=add_array(read(p),'borderExpected',OLD,NEW); s=s.replace('sidecars/additifs RC48 nettoyes','sidecars/additifs RC51 nettoyes'); write(out/p.name,s)

    p=src/'collect_rc41.ps1'; s=read(p)
    for a,b in [
      ('PTAR_RC48_DIAG_','PTAR_RC51_DIAG_'),("'RC48_DIAGNOSTIC=1'","'RC51_DIAGNOSTIC=1'"),("'RC48_BORDERLESS_SHA256='","'RC51_BORDERLESS_SHA256='"),
      ('RC48_ACTIVE_PAYLOAD_SHA256.txt','RC51_ACTIVE_PAYLOAD_SHA256.txt'),('RC48_BORDERLESS_LOG_ABSENT.txt','RC51_BORDERLESS_LOG_ABSENT.txt'),('RC48_RASTER_LOG_ABSENT.txt','RC51_RASTER_LOG_ABSENT.txt'),('RC48_BOOTSTRAP_LOG_ABSENT.txt','RC51_BOOTSTRAP_LOG_ABSENT.txt'),('PTAR_RC48_RESULTS_','PTAR_RC51_RESULTS_'),('RESULTAT_RC48=','RESULTAT_RC51='),('journaux RC48/RC42 raster/bootstrap','journaux RC51/RC42 raster/bootstrap')]: s=s.replace(a,b)
    write(out/p.name,s)

if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: build_rc51_scripts.py RC48_SCRIPT_DIR OUTPUT_DIR')
    transform(Path(sys.argv[1]),Path(sys.argv[2]))
