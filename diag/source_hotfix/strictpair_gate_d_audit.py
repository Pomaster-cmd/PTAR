#!/usr/bin/env python3
"""STRICTPAIR Gate D static binary audit for PTAR RC18/DWMPHASE2.

Second-pass, read-only lab tool.  It never patches the DLL.

The first pass only decoded .text.  RC18 also has executable injected sections
(.fgov/.fgdia/.dwmlab), so this pass decodes every executable PE section,
resolves diagnostic string xrefs (direct and through pointer cells), and maps
all readers/writers of the counters involved in source/publisher/mailbox/drop
handling.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_READ, CS_AC_WRITE
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

IMAGE_BASE_EXPECTED = 0x180000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000

LABELS = [
    b"SOURCE SUBMITS",
    b"SOURCE FPS",
    b"SOURCE QUEUE FULL",
    b"GENERATED BUSY DROPS",
    b"LATE MIDPOINT DROPS",
    b"PRESENT QUEUE DROPS",
    b"MAILBOX GENERATED DROPS",
    b"NO-FREE-JOB DROPS",
    b"PUBLISHER GENERATED COALESCED",
    b"REAL_ONLY",
]

# RVAs observed in pass 1.  Pass 2 maps *every* executable xref to them and
# emits neighborhoods so reachability predicates can be inspected.
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


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u64(b, o): return struct.unpack_from("<Q", b, o)[0]


class PE:
    def __init__(self, blob: bytes):
        self.b = blob
        if blob[:2] != b"MZ":
            raise RuntimeError("MZ signature missing")
        self.e_lfanew = u32(blob, 0x3C)
        if blob[self.e_lfanew:self.e_lfanew+4] != b"PE\0\0":
            raise RuntimeError("PE signature missing")
        coff = self.e_lfanew + 4
        self.machine = u16(blob, coff)
        self.nsec = u16(blob, coff + 2)
        self.optsz = u16(blob, coff + 16)
        self.opt = coff + 20
        if self.machine != 0x8664 or u16(blob, self.opt) != 0x20B:
            raise RuntimeError("Expected PE32+ x64")
        self.image_base = u64(blob, self.opt + 24)
        self.shoff = self.opt + self.optsz
        self.sections = []
        for i in range(self.nsec):
            o = self.shoff + i * 40
            name = blob[o:o+8].rstrip(b"\0").decode("ascii", "replace")
            vs, va, rs, rp = struct.unpack_from("<IIII", blob, o + 8)
            chars = u32(blob, o + 36)
            self.sections.append({
                "name": name, "vs": vs, "va": va, "rs": rs, "rp": rp,
                "chars": chars, "exec": bool(chars & IMAGE_SCN_MEM_EXECUTE),
            })

    def sec(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        raise KeyError(name)

    def section_for_rva(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vs"], s["rs"]):
                return s
        return None

    def rva_to_off(self, rva):
        s = self.section_for_rva(rva)
        if not s:
            raise KeyError(f"RVA {rva:#x}")
        return s["rp"] + (rva - s["va"])

    def off_to_rva(self, off):
        for s in self.sections:
            if s["rp"] <= off < s["rp"] + s["rs"]:
                return s["va"] + (off - s["rp"])
        raise KeyError(f"offset {off:#x}")


def pdata_ranges(pe: PE):
    try:
        s = pe.sec(".pdata")
    except KeyError:
        return []
    raw = pe.b[s["rp"]:s["rp"] + s["rs"]]
    out = []
    for o in range(0, len(raw) - 11, 12):
        begin, end, unwind = struct.unpack_from("<III", raw, o)
        if begin == end == unwind == 0:
            continue
        if begin < end:
            out.append((begin, end, unwind))
    out.sort()
    return out


def containing_func(ranges, rva):
    for a, b, u in ranges:
        if a <= rva < b:
            return a, b, u
    return None


def fmt_func(pe, ranges, rva):
    f = containing_func(ranges, rva)
    if f:
        return f"{f[0]:#x}-{f[1]:#x}"
    s = pe.section_for_rva(rva)
    return f"{s['name']}:no-pdata" if s else "unmapped"


def rip_targets(ins):
    out = []
    try:
        operands = ins.operands
    except Exception:
        return out
    for op in operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            target = ins.address + ins.size + op.mem.disp
            rw = []
            if op.access & CS_AC_READ:
                rw.append("R")
            if op.access & CS_AC_WRITE:
                rw.append("W")
            # Some Capstone builds leave access unset for LEA. It is an address
            # reference, not a memory read, but keeping '?' is useful for xrefs.
            out.append((target, "".join(rw) or "?"))
    return out


def direct_call_target(ins):
    if ins.mnemonic != "call":
        return None
    try:
        if len(ins.operands) == 1 and ins.operands[0].type == X86_OP_IMM:
            return ins.operands[0].imm
    except Exception:
        pass
    return None


def disasm_exec_sections(pe: PE):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    by_section = {}
    all_ins = []
    for s in pe.sections:
        if not s["exec"] or not s["rs"]:
            continue
        raw = pe.b[s["rp"]:s["rp"] + s["rs"]]
        ins = list(md.disasm(raw, pe.image_base + s["va"]))
        by_section[s["name"]] = ins
        all_ins.extend(ins)
    all_ins.sort(key=lambda i: i.address)
    return by_section, all_ins


def ins_line(pe, ranges, ins):
    rva = ins.address - pe.image_base
    s = pe.section_for_rva(rva)
    sec = s["name"] if s else "?"
    refs = rip_targets(ins)
    suffix = ""
    if refs:
        suffix = " ; " + ", ".join(f"{rw}@{(t-pe.image_base):#x}" for t, rw in refs)
    ct = direct_call_target(ins)
    if ct is not None:
        suffix += f" ; CALL_RVA={(ct-pe.image_base):#x}"
    return f"{sec}:{rva:08x}  {ins.bytes.hex(' '):<28} {ins.mnemonic:<8} {ins.op_str}{suffix}"


def section_window(by_section, pe, center_rva, before=10, after=16):
    s = pe.section_for_rva(center_rva)
    if not s or s["name"] not in by_section or not by_section[s["name"]]:
        return []
    seq = by_section[s["name"]]
    target = pe.image_base + center_rva
    idx = min(range(len(seq)), key=lambda i: abs(seq[i].address - target))
    return seq[max(0, idx-before):min(len(seq), idx+after+1)]


def label_occurrences(pe: PE, label: bytes):
    out = []
    pos = 0
    while True:
        pos = pe.b.find(label, pos)
        if pos < 0:
            break
        try:
            rva = pe.off_to_rva(pos)
            out.append((pos, rva))
        except KeyError:
            pass
        pos += 1
    return out


def raw_pointer_cells(pe: PE, target_rva):
    """Find raw section cells containing VA64 or RVA32 of target_rva."""
    pats = [
        ("VA64", struct.pack("<Q", pe.image_base + target_rva), 8),
        ("RVA32", struct.pack("<I", target_rva), 4),
    ]
    found = []
    for kind, pat, width in pats:
        for s in pe.sections:
            if not s["rs"]:
                continue
            raw = pe.b[s["rp"]:s["rp"] + s["rs"]]
            p = 0
            while True:
                p = raw.find(pat, p)
                if p < 0:
                    break
                rva = s["va"] + p
                if rva != target_rva:  # suppress self/string coincidence
                    found.append((kind, rva, s["name"]))
                p += width
    return sorted(set(found), key=lambda x: (x[1], x[0]))


def build_target_xrefs(all_ins):
    xrefs = {}
    for ins in all_ins:
        for target, rw in rip_targets(ins):
            xrefs.setdefault(target, []).append((ins, rw))
    return xrefs


def writer_kind(ins, access):
    m = ins.mnemonic
    if "W" in access:
        return "WRITE"
    if m in ("inc", "dec", "xadd", "cmpxchg", "lock"):
        return "POSSIBLE_WRITE"
    return "READ/ADDR"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: strictpair_gate_d_audit.py win81_nis_dx11_x64.dll")
    path = Path(sys.argv[1])
    blob = path.read_bytes()
    pe = PE(blob)
    ranges = pdata_ranges(pe)
    by_section, all_ins = disasm_exec_sections(pe)
    target_xrefs = build_target_xrefs(all_ins)

    print("STRICTPAIR_GATE_D_AUDIT=2")
    print("FILE=" + str(path))
    print("SIZE=" + str(len(blob)))
    print("SHA256=" + hashlib.sha256(blob).hexdigest())
    print(f"IMAGE_BASE={pe.image_base:#x}")
    print("SECTIONS=" + ",".join(
        f"{s['name']}@{s['va']:#x}+{max(s['vs'],s['rs']):#x}:exec={int(s['exec'])}"
        for s in pe.sections))
    print(f"PDATA_RANGES={len(ranges)}")
    print("EXEC_INSNS=" + ",".join(f"{k}:{len(v)}" for k,v in by_section.items()))
    print(f"EXEC_INSNS_TOTAL={len(all_ins)}")
    print()

    if pe.image_base != IMAGE_BASE_EXPECTED:
        raise RuntimeError(f"unexpected image base {pe.image_base:#x}")

    print("=== DIAGNOSTIC LABEL XREFS ALL EXEC SECTIONS ===")
    label_summary = {}
    for label in LABELS:
        text = label.decode("ascii")
        occ = label_occurrences(pe, label)
        direct_total = 0
        indirect_total = 0
        print(f"LABEL {text!r}: occurrences={len(occ)}")
        for off, rva in occ:
            direct = target_xrefs.get(pe.image_base + rva, [])
            direct_total += len(direct)
            print(f"  string off={off:#x} rva={rva:#x} direct_xrefs={len(direct)}")
            for ins, rw in direct:
                irva = ins.address - pe.image_base
                print(f"    DIRECT {irva:#x} func={fmt_func(pe,ranges,irva)} access={rw} {ins.mnemonic} {ins.op_str}")
                for q in section_window(by_section, pe, irva, 7, 14):
                    print("      " + ins_line(pe, ranges, q))
            cells = raw_pointer_cells(pe, rva)
            print(f"    pointer_cells={len(cells)}")
            for kind, crva, csec in cells[:32]:
                xs = target_xrefs.get(pe.image_base + crva, [])
                indirect_total += len(xs)
                print(f"      CELL {kind} {csec}:{crva:#x} exec_xrefs={len(xs)}")
                for ins, rw in xs:
                    irva = ins.address - pe.image_base
                    print(f"        VIA_CELL {irva:#x} func={fmt_func(pe,ranges,irva)} access={rw} {ins.mnemonic} {ins.op_str}")
                    for q in section_window(by_section, pe, irva, 5, 10):
                        print("          " + ins_line(pe, ranges, q))
        label_summary[text] = (direct_total, indirect_total)
        print()

    print("=== SUSPECT COUNTER READERS/WRITERS ===")
    function_hits = {}
    for rva in COUNTER_RVAS:
        xs = target_xrefs.get(pe.image_base + rva, [])
        print(f"COUNTER {rva:#x} xrefs={len(xs)}")
        for ins, rw in xs:
            irva = ins.address - pe.image_base
            f = containing_func(ranges, irva)
            fkey = f[:2] if f else (irva, irva+ins.size)
            function_hits.setdefault(fkey, set()).add(rva)
            print(f"  {writer_kind(ins,rw)} at={irva:#x} func={fmt_func(pe,ranges,irva)} access={rw} {ins.mnemonic} {ins.op_str}")
            for q in section_window(by_section, pe, irva, 7, 10):
                print("    " + ins_line(pe, ranges, q))
        print()

    print("=== FUNCTIONS TOUCHING SUSPECT COUNTERS ===")
    for (a,b), targets in sorted(function_hits.items()):
        print(f"FUNC {a:#x}-{b:#x} counters=" + ",".join(hex(x) for x in sorted(targets)))
        # Direct call summary within pdata-backed functions; enough to identify
        # publisher/selector/recovery relationships without flooding the log.
        calls = []
        for ins in all_ins:
            rva = ins.address - pe.image_base
            if a <= rva < b:
                ct = direct_call_target(ins)
                if ct is not None:
                    calls.append((rva, ct-pe.image_base))
        if calls:
            print("  CALLS " + ", ".join(f"{src:#x}->{dst:#x}" for src,dst in calls))
        else:
            print("  CALLS none/direct-not-found")
    print()

    print("=== KNOWN HOT PATH ENTRY WINDOWS ===")
    for name, rva in SEEDS.items():
        print(f"SEED {name} rva={rva:#x} func={fmt_func(pe,ranges,rva)}")
        for q in section_window(by_section, pe, rva, 12, 20):
            print("  " + ins_line(pe, ranges, q))
        print()

    # Full writer index for globals touched by the source-pair function.  This
    # answers whether another function can mutate the same queue/counter state.
    pair = containing_func(ranges, SEEDS["source_pair_pipeline"])
    pair_written = set()
    if pair:
        a,b,_ = pair
        for ins in all_ins:
            rva = ins.address - pe.image_base
            if a <= rva < b:
                for target,rw in rip_targets(ins):
                    trva = target-pe.image_base
                    if "W" in rw and trva >= 0:
                        pair_written.add(trva)
    print("=== SOURCE-PAIR GLOBAL WRITE ALIAS MAP ===")
    for trva in sorted(pair_written):
        xs = target_xrefs.get(pe.image_base + trva, [])
        writers = []
        for ins,rw in xs:
            if "W" in rw:
                irva = ins.address-pe.image_base
                writers.append((irva, fmt_func(pe,ranges,irva), ins.mnemonic, ins.op_str))
        if writers:
            print(f"GLOBAL {trva:#x} writers={len(writers)}")
            for irva,fn,mn,op in writers:
                print(f"  {irva:#x} {fn} {mn} {op}")
    print()

    print("=== LABEL SUMMARY ===")
    for name,(d,i) in label_summary.items():
        print(f"{name}: direct={d} via_pointer_cell={i}")

    print("AUDIT_COMPLETE=1")


if __name__ == "__main__":
    main()
