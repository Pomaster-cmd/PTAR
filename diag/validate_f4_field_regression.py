#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,sys
ROOT=Path(__file__).resolve().parents[1]
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d)));print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
src=(ROOT/'diag/visible_pacing/PTARVisiblePacingVerifier.cs').read_text(errors='replace')
run=(ROOT/'diag/visible_pacing/run_single_engine_verifier.ps1').read_text(errors='replace')
arm=(ROOT/'03-ARM_VISIBLE_FRAME_VERIFIER.bat').read_text(errors='replace')
finding=(ROOT/'diag/PACINGVERIFIER2_F3_FIELD_FINDING.txt').read_text(errors='replace')
manifest=(ROOT/'diag/BUILD_MANIFEST.json').read_text(errors='replace')
expected='2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d'
ck('diagnostic engine bundled with GW15',sha(ROOT/'payload/d3d11.dll')==expected,sha(ROOT/'payload/d3d11.dll'))
for token in ['capture rate: 59.849 Hz','marker valid samples: 0','low-contrast samples: 1197','marker rendered: 3373','marker accepted by PTAR: 3373']:
    ck('F3 field evidence '+token,token in finding)
ck('dedicated EXE target',"'/target:exe'" in run and "'/platform:x64'" in run)
ck('no powershell assembly host','[Reflection.Assembly]' not in run and '$runMethod' not in run)
ck('DPI awareness API present','SetProcessDPIAware' in src and 'IsProcessDPIAware' in src)
ck('DPI awareness precedes Run in Main',src.index('EnsureDpiAware()') < src.index('return Run(gameRoot, duration)'))
ck('target selftest executes EXE',"& $tmpExe '--selftest'" in run)
ck('selftest checks DPI','SELFTEST=FAIL DPI_AWARE=NO' in src and 'SELFTEST=PASS DPI_AWARE=YES' in src)
ck('marker presence gate exists','bool markerSeen = false' in src and 'return 43' in src)
ck('marker gate precedes measurement beep',src.index('bool markerSeen = false') < src.index('SafeBeep(900, 140)'))
ck('start beep precedes timer start',src.index('SafeBeep(900, 140)') < src.index('QueryPerformanceCounter(out startQpc)'))
ck('end beep follows measurement complete',src.index('MEASUREMENT COMPLETE') < src.index('SafeBeep(1400, 180)'))
ck('error beep distinct','SafeBeep(320, 250)' in src)
ck('no false even verdict on zero marker','valid == 0 || frames.Count < 3' in src and 'INVALID - MARKER NOT CAPTURED' in src)
ck('launcher documents beeps','BIP 900 Hz' in arm and 'BIP 1400 Hz' in arm)
ck('short package name','PTAR_GW15_LOCK30.zip' in manifest)
ck('one BitBlt site',src.count('BitBlt(memDC')==1,src.count('BitBlt(memDC'))
bad=[n for n,v,_ in checks if not v]
print('F4_FIELD_REGRESSION_VALIDATION=%d/%d %s'%(len(checks)-len(bad),len(checks),'PASS' if not bad else 'FAIL'))
if bad:sys.exit(1)
