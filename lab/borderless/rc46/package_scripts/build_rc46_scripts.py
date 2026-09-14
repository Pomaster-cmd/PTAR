from pathlib import Path
import re, sys

OLD='a9c0eb2ec74ac278c2aaef33d1bca237aa504366b43774f8e624374314a6695c'
NEW='8269d32ccb77b5e65ed159f75ae24266c22b410b8b465a7d504775a937b54890'

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
    s=s.replace("package='PTAR_RC45_WINDOWED_PRESENTER_FOLLOW'","package='PTAR_RC46_WINDOWED_GEOMETRY_GUARD'")
    s=s.replace("F 'Payload RC45 hash mismatch.'","F 'Payload RC46 hash mismatch.'")
    s=s.replace("L 'INSTALL_RC45=PASS'","L 'INSTALL_RC46=PASS'")
    s=s.replace("L 'MODE=RC45_WINDOWED_PRESENTER_FOLLOW_WITH_RC42_RASTER'","L 'MODE=RC46_WINDOWED_GEOMETRY_GUARD_WITH_RC42_RASTER'")
    write(out/p.name,s)

    p=src/'verify.ps1'; s=read(p)
    s=repl(s,f"$ExpectedBorderless='{OLD}'",f"$ExpectedBorderless='{NEW}'",'verify expected')
    s=s.replace('VERIFY_RC45_WINDOWED_PRESENTER_FOLLOW=PASS','VERIFY_RC46_WINDOWED_GEOMETRY_GUARD=PASS')
    write(out/p.name,s)

    p=src/'rollback.ps1'; s=add_array(read(p),'KnownBorder',OLD,NEW); s=s.replace('[PASS] RC45 retire;','[PASS] RC46 retire;'); write(out/p.name,s)
    p=src/'uninstall_overlay.ps1'; s=add_array(read(p),'borderExpected',OLD,NEW); s=s.replace('sidecars/additifs RC45 nettoyes','sidecars/additifs RC46 nettoyes'); write(out/p.name,s)
    p=src/'collect_rc41.ps1'; s=read(p)
    for a,b in [
      ('PTAR_RC45_DIAG_','PTAR_RC46_DIAG_'),("'RC45_DIAGNOSTIC=1'","'RC46_DIAGNOSTIC=1'"),("'RC45_BORDERLESS_SHA256='","'RC46_BORDERLESS_SHA256='"),
      ('RC45_ACTIVE_PAYLOAD_SHA256.txt','RC46_ACTIVE_PAYLOAD_SHA256.txt'),('RC45_BORDERLESS_LOG_ABSENT.txt','RC46_BORDERLESS_LOG_ABSENT.txt'),('RC45_RASTER_LOG_ABSENT.txt','RC46_RASTER_LOG_ABSENT.txt'),('RC45_BOOTSTRAP_LOG_ABSENT.txt','RC46_BOOTSTRAP_LOG_ABSENT.txt'),('PTAR_RC45_RESULTS_','PTAR_RC46_RESULTS_'),('RESULTAT_RC45=','RESULTAT_RC46='),('journaux RC45/RC42 raster/bootstrap','journaux RC46/RC42 raster/bootstrap')]: s=s.replace(a,b)
    write(out/p.name,s)

if __name__=='__main__': transform(Path(sys.argv[1]),Path(sys.argv[2]))
