from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "lab_results" / "lab31g_full_v2"
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


def patch_assignment(text: str, field: str, rhs: str) -> tuple[str, int]:
    pattern = re.compile(
        rf"(constants\.{re.escape(field)}\s*=\s*)([^;]+)(;)",
        flags=re.MULTILINE,
    )
    return pattern.subn(rf"\g<1>{rhs}\3", text)


def main() -> None:
    for name, source in SOURCES.items():
        text = source.read_text(encoding="utf-8")
        (OUT / f"warp_{name}_legacy.cpp").write_text(text, encoding="utf-8")

        patched = text
        total = 0
        for field, rhs in ASSIGNMENTS.items():
            patched, count = patch_assignment(patched, field, rhs)
            if count != 1:
                raise SystemExit(
                    f"expected exactly one assignment for constants.{field} in {source}, got {count}"
                )
            total += count

        if total != 4 or patched == text:
            raise SystemExit(f"LAB31G ABI patch failed for {source}")

        for field in ASSIGNMENTS:
            if not re.search(rf"constants\.{re.escape(field)}\s*=", patched):
                raise SystemExit(f"patched assignment missing for constants.{field} in {source}")

        (OUT / f"warp_{name}_lab31g.cpp").write_text(patched, encoding="utf-8")
        print(f"prepared {name}: 4 ABI assignments patched")


if __name__ == "__main__":
    main()
