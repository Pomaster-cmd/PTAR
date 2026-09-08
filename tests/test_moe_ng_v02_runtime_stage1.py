#!/usr/bin/env python3
"""Stage-1 non-destructive runtime integration checks for PTAR-NG MoE v02.

This gate intentionally does not claim GPU validation. It proves that the
selected LAB07 shader was added without modifying the frozen MoE v01 runtime,
and that its source keeps the validated D3D11/SM5 texture and x1.5 contracts.
"""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
V02 = ROOT / "src/hlsl/moe_ng_v02/ptar_moe_ng_v02_mc_native_detail_ps.hlsl"
V01 = ROOT / "src/hlsl/moe_ng_v01/ptar_moe_ng_v01_sf5_ps.hlsl"
HOST = ROOT / "runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64/source/ptar_autonomous_validation.cpp"
SHADERS = ROOT / "runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64/shaders"

FROZEN_SHA256 = {
    V01: "6550847a5eab75d3cd8ee4494c7198933e3ea989f8100ebce3d09dc63975e355",
    HOST: "6ce901a529e7a6697cf2cf8cfbd5fe1110db6d573be57f7c958d14e79e50088b",
    SHADERS / "ptar_vs.cso": "6328bbd87aac73b07d6112de593f781b2769381aae65fa9c2bcfa06fdc68585c",
    SHADERS / "ptar_moe_ng_v01_sf5_ps.cso": "bb531ad6f797d2db4a362c82673067e39b7aa3a8fbb0ea19f786a053556a2279",
    SHADERS / "ptar_k185_control_ps.cso": "6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def code_without_line_comments(text: str) -> str:
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def check(name: str, condition: bool) -> None:
    if not condition:
        raise AssertionError(name)
    print(f"PASS - {name}")


def main() -> None:
    for path, expected in FROZEN_SHA256.items():
        check(f"frozen SHA256 {path.relative_to(ROOT)}", path.is_file() and sha256(path) == expected)

    check("v02 source exists", V02.is_file())
    text = V02.read_text(encoding="utf-8")
    code = code_without_line_comments(text)

    check("D3D11 Texture2D source", "Texture2D<float4> gSource : register(t0);" in code)
    check("SM5-compatible sampler contract", "SamplerState gLinearClamp : register(s0);" in code)
    check("one GatherGreen instruction in source", len(re.findall(r"\.GatherGreen\s*\(", code)) == 1)
    check("four SampleLevel instructions in source", len(re.findall(r"\.SampleLevel\s*\(", code)) == 4)
    check("no UAV resource", "RWTexture" not in code and "RWBuffer" not in code and "AppendStructuredBuffer" not in code)
    check("no curvature router", "RelativeDetailGate" not in code and "relCurv" not in code and "absCurv" not in code)
    check("no bilinear blend router", "lerp(bilinear" not in code)
    check("MC slope present", "float4 MCSlope" in code)
    check("Hermite t=1/3 present", "float4 Hermite13" in code and "20.0f*f0" in code and "7.0f*f1" in code)
    check("Hermite t=2/3 present", "float4 Hermite23" in code and "7.0f*f0" in code and "20.0f*f1" in code)
    check("exact x1.5 mapping", "float2(outPix)*(2.0f/3.0f)" in code)
    check("three-phase reconstruction", "%3u" in code)
    check("shape clamp t=1/3 and t=2/3", code.count("return clamp(h,min(f0,f1),max(f0,f1));") == 2)

    print(f"V02_SHA256={sha256(V02)}")
    print("STAGE1_PASS")


if __name__ == "__main__":
    main()
