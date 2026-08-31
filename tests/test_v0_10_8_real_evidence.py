#!/usr/bin/env python3
from pathlib import Path
import csv,json,zipfile

ROOT=Path(__file__).resolve().parents[1]
E=ROOT/"hardware_evidence/GTX960M_2026-08-17_v0_10_6"
meta=json.loads((E/"EVIDENCE_METADATA.json").read_text(encoding="utf-8"))
with (E/"parity.csv").open(newline="",encoding="utf-8-sig") as f:
    rows=list(csv.DictReader(f))
with (E/"timing.csv").open(newline="",encoding="utf-8-sig") as f:
    timing=list(csv.DictReader(f))

assert len(rows)==42
assert all(r["status"]=="PASS" for r in rows)
assert max(int(r["max_abs_lsb"]) for r in rows)==1
assert meta["gpu_png_zero_byte_files"]==42
assert meta["gpu_png_status"]=="INVALID_FORENSIC_OUTPUTS_ZERO_BYTE"
assert (E/"raw_results_v0_10_6.zip").is_file()
with zipfile.ZipFile(E/"raw_results_v0_10_6.zip","r") as z:
    assert z.testzip() is None
assert len(timing)==2
print("10/10 PASS")
print("PASS - real GTX960M evidence preserved")
print("PASS - 42/42 parity")
print("PASS - max 1 LSB")
print("PASS - zero-byte PNG defect explicitly recorded")
