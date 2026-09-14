from pathlib import Path
import re, sys

OLD_RASTER='7ffe7aa1b9afa42d5f22557cae29a4f65e17379c2a2f181f879427277839f1ca'
NEW_RASTER='fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d'
CARRIER='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'

def read(p): return p.read_text(encoding='utf-8-sig')
def write(p,s): p.parent.mkdir(parents=True,exist_ok=True); p.write_text(s,encoding='utf-8-sig',newline='')
def repl(s,a,b,label):
    if a not in s: raise RuntimeError('missing '+label)
    return s.replace(a,b,1)
def add_array(s,var,old,new):
    pat=r'(\$'+re.escape(var)+r'=@\([\s\S]*?\''+re.escape(old)+r"')([\s\S]*?\))"
    m=re.search(pat,s)
    if m:
        block=m.group(0)
        if new in block: return s
        changed=block.replace("'"+old+"'", "'"+old+"',\n '"+new+"'",1)
        return s[:m.start()]+changed+s[m.end():]
    # compact one-line arrays are used for KnownRaster/rasterExpected
    pat2=r'(\$'+re.escape(var)+r"=@\([^\r\n]*?'"+re.escape(old)+r"')([^\r\n]*?\))"
    m=re.search(pat2,s)
    if not m: raise RuntimeError('array '+var)
    block=m.group(0)
    if new in block: return s
    changed=block.replace("'"+old+"'", "'"+old+"','"+new+"'",1)
    return s[:m.start()]+changed+s[m.end():]

def transform(src,out):
    out.mkdir(parents=True,exist_ok=True)

    p=src/'install.ps1'; s=read(p)
    s=repl(s,f"$ExpectedRaster='{OLD_RASTER}'",f"$ExpectedRaster='{NEW_RASTER}'",'install raster expected')
    s=add_array(s,'KnownRaster',OLD_RASTER,NEW_RASTER)
    s=s.replace("package='PTAR_RC51_PRESENTER_SWAPCHAIN_SYNC'","package='PTAR_RC52_RASTER_STATE_QUARANTINE'")
    s=s.replace("F 'Payload RC51 hash mismatch.'","F 'Payload RC52 hash mismatch.'")
    s=s.replace("L 'INSTALL_RC51=PASS'","L 'INSTALL_RC52=PASS'")
    s=s.replace("L ('RC42_RASTER_SIDECAR_SHA256='+$ExpectedRaster)","L ('RC52_RASTER_SIDECAR_SHA256='+$ExpectedRaster)")
    s=s.replace("L 'MODE=RC51_PRESENTER_SWAPCHAIN_SYNC_WITH_RC46_GEOMETRY_AND_RC42_RASTER'","L 'MODE=RC52_PRESENTER_SYNC_WITH_RASTER_STATE_QUARANTINE'")
    write(out/p.name,s)

    p=src/'verify.ps1'; s=read(p)
    s=repl(s,f"$ExpectedRaster='{OLD_RASTER}'",f"$ExpectedRaster='{NEW_RASTER}'",'verify raster expected')
    s=s.replace('VERIFY_RC51_PRESENTER_SWAPCHAIN_SYNC=PASS','VERIFY_RC52_RASTER_STATE_QUARANTINE=PASS')
    write(out/p.name,s)

    p=src/'rollback.ps1'; s=add_array(read(p),'KnownRaster',OLD_RASTER,NEW_RASTER)
    s=s.replace('[PASS] RC51 retire;','[PASS] RC52 retire;')
    write(out/p.name,s)

    p=src/'uninstall_overlay.ps1'; s=add_array(read(p),'rasterExpected',OLD_RASTER,NEW_RASTER)
    s=s.replace('sidecars/additifs RC51 nettoyes','sidecars/additifs RC52 nettoyes')
    write(out/p.name,s)

    p=src/'collect_rc41.ps1'; s=read(p)
    for a,b in [
      ('PTAR_RC51_DIAG_','PTAR_RC52_DIAG_'),("'RC51_DIAGNOSTIC=1'","'RC52_DIAGNOSTIC=1'"),
      ("'RC51_BORDERLESS_SHA256='","'RC52_BORDERLESS_SHA256='"),("'RC42_RASTER_SIDECAR_SHA256='","'RC52_RASTER_SIDECAR_SHA256='"),
      ('RC51_ACTIVE_PAYLOAD_SHA256.txt','RC52_ACTIVE_PAYLOAD_SHA256.txt'),('RC51_BORDERLESS_LOG_ABSENT.txt','RC52_BORDERLESS_LOG_ABSENT.txt'),
      ('RC51_RASTER_LOG_ABSENT.txt','RC52_RASTER_LOG_ABSENT.txt'),('RC51_BOOTSTRAP_LOG_ABSENT.txt','RC52_BOOTSTRAP_LOG_ABSENT.txt'),
      ('PTAR_RC51_RESULTS_','PTAR_RC52_RESULTS_'),('RESULTAT_RC51=','RESULTAT_RC52='),('journaux RC51/RC42 raster/bootstrap','journaux RC52 raster/bootstrap')]:
        s=s.replace(a,b)
    write(out/p.name,s)

if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: build_rc52_scripts.py RC51_SCRIPT_DIR OUTPUT_DIR')
    transform(Path(sys.argv[1]),Path(sys.argv[2]))
