#!/usr/bin/env python3
import hashlib, json, re, struct, sys
from pathlib import Path

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OPT_DETAIL
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

DLL = Path(sys.argv[1] if len(sys.argv) > 1 else "payload/win81_nis_dx11_x64.dll")
EXPECTED_SHA = "864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c"
OUT_TXT = Path(sys.argv[2] if len(sys.argv) > 2 else "RC35_AUTHORITY_ANALYSIS.txt")
OUT_JSON = OUT_TXT.with_suffix(".json")

raw = DLL.read_bytes()
sha = hashlib.sha256(raw).hexdigest()
if sha != EXPECTED_SHA:
    raise SystemExit(f"canonical runtime hash mismatch: {sha}")

pe = pefile.PE(data=raw, fast_load=False)
image_base = pe.OPTIONAL_HEADER.ImageBase

keywords = [
    "cartographer", "universal spatial presenter", "graphicscartographer",
    "rootauthority", "renderwidth", "renderheight", "engine-facing",
    "logical game", "resizebuffers", "native path", "native frame",
    "windowed", "borderless", "swapchain", "screen size", "presentation size",
    "spatial scaling", "fallback", "setwindow", "getclientrect", "adjustwindow",
    "1920x1080", "1280x720", "copy native", "filter copy"
]

# Extract printable ASCII and UTF-16LE strings with their raw-file offsets.
strings = []
for m in re.finditer(rb"[\x20-\x7e]{5,}", raw):
    try:
        s = m.group().decode("ascii")
        strings.append((m.start(), s, "ascii"))
    except Exception:
        pass
for m in re.finditer(rb"(?:[\x20-\x7e]\x00){5,}", raw):
    try:
        s = m.group().decode("utf-16le")
        strings.append((m.start(), s, "utf16"))
    except Exception:
        pass

interesting_strings = []
for off, s, enc in strings:
    sl = s.lower()
    if any(k in sl for k in keywords):
        try:
            rva = pe.get_rva_from_offset(off)
            va = image_base + rva
        except Exception:
            continue
        interesting_strings.append({"offset": off, "rva": rva, "va": va, "text": s, "encoding": enc})

# Imports, especially window / DXGI authority APIs.
imports = []
import_iat = {}
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for desc in pe.DIRECTORY_ENTRY_IMPORT:
        dll = desc.dll.decode("ascii", "replace")
        for imp in desc.imports:
            name = imp.name.decode("ascii", "replace") if imp.name else f"ord_{imp.ordinal}"
            rec = {"dll": dll, "name": name, "iat_va": imp.address}
            imports.append(rec)
            import_iat[imp.address] = rec

api_terms = (
    "SetWindowLong", "SetWindowPos", "GetWindowLong", "GetClientRect", "GetWindowRect",
    "AdjustWindowRect", "MoveWindow", "ShowWindow", "SetWindowPlacement", "GetWindowPlacement",
    "MonitorFromWindow", "GetMonitorInfo", "GetSystemMetrics", "SetThreadDpi", "GetDpi",
    "CreateDXGIFactory", "CreateSwapChain", "MakeWindowAssociation"
)
interesting_imports = [x for x in imports if any(t.lower() in x["name"].lower() for t in api_terms)]

# Locate executable .text and disassemble once.
text = None
for sec in pe.sections:
    name = sec.Name.rstrip(b"\0").decode("ascii", "replace")
    if name == ".text":
        text = sec
        break
if text is None:
    raise SystemExit(".text not found")
text_bytes = text.get_data()
text_va = image_base + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
insns = list(md.disasm(text_bytes, text_va))
addr_to_index = {ins.address: i for i, ins in enumerate(insns)}

str_by_va = {x["va"]: x for x in interesting_strings}
# Any target inside the bytes of an interesting string counts too.
str_ranges = []
for x in interesting_strings:
    unit = 2 if x["encoding"] == "utf16" else 1
    str_ranges.append((x["va"], x["va"] + (len(x["text"]) + 1) * unit, x))

def resolve_string_target(va):
    for lo, hi, rec in str_ranges:
        if lo <= va < hi:
            return rec
    return None

def rip_target(ins, op):
    if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
        return ins.address + ins.size + op.mem.disp
    if op.type == X86_OP_IMM:
        return op.imm
    return None

string_xrefs = []
api_xrefs = []
for idx, ins in enumerate(insns):
    for op in ins.operands:
        target = rip_target(ins, op)
        if target is None:
            continue
        srec = resolve_string_target(target)
        if srec is not None:
            string_xrefs.append({"ins_index": idx, "ins_va": ins.address, "target_va": target, "string": srec})
        if target in import_iat and import_iat[target] in interesting_imports:
            api_xrefs.append({"ins_index": idx, "ins_va": ins.address, "iat_va": target, "import": import_iat[target]})

# Add nearby string references to each context for semantic mapping.
def format_ins(ins):
    return f"{ins.address:016X}: {ins.mnemonic:<9} {ins.op_str}".rstrip()

def context(center, radius=28):
    a = max(0, center-radius)
    b = min(len(insns), center+radius+1)
    lines = []
    nearby = []
    for i in range(a, b):
        ins = insns[i]
        mark = ">>" if i == center else "  "
        annot = []
        for op in ins.operands:
            t = rip_target(ins, op)
            if t is None:
                continue
            sr = resolve_string_target(t)
            if sr:
                annot.append("STR=" + repr(sr["text"][:180]))
                nearby.append(sr["text"])
            if t in import_iat:
                rec = import_iat[t]
                annot.append(f"IAT={rec['dll']}!{rec['name']}")
        suffix = (" ; " + " | ".join(annot)) if annot else ""
        lines.append(mark + " " + format_ins(ins) + suffix)
    return lines, sorted(set(nearby))

# Deduplicate xrefs by instruction + target.
def unique_xrefs(seq, keyfn):
    seen = set(); out = []
    for x in seq:
        k = keyfn(x)
        if k in seen: continue
        seen.add(k); out.append(x)
    return out
string_xrefs = unique_xrefs(string_xrefs, lambda x:(x["ins_va"], x["string"]["va"]))
api_xrefs = unique_xrefs(api_xrefs, lambda x:(x["ins_va"], x["iat_va"]))

# Immediate constants can help find width/height policies, but only report those near mapped authority code.
constants = {1920, 1080, 1280, 720, 1904, 1041}
constant_hits = []
for idx, ins in enumerate(insns):
    vals = []
    for op in ins.operands:
        if op.type == X86_OP_IMM and 0 <= op.imm <= 0xFFFFFFFF and op.imm in constants:
            vals.append(op.imm)
    if vals:
        constant_hits.append({"ins_index": idx, "ins_va": ins.address, "values": vals})

report = {
    "dll": str(DLL), "sha256": sha, "size": len(raw), "image_base": image_base,
    "machine": pe.FILE_HEADER.Machine, "subsystem": pe.OPTIONAL_HEADER.Subsystem,
    "interesting_strings": interesting_strings,
    "interesting_imports": interesting_imports,
    "string_xrefs": [], "api_xrefs": [], "constant_hits": constant_hits,
}

for x in string_xrefs:
    lines, nearby = context(x["ins_index"])
    y = dict(x); y["context"] = lines; y["nearby_strings"] = nearby
    report["string_xrefs"].append(y)
for x in api_xrefs:
    lines, nearby = context(x["ins_index"])
    y = dict(x); y["context"] = lines; y["nearby_strings"] = nearby
    report["api_xrefs"].append(y)

OUT_JSON.write_text(json.dumps(report, indent=2), encoding="utf-8")

lines = []
lines.append("PTAR RC35 AUTHORITY BINARY ANALYSIS")
lines.append("=================================")
lines.append(f"DLL={DLL}")
lines.append(f"SHA256={sha}")
lines.append(f"SIZE={len(raw)}")
lines.append(f"IMAGE_BASE=0x{image_base:X}")
lines.append(f"MACHINE=0x{pe.FILE_HEADER.Machine:04X} SUBSYSTEM={pe.OPTIONAL_HEADER.Subsystem}")
lines.append("")
lines.append("INTERESTING IMPORTS")
for x in interesting_imports:
    lines.append(f"0x{x['iat_va']:016X} {x['dll']}!{x['name']}")
lines.append("")
lines.append("INTERESTING STRINGS")
for x in interesting_strings:
    lines.append(f"off=0x{x['offset']:X} rva=0x{x['rva']:X} va=0x{x['va']:016X} [{x['encoding']}] {x['text']}")
lines.append("")
lines.append(f"STRING XREFS: {len(report['string_xrefs'])}")
for n,x in enumerate(report["string_xrefs"],1):
    lines.append("")
    lines.append(f"--- STRING XREF {n}: ins=0x{x['ins_va']:X} target={x['string']['text']!r} ---")
    lines.extend(x["context"])
lines.append("")
lines.append(f"WINDOW/DXGI API XREFS: {len(report['api_xrefs'])}")
for n,x in enumerate(report["api_xrefs"],1):
    imp=x["import"]
    lines.append("")
    lines.append(f"--- API XREF {n}: ins=0x{x['ins_va']:X} {imp['dll']}!{imp['name']} ---")
    lines.extend(x["context"])
lines.append("")
lines.append(f"WIDTH/HEIGHT IMMEDIATE HITS: {len(constant_hits)}")
for x in constant_hits:
    lines.append(f"0x{x['ins_va']:016X}: {x['values']}")

OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"PASS canonical SHA {sha}")
print(f"interesting_strings={len(interesting_strings)} string_xrefs={len(string_xrefs)} api_xrefs={len(api_xrefs)} constants={len(constant_hits)}")
print(f"wrote {OUT_TXT} and {OUT_JSON}")
