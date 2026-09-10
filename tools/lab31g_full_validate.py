from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

LAB30C_SHA = "be967e9bdb56cad2f1b90e9d7e553a0316140577329e900f4caa02d337aabb8a"
LAB31G_SHA = "ead1e18944b392ef23562d9a45a9429d5bdee77ae914e313b415c3143c39aed1"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def op_audit(asm: Path, cso: Path) -> dict:
    op_re = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_]*)\b")
    ops: list[str] = []
    for raw in asm.read_text(encoding="utf-8", errors="replace").splitlines():
        s = raw.lstrip()
        if not s or s.startswith("//") or s.startswith("ps_5_0"):
            continue
        m = op_re.match(raw)
        if not m:
            continue
        op = m.group(1).lower()
        if op.startswith("dcl_") or op in {"def", "defi", "defb"}:
            continue
        ops.append(op)
    c = Counter(ops)
    return {
        "sha256": sha256(cso),
        "instruction_total": len(ops),
        "cso_bytes": cso.stat().st_size,
        "div_ops": c.get("div", 0),
        "gather4_ops": sum(v for k, v in c.items() if k.startswith("gather4")),
        "sample_l_ops": sum(v for k, v in c.items() if k.startswith("sample_l")),
        "uav_ops": sum(v for k, v in c.items() if k.startswith("store_uav") or k.startswith("atomic_")),
        "op_counts": dict(sorted(c.items())),
    }


def run(cmd: list[str | Path], log: Path | None = None) -> None:
    args = [str(x) for x in cmd]
    if log is None:
        subprocess.run(args, check=True)
        return
    with log.open("w", encoding="utf-8") as f:
        subprocess.run(args, check=True, stdout=f, stderr=subprocess.STDOUT)


def compare_f32(a: Path, b: Path, name: str) -> dict:
    aa = np.fromfile(a, np.float32)
    bb = np.fromfile(b, np.float32)
    if aa.shape != bb.shape:
        raise SystemExit(f"{name}: float output size mismatch {aa.shape} != {bb.shape}")
    d = np.abs(aa - bb)
    return {
        "probe": name,
        "values": int(aa.size),
        "different_values": int(np.count_nonzero(d)),
        "max_abs": float(d.max()) if d.size else 0.0,
        "bit_exact": bool(np.array_equal(aa, bb)),
    }


def canonical_and_hf(root: Path, out: Path) -> tuple[dict, dict, dict]:
    validator = root / "tools" / "lab_moe_ng_v02_warp_validate.py"
    vs = out / "fullscreen_vs.cso"
    run([sys.executable, validator, "--outdir", out, "--prepare"])
    run([
        out / "warp_float_legacy.exe", out / "input.f32", "24", "24", "36", "36",
        out / "lab30c.cso", vs, out / "lab30c_output.f32"
    ], out / "WARP_LAB30C.txt")
    run([
        out / "warp_float_lab31g.exe", out / "input.f32", "24", "24", "36", "36",
        out / "lab31g.cso", vs, out / "lab31g_output.f32"
    ], out / "WARP_LAB31G.txt")
    run([sys.executable, validator, "--outdir", out, "--gpu", out / "lab31g_output.f32", "--tol", "0.000002"])
    cpu = json.loads((out / "SUMMARY.json").read_text(encoding="utf-8"))
    (out / "LAB31G_CPU_PARITY.json").write_text(json.dumps(cpu, indent=2) + "\n", encoding="utf-8")

    canonical = compare_f32(out / "lab30c_output.f32", out / "lab31g_output.f32", "canonical")
    (out / "CANONICAL_FLOAT.json").write_text(json.dumps(canonical, indent=2) + "\n", encoding="utf-8")
    if not canonical["bit_exact"]:
        raise SystemExit("canonical LAB30C/LAB31G float drift")

    h = w = 24
    y, x = np.mgrid[0:h, 0:w]
    f = np.float32
    checker = ((x + y) & 1).astype(np.float32)
    stripes = ((2 * x + y) % 5 < 2).astype(np.float32)
    rng = np.random.default_rng(3131)
    noise = rng.random((h, w), dtype=np.float32)
    img = np.stack([
        f(0.03) + f(0.94) * checker,
        f(0.05) + f(0.88) * stripes,
        f(0.02) + f(0.95) * noise,
        np.ones((h, w), np.float32),
    ], axis=2).astype(np.float32)
    img.tofile(out / "probe_hf.f32")
    run([out / "warp_float_legacy.exe", out / "probe_hf.f32", "24", "24", "36", "36", out / "lab30c.cso", vs, out / "hf30.f32"], out / "WARP_HF30.txt")
    run([out / "warp_float_lab31g.exe", out / "probe_hf.f32", "24", "24", "36", "36", out / "lab31g.cso", vs, out / "hf31.f32"], out / "WARP_HF31.txt")
    hf = compare_f32(out / "hf30.f32", out / "hf31.f32", "high_frequency")
    (out / "HIGH_FREQUENCY_FLOAT.json").write_text(json.dumps(hf, indent=2) + "\n", encoding="utf-8")
    if not hf["bit_exact"]:
        raise SystemExit("high-frequency LAB30C/LAB31G float drift")
    return cpu, canonical, hf


def corpus(root: Path, out: Path) -> dict:
    src = root / "runtime_autonomous" / "PTAR_NG_MOE_V01_WIN81_X64" / "corpus" / "input_lr"
    files = sorted(src.glob("*.png"))
    if len(files) != 42:
        raise SystemExit(f"expected 42 corpus PNGs, got {len(files)}")
    inp = out / "inputs_f32"
    od = out / "corpus_outputs"
    inp.mkdir(parents=True, exist_ok=True)
    od.mkdir(parents=True, exist_ok=True)
    manifest = []
    rows = []
    total_diff = 0
    total_bytes = 0
    vs = out / "fullscreen_vs.cso"
    for path in files:
        im = Image.open(path).convert("RGBA")
        w, h = im.size
        arr = np.asarray(im, dtype=np.float32) / np.float32(255.0)
        fp = inp / f"{path.stem}.f32"
        arr.tofile(fp)
        ow, oh = w * 3 // 2, h * 3 // 2
        manifest.append({"case": path.stem, "input": str(fp), "inW": w, "inH": h, "outW": ow, "outH": oh})
        a = od / f"{path.stem}_30.rgba8"
        b = od / f"{path.stem}_31.rgba8"
        common = [str(fp), str(w), str(h), str(ow), str(oh)]
        run([out / "warp_unorm8_legacy.exe", *common, out / "lab30c.cso", vs, a])
        run([out / "warp_unorm8_lab31g.exe", *common, out / "lab31g.cso", vs, b])
        ba, bb = a.read_bytes(), b.read_bytes()
        if len(ba) != len(bb):
            raise SystemExit(f"{path.stem}: UNORM output size mismatch")
        diff = sum(x != y for x, y in zip(ba, bb))
        total_diff += diff
        total_bytes += len(ba)
        rows.append({"case": path.stem, "bytes": len(ba), "different_bytes": diff, "byte_exact": diff == 0})
    with (out / "CORPUS_MANIFEST.csv").open("w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=manifest[0].keys())
        wr.writeheader()
        wr.writerows(manifest)
    report = {
        "protocol": "LAB31G_42_CASE_WARP_R8G8B8A8_UNORM",
        "cases": len(rows),
        "exact_cases": sum(r["byte_exact"] for r in rows),
        "different_bytes": total_diff,
        "total_bytes": total_bytes,
        "pass": all(r["byte_exact"] for r in rows),
    }
    (out / "CORPUS_UNORM8_SUMMARY.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("42-case UNORM8 drift")
    return report


def synthetic(out: Path) -> dict:
    td = out / "synthetic"
    td.mkdir(exist_ok=True)
    h = w = 64
    y, x = np.mgrid[0:h, 0:w]
    rng = np.random.default_rng(0x4C4142333147)

    def rgba8(r, g, b, a=None):
        if a is None:
            a = np.full((h, w), 255, dtype=np.uint8)
        return np.stack([r, g, b, a], axis=2).astype(np.uint8)

    ramp = np.rint(np.linspace(0, 255, w, dtype=np.float32)).astype(np.uint8)[None, :].repeat(h, 0)
    vramp = ramp.T
    imgs = [("ramps", rgba8(ramp, vramp, ((ramp.astype(np.uint16) + vramp.astype(np.uint16)) // 2).astype(np.uint8)))]
    step = np.where(x >= w // 2, 255, 0).astype(np.uint8)
    diag = np.where(x + y >= w - 1, 255, 0).astype(np.uint8)
    imgs.append(("steps", rgba8(step, diag, np.where((2 * x + y) % 5 < 2, 240, 15).astype(np.uint8))))
    checker = np.where((x + y) & 1, 255, 0).astype(np.uint8)
    imgs.append(("checker", rgba8(checker, np.roll(checker, 1, 1), np.roll(checker, 1, 0))))
    for i in range(5):
        imgs.append((f"random_{i}", rng.integers(0, 256, size=(h, w, 4), dtype=np.uint8)))

    rows = []
    vs = out / "fullscreen_vs.cso"
    for name, u8 in imgs:
        inp = td / f"{name}.f32"
        (u8.astype(np.float32) / np.float32(255.0)).tofile(inp)
        a = td / f"{name}_30.rgba8"
        b = td / f"{name}_31.rgba8"
        common = [str(inp), str(w), str(h), str(w * 3 // 2), str(h * 3 // 2)]
        run([out / "warp_unorm8_legacy.exe", *common, out / "lab30c.cso", vs, a])
        run([out / "warp_unorm8_lab31g.exe", *common, out / "lab31g.cso", vs, b])
        ba, bb = a.read_bytes(), b.read_bytes()
        diff = sum(x != y for x, y in zip(ba, bb))
        rows.append({"probe": name, "different_bytes": diff, "byte_exact": diff == 0})
    report = {"protocol": "LAB31G_SYNTHETIC_WARP_R8G8B8A8_UNORM", "probes": rows, "pass": all(r["byte_exact"] for r in rows)}
    (out / "SYNTHETIC_UNORM8_SUMMARY.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("synthetic UNORM8 drift")
    return report


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = Path(args.outdir).resolve()

    if sha256(out / "lab30c.cso") != LAB30C_SHA:
        raise SystemExit("LAB30C SHA mismatch")
    if sha256(out / "lab31g.cso") != LAB31G_SHA:
        raise SystemExit("LAB31G SHA mismatch")

    dx30 = op_audit(out / "lab30c.asm", out / "lab30c.cso")
    dx31 = op_audit(out / "lab31g.asm", out / "lab31g.cso")
    contract = all(r["gather4_ops"] == 1 and r["sample_l_ops"] == 4 and r["uav_ops"] == 0 for r in (dx30, dx31))
    expected = (
        dx30["instruction_total"] == 55 and dx30["cso_bytes"] == 2632 and dx30["div_ops"] == 3
        and dx31["instruction_total"] == 53 and dx31["cso_bytes"] == 2568 and dx31["div_ops"] == 0
    )
    dx = {"protocol": "LAB31G_FULL_DXBC_AUDIT", "lab30c": dx30, "lab31g": dx31, "texture_contract_pass": contract, "expected_codegen_pass": expected, "pass": bool(contract and expected)}
    (out / "DXBC_AUDIT.json").write_text(json.dumps(dx, indent=2) + "\n", encoding="utf-8")
    if not dx["pass"]:
        raise SystemExit("DXBC audit failed")

    cpu, canonical, hf = canonical_and_hf(root, out)
    co = corpus(root, out)
    sy = synthetic(out)
    report = {
        "protocol": "LAB31G_FULL_SOFTWARE_ADMISSION",
        "lab31g_sha256": LAB31G_SHA,
        "baseline_lab30c_sha256": LAB30C_SHA,
        "abi": "cb0.xy=invInputSize, cb0.zw=halfInvInputSize",
        "cpu_parity": cpu,
        "canonical_float_bit_exact": canonical["bit_exact"],
        "high_frequency_float_bit_exact": hf["bit_exact"],
        "corpus_unorm8_pass": co["pass"],
        "synthetic_unorm8_pass": sy["pass"],
        "dxbc_audit_pass": dx["pass"],
        "lab31g_instructions": 53,
        "lab31g_cso_bytes": 2568,
        "lab31g_div_ops": 0,
        "baseline_instructions": 55,
        "baseline_cso_bytes": 2632,
        "baseline_div_ops": 3,
    }
    report["pass"] = bool(canonical["bit_exact"] and hf["bit_exact"] and co["pass"] and sy["pass"] and dx["pass"])
    (out / "ADMISSION.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["pass"]:
        raise SystemExit("LAB31G software admission failed")


if __name__ == "__main__":
    main()
