from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "lab_results" / "lab34b_full"
OUT.mkdir(parents=True, exist_ok=True)

SOURCES = {
    "float": ROOT / "tools" / "lab_moe_ng_v02_warp_harness.cpp",
    "unorm8": ROOT / "tools" / "lab_moe_ng_v02_warp_harness_unorm8.cpp",
}

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
        raise SystemExit(f"expected one assignment for constants.{field}, got {count}")
    return patched


def main() -> None:
    for kind, source in SOURCES.items():
        text = source.read_text(encoding="utf-8")
        patched = text
        for field, rhs in ASSIGNMENTS.items():
            patched = patch_assignment(patched, field, rhs)
        if patched == text:
            raise SystemExit(f"{kind}: ABI patch produced no change")
        if "axisUVPair" in patched:
            raise SystemExit(f"{kind}: unexpected expanded ABI residue")
        (OUT / f"warp_{kind}_shared.cpp").write_text(patched, encoding="utf-8")
        print(f"prepared {kind}: shared LAB31G/LAB34B 16-byte inverse+half-inverse ABI")


if __name__ == "__main__":
    main()
