from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "hardware_validation" / "ptar_moe_ng_v02_lab29_compare.cpp"
OUTDIR = ROOT / "lab_results" / "lab31g_gtx_pack"
OUT = OUTDIR / "ptar_lab31g_compare.cpp"

OLD_LAB28_SHA = "fb77c785fd79891c321a4abf5d517367a7d873574c04046fe6c44b45a0df9a77"
OLD_LAB29_SHA = "6a407b37938ec6a0bf89d13cf84fa4ae23c6f712a947fc6926bbfde9725028ed"
LAB30C_SHA = "be967e9bdb56cad2f1b90e9d7e553a0316140577329e900f4caa02d337aabb8a"
LAB31G_SHA = "ead1e18944b392ef23562d9a45a9429d5bdee77ae914e313b415c3143c39aed1"

OLD_CONSTANT_INIT = "Constants c={{(float)kInW,(float)kInH},{(float)kOutW,(float)kOutH}};"
NEW_CONSTANT_INIT = """Constants c={};
        if(variant==1){
            c.inputSize[0]=1.0f/(float)kInW;
            c.inputSize[1]=1.0f/(float)kInH;
            c.outputSize[0]=0.5f/(float)kInW;
            c.outputSize[1]=0.5f/(float)kInH;
        }else{
            c.inputSize[0]=(float)kInW;
            c.inputSize[1]=(float)kInH;
            c.outputSize[0]=(float)kOutW;
            c.outputSize[1]=(float)kOutH;
        }"""


def replace_exact(text: str, old: str, new: str, expected: int = 1) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"expected {expected} occurrence(s) of {old!r}, got {count}")
    return text.replace(old, new)


def main() -> None:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    text = SRC.read_text(encoding="utf-8")

    text = replace_exact(text, OLD_LAB28_SHA, LAB30C_SHA)
    text = replace_exact(text, OLD_LAB29_SHA, LAB31G_SHA)

    # Preserve transformation order so LAB30C is never accidentally rewritten
    # while the historical LAB28/LAB29 comparator is relabelled.
    for old, new in (
        ("Lab28", "Lab30C"),
        ("Lab29", "Lab31G"),
        ("LAB28", "LAB30C"),
        ("LAB29", "LAB31G"),
        ("lab28", "lab30c"),
        ("lab29", "lab31g"),
    ):
        text = text.replace(old, new)

    text = replace_exact(text, OLD_CONSTANT_INIT, NEW_CONSTANT_INIT)

    stale = [OLD_LAB28_SHA, OLD_LAB29_SHA, "LAB28", "lab28", "LAB29", "lab29"]
    for token in stale:
        if token in text:
            raise SystemExit(f"stale comparator token remains: {token}")

    required = [
        LAB30C_SHA,
        LAB31G_SHA,
        "LAB30C",
        "LAB31G",
        "lab30c.cso",
        "lab31g.cso",
        "LAB31G_COMPARE_RAW.csv",
        "LAB31G_COMPARE_SUMMARY.json",
        "if(variant==1)",
        "1.0f/(float)kInW",
        "0.5f/(float)kInW",
        "c.inputSize[0]=(float)kInW",
        "c.outputSize[0]=(float)kOutW",
    ]
    for token in required:
        if token not in text:
            raise SystemExit(f"required comparator token missing: {token}")

    OUT.write_text(text, encoding="utf-8")
    print(f"generated {OUT}")


if __name__ == "__main__":
    main()
