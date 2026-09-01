#!/usr/bin/env python3
"""STRICTPAIR Gate D pass 5: counter-writer machine-code audit.

Read-only. This pass deliberately avoids diagnostic string correlation.
It starts from the candidate counter RVAs established by previous binary
passes, finds every executable RIP-relative reference, classifies writes,
and emits compact control-flow neighborhoods around destructive writers and
known source/publisher/mailbox functions.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_READ, CS_AC_WRITE
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

IMAGE_BASE = 0x180000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000

COUNTER_RVAS = [
    0x2CCE55C, 0x2CCE560, 0x2CCE564, 0x2CCE568, 0x2CCE56C, 0x2CCE570,
    0x2CCE5CC, 0x2CCE5D0, 0x2CCE5F0, 0x2CCE608, 0x2CCE644,
    0x2C9600C, 0x2C9604C, 0x2C96054, 0x2CCF3F0, 0x2CC4214,
    0x2C7E068, 0x2C7E06C, 0x2C7E070, 0x2C7E10C, 0x2C92544,
    0x4B114,
]

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

WRITE_MNEMONICS = {
    "mov", "movzx", "movsx", "movsxd", "inc", "dec", "add", "sub",
    "and", "or", "xor", "not", "neg", "xadd", "xchg", "cmpxchg",
    "bts", "btr", "btc", "adc", "sbb",
}
COND_PREFIX = ("ja", "jb", "jc", "je", "jg", "jl", "jna", "jnb", "jnc",
               "jne", "jng", "jnl", "jno", "jnp", "jns", "jnz", "jo",
               "jp", "jpe", "jpo", "js", "jz")


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u64(b, o): return struct.unpack_from("<Q", b, o)[0]


class PE:
    def __init__(self, blob: bytes):
        self.b = blob
        if blob[:2] != b"MZ":
            raise RuntimeError("MZ signature missing")
        p = u32(blob, 0x3C)
        if blob[p:p+4] != b"PE\0\0":
            raise RuntimeError("PE signature missing")
        coff = p + 4
        if u16(blob, coff) != 0x8664:
            raise RuntimeError("Expected x64 PE")
        nsec = u16(blob, coff + 2)
        optsz = u16(blob, coff + 16)
        opt = coff + 20
        if u16(blob, opt) != 0x20B:
            raise RuntimeError("Expected PE32+")
        self.base = u64(blob, opt + 24)
        shoff = opt + optsz
        self.sections = []
        for i in range(nsec):
            o = shoff + i * 40
            name = blob[o:o+8].rstrip(b"\0").decode("ascii", "replace")
            vs, va, rs, rp = struct.unpack_from("<IIII", blob, o + 8)
            ch = u32(blob, o + 36)
            self.sections.append({
                "name": name, "vs": vs, "va": va, "rs": rs, "rp": rp,
                "exec": bool(ch & IMAGE_SCN_MEM_EXECUTE),
            })

    def section(self, name):
        return next((s for s in self.sections if s["name"] == name), None)

    def section_for_rva(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vs"], s["rs"]):
                return s
        return None


def pdata_ranges(pe: PE):
    s = pe.section(".pdata")
    if not s:
        return []
    raw = pe.b[s["rp"]:s["rp"] + s["rs"]]
    out = []
    for o in range(0, len(raw) - 11, 12):
        begin, end, unwind = struct.unpack_from("<III", raw, o)
        if begin and begin < end:
            out.append((begin, end, unwind))
    return sorted(out)


def containing_func(ranges, rva):
    for a, b, u in ranges:
        if a <= rva < b:
            return a, b, u
    return None


def decode(pe: PE):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    md.skipdata = True
    by = {}
    all_ins = []
    for s in pe.sections:
        if not s["exec"] or not s["rs"]:
            continue
        raw = pe.b[s["rp"]:s["rp"] + s["rs"]]
        seq = list(md.disasm(raw, pe.base + s["va"]))
        by[s["name"]] = seq
        all_ins.extend(seq)
    return by, sorted(all_ins, key=lambda x: x.address)


def operands(ins):
    try:
        return ins.operands
    except Exception:
        return []


def rip_mem_refs(ins):
    """Return [(target_va, access, operand_index)]."""
    out = []
    for idx, op in enumerate(operands(ins)):
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            target = ins.address + ins.size + op.mem.disp
            access = ""
            if op.access & CS_AC_READ:
                access += "R"
            if op.access & CS_AC_WRITE:
                access += "W"
            # Capstone can leave access unset. For ordinary x86 two-operand
            # instructions, destination memory operand 0 is a write.
            if not access:
                if idx == 0 and ins.mnemonic in WRITE_MNEMONICS:
                    access = "W"
                else:
                    access = "?"
            out.append((target, access, idx))
    return out


def direct_call_target(ins):
    ops = operands(ins)
    if ins.mnemonic == "call" and len(ops) == 1 and ops[0].type == X86_OP_IMM:
        return ops[0].imm
    return None


def section_seq(pe, by, rva):
    s = pe.section_for_rva(rva)
    if not s:
        return None, None
    return s, by.get(s["name"])


def nearest_index(seq, va):
    if not seq:
        return None
    # Exact addresses are expected for xrefs; nearest keeps seed dumps robust.
    return min(range(len(seq)), key=lambda i: abs(seq[i].address - va))


def fmt(pe, ins):
    rva = ins.address - pe.base
    s = pe.section_for_rva(rva)
    refs = rip_mem_refs(ins)
    extra = ""
    if refs:
        extra = " ; " + ", ".join(f"{acc}@{(va-pe.base):#x}" for va, acc, _ in refs)
    ct = direct_call_target(ins)
    if ct is not None:
        extra += f" ; CALL={(ct-pe.base):#x}"
    return f"{s['name'] if s else '?'}:{rva:#010x}  {ins.mnemonic:<8} {ins.op_str}{extra}"


def window(pe, by, rva, before=18, after=12):
    s, seq = section_seq(pe, by, rva)
    if not seq:
        return []
    i = nearest_index(seq, pe.base + rva)
    return seq[max(0, i-before):min(len(seq), i+after+1)]


def is_cond_jump(ins):
    m = ins.mnemonic.lower()
    return m != "jmp" and m.startswith(COND_PREFIX)


def predicates_before(pe, by, rva, lookback=28):
    s, seq = section_seq(pe, by, rva)
    if not seq:
        return []
    i = nearest_index(seq, pe.base + rva)
    cand = seq[max(0, i-lookback):i]
    pred = [q for q in cand if q.mnemonic in ("cmp", "test", "bt") or is_cond_jump(q)]
    return pred[-12:]


def callers(all_ins, pe, fn):
    if not fn:
        return []
    target = pe.base + fn[0]
    return [i for i in all_ins if direct_call_target(i) == target]


def counter_xrefs(all_ins, pe, counter_rva):
    va = pe.base + counter_rva
    hits = []
    for ins in all_ins:
        for target, access, idx in rip_mem_refs(ins):
            if target == va:
                hits.append((ins, access, idx))
    return hits


def classify_writer(ins, access, opidx):
    if "W" in access:
        return True
    # Defensive fallback for RMW instructions if decoder reports read only.
    return opidx == 0 and ins.mnemonic in WRITE_MNEMONICS


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: strictpair_gate_d_audit.py win81_nis_dx11_x64.dll")
    path = Path(sys.argv[1])
    blob = path.read_bytes()
    pe = PE(blob)
    if pe.base != IMAGE_BASE:
        raise RuntimeError(f"unexpected image base {pe.base:#x}")
    ranges = pdata_ranges(pe)
    by, all_ins = decode(pe)

    print("STRICTPAIR_GATE_D_COUNTER_WRITERS=5")
    print("FILE=" + str(path))
    print("SIZE=" + str(len(blob)))
    print("SHA256=" + hashlib.sha256(blob).hexdigest())
    print("EXEC_SECTIONS=" + ",".join(f"{k}:{len(v)}" for k, v in by.items()))
    print(f"PDATA_RANGES={len(ranges)}")
    print()

    base_cluster = 0x2CCE55C
    print("=== COUNTER_CLUSTER_2CCE55C ===")
    for rva in COUNTER_RVAS:
        if 0x2CCE55C <= rva <= 0x2CCE644:
            print(f"COUNTER={rva:#x} OFFSET_FROM_CLUSTER={rva-base_cluster:+#x}")
    print()

    writer_total = 0
    referenced = 0
    print("=== COUNTER_XREFS_AND_WRITERS ===")
    for crva in COUNTER_RVAS:
        hits = counter_xrefs(all_ins, pe, crva)
        if hits:
            referenced += 1
        writers = [(i, a, oi) for i, a, oi in hits if classify_writer(i, a, oi)]
        writer_total += len(writers)
        print(f"COUNTER={crva:#x} XREFS={len(hits)} WRITERS={len(writers)}")
        for ins, access, opidx in hits:
            irva = ins.address - pe.base
            fn = containing_func(ranges, irva)
            role = "WRITE" if classify_writer(ins, access, opidx) else "READ"
            ftxt = f"{fn[0]:#x}-{fn[1]:#x}" if fn else "NO_PDATA"
            print(f"  {role} ACCESS={access} OP={opidx} AT={irva:#x} FUNC={ftxt} :: {fmt(pe, ins)}")
            if role != "WRITE":
                continue
            preds = predicates_before(pe, by, irva)
            print("    PREDICATES=" + (" | ".join(fmt(pe, q) for q in preds) if preds else "NONE_LOCAL"))
            cs = callers(all_ins, pe, fn)
            print("    DIRECT_CALLERS=" + (" | ".join(fmt(pe, q) for q in cs[:12]) if cs else "NONE"))
            print("    CONTEXT_BEGIN")
            for q in window(pe, by, irva, 20, 12):
                print("      " + fmt(pe, q))
            print("    CONTEXT_END")
        print()

    print("=== KNOWN_SEED_NEIGHBORHOODS ===")
    for name, rva in SEEDS.items():
        fn = containing_func(ranges, rva)
        ftxt = f"{fn[0]:#x}-{fn[1]:#x}" if fn else "NO_PDATA"
        print(f"SEED={name} RVA={rva:#x} FUNC={ftxt}")
        for q in window(pe, by, rva, 12, 20):
            print("  " + fmt(pe, q))
        print()

    print("=== FUNCTION_COUNTER_TOUCH_SUMMARY ===")
    grouped = {}
    for crva in COUNTER_RVAS:
        for ins, access, opidx in counter_xrefs(all_ins, pe, crva):
            irva = ins.address - pe.base
            fn = containing_func(ranges, irva)
            key = (fn[0], fn[1]) if fn else (irva, irva + ins.size)
            grouped.setdefault(key, set()).add(crva)
    for (a, b), cs in sorted(grouped.items()):
        print(f"FUNC={a:#x}-{b:#x} COUNTERS=" + ",".join(hex(x) for x in sorted(cs)))

    print()
    print("=== DECISION_INPUT ===")
    print(f"CANDIDATE_COUNTERS={len(COUNTER_RVAS)}")
    print(f"REFERENCED_COUNTERS={referenced}")
    print(f"WRITER_SITES={writer_total}")
    print("NOTE=INTERPRET_WRITER_CONTEXT_NOT_DIAGNOSTIC_STRINGS")
    print("AUDIT_COMPLETE=1")


if __name__ == "__main__":
    main()
