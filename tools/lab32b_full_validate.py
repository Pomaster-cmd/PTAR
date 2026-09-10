from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

LAB31G_SHA = "ead1e18944b392ef23562d9a45a9429d5bdee77ae914e313b415c3143c39aed1"
LAB32B_SHA = "65ed361923b37f0f2ec93e602b841bd11801820afa7ca94382606974d2eccab2"


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
        raise SystemExit(f"{name}: float size mismatch")
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
    ops = []
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
        "mov": c.get("mov", 0),
        "movc": c.get("movc", 0),
        "gather4": sum(v for k, v in c.items() if k.startswith("gather4")),
        "sample_l": sum(v for k, v in c.items() if k.startswith("sample_l")),
        "uav": sum(v for k, v in c.items() if k.startswith("store_uav") or k.startswith("atomic_")),
        "op_counts": dict(sorted(c.items())),
    }


def run_float_pair(out: Path, inp: Path, w: int, h: int, ow: int, oh: int, tag: str) -> dict:
    vs = out / "fullscreen_vs.cso"
    a = out / f"{tag}_31.f32"
    b = out / f"{tag}_32b.f32"
    exe = out / "warp_float_shared.exe"
    run([exe, inp, w, h, ow, oh, out / "lab31g.cso", vs, a])
    run([exe, inp, w, h, ow, oh, out / "lab32b.cso", vs, b])
    r = compare_f32(a, b, tag)
    a.unlink(missing_ok=True)
    b.unlink(missing_ok=True)
    return r


def multires(out: Path) -> dict:
    rows = []
    for w, h in [(24, 24), (64, 64), (192, 192), (320, 180), (640, 360), (1280, 720)]:
        ow, oh = w * 3 // 2, h * 3 // 2
        y, x = np.mgrid[0:h, 0:w]
        xf = x.astype(np.float32) / np.float32(max(w - 1, 1))
        yf = y.astype(np.float32) / np.float32(max(h - 1, 1))
        rng = np.random.default_rng(322000 + w * 7 + h * 11)
        noise = rng.random((h, w), dtype=np.float32)
        checker = ((x + y) & 1).astype(np.float32)
        img = np.stack(
            [
                np.clip(.06 + .62 * xf + .28 * checker, 0, 1),
                np.clip(.04 + .67 * yf + .24 * noise, 0, 1),
                np.clip(.08 + .50 * noise + .35 * (xf * yf), 0, 1),
                np.ones((h, w), np.float32),
            ],
            axis=2,
        ).astype(np.float32)
        inp = out / f"mr_{w}x{h}.f32"
        img.tofile(inp)
        r = run_float_pair(out, inp, w, h, ow, oh, f"mr_{w}x{h}")
        r.update({"input": f"{w}x{h}", "output": f"{ow}x{oh}"})
        rows.append(r)
        inp.unlink(missing_ok=True)
    report = {"protocol": "LAB32B_MULTIRES_FLOAT_EQUIVALENCE", "cases": rows, "pass": all(r["bit_exact"] for r in rows)}
    (out / "MULTIRES_FLOAT.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("multires float drift")
    return report


def corpus(root: Path, out: Path) -> dict:
    src = root / "runtime_autonomous" / "PTAR_NG_MOE_V01_WIN81_X64" / "corpus" / "input_lr"
    files = sorted(src.glob("*.png"))
    if len(files) != 42:
        raise SystemExit(f"expected 42 corpus PNGs, got {len(files)}")
    td = out / "corpus"
    td.mkdir(exist_ok=True)
    rows = []
    total = 0
    diff_total = 0
    vs = out / "fullscreen_vs.cso"
    exe = out / "warp_unorm8_shared.exe"
    for q in files:
        im = Image.open(q).convert("RGBA")
        w, h = im.size
        ow, oh = w * 3 // 2, h * 3 // 2
        inp = td / f"{q.stem}.f32"
        (np.asarray(im, dtype=np.float32) / np.float32(255.0)).tofile(inp)
        a = td / f"{q.stem}_31.rgba8"
        b = td / f"{q.stem}_32b.rgba8"
        common = [inp, w, h, ow, oh]
        run([exe, *common, out / "lab31g.cso", vs, a])
        run([exe, *common, out / "lab32b.cso", vs, b])
        ba, bb = a.read_bytes(), b.read_bytes()
        if len(ba) != len(bb):
            raise SystemExit(f"{q.stem}: output size mismatch")
        d = sum(x != y for x, y in zip(ba, bb))
        total += len(ba)
        diff_total += d
        rows.append({"case": q.stem, "bytes": len(ba), "different_bytes": d, "byte_exact": d == 0})
    report = {
        "protocol": "LAB32B_42_CASE_WARP_R8G8B8A8_UNORM",
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


def synthetic(out: Path) -> dict:
    td = out / "synthetic"
    td.mkdir(exist_ok=True)
    h = w = 64
    y, x = np.mgrid[0:h, 0:w]
    rng = np.random.default_rng(0x4C4142333242)
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
        a = td / f"{name}_31.rgba8"
        b = td / f"{name}_32b.rgba8"
        common = [inp, w, h, w * 3 // 2, h * 3 // 2]
        run([exe, *common, out / "lab31g.cso", vs, a])
        run([exe, *common, out / "lab32b.cso", vs, b])
        ba, bb = a.read_bytes(), b.read_bytes()
        d = sum(x != y for x, y in zip(ba, bb))
        rows.append({"probe": name, "different_bytes": d, "byte_exact": d == 0})
    report = {"protocol": "LAB32B_SYNTHETIC_WARP_R8G8B8A8_UNORM", "probes": rows, "pass": all(r["byte_exact"] for r in rows)}
    (out / "SYNTHETIC_UNORM8_SUMMARY.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("synthetic drift")
    return report


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    out = (root / "lab_results" / "lab32b_full").resolve()
    if sha256(out / "lab31g.cso") != LAB31G_SHA:
        raise SystemExit("LAB31G SHA mismatch")
    if sha256(out / "lab32b.cso") != LAB32B_SHA:
        raise SystemExit("LAB32B SHA mismatch")

    a = op_audit(out / "lab31g.asm", out / "lab31g.cso")
    b = op_audit(out / "lab32b.asm", out / "lab32b.cso")
    contract = all(r["gather4"] == 1 and r["sample_l"] == 4 and r["uav"] == 0 for r in (a, b))
    expected = a["instructions"] == 53 and a["bytes"] == 2568 and b["instructions"] == 52 and b["bytes"] == 2548
    dx = {
        "protocol": "LAB32B_DXBC_AUDIT",
        "lab31g": a,
        "lab32b": b,
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
    run([exe, out / "input.f32", 24, 24, 36, 36, out / "lab31g.cso", vs, out / "canonical31.f32"])
    run([exe, out / "input.f32", 24, 24, 36, 36, out / "lab32b.cso", vs, out / "canonical32b.f32"])
    run([sys.executable, validator, "--outdir", out, "--gpu", out / "canonical32b.f32", "--tol", "0.000002"])
    cpu = json.loads((out / "SUMMARY.json").read_text())
    canonical = compare_f32(out / "canonical31.f32", out / "canonical32b.f32", "canonical")
    if not canonical["bit_exact"]:
        raise SystemExit("canonical drift")

    mr = multires(out)
    co = corpus(root, out)
    sy = synthetic(out)
    report = {
        "protocol": "LAB32B_FULL_SOFTWARE_ADMISSION",
        "lab32b_sha256": LAB32B_SHA,
        "baseline_lab31g_sha256": LAB31G_SHA,
        "shared_abi": "cb0.xy=invInputSize; cb0.zw=halfInvInputSize",
        "cpu_parity": cpu,
        "canonical_float_bit_exact": canonical["bit_exact"],
        "multires_float_pass": mr["pass"],
        "corpus_unorm8_pass": co["pass"],
        "synthetic_unorm8_pass": sy["pass"],
        "dxbc_audit_pass": dx["pass"],
        "lab32b_instructions": 52,
        "lab32b_cso_bytes": 2548,
        "baseline_instructions": 53,
        "baseline_cso_bytes": 2568,
    }
    report["pass"] = bool(cpu.get("pass") and canonical["bit_exact"] and mr["pass"] and co["pass"] and sy["pass"] and dx["pass"])
    (out / "ADMISSION.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["pass"]:
        raise SystemExit("LAB32B full software admission failed")


if __name__ == "__main__":
    main()
