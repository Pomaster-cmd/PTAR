#!/usr/bin/env python3
"""STRICTPAIR Gate D pass 6: targeted memory-flow audit.

Read-only machine-code analysis of the exact PTAR runtime.  Pass 5 proved that
candidate counters are not referenced as direct RIP globals.  This pass instead
examines every memory read/write in the critical source/publisher/mailbox
functions and tracks simple register provenance so structure-relative writes
([reg+field]) become visible.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_READ, CS_AC_WRITE
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP

IMAGE_BASE = 0x180000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000

SEEDS = {
    "source_present_wrapper": 0xC6C0,
    "source_present1_wrapper": 0xD590,
    "source_submit_function": 0x127C0,
    "source_submit_failure_writer": 0x12ACC,
    "source_pair_pipeline": 0x12C90,
    "vblank_wait_helper": 0x26400,
    "present_helper": 0x26540,
    "mailbox_selector": 0x267D0,
    "mailbox_selector_failure": 0x26D1D,
    "post_recovery_candidate": 0x27C20,
}

FIELD_OFFSETS = {0x0,0x4,0x8,0xc,0x10,0x14,0x70,0x74,0x94,0xac,0xe8}
WRITE_MNEMONICS = {
    "mov","movzx","movsx","movsxd","inc","dec","add","sub","and","or",
    "xor","not","neg","xadd","xchg","cmpxchg","bts","btr","btc","adc","sbb",
}
VOLATILE = {"rax","rcx","rdx","r8","r9","r10","r11"}

ALIASES = {
    "al":"rax","ah":"rax","ax":"rax","eax":"rax","rax":"rax",
    "bl":"rbx","bh":"rbx","bx":"rbx","ebx":"rbx","rbx":"rbx",
    "cl":"rcx","ch":"rcx","cx":"rcx","ecx":"rcx","rcx":"rcx",
    "dl":"rdx","dh":"rdx","dx":"rdx","edx":"rdx","rdx":"rdx",
    "sil":"rsi","si":"rsi","esi":"rsi","rsi":"rsi",
    "dil":"rdi","di":"rdi","edi":"rdi","rdi":"rdi",
    "bpl":"rbp","bp":"rbp","ebp":"rbp","rbp":"rbp",
    "spl":"rsp","sp":"rsp","esp":"rsp","rsp":"rsp",
}
for n in range(8,16):
    ALIASES[f"r{n}b"] = f"r{n}"
    ALIASES[f"r{n}w"] = f"r{n}"
    ALIASES[f"r{n}d"] = f"r{n}"
    ALIASES[f"r{n}"] = f"r{n}"


def u16(b,o): return struct.unpack_from("<H",b,o)[0]
def u32(b,o): return struct.unpack_from("<I",b,o)[0]
def u64(b,o): return struct.unpack_from("<Q",b,o)[0]


class PE:
    def __init__(self, blob: bytes):
        self.b = blob
        if blob[:2] != b"MZ": raise RuntimeError("MZ missing")
        p = u32(blob,0x3c)
        if blob[p:p+4] != b"PE\0\0": raise RuntimeError("PE missing")
        coff = p+4
        if u16(blob,coff) != 0x8664: raise RuntimeError("not x64")
        nsec = u16(blob,coff+2); optsz = u16(blob,coff+16); opt = coff+20
        if u16(blob,opt) != 0x20b: raise RuntimeError("not PE32+")
        self.base = u64(blob,opt+24)
        sh = opt+optsz
        self.sections=[]
        for i in range(nsec):
            o=sh+i*40
            name=blob[o:o+8].rstrip(b"\0").decode("ascii","replace")
            vs,va,rs,rp=struct.unpack_from("<IIII",blob,o+8)
            ch=u32(blob,o+36)
            self.sections.append({"name":name,"vs":vs,"va":va,"rs":rs,"rp":rp,
                                  "exec":bool(ch & IMAGE_SCN_MEM_EXECUTE)})

    def section(self,name):
        return next((s for s in self.sections if s["name"]==name),None)

    def section_for_rva(self,rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vs"],s["rs"]): return s
        return None


def pdata_ranges(pe):
    s=pe.section(".pdata")
    if not s: return []
    raw=pe.b[s["rp"]:s["rp"]+s["rs"]]
    out=[]
    for o in range(0,len(raw)-11,12):
        a,b,u=struct.unpack_from("<III",raw,o)
        if a and a < b: out.append((a,b,u))
    return sorted(out)


def containing_func(ranges,rva):
    for a,b,u in ranges:
        if a <= rva < b: return (a,b,u)
    return None


def decode(pe):
    md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
    by={}; all_ins=[]
    for s in pe.sections:
        if not s["exec"] or not s["rs"]: continue
        raw=pe.b[s["rp"]:s["rp"]+s["rs"]]
        seq=list(md.disasm(raw,pe.base+s["va"]))
        by[s["name"]]=seq; all_ins.extend(seq)
    return by,sorted(all_ins,key=lambda i:i.address)


def ops(ins):
    try: return ins.operands
    except Exception: return []


def canon(md, reg_id):
    if not reg_id: return None
    try: n=md.reg_name(reg_id)
    except Exception: return None
    return ALIASES.get(n,n)


def direct_call(ins):
    q=ops(ins)
    if ins.mnemonic=="call" and len(q)==1 and q[0].type==X86_OP_IMM: return q[0].imm
    return None


def is_write_operand(ins,idx,op):
    if op.access & CS_AC_WRITE: return True
    return idx==0 and ins.mnemonic in WRITE_MNEMONICS


def rip_target(ins,op):
    if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
        return ins.address+ins.size+op.mem.disp
    return None


def fmt_mem(md,ins,idx,op,prov):
    base=canon(md,op.mem.base); index=canon(md,op.mem.index); disp=op.mem.disp
    rip=rip_target(ins,op)
    rw=("W" if is_write_operand(ins,idx,op) else "R" if op.access & CS_AC_READ else "?")
    if rip is not None:
        return f"{rw}:RIP@{rip-IMAGE_BASE:#x}"
    bp=prov.get(base,"UNKNOWN") if base else "NONE"
    ip=prov.get(index,"UNKNOWN") if index else "NONE"
    return f"{rw}:BASE={base or '-'}({bp}) INDEX={index or '-'}({ip}) SCALE={op.mem.scale} DISP={disp:+#x}"


def func_instructions(pe,by,fn,seed):
    if fn:
        a,b,_=fn
    else:
        # injected/no-pdata fallback: deliberately narrow around seed
        a=max(0,seed-0x120); b=seed+0x280
    s=pe.section_for_rva(seed)
    if not s or s["name"] not in by: return (a,b,[])
    seq=[i for i in by[s["name"]] if a <= i.address-pe.base < b]
    return a,b,seq


def pred_context(seq,idx,n=18):
    out=[]
    for q in seq[max(0,idx-n):idx]:
        m=q.mnemonic
        if m in ("cmp","test","bt") or (m.startswith("j") and m!="jmp"):
            out.append(q)
    return out[-10:]


def ins_text(pe,ins):
    rva=ins.address-pe.base
    s=pe.section_for_rva(rva)
    ct=direct_call(ins)
    x=f"{s['name'] if s else '?'}:{rva:#x} {ins.mnemonic} {ins.op_str}"
    if ct is not None: x += f" ; CALL={ct-pe.base:#x}"
    return x


def provenance_after(md,ins,prov):
    q=ops(ins)
    m=ins.mnemonic
    # calls clobber volatile registers; exact callee effects are intentionally not guessed
    if m=="call":
        for r in VOLATILE: prov.pop(r,None)
        prov["rax"]="CALL_RESULT"
        return
    if not q or q[0].type != X86_OP_REG: return
    dst=canon(md,q[0].reg)
    if not dst: return
    if m=="lea" and len(q)>=2 and q[1].type==X86_OP_MEM:
        op=q[1]; rt=rip_target(ins,op)
        if rt is not None:
            prov[dst]=f"ADDR@{rt-IMAGE_BASE:#x}"
            return
        base=canon(md,op.mem.base); index=canon(md,op.mem.index)
        prov[dst]=f"ADDR[{prov.get(base,base or '-')}{op.mem.disp:+#x};idx={prov.get(index,index or '-')}]"
        return
    if m.startswith("mov") and len(q)>=2:
        src=q[1]
        if src.type==X86_OP_REG:
            sr=canon(md,src.reg); prov[dst]=prov.get(sr,f"REG:{sr}")
            return
        if src.type==X86_OP_IMM:
            prov[dst]=f"IMM:{src.imm:#x}"
            return
        if src.type==X86_OP_MEM:
            rt=rip_target(ins,src)
            if rt is not None:
                prov[dst]=f"LOAD_GLOBAL@{rt-IMAGE_BASE:#x}"
            else:
                br=canon(md,src.mem.base); ix=canon(md,src.mem.index)
                prov[dst]=f"LOAD[{prov.get(br,br or '-')}{src.mem.disp:+#x};idx={prov.get(ix,ix or '-')}]"
            return
    if m=="xor" and len(q)>=2 and q[1].type==X86_OP_REG and canon(md,q[1].reg)==dst:
        prov[dst]="ZERO"; return
    if m in ("add","sub") and len(q)>=2 and q[1].type==X86_OP_IMM and dst in prov:
        sign=1 if m=="add" else -1
        prov[dst]=f"({prov[dst]}{sign*q[1].imm:+#x})"; return
    # Any other explicit write to a register invalidates a simple provenance claim.
    try:
        if q[0].access & CS_AC_WRITE: prov.pop(dst,None)
    except Exception:
        pass


def analyze_target(md,pe,by,ranges,name,seed):
    fn=containing_func(ranges,seed)
    a,b,seq=func_instructions(pe,by,fn,seed)
    print(f"TARGET={name} SEED={seed:#x} RANGE={a:#x}-{b:#x} PDATA={1 if fn else 0} INSNS={len(seq)}")
    prov={}
    writes=0; field_hits=0; rip_globals=set(); calls=[]
    for idx,ins in enumerate(seq):
        ct=direct_call(ins)
        if ct is not None: calls.append(ct-pe.base)
        for oi,op in enumerate(ops(ins)):
            if op.type != X86_OP_MEM: continue
            rt=rip_target(ins,op)
            if rt is not None: rip_globals.add(rt-pe.base)
            if op.mem.base != X86_REG_RIP and abs(op.mem.disp) in FIELD_OFFSETS:
                field_hits += 1
            if not is_write_operand(ins,oi,op): continue
            writes += 1
            print(f"  WRITE#{writes} {ins_text(pe,ins)}")
            print("    MEM="+fmt_mem(md,ins,oi,op,prov))
            # value being stored, when obvious
            q=ops(ins)
            if len(q)>=2:
                src=q[1]
                if src.type==X86_OP_REG:
                    sr=canon(md,src.reg); print(f"    SRC={sr}:{prov.get(sr,'UNKNOWN')}")
                elif src.type==X86_OP_IMM:
                    print(f"    SRC=IMM:{src.imm:#x}")
            preds=pred_context(seq,idx)
            print("    PRED="+(" | ".join(ins_text(pe,x) for x in preds) if preds else "NONE_LOCAL"))
        provenance_after(md,ins,prov)
    print("  SUMMARY_WRITES="+str(writes))
    print("  SUMMARY_FIELD_OFFSET_MEMOPS="+str(field_hits))
    print("  RIP_GLOBALS="+(";".join(hex(x) for x in sorted(rip_globals)) if rip_globals else "NONE"))
    print("  DIRECT_CALLS="+(";".join(hex(x) for x in calls) if calls else "NONE"))
    print()
    return {"range":(a,b),"writes":writes,"field_hits":field_hits,"globals":rip_globals,"calls":calls}


def main():
    if len(sys.argv)!=2: raise SystemExit("usage: strictpair_gate_d_audit.py win81_nis_dx11_x64.dll")
    path=Path(sys.argv[1]); blob=path.read_bytes(); pe=PE(blob)
    if pe.base != IMAGE_BASE: raise RuntimeError(f"unexpected image base {pe.base:#x}")
    ranges=pdata_ranges(pe); by,all_ins=decode(pe)
    md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True

    print("STRICTPAIR_GATE_D_MEMFLOW=6")
    print("FILE="+str(path)); print("SIZE="+str(len(blob)))
    print("SHA256="+hashlib.sha256(blob).hexdigest())
    print("EXEC_SECTIONS="+",".join(f"{k}:{len(v)}" for k,v in by.items()))
    print(f"PDATA_RANGES={len(ranges)}")
    print()

    results={}
    for name,seed in SEEDS.items():
        results[name]=analyze_target(md,pe,by,ranges,name,seed)

    print("=== CROSS_TARGET_GLOBALS ===")
    owners={}
    for name,r in results.items():
        for g in r["globals"]: owners.setdefault(g,[]).append(name)
    for g,names in sorted(owners.items()):
        if len(names)>=2:
            print(f"GLOBAL={g:#x} TARGETS="+",".join(names))

    print("=== FIELD_OFFSET_FOCUS ===")
    for name,r in results.items():
        if r["field_hits"]:
            print(f"TARGET={name} FIELD_OFFSET_MEMOPS={r['field_hits']}")

    print("=== DECISION_INPUT ===")
    print("TARGETS="+str(len(results)))
    print("TOTAL_WRITES="+str(sum(r["writes"] for r in results.values())))
    print("TARGETS_WITH_STRUCT_STYLE_FIELDS="+str(sum(1 for r in results.values() if r["field_hits"])))
    print("NOTE=USE_WRITE_PREDICATES_AND_BASE_PROVENANCE_TO_CLASSIFY_DESTRUCTIVE_POLICY")
    print("AUDIT_COMPLETE=1")


if __name__=="__main__": main()
