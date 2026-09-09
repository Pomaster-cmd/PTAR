#!/usr/bin/env python3
"""Prepare LAB24 runtime host sources from already-validated LAB22/LAB07 scaffolds.

This tool performs source-only, assertion-heavy transformations.  It does not
compile shaders, touch stable bundles, or run hardware.  LAB24 changes the
meaning of cb0.zw only for its own pixel shader: reciprocal input size instead
of output size.  All other shaders keep the historical constants.
"""
from __future__ import annotations

import argparse
from pathlib import Path

LAB24_SHA = "fdf85502f95404d99ac308f5ed67faff4e0d326550ba8502f722279ab406a618"
LAB24_FILE = "ptar_moe_ng_v02_lab24_host_invsize_ps.cso"
LAB22_FILE = "ptar_moe_ng_v02_lab22_phase_fused_mc_ps.cso"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected exactly 1 occurrence, found {n}: {old!r}")
    return text.replace(old, new, 1)


def patch_full_validator(path: Path) -> None:
    text = path.read_text(encoding="utf-8-sig")

    # The generic v02 bundle preparer emits LAB07 labels/file name while already
    # embedding the SHA of the supplied candidate CSO.  Relabel only this
    # isolated generated host.
    text = text.replace(
        "ptar_moe_ng_v02_mc_native_detail_ps.cso",
        LAB24_FILE,
    )
    text = text.replace("PTAR-NG MoE v02 LAB07", "PTAR-NG MoE v02 LAB24")
    text = text.replace("PTAR_NG_MOE_V02_LAB07_MC", "PTAR_NG_MOE_V02_LAB24_HOST_INVSIZE")
    text = text.replace("MoE v02 LAB07", "MoE v02 LAB24")

    # Change cb0.zw only when the LAB24 shader is bound. K185 retains historical
    # {outW,outH}. UpdateSubresource remains outside the GPU timestamp query.
    old = (
        "Constants c={{(float)inW,(float)inH},{(float)outW,(float)outH}};\n"
        "        m_context->UpdateSubresource(m_constants.Get(),0,0,&c,0,0);"
    )
    new = (
        "Constants c={{(float)inW,(float)inH},{(float)outW,(float)outH}};\n"
        "        if(ps==m_psMoe.Get())\n"
        "        {\n"
        "            c.outputSize[0]=1.0f/(float)inW;\n"
        "            c.outputSize[1]=1.0f/(float)inH;\n"
        "        }\n"
        "        m_context->UpdateSubresource(m_constants.Get(),0,0,&c,0,0);"
    )
    text = replace_once(text, old, new, "full-validator constants ABI")

    if "ptar_moe_ng_v02_mc_native_detail_ps.cso" in text:
        raise RuntimeError("full validator still references LAB07 shader file")
    if "PTAR_NG_MOE_V02_LAB07_MC" in text:
        raise RuntimeError("full validator still contains LAB07 runtime label")
    if text.count("c.outputSize[0]=1.0f/(float)inW") != 1:
        raise RuntimeError("full validator LAB24 inverse-size patch missing/duplicated")

    path.write_text(text, encoding="utf-8", newline="\n")


def make_comparator(template: Path, out: Path) -> None:
    text = template.read_text(encoding="utf-8-sig")

    # Reuse the already hardware-proven six-permutation, 600-triad comparator.
    # Slot 0 becomes LAB24, slot 1 remains LAB22, slot 2 remains K185.
    text = text.replace("LAB07", "LAB24").replace("lab07", "lab24")
    text = text.replace(
        'static const char* LAB24_SHA="caa7352d84c1a9ea840ff0f783323478a4f1532a949a1d3640ba4c04b441ebd1";',
        f'static const char* LAB24_SHA="{LAB24_SHA}";',
    )
    text = text.replace(
        'L"ptar_moe_ng_v02_mc_native_detail_ps.cso"',
        f'L"{LAB24_FILE}"',
    )

    # LAB24 gets inverse-size constants; LAB22 and K185 keep normal output size.
    old = (
        "Constants c={{(float)iw,(float)ih},{(float)ow,(float)oh}}; "
        "m_ctx->UpdateSubresource(m_cb.Get(),0,0,&c,0,0);"
    )
    new = (
        "Constants c={{(float)iw,(float)ih},{(float)ow,(float)oh}}; "
        "if(ps==m_lab24.Get()){ c.outSize[0]=1.0f/(float)iw; c.outSize[1]=1.0f/(float)ih; } "
        "m_ctx->UpdateSubresource(m_cb.Get(),0,0,&c,0,0);"
    )
    text = replace_once(text, old, new, "comparator constants ABI")

    # Make the result semantics explicit.  The transformed legacy variable name
    # d2207 now carries LAB22-LAB24, so positive means LAB24 is faster.
    text = text.replace(
        "PTAR-NG MoE v02 LAB22 SAME-RUN HARDWARE COMPARISON",
        "PTAR-NG MoE v02 LAB24 SAME-RUN HARDWARE COMPARISON",
    )
    text = text.replace(
        "[PASS] same-run tri-shader comparison complete.",
        "[PASS] same-run LAB22/LAB24/K185 comparison complete.",
    )

    required = [
        LAB24_SHA,
        LAB24_FILE,
        LAB22_FILE,
        "m_lab24.Get()",
        "c.outSize[0]=1.0f/(float)iw",
        "SIX_ROTATING_PERMUTATIONS_BALANCED",
        "SAMPLE_TRIADS=600u",
    ]
    for token in required:
        if token not in text:
            raise RuntimeError(f"generated comparator missing token: {token}")
    if "LAB07" in text or "lab07" in text:
        raise RuntimeError("generated comparator still contains LAB07 token")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8", newline="\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--full-validator", required=True)
    ap.add_argument("--comparator-template", required=True)
    ap.add_argument("--comparator-out", required=True)
    args = ap.parse_args()

    patch_full_validator(Path(args.full_validator))
    make_comparator(Path(args.comparator_template), Path(args.comparator_out))
    print("[PASS] LAB24 full-validator and same-run comparator sources prepared")


if __name__ == "__main__":
    main()
