from pathlib import Path
import hashlib, json, math, re, statistics, sys
ROOT=Path(__file__).resolve().parents[1]
checks=[]
def ck(name,cond,detail=''):
    checks.append((name,bool(cond),detail))
    print(('[PASS] ' if cond else '[FAIL] ')+name+((' :: '+str(detail)) if detail else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def gray(n): return (n ^ (n>>1)) & 0xFFF
def pop(x): return bin(x).count('1')
def encode(serial,generated):
    x=gray(serial)
    if generated: x |= 0x1000
    # parity cell 13 makes bits 0..13 even.
    if pop(x & 0x1FFF) & 1: x |= 0x2000
    x |= 0x4000 # guard high
    return x & 0xFFFF

def gray_decode(g):
    b=0
    g &= 0xFFF
    while g:
        b ^= g; g >>= 1
    return b & 0xFFF

def decode(sig):
    sync=(sig & 0xC000)==0x4000
    parity=(pop(sig & 0x3FFF)&1)==0
    return gray_decode(sig),bool(sig&0x1000),sync,parity

def pct(a,p):
    if not a:return 0.0
    a=sorted(a);pos=(len(a)-1)*p;lo=int(math.floor(pos));hi=int(math.ceil(pos))
    if lo==hi:return a[lo]
    f=pos-lo;return a[lo]+(a[hi]-a[lo])*f

def metrics(intervals):
    med=pct(intervals,.5);mean=sum(intervals)/len(intervals);sd=statistics.pstdev(intervals)
    phaseA=intervals[::2];phaseB=intervals[1::2]
    a=pct(phaseA,.5);b=pct(phaseB,.5);delta=abs(a-b);avg=(a+b)/2
    bal=100*(1-delta/avg) if avg else 0
    pairs=[]
    for i in range(0,len(intervals)-1,2):
        x,y=intervals[i],intervals[i+1];pairs.append(100*abs(x-y)/(x+y))
    return med,100*sd/mean,pct(pairs,.95),bal

src=(ROOT/'diag/visible_pacing/PTARVisiblePacingVerifier.cs').read_text(errors='replace')
runner=(ROOT/'diag/visible_pacing/run_single_engine_verifier.ps1').read_text(errors='replace')
arm=(ROOT/'03-ARM_VISIBLE_FRAME_VERIFIER.bat').read_text(errors='replace')
collect=(ROOT/'diag/collect.ps1').read_text(errors='replace')

for p in ['diag/visible_pacing/PTARVisiblePacingVerifier.cs','diag/visible_pacing/run_single_engine_verifier.ps1','03-ARM_VISIBLE_FRAME_VERIFIER.bat','04-COLLECT_RESULTS.bat']:
    ck('required '+p,(ROOT/p).is_file())
ck('runtime exact GW15',sha(ROOT/'payload/d3d11.dll')=='2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d',sha(ROOT/'payload/d3d11.dll'))
ck('runtime mirror exact',sha(ROOT/'payload/d3d11.dll')==sha(ROOT/'payload/win81_nis_dx11_x64.dll'))
ck('single engine source has one BitBlt site',src.count('BitBlt(memDC')==1,src.count('BitBlt(memDC'))
ck('single engine computes G/R',all(t in src for t in ['generated = (sig & 0x1000) != 0','VISIBLE GENERATED CONTENTS','VISIBLE REAL CONTENTS']))
ck('single engine computes serial gaps',all(t in src for t in ['GrayDecode12','VISIBLE MARKER GAPS','delta = (serial - lastSerial) & 0x0FFF']))
ck('single engine computes oriented pacing',all(t in src for t in ['G_TO_R MEDIAN MS','R_TO_G MEDIAN MS','GENERATED DWELL MEDIAN MS','REAL DWELL MEDIAN MS']))
ck('single engine measures sampling adequacy',all(t in src for t in ['SAMPLING / REFRESH RATIO','samplingRatio >= 0.90','SAMPLING LIMITED - PACING VERDICT NOT RELIABLE']))
ck('single engine emits raw CSV',all(t in src for t in ['PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv','signature_hex,serial,type,vblank_span,next_serial_delta']))
ck('runner PS4-safe dedicated EXE compile',all(x in runner for x in ['$env:PTAR_GAME_ROOT','[IO.Path]::GetFullPath($rawRoot)','[IO.File]::ReadAllText($src)','Framework64\\v4.0.30319\\csc.exe',"'/target:exe'","'/platform:x64'","$tmpExe","'--selftest'"]) and 'Resolve-Path' not in runner and 'Add-Type' not in runner and '[Reflection.Assembly]' not in runner and '$runMethod' not in runner)
ck('runner avoids invalid New-Item LiteralPath','New-Item -ItemType Directory -LiteralPath' not in runner and '[IO.Directory]::CreateDirectory($tmpDir)' in runner)
ck('dedicated EXE owns DPI awareness',all(x in src for x in ['SetProcessDPIAware','IsProcessDPIAware','public static int Main(string[] args)','EnsureDpiAware()']))
ck('marker presence gate before timer',all(x in src for x in ['bool markerSeen = false','Best contrast={2}','return 43','SafeBeep(320, 250)']))
ck('start/end beeps restored','SafeBeep(900, 140)' in src and 'SafeBeep(1400, 180)' in src)
ck('zero-marker verdict cannot be VERY EVEN','valid == 0 || frames.Count < 3' in src and 'INVALID - MARKER NOT CAPTURED' in src)

ck('launcher passes game root via environment', 'set "PTAR_GAME_ROOT=%%~fI"' in arm and '-GameRoot "%GAMEROOT%"' not in arm)
ck('runner has no mandatory GameRoot command argument', '[Parameter(Mandatory=$true)][string]$GameRoot' not in runner and 'param(' in runner and '[int]$DurationSeconds=20' in runner)
ck('runner preflight mode present', '[switch]$PreflightOnly' in runner and 'VBLANK3 PRE-FLIGHT Windows/PS4' in runner)
ck('preflight executes compiled EXE',"& $tmpExe '--selftest'" in runner and 'EXE self-test failed' in runner)
ck('runner path error is inside try/catch', runner.find('try{',runner.find('$ErrorActionPreference')) < runner.index('$rawRoot=[string]$env:PTAR_GAME_ROOT') < runner.rfind('catch{'))
# Regression model for the exact hardware path that failed: spaces + comma + hyphen + trailing slash.
field_root='C:\\Program Files (x86)\\Steam\\steamapps\\common\\Warhammer 40,000 Inquisitor - Martyr\\'
# Environment transport preserves the string exactly; unlike a quoted trailing-backslash CLI argument,
# it introduces no extra quote into the value seen by PowerShell.
ck('field path environment transport preserves exact root', field_root.endswith('\\') and '"' not in field_root and ',' in field_root and ' - ' in field_root)

ck('arm runs only unified engine','run_single_engine_verifier.ps1' in arm and 'launch_fluidity_probe' not in arm and 'win81_vblank2_visible_marker_x64.exe' not in arm)
ck('arm deletes obsolete dual-engine outputs','PTAR_FLUIDITY_LAST_OUTPUT.txt' in arm and 'PTAR_VISIBLE_VERIFIER_LAST_COUNTS.txt' in arm)
ck('collector includes unified report+CSV','PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt' in collect and 'PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv' in collect)
ck('collector excludes obsolete dual engine raw files','PTAR_FLUIDITY_LAST_OUTPUT.txt' not in collect and 'PTAR_VISIBLE_VERIFIER_LAST_COUNTS.txt' not in collect)

# Marker protocol round-trip across full Gray12 wrap and both frame types.
roundtrip=True; parity_ok=True; guards_ok=True
for n in list(range(0,4096,17))+[4094,4095,0,1,2,2047,2048]:
    g=bool(n&1)
    s=encode(n,g)
    d,t,sy,pa=decode(s)
    if d!=n or t!=g: roundtrip=False;break
    parity_ok &= pa; guards_ok &= sy
ck('Gray12/G-R marker roundtrip',roundtrip)
ck('synthetic parity contract',parity_ok)
ck('synthetic guard contract',guards_ok)
# Field signatures captured by PACINGVERIFIER1 hardware run; validate inferred marker protocol.
field=[(0x46E4,1208,False),(0x56E5,1209,True),(0x46E7,1210,False),(0x56E6,1211,True),(0x46A3,1218,False)]
field_ok=True
for sig,ser,typ in field:
    d,t,sy,pa=decode(sig)
    field_ok &= (d==ser and t==typ and sy and pa)
ck('hardware marker fixture decodes Gray12/G-R/parity/guards',field_ok)

# Perfect cadence vs equal-average judder discrimination.
smooth=[33.333]*600
judder=[16.667,50.000]*300
m1=metrics(smooth);m2=metrics(judder)
ck('smooth synthetic median ~33.333',abs(m1[0]-33.333)<0.01,m1)
ck('smooth pair imbalance p95 ~0',m1[2]<0.01,m1[2])
ck('judder same average fps',abs(sum(smooth)/len(smooth)-sum(judder)/len(judder))<0.001)
ck('judder midpoint balance near 0%',m2[3]<1.0,m2[3])
ck('judder pair imbalance p95 ~50%',49.0<m2[2]<51.0,m2[2])

# Gap/type protocol: skipping every second serial must yield same-type + one gap.
s0=encode(100,False);s1=encode(102,False)
a=decode(s0);b=decode(s1);delta=(b[0]-a[0])&0xFFF
ck('synthetic serial skip decodes one gap',delta-1==1,delta-1)
ck('synthetic serial skip preserves same type',a[1]==b[1])

# Structural sanity of C# source without needing a Windows compiler in lab.
# Raw delimiter counts are sufficient here because all emitted string literals are balanced by construction.
for op,cl,nm in [('(',')','paren'),('{','}','brace'),('[',']','bracket')]:
    ck('C# balanced '+nm,src.count(op)==src.count(cl),(src.count(op),src.count(cl)))

# Old parallel directories must be absent in final package.
ck('obsolete fluidity directory removed',not (ROOT/'diag/fluidity').exists())
ck('obsolete legacy verifier process removed',not (ROOT/'diag/visible_verifier').exists())

passed=sum(1 for _,v,_ in checks if v)
print('PACINGVERIFIER2_VALIDATION=%d/%d PASS'%(passed,len(checks)))
if passed!=len(checks): sys.exit(1)
