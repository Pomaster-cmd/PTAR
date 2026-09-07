#!/usr/bin/env python3
import hashlib,re,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; D=ROOT/'diag'; checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def norm_reloc(text):
    out=[]; rx=re.compile(r'^([0-9A-Fa-f]{16})\s+IMAGE_REL_AMD64_REL32\s+(\S+)\s*$')
    for line in text.splitlines():
        m=rx.match(line.strip())
        if m: out.append((m.group(1).lower(),m.group(2)))
    return out
src=D/'auto60_30_wrapper.s'; packaged=D/'auto60_30_wrapper.bin'; relfile=D/'auto60_30_wrapper.relocs.txt'
ck('assembly source present',src.is_file()); ck('packaged .text present',packaged.is_file()); ck('packaged reloc map present',relfile.is_file())
ck('packaged .text size 0x295',packaged.stat().st_size==0x295,packaged.stat().st_size)
s=src.read_text(encoding='utf-8',errors='replace')
ck('RETURNFIX1 preserves governor EAX','mov dword ptr [rsp+0x28], eax' in s and 'mov eax, dword ptr [rsp+0x28]' in s)
ck('FGGATE1 checks actual FG-enabled state','cmp dword ptr [rip+FG_ENABLED], 0' in s and 'je .force_reset' in s)
ck('NOLOCK30 marker present','NOLOCK30_1' in s)
ck('FG activation initializes HIGH mode','mov dword ptr [rip+AUTO_MODE], 0' in s)
ck('FG activation clears backoff','mov dword ptr [rip+AUTO_BACKOFF], 0' in s)
ck('FG activation targets 60','mov dword ptr [rip+FG_TARGET_FPS], 60' in s)
ck('HIGH never branches to LOW','cmp r8, qword ptr [rip+AUTO_DOWN_TICKS]\n    jmp .high_good' in s)
ck('legacy LOW branch retained','mov dword ptr [rip+FG_TARGET_FPS], 30' in s and '.low_mode:' in s)
ck('FG OFF neutral target120 retained','mov dword ptr [rip+FG_TARGET_FPS], 120' in s)
expected_rel=norm_reloc(relfile.read_text(errors='replace'))
ck('packaged reloc count 55',len(expected_rel)==55,len(expected_rel)); ck('all relocations REL32',len(expected_rel)==55)
try:
    clang=subprocess.check_output(['which','clang'],text=True).strip(); objcopy=subprocess.check_output(['which','llvm-objcopy'],text=True).strip(); objdump=subprocess.check_output(['which','llvm-objdump'],text=True).strip()
    ck('clang available',bool(clang),clang); ck('llvm-objcopy available',bool(objcopy),objcopy); ck('llvm-objdump available',bool(objdump),objdump)
    bins=[]; rels=[]
    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        for name in ('a','b'):
            obj=td/(name+'.obj'); raw=td/(name+'.bin')
            r=subprocess.run([clang,'-target','x86_64-pc-windows-msvc','-c',str(src),'-o',str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            ck(name+' assembly compiles',r.returncode==0,r.stdout.strip())
            if r.returncode: continue
            r=subprocess.run([objcopy,'--dump-section','.text='+str(raw),str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            ck(name+' .text extraction',r.returncode==0 and raw.is_file(),r.stdout.strip())
            rr=subprocess.check_output([objdump,'-r',str(obj)],text=True,stderr=subprocess.STDOUT)
            bins.append(raw.read_bytes()); rels.append(norm_reloc(rr))
        ck('two rebuilt .text images byte-identical',len(bins)==2 and bins[0]==bins[1],tuple(hashlib.sha256(x).hexdigest() for x in bins))
        ck('rebuilt .text equals packaged',len(bins)==2 and bins[0]==packaged.read_bytes(),sha(packaged))
        ck('two rebuilt relocation maps identical',len(rels)==2 and rels[0]==rels[1],tuple(len(x) for x in rels))
        ck('rebuilt relocation map equals packaged',len(rels)==2 and rels[0]==expected_rel,len(expected_rel))
except Exception as e: ck('toolchain rebuild audit',False,e)
failed=[x for x in checks if not x[1]]
print('AUTO_WRAPPER_BUILD_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    sys.exit(1)
