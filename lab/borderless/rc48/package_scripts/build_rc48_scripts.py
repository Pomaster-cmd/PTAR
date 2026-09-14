from pathlib import Path
import re, sys

OLD='a4f0c2cc95a8e0624d493b402e46f6938c60cb59c1827d2b677f786fe005c61f'
NEW='0201c9ae7c1b02163a7960e26effebedd9f6d478987da63a19412af7b758cad3'

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
    s=s.replace("package='PTAR_RC47_WINDOWED_PRESENTER_OWNER'","package='PTAR_RC48_WINDOWED_CHILD_PRESENTER'")
    s=s.replace("F 'Payload RC47 hash mismatch.'","F 'Payload RC48 hash mismatch.'")
    s=s.replace("L 'INSTALL_RC47=PASS'","L 'INSTALL_RC48=PASS'")
    s=s.replace("L 'MODE=RC47_WINDOWED_PRESENTER_OWNER_WITH_RC42_RASTER'","L 'MODE=RC48_WINDOWED_CHILD_PRESENTER_WITH_RC42_RASTER'")
    write(out/p.name,s)

    p=src/'verify.ps1'; s=read(p)
    s=repl(s,f"$ExpectedBorderless='{OLD}'",f"$ExpectedBorderless='{NEW}'",'verify expected')
    s=s.replace('VERIFY_RC47_WINDOWED_PRESENTER_OWNER=PASS','VERIFY_RC48_WINDOWED_CHILD_PRESENTER=PASS')
    write(out/p.name,s)

    p=src/'rollback.ps1'; s=add_array(read(p),'KnownBorder',OLD,NEW); s=s.replace('[PASS] RC47 retire;','[PASS] RC48 retire;'); write(out/p.name,s)
    p=src/'uninstall_overlay.ps1'; s=add_array(read(p),'borderExpected',OLD,NEW); s=s.replace('sidecars/additifs RC47 nettoyes','sidecars/additifs RC48 nettoyes'); write(out/p.name,s)
    p=src/'collect_rc41.ps1'; s=read(p)
    for a,b in [
      ('PTAR_RC47_DIAG_','PTAR_RC48_DIAG_'),("'RC47_DIAGNOSTIC=1'","'RC48_DIAGNOSTIC=1'"),("'RC47_BORDERLESS_SHA256='","'RC48_BORDERLESS_SHA256='"),
      ('RC47_ACTIVE_PAYLOAD_SHA256.txt','RC48_ACTIVE_PAYLOAD_SHA256.txt'),('RC47_BORDERLESS_LOG_ABSENT.txt','RC48_BORDERLESS_LOG_ABSENT.txt'),('RC47_RASTER_LOG_ABSENT.txt','RC48_RASTER_LOG_ABSENT.txt'),('RC47_BOOTSTRAP_LOG_ABSENT.txt','RC48_BOOTSTRAP_LOG_ABSENT.txt'),('PTAR_RC47_RESULTS_','PTAR_RC48_RESULTS_'),('RESULTAT_RC47=','RESULTAT_RC48='),('journaux RC47/RC42 raster/bootstrap','journaux RC48/RC42 raster/bootstrap')]: s=s.replace(a,b)
    write(out/p.name,s)

if __name__=='__main__': transform(Path(sys.argv[1]),Path(sys.argv[2]))
