from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "lab_results" / "lab32c_full"
OUT.mkdir(parents=True, exist_ok=True)

SOURCES = {
    "float": ROOT / "tools" / "lab_moe_ng_v02_warp_harness.cpp",
    "unorm8": ROOT / "tools" / "lab_moe_ng_v02_warp_harness_unorm8.cpp",
}

BASE_ASSIGNMENTS = {
    "inputSize[0]": "1.0f / static_cast<float>(inW)",
    "inputSize[1]": "1.0f / static_cast<float>(inH)",
    "outputSize[0]": "0.5f / static_cast<float>(inW)",
    "outputSize[1]": "0.5f / static_cast<float>(inH)",
}


def replace_exact(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, replacement, text, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {count}")
    return out


def patch_assignment(text: str, field: str, rhs: str) -> str:
    pattern = rf"(constants\.{re.escape(field)}\s*=\s*)([^;]+)(;)"
    return replace_exact(text, pattern, rf"\g<1>{rhs}\3", f"constants.{field}")


def prepare_lab31g(text: str) -> str:
    patched = text
    for field, rhs in BASE_ASSIGNMENTS.items():
        patched = patch_assignment(patched, field, rhs)
    return patched


def prepare_lab32c(text: str) -> str:
    patched = prepare_lab31g(text)
    struct_pattern = r"struct alignas\(16\) Constants\s*\{\s*float inputSize\[2\];\s*float outputSize\[2\];\s*\};"
    struct_replacement = "struct alignas(16) Constants\n{\n    float inputSize[2];\n    float outputSize[2];\n    float axisUVPair[4];\n};"
    patched = replace_exact(patched, struct_pattern, struct_replacement, "Constants struct")
    anchor = re.compile(r"(constants\.outputSize\[1\]\s*=\s*0\.5f\s*/\s*static_cast<float>\(inH\)\s*;)")
    extra = (
        r"\1\n    constants.axisUVPair[0] = 1.0f / static_cast<float>(inW);"
        r"\n    constants.axisUVPair[1] = 0.0f;"
        r"\n    constants.axisUVPair[2] = 0.0f;"
        r"\n    constants.axisUVPair[3] = 1.0f / static_cast<float>(inH);"
    )
    patched, count = anchor.subn(extra, patched)
    if count != 1:
        raise SystemExit(f"axis constants anchor: expected one match, got {count}")
    for i in range(4):
        if patched.count(f"constants.axisUVPair[{i}]") != 1:
            raise SystemExit(f"axisUVPair[{i}] assignment count mismatch")
    return patched


def main() -> None:
    for kind, source in SOURCES.items():
        text = source.read_text(encoding="utf-8")
        p31 = prepare_lab31g(text)
        p32 = prepare_lab32c(text)
        (OUT / f"warp_{kind}_lab31g.cpp").write_text(p31, encoding="utf-8")
        (OUT / f"warp_{kind}_lab32c.cpp").write_text(p32, encoding="utf-8")
        if "axisUVPair[4]" in p31:
            raise SystemExit(f"{kind}: LAB31G harness accidentally expanded")
        if "axisUVPair[4]" not in p32:
            raise SystemExit(f"{kind}: LAB32C harness not expanded")
        print(f"prepared {kind}: LAB31G 16-byte ABI + LAB32C 32-byte ABI")


if __name__ == "__main__":
    main()
