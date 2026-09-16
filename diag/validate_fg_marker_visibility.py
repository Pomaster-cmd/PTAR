#!/usr/bin/env python3
"""Validate the FG VBlank marker ON/OFF diagnostic control without touching HUD/FPS."""
from pathlib import Path
import re, sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; ini=ROOT/'payload/win81_nis.ini'; ps=D/'set_vblank_diagnostics.ps1'; bat=D/'FG_MARKER_VISIBILITY.bat'; ver=D/'verify.ps1'
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
base=ini.read_text(errors='replace').splitlines(); pst=ps.read_text(errors='replace'); batt=bat.read_text(errors='replace'); vt=ver.read_text(errors='replace')
keys=[x for x in base if re.match(r'^\s*VBlankDiagnostics\s*=',x)]
ck('payload has one VBlankDiagnostics key',len(keys)==1,keys)
ck('payload default marker enabled',keys==['VBlankDiagnostics=1'],keys)
ck('payload HUD enabled independently','Overlay=1' in base)
ck('menu STATUS action','-Value -1' in batt)
ck('menu ON action','-Value 1' in batt)
ck('menu OFF action','-Value 0' in batt)
ck('menu says HUD/FPS stays active','HUD' in batt and 'FPS' in batt)
ck('helper targets only VBlankDiagnostics','VBlankDiagnostics' in pst and 'Overlay=' not in pst)
ck('helper makes backup','Copy-Item' in pst and 'backups' in pst)
ck('helper readback uses GetPrivateProfileIntW','GetPrivateProfileIntW' in pst)
ck('helper no destructive remove','Remove-Item' not in pst)
ck('verifier explicitly supports marker toggle','marker toggle supported' in vt and 'VBlankDiagnostics=<SUPPORTED_TOGGLE>' in vt)

def normalize(lines):
    out=[]; cnt=0; val=None; invalid=False
    in_sec=False
    for line in lines:
        st=line.strip()
        if st.startswith('[') and st.endswith(']'):
            in_sec=(st.lower()=='[win81_nis]')
        if in_sec and re.match(r'^\s*VBlankDiagnostics\s*=',line,re.I):
            cnt+=1
            m=re.match(r'^\s*VBlankDiagnostics\s*=\s*([01])\s*$',line,re.I)
            if not m: invalid=True; out.append(line)
            else: val=int(m.group(1)); out.append('VBlankDiagnostics=<SUPPORTED_TOGGLE>')
        else: out.append(line)
    return out,cnt,val,invalid

def accepted(candidate):
    b,bc,bv,bi=normalize(base); c,cc,cv,ci=normalize(candidate)
    return cc==1 and cv in (0,1) and not ci and b==c

ck('verify model accepts default marker ON',accepted(list(base)))
off=list(base); off[off.index('VBlankDiagnostics=1')]='VBlankDiagnostics=0'
ck('verify model accepts marker OFF only',accepted(off))
bad=list(off); bad[bad.index('Overlay=1')]='Overlay=0'
ck('verify model rejects unrelated HUD change',not accepted(bad))
dup=list(off); dup.insert(dup.index('VBlankDiagnostics=0')+1,'VBlankDiagnostics=1')
ck('verify model rejects duplicate marker key',not accepted(dup))
invalid=list(base); invalid[invalid.index('VBlankDiagnostics=1')]='VBlankDiagnostics=2'
ck('verify model rejects invalid marker value',not accepted(invalid))
failed=[x for x in checks if not x[1]]
print('FG_MARKER_VISIBILITY_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
