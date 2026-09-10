from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, log: Path | None = None) -> None:
    cmd = [str(x) for x in args]
    if log is None:
        subprocess.run(cmd, check=True)
    else:
        with log.open("w", encoding="utf-8") as f:
            subprocess.run(cmd, check=True, stdout=f, stderr=subprocess.STDOUT)


def compare_f32(a: Path, b: Path, name: str) -> dict:
    aa = np.fromfile(a, np.float32)
    bb = np.fromfile(b, np.float32)
    if aa.shape != bb.shape:
        raise SystemExit(f"{name}: float size mismatch {aa.shape} != {bb.shape}")
    d = np.abs(aa - bb)
    return {
        "probe": name,
        "values": int(aa.size),
        "different_values": int(np.count_nonzero(d)),
        "max_abs": float(d.max()) if d.size else 0.0,
        "bit_exact": bool(np.array_equal(aa, bb)),
    }


def op_audit(asm: Path, cso: Path) -> dict:
    pat = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_]*)\b")
    ops: list[str] = []
    for raw in asm.read_text(encoding="utf-8", errors="replace").splitlines():
        s = raw.lstrip()
        if not s or s.startswith("//") or s.startswith("ps_5_0"):
            continue
        m = pat.match(raw)
        if not m:
            continue
        op = m.group(1).lower()
        if op.startswith("dcl_") or op in {"def", "defi", "defb"}:
            continue
        ops.append(op)
    c = Counter(ops)
    return {
        "sha256": sha256(cso),
        "instructions": len(ops),
        "bytes": cso.stat().st_size,
        "div": c.get("div", 0),
        "udiv": c.get("udiv", 0),
        "eq": c.get("eq", 0),
        "ne": c.get("ne", 0),
        "lt": c.get("lt", 0),
        "mov": c.get("mov", 0),
        "movc": c.get("movc", 0),
        "mad": c.get("mad", 0),
        "mul": c.get("mul", 0),
        "gather4": sum(v for k, v in c.items() if k.startswith("gather4")),
        "sample_l": sum(v for k, v in c.items() if k.startswith("sample_l")),
        "uav": sum(v for k, v in c.items() if k.startswith("store_uav") or k.startswith("atomic_")),
        "op_counts": dict(sorted(c.items())),
    }


def run_float_pair(out: Path, baseline: str, candidate: str, inp: Path, w: int, h: int, ow: int, oh: int, tag: str) -> dict:
    vs = out / "fullscreen_vs.cso"
    a = out / f"{tag}_{baseline}.f32"
    b = out / f"{tag}_{candidate}.f32"
    exe = out / "warp_float_shared.exe"
    run([exe, inp, w, h, ow, oh, out / f"{baseline}.cso", vs, a])
    run([exe, inp, w, h, ow, oh, out / f"{candidate}.cso", vs, b])
    r = compare_f32(a, b, tag)
    a.unlink(missing_ok=True)
    b.unlink(missing_ok=True)
    return r


def multires(out: Path, baseline: str, candidate: str) -> dict:
    rows = []
    for w, h in [(24, 24), (64, 64), (192, 192), (320, 180), (640, 360), (1280, 720)]:
        ow, oh = w * 3 // 2, h * 3 // 2
        y, x = np.mgrid[0:h, 0:w]
        xf = x.astype(np.float32) / np.float32(max(w - 1, 1))
        yf = y.astype(np.float32) / np.float32(max(h - 1, 1))
        rng = np.random.default_rng(340000 + w * 7 + h * 11)
        noise = rng.random((h, w), dtype=np.float32)
        checker = ((x + y) & 1).astype(np.float32)
        img = np.stack([
            np.clip(.06 + .62 * xf + .28 * checker, 0, 1),
            np.clip(.04 + .67 * yf + .24 * noise, 0, 1),
            np.clip(.08 + .50 * noise + .35 * (xf * yf), 0, 1),
            np.ones((h, w), np.float32),
        ], axis=2).astype(np.float32)
        inp = out / f"mr_{w}x{h}.f32"
        img.tofile(inp)
        r = run_float_pair(out, baseline, candidate, inp, w, h, ow, oh, f"mr_{w}x{h}")
        r.update({"input": f"{w}x{h}", "output": f"{ow}x{oh}"})
        rows.append(r)
        inp.unlink(missing_ok=True)
    report = {"protocol": f"{candidate.upper()}_MULTIRES_FLOAT_EQUIVALENCE", "cases": rows, "pass": all(r["bit_exact"] for r in rows)}
    (out / "MULTIRES_FLOAT.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("multires float drift")
    return report


def corpus(root: Path, out: Path, baseline: str, candidate: str) -> dict:
    src = root / "runtime_autonomous" / "PTAR_NG_MOE_V01_WIN81_X64" / "corpus" / "input_lr"
    files = sorted(src.glob("*.png"))
    if len(files) != 42:
        raise SystemExit(f"expected 42 corpus PNGs, got {len(files)}")
    td = out / "corpus"
    td.mkdir(exist_ok=True)
    rows = []
    total = diff_total = 0
    vs = out / "fullscreen_vs.cso"
    exe = out / "warp_unorm8_shared.exe"
    for q in files:
        im = Image.open(q).convert("RGBA")
        w, h = im.size
        ow, oh = w * 3 // 2, h * 3 // 2
        inp = td / f"{q.stem}.f32"
        (np.asarray(im, dtype=np.float32) / np.float32(255.0)).tofile(inp)
        a = td / f"{q.stem}_{baseline}.rgba8"
        b = td / f"{q.stem}_{candidate}.rgba8"
        common = [inp, w, h, ow, oh]
        run([exe, *common, out / f"{baseline}.cso", vs, a])
        run([exe, *common, out / f"{candidate}.cso", vs, b])
        ba, bb = a.read_bytes(), b.read_bytes()
        if len(ba) != len(bb):
            raise SystemExit(f"{q.stem}: output size mismatch")
        d = sum(x != y for x, y in zip(ba, bb))
        total += len(ba)
        diff_total += d
        rows.append({"case": q.stem, "bytes": len(ba), "different_bytes": d, "byte_exact": d == 0})
    report = {
        "protocol": f"{candidate.upper()}_42_CASE_WARP_R8G8B8A8_UNORM",
        "cases": len(rows),
        "exact_cases": sum(r["byte_exact"] for r in rows),
        "different_bytes": diff_total,
        "total_bytes": total,
        "pass": all(r["byte_exact"] for r in rows),
    }
    (out / "CORPUS_UNORM8_SUMMARY.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("corpus drift")
    return report


def synthetic(out: Path, baseline: str, candidate: str) -> dict:
    td = out / "synthetic"
    td.mkdir(exist_ok=True)
    h = w = 64
    y, x = np.mgrid[0:h, 0:w]
    rng = np.random.default_rng(0x4C4142333442)
    vs = out / "fullscreen_vs.cso"
    exe = out / "warp_unorm8_shared.exe"

    def rgba8(r, g, b, a=None):
        if a is None:
            a = np.full((h, w), 255, dtype=np.uint8)
        return np.stack([r, g, b, a], axis=2).astype(np.uint8)

    ramp = np.rint(np.linspace(0, 255, w, dtype=np.float32)).astype(np.uint8)[None, :].repeat(h, 0)
    vr = ramp.T
    imgs = [("ramps", rgba8(ramp, vr, ((ramp.astype(np.uint16) + vr.astype(np.uint16)) // 2).astype(np.uint8)))]
    step = np.where(x >= w // 2, 255, 0).astype(np.uint8)
    diag = np.where(x + y >= w - 1, 255, 0).astype(np.uint8)
    imgs.append(("steps", rgba8(step, diag, np.where((2 * x + y) % 5 < 2, 240, 15).astype(np.uint8))))
    checker = np.where((x + y) & 1, 255, 0).astype(np.uint8)
    imgs.append(("checker", rgba8(checker, np.roll(checker, 1, 1), np.roll(checker, 1, 0))))
    for i in range(5):
        imgs.append((f"random_{i}", rng.integers(0, 256, size=(h, w, 4), dtype=np.uint8)))

    rows = []
    for name, u8 in imgs:
        inp = td / f"{name}.f32"
        (u8.astype(np.float32) / np.float32(255.0)).tofile(inp)
        a = td / f"{name}_{baseline}.rgba8"
        b = td / f"{name}_{candidate}.rgba8"
        common = [inp, w, h, w * 3 // 2, h * 3 // 2]
        run([exe, *common, out / f"{baseline}.cso", vs, a])
        run([exe, *common, out / f"{candidate}.cso", vs, b])
        ba, bb = a.read_bytes(), b.read_bytes()
        d = sum(x != y for x, y in zip(ba, bb))
        rows.append({"probe": name, "different_bytes": d, "byte_exact": d == 0})
    report = {"protocol": f"{candidate.upper()}_SYNTHETIC_WARP_R8G8B8A8_UNORM", "probes": rows, "pass": all(r["byte_exact"] for r in rows)}
    (out / "SYNTHETIC_UNORM8_SUMMARY.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("synthetic drift")
    return report


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--outdir", required=True)
    p.add_argument("--baseline", required=True)
    p.add_argument("--candidate", required=True)
    p.add_argument("--baseline-sha", required=True)
    p.add_argument("--candidate-sha", required=True)
    p.add_argument("--baseline-instructions", required=True, type=int)
    p.add_argument("--candidate-instructions", required=True, type=int)
    p.add_argument("--baseline-bytes", required=True, type=int)
    p.add_argument("--candidate-bytes", required=True, type=int)
    return p.parse_args()


def main() -> None:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    out = (root / args.outdir).resolve()
    baseline = args.baseline.lower()
    candidate = args.candidate.lower()
    if sha256(out / f"{baseline}.cso") != args.baseline_sha.lower():
        raise SystemExit("baseline SHA mismatch")
    if sha256(out / f"{candidate}.cso") != args.candidate_sha.lower():
        raise SystemExit("candidate SHA mismatch")

    a = op_audit(out / f"{baseline}.asm", out / f"{baseline}.cso")
    b = op_audit(out / f"{candidate}.asm", out / f"{candidate}.cso")
    contract = all(r["gather4"] == 1 and r["sample_l"] == 4 and r["uav"] == 0 for r in (a, b))
    expected = (
        a["instructions"] == args.baseline_instructions and a["bytes"] == args.baseline_bytes and
        b["instructions"] == args.candidate_instructions and b["bytes"] == args.candidate_bytes
    )
    dx = {
        "protocol": f"{candidate.upper()}_DXBC_AUDIT",
        baseline: a,
        candidate: b,
        "texture_contract_pass": contract,
        "expected_codegen_pass": expected,
        "instruction_delta": b["instructions"] - a["instructions"],
        "byte_delta": b["bytes"] - a["bytes"],
        "pass": bool(contract and expected),
    }
    (out / "DXBC_AUDIT.json").write_text(json.dumps(dx, indent=2) + "\n", encoding="utf-8")
    if not dx["pass"]:
        raise SystemExit("DXBC audit failed")

    validator = root / "tools" / "lab_moe_ng_v02_warp_validate.py"
    run([sys.executable, validator, "--outdir", out, "--prepare"])
    vs = out / "fullscreen_vs.cso"
    exe = out / "warp_float_shared.exe"
    run([exe, out / "input.f32", 24, 24, 36, 36, out / f"{baseline}.cso", vs, out / "canonical_base.f32"])
    run([exe, out / "input.f32", 24, 24, 36, 36, out / f"{candidate}.cso", vs, out / "canonical_candidate.f32"])
    run([sys.executable, validator, "--outdir", out, "--gpu", out / "canonical_candidate.f32", "--tol", "0.000002"])
    cpu = json.loads((out / "SUMMARY.json").read_text())
    canonical = compare_f32(out / "canonical_base.f32", out / "canonical_candidate.f32", "canonical")
    (out / "CANONICAL_FLOAT.json").write_text(json.dumps(canonical, indent=2) + "\n", encoding="utf-8")
    if not canonical["bit_exact"]:
        raise SystemExit("canonical float drift")

    mr = multires(out, baseline, candidate)
    co = corpus(root, out, baseline, candidate)
    sy = synthetic(out, baseline, candidate)
    report = {
        "protocol": f"{candidate.upper()}_FULL_SOFTWARE_ADMISSION",
        "candidate_sha256": args.candidate_sha.lower(),
        "baseline_sha256": args.baseline_sha.lower(),
        "shared_abi": "cb0.xy=invInputSize; cb0.zw=halfInvInputSize",
        "cpu_parity": cpu,
        "canonical_float_bit_exact": canonical["bit_exact"],
        "multires_float_pass": mr["pass"],
        "corpus_unorm8_pass": co["pass"],
        "synthetic_unorm8_pass": sy["pass"],
        "dxbc_audit_pass": dx["pass"],
        "candidate_instructions": args.candidate_instructions,
        "candidate_cso_bytes": args.candidate_bytes,
        "baseline_instructions": args.baseline_instructions,
        "baseline_cso_bytes": args.baseline_bytes,
    }
    report["pass"] = bool(cpu.get("pass") and canonical["bit_exact"] and mr["pass"] and co["pass"] and sy["pass"] and dx["pass"])
    (out / "ADMISSION.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["pass"]:
        raise SystemExit(f"{candidate.upper()} full software admission failed")


if __name__ == "__main__":
    main()
