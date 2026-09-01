#!/usr/bin/env python3
"""STRICTPAIR Gate D pass 3b: compact destructive-path audit.

Read-only.  The goal is intentionally narrow:
  F8 diagnostic label -> backing counter -> every executable writer ->
  local branch predicate + direct callers of the writer's pdata function.

Important: injected executable sections contain alignment/data islands. Capstone
skipdata is enabled so decoding continues after those islands; otherwise the
F8 diagnostic routine near the end of .fgdia is silently missed.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_WRITE
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP, X86_REG_R8D

IMAGE_BASE = 0x180000000
EXECUTE = 0x20000000

TARGET_LABELS = [
    "SOURCE QUEUE FULL",
    "GENERATED BUSY DROPS",
    "LATE MIDPOINT DROPS",
    "PRESENT QUEUE DROPS",
    "MAILBOX GENERATED DROPS",
    "NO-FREE-JOB DROPS",
    "PUBLISHER GENERATED COALESCED",
    "REAL_ONLY",
]
REFERENCE_LABELS = ["SOURCE SUBMITS", "SOURCE FPS"]


def u16(b,o): return struct.unpack_from("<H",b,o)[0]
def u32(b,o): return struct.unpack_from("<I",b,o)[0]
def u64(b,o): return struct.unpack_from("<Q",b,o)[0]


class PE:
    def __init__(self,b:bytes):
        self.b=b
        if b[:2]!=b"MZ": raise RuntimeError("MZ missing")
        p=u32(b,0x3C)
        if b[p:p+4]!=b"PE\0\0": raise RuntimeError("PE missing")
        coff=p+4
        if u16(b,coff)!=0x8664: raise RuntimeError("not x64")
        n=u16(b,coff+2); optsz=u16(b,coff+16); opt=coff+20
        if u16(b,opt)!=0x20B: raise RuntimeError("not PE32+")
        self.base=u64(b,opt+24); sh=opt+optsz; self.sections=[]
        for i in range(n):
            o=sh+40*i
            name=b[o:o+8].rstrip(b"\0").decode("ascii","replace")
            vs,va,rs,rp=struct.unpack_from("<IIII",b,o+8); ch=u32(b,o+36)
            self.sections.append(dict(name=name,vs=vs,va=va,rs=rs,rp=rp,exec=bool(ch&EXECUTE)))
    def sec_rva(self,rva):
        for s in self.sections:
            if s["va"]<=rva<s["va"]+max(s["vs"],s["rs"]): return s
        return None
    def off_to_rva(self,off):
        for s in self.sections:
            if s["rp"]<=off<s["rp"]+s["rs"]: return s["va"]+off-s["rp"]
        return None
    def section(self,name):
        for s in self.sections:
            if s["name"]==name:return s
        return None


def pdata(pe):
    s=pe.section(".pdata"); out=[]
    if not s:return out
    raw=pe.b[s["rp"]:s["rp"]+s["rs"]]
    for o in range(0,len(raw)-11,12):
        a,b,u=struct.unpack_from("<III",raw,o)
        if a and a<b:out.append((a,b,u))
    return sorted(out)


def func_of(ranges,rva):
    for a,b,u in ranges:
        if a<=rva<b:return (a,b)
    return None


def rip_refs(ins):
    refs=[]
    # skipdata pseudo-instructions do not necessarily expose normal operands.
    try: ops=ins.operands
    except Exception:return refs
    for op in ops:
        if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
            refs.append((ins.address+ins.size+op.mem.disp,bool(op.access&CS_AC_WRITE)))
    return refs


def direct_call(ins):
    try:
        if ins.mnemonic=="call" and len(ins.operands)==1 and ins.operands[0].type==X86_OP_IMM:
            return ins.operands[0].imm
    except Exception: pass
    return None


def decode(pe):
    md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
    bysec={}; allins=[]
    for s in pe.sections:
        if not s["exec"] or not s["rs"]:continue
        raw=pe.b[s["rp"]:s["rp"]+s["rs"]]
        seq=list(md.disasm(raw,pe.base+s["va"]))
        bysec[s["name"]]=seq; allins.extend(seq)
    allins.sort(key=lambda x:x.address)
    return bysec,allins


def xref_index(allins):
    idx={}
    for ins in allins:
        for va,wr in rip_refs(ins):idx.setdefault(va,[]).append((ins,wr))
    return idx


def str_rvas(pe,text):
    needle=text.encode("ascii");out=[];p=0
    while True:
        p=pe.b.find(needle,p)
        if p<0:break
        r=pe.off_to_rva(p)
        if r is not None:out.append(r)
        p+=1
    return sorted(set(out))


def seq_index(seq,address):
    for i,x in enumerate(seq):
        if x.address==address:return i
    return None


def fmt(pe,ins):
    rva=ins.address-pe.base;s=pe.sec_rva(rva);sec=s["name"] if s else "?"
    return f"{sec}:{rva:#x} {ins.mnemonic} {ins.op_str}"


def infer_counter_from_label_xref(pe,bysec,label_xref):
    rva=label_xref.address-pe.base;s=pe.sec_rva(rva)
    if not s or s["name"] not in bysec:return None
    seq=bysec[s["name"]];i=seq_index(seq,label_xref.address)
    if i is None:return None
    for j in range(i-1,max(-1,i-13),-1):
        q=seq[j]
        try:ops=q.operands
        except Exception:continue
        if q.mnemonic!="mov" or len(ops)<2:continue
        if ops[0].type!=X86_OP_REG or ops[0].reg!=X86_REG_R8D:continue
        refs=rip_refs(q)
        if refs:return refs[0][0]-pe.base
    return None


def local_window(pe,bysec,ins,before=9,after=5):
    s=pe.sec_rva(ins.address-pe.base)
    if not s or s["name"] not in bysec:return []
    seq=bysec[s["name"]];i=seq_index(seq,ins.address)
    return [] if i is None else seq[max(0,i-before):min(len(seq),i+after+1)]


def nearest_predicate(pe,bysec,ins):
    s=pe.sec_rva(ins.address-pe.base)
    if not s or s["name"] not in bysec:return []
    seq=bysec[s["name"]];i=seq_index(seq,ins.address)
    if i is None:return []
    found=[]
    for q in seq[max(0,i-16):i+2]:
        m=q.mnemonic
        if m in ("cmp","test","bt") or (m.startswith("j") and m!="jmp"):found.append(q)
    return found[-8:]


def is_writer(ins,counter_va):
    for va,wr in rip_refs(ins):
        if va!=counter_va:continue
        if wr:return True
        if ins.mnemonic in ("inc","dec","xadd","cmpxchg","or","and","xor","add","sub"):return True
    return False


def direct_callers(pe,allins,fn):
    if not fn:return []
    target=pe.base+fn[0]
    return [ins for ins in allins if direct_call(ins)==target]


def main():
    if len(sys.argv)!=2:raise SystemExit("usage: script DLL")
    path=Path(sys.argv[1]);blob=path.read_bytes();pe=PE(blob)
    if pe.base!=IMAGE_BASE:raise RuntimeError(f"image base {pe.base:#x}")
    ranges=pdata(pe);bysec,allins=decode(pe);xrefs=xref_index(allins)
    print("STRICTPAIR_GATE_D_COMPACT=3B")
    print("SHA256="+hashlib.sha256(blob).hexdigest());print("SIZE="+str(len(blob)))
    print("EXEC_SECTIONS="+",".join(f"{k}:{len(v)}" for k,v in bysec.items()))
    print()

    mapping={}
    print("=== STRING_AND_XREF_SANITY ===")
    for label in TARGET_LABELS+REFERENCE_LABELS:
        srvas=str_rvas(pe,label); candidates=[]; direct_count=0
        for srva in srvas:
            xs=xrefs.get(pe.base+srva,[]);direct_count+=len(xs)
            for ins,wr in xs:
                counter=infer_counter_from_label_xref(pe,bysec,ins)
                if counter is not None:candidates.append((srva,ins,counter))
        bycounter={}
        for c in candidates:bycounter.setdefault(c[2],c)
        mapping[label]=list(bycounter.values())
        print(f"LABEL={label} STRINGS={len(srvas)} DIRECT_XREFS={direct_count} COUNTER_CANDIDATES={len(bycounter)}")
    print()

    print("=== LABEL_TO_COUNTER ===")
    for label in TARGET_LABELS+REFERENCE_LABELS:
        cs=mapping[label]
        if not cs:print(f"LABEL={label} COUNTER=UNRESOLVED")
        for srva,ins,counter in cs:
            print(f"LABEL={label} COUNTER={counter:#x} LABEL_RVA={srva:#x} XREF={ins.address-pe.base:#x}")
    print()

    unresolved=[];multi=[];remote=[]
    print("=== DESTRUCTIVE_WRITERS ===")
    for label in TARGET_LABELS:
        cs=mapping[label]
        if not cs:
            unresolved.append(label);print(f"PATH={label} RESULT=UNRESOLVED_LABEL_COUNTER");continue
        if len(cs)>1:multi.append(label)
        for _,_,counter in cs:
            writers=[ins for ins in allins if is_writer(ins,pe.base+counter)]
            print(f"PATH={label} COUNTER={counter:#x} WRITERS={len(writers)}")
            for wi,w in enumerate(writers,1):
                wrva=w.address-pe.base;fn=func_of(ranges,wrva);preds=nearest_predicate(pe,bysec,w);callers=direct_callers(pe,allins,fn)
                print(f"  WRITER#{wi} {fmt(pe,w)} FUNC="+(f"{fn[0]:#x}-{fn[1]:#x}" if fn else "NO_PDATA"))
                print("    PREDICATES="+(" | ".join(fmt(pe,q) for q in preds) if preds else "NONE_LOCAL"))
                print("    DIRECT_CALLERS="+(" | ".join(fmt(pe,q) for q in callers[:16]) if callers else "NONE"))
                print("    CONTEXT_BEGIN")
                for q in local_window(pe,bysec,w):print("      "+fmt(pe,q))
                print("    CONTEXT_END")
                if not preds:remote.append((label,counter,wrva))
            print()

    print("=== SOURCE_ADMISSION_REFERENCE ===")
    for label in REFERENCE_LABELS:
        for _,_,counter in mapping[label]:
            writers=[ins for ins in allins if is_writer(ins,pe.base+counter)]
            print(f"REF={label} COUNTER={counter:#x} WRITERS={len(writers)}")
            for w in writers:
                wrva=w.address-pe.base;fn=func_of(ranges,wrva)
                print(f"  {fmt(pe,w)} FUNC="+(f"{fn[0]:#x}-{fn[1]:#x}" if fn else "NO_PDATA"))
    print()

    print("=== DECISION_INPUT ===")
    print("UNRESOLVED_LABELS="+(";".join(unresolved) if unresolved else "NONE"))
    print("MULTI_COUNTER_LABELS="+(";".join(multi) if multi else "NONE"))
    print("NO_LOCAL_PREDICATE_WRITERS="+(";".join(f"{a}@{b:#x}/writer={c:#x}" for a,b,c in remote) if remote else "NONE"))
    print("NOTE=FINAL_ARCHITECTURE_DECISION_REQUIRES_MACHINE_CODE_CONTEXT_REVIEW")
    print("AUDIT_COMPLETE=1")

if __name__=="__main__":main()
