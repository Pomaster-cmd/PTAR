#!/usr/bin/env python3
"""STRICTPAIR Gate D static binary audit for PTAR RC18/DWMPHASE2.

Read-only lab tool. It does not patch the DLL.
It maps destructive diagnostics/counters to their code xrefs and dumps the
known source/publisher/mailbox/presenter hot paths from the exact PE image.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_READ, CS_AC_WRITE
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

IMAGE_BASE_EXPECTED = 0x180000000

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

# Addresses already established during the RC18 reverse-map work.  The audit
# deliberately re-validates them against the exact DLL instead of trusting notes.
SEEDS = {
    "source_present_wrapper": 0xC6C0,
    "source_present1_wrapper": 0xD590,
    "source_submit_counter_writer": 0x12ACC,
    "source_pair_pipeline": 0x12C90,
    "vblank_wait_helper": 0x26400,
    "present_helper": 0x26540,
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
            self.sections.append({"name": name, "vs": vs, "va": va, "rs": rs, "rp": rp})

    def sec(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        raise KeyError(name)

    def rva_to_off(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vs"], s["rs"]):
                return s["rp"] + (rva - s["va"])
        raise KeyError(f"RVA {rva:#x}")

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


def fmt_func(ranges, rva):
    f = containing_func(ranges, rva)
    if not f:
        return "no-pdata"
    return f"{f[0]:#x}-{f[1]:#x}"


def rip_targets(ins):
    out = []
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            target = ins.address + ins.size + op.mem.disp
            rw = []
            if op.access & CS_AC_READ:
                rw.append("R")
            if op.access & CS_AC_WRITE:
                rw.append("W")
            out.append((target, "".join(rw) or "?"))
    return out


def disasm_text(pe: PE):
    s = pe.sec(".text")
    code = pe.b[s["rp"]:s["rp"] + s["rs"]]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    return list(md.disasm(code, pe.image_base + s["va"]))


def ins_line(pe, ranges, ins):
    rva = ins.address - pe.image_base
    refs = rip_targets(ins)
    suffix = ""
    if refs:
        suffix = " ; " + ", ".join(f"{rw}@{(t-pe.image_base):#x}" for t, rw in refs)
    return f"{rva:08x}  {ins.bytes.hex(' '):<28} {ins.mnemonic:<8} {ins.op_str}{suffix}"


def window(instructions, pe, ranges, center_rva, before=12, after=18):
    target = pe.image_base + center_rva
    idx = min(range(len(instructions)), key=lambda i: abs(instructions[i].address - target))
    lo = max(0, idx-before)
    hi = min(len(instructions), idx+after+1)
    return instructions[lo:hi]


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


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: strictpair_gate_d_audit.py win81_nis_dx11_x64.dll")
    path = Path(sys.argv[1])
    blob = path.read_bytes()
    pe = PE(blob)
    ranges = pdata_ranges(pe)
    insns = disasm_text(pe)

    print("STRICTPAIR_GATE_D_AUDIT=1")
    print("FILE=" + str(path))
    print("SIZE=" + str(len(blob)))
    print("SHA256=" + hashlib.sha256(blob).hexdigest())
    print(f"IMAGE_BASE={pe.image_base:#x}")
    print("SECTIONS=" + ",".join(f"{s['name']}@{s['va']:#x}+{max(s['vs'],s['rs']):#x}" for s in pe.sections))
    print(f"PDATA_RANGES={len(ranges)}")
    print(f"TEXT_INSNS={len(insns)}")
    print()

    if pe.image_base != IMAGE_BASE_EXPECTED:
        raise RuntimeError(f"unexpected image base {pe.image_base:#x}")

    # Build reverse map of every RIP-relative target used by .text.
    target_xrefs = {}
    for ins in insns:
        for target, rw in rip_targets(ins):
            target_xrefs.setdefault(target, []).append((ins, rw))

    print("=== DIAGNOSTIC LABEL XREFS ===")
    for label in LABELS:
        text = label.decode("ascii")
        occ = label_occurrences(pe, label)
        print(f"LABEL {text!r}: occurrences={len(occ)}")
        for off, rva in occ:
            va = pe.image_base + rva
            xs = target_xrefs.get(va, [])
            print(f"  string off={off:#x} rva={rva:#x} xrefs={len(xs)}")
            for ins, rw in xs:
                irva = ins.address - pe.image_base
                print(f"    XREF {irva:#x} func={fmt_func(ranges, irva)} {ins.mnemonic} {ins.op_str}")
                print("    -- neighborhood --")
                for q in window(insns, pe, ranges, irva, 10, 14):
                    print("      " + ins_line(pe, ranges, q))
        print()

    print("=== KNOWN HOT PATH WINDOWS ===")
    for name, rva in SEEDS.items():
        print(f"SEED {name} rva={rva:#x} func={fmt_func(ranges, rva)}")
        for q in window(insns, pe, ranges, rva, 18, 28):
            print("  " + ins_line(pe, ranges, q))
        print()

    # Summarize writable RIP targets in the known source-pair function.  These are
    # the globals most likely to be queue states/counters and are the input to the
    # second-pass destructive-writer audit.
    pair_func = containing_func(ranges, SEEDS["source_pair_pipeline"])
    print("=== SOURCE PAIR FUNCTION RIP WRITES ===")
    if pair_func:
        a, b, _ = pair_func
        for ins in insns:
            rva = ins.address - pe.image_base
            if a <= rva < b:
                for target, rw in rip_targets(ins):
                    if "W" in rw:
                        print(f"  {rva:#x} -> {(target-pe.image_base):#x} {ins.mnemonic} {ins.op_str}")
    else:
        print("  no pdata range")

    print("AUDIT_COMPLETE=1")


if __name__ == "__main__":
    main()
