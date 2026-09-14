from pathlib import Path
import sys

OLD='fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d'
NEW='dc23f3bbb2780b92809d5ae218d5cf42186682985e1be7608e7e9ee812ddb80e'

def read(p): return p.read_text(encoding='utf-8-sig')
def write(p,s): p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8-sig',newline='')
def one(s,a,b,label):
    n=s.count(a)
    if n!=1: raise RuntimeError(f'{label}: expected 1 got {n}')
    return s.replace(a,b,1)

def add_known_raster(s,label):
    needle=" 'fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d'"
    if needle not in s: raise RuntimeError(label+' raster known anchor missing')
    # Only append when not already present.
    if NEW not in s:
        s=s.replace(needle,needle+",\n '"+NEW+"'",1)
    return s

def transform(src,out):
    out.mkdir(parents=True,exist_ok=True)

    p=src/'install.ps1';s=read(p)
    s=one(s,"$ExpectedRaster='"+OLD+"'","$ExpectedRaster='"+NEW+"'",'install expected raster')
    s=add_known_raster(s,'install')
    s=one(s,"package='PTAR_RC53_GUI_RESOLUTION_SYNC'","package='PTAR_RC54_GUI_DOMAIN_PROBE'",'install package')
    s=s.replace("Payload RC53 hash mismatch.","Payload RC54 hash mismatch.")
    s=s.replace("INSTALL_RC53=PASS","INSTALL_RC54=PASS")
    s=s.replace("RC53_RASTER_SIDECAR_SHA256=","RC54_RASTER_SIDECAR_SHA256=")
    s=s.replace("MODE=RC53_GUI_RESOLUTION_SYNC_WITH_RC52_RASTER_AND_RC51_PRESENTER","MODE=RC54_GUI_DOMAIN_PROBE_TRACE_ONLY_WITH_RC52_BEHAVIOR")
    write(out/p.name,s)

    p=src/'verify.ps1';s=read(p)
    s=one(s,"$ExpectedRaster='"+OLD+"'","$ExpectedRaster='"+NEW+"'",'verify expected raster')
    s=s.replace('VERIFY_RC53_GUI_RESOLUTION_SYNC=PASS','VERIFY_RC54_GUI_DOMAIN_PROBE=PASS')
    write(out/p.name,s)

    p=src/'rollback.ps1';s=read(p)
    s=add_known_raster(s,'rollback')
    s=s.replace('[PASS] RC53 retire;','[PASS] RC54 retire;')
    write(out/p.name,s)

    p=src/'uninstall_overlay.ps1';s=read(p)
    # uninstall has the same known-raster list under $rasterExpected.
    s=add_known_raster(s,'uninstall')
    s=s.replace('sidecars/additifs RC53 nettoyes','sidecars/additifs RC54 nettoyes')
    write(out/p.name,s)

    p=src/'collect_rc41.ps1';s=read(p)
    s=s.replace('PTAR_RC53_DIAG_','PTAR_RC54_DIAG_')
    s=s.replace('RC53_DIAGNOSTIC=1','RC54_DIAGNOSTIC=1')
    s=s.replace('RC53_BORDERLESS_SHA256=','RC54_BORDERLESS_SHA256=')
    s=s.replace('RC53_RASTER_SIDECAR_SHA256=','RC54_RASTER_SIDECAR_SHA256=')
    s=s.replace('RC53_ACTIVE_PAYLOAD_SHA256.txt','RC54_ACTIVE_PAYLOAD_SHA256.txt')
    s=s.replace('RC53_BORDERLESS_LOG_ABSENT.txt','RC54_BORDERLESS_LOG_ABSENT.txt')
    s=s.replace('RC53_RASTER_LOG_ABSENT.txt','RC54_RASTER_LOG_ABSENT.txt')
    s=s.replace('RC53_BOOTSTRAP_LOG_ABSENT.txt','RC54_BOOTSTRAP_LOG_ABSENT.txt')
    s=s.replace('PTAR_RC53_RESULTS_','PTAR_RC54_RESULTS_')
    s=s.replace('RESULTAT_RC53=','RESULTAT_RC54=')
    s=s.replace('journaux RC53 raster/bootstrap','journaux RC54 raster/bootstrap/probe')
    first="@((Join-Path $g 'ptar_borderless_rc38.log'),'ptar_borderless_rc38.log'),@((Join-Path $g 'ptar_rc41.log'),'ptar_rc41.log'),@((Join-Path $g 'ptar_rc41_bootstrap.log'),'ptar_rc41_bootstrap.log'),@($tmp,'RC54_ACTIVE_PAYLOAD_SHA256.txt')"
    repl="@((Join-Path $g 'ptar_borderless_rc38.log'),'ptar_borderless_rc38.log'),@((Join-Path $g 'ptar_rc41.log'),'ptar_rc41.log'),@((Join-Path $g 'ptar_rc41_bootstrap.log'),'ptar_rc41_bootstrap.log'),@((Join-Path $g 'ptar_rc54_gui_probe.log'),'ptar_rc54_gui_probe.log'),@($tmp,'RC54_ACTIVE_PAYLOAD_SHA256.txt')"
    s=one(s,first,repl,'collector probe inclusion')
    second="@((Join-Path $g 'ptar_borderless_rc38.log'),'RC54_BORDERLESS_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41.log'),'RC54_RASTER_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41_bootstrap.log'),'RC54_BOOTSTRAP_LOG_ABSENT.txt')"
    repl2=second[:-1]+",@((Join-Path $g 'ptar_rc54_gui_probe.log'),'RC54_GUI_PROBE_LOG_ABSENT.txt'))"
    s=one(s,second,repl2,'collector absent probe')
    write(out/p.name,s)

if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: build_rc54_scripts.py RC53_SCRIPT_DIR OUTPUT_DIR')
    transform(Path(sys.argv[1]),Path(sys.argv[2]))
