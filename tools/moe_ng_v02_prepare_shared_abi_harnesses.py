from __future__ import annotations

import argparse
import re
from pathlib import Path

ASSIGNMENTS = {
    "inputSize[0]": "1.0f / static_cast<float>(inW)",
    "inputSize[1]": "1.0f / static_cast<float>(inH)",
    "outputSize[0]": "0.5f / static_cast<float>(inW)",
    "outputSize[1]": "0.5f / static_cast<float>(inH)",
}


def patch_assignment(text: str, field: str, rhs: str) -> str:
    pat = re.compile(rf"(constants\.{re.escape(field)}\s*=\s*)([^;]+)(;)", re.MULTILINE)
    patched, count = pat.subn(rf"\g<1>{rhs}\3", text)
    if count != 1:
        raise SystemExit(f"expected exactly one assignment for constants.{field}, got {count}")
    return patched


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = (root / args.outdir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = {
        "float": root / "tools" / "lab_moe_ng_v02_warp_harness.cpp",
        "unorm8": root / "tools" / "lab_moe_ng_v02_warp_harness_unorm8.cpp",
    }
    for kind, source in sources.items():
        text = source.read_text(encoding="utf-8")
        patched = text
        for field, rhs in ASSIGNMENTS.items():
            patched = patch_assignment(patched, field, rhs)
        if patched == text:
            raise SystemExit(f"{kind}: ABI patch produced no change")
        if "axisUVPair" in patched:
            raise SystemExit(f"{kind}: expanded-axis ABI residue detected")
        target = out / f"warp_{kind}_shared.cpp"
        target.write_text(patched, encoding="utf-8")
        print(f"prepared {target.name}: 16-byte invInput+halfInvInput ABI")


if __name__ == "__main__":
    main()
