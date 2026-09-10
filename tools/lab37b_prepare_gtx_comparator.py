from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "hardware_validation" / "ptar_moe_ng_v02_lab29_compare.cpp"
OUTDIR = ROOT / "lab_results" / "lab37b_gtx_pack"
OUT = OUTDIR / "ptar_lab37b_compare.cpp"

OLD_LAB28_SHA = "fb77c785fd79891c321a4abf5d517367a7d873574c04046fe6c44b45a0df9a77"
OLD_LAB29_SHA = "6a407b37938ec6a0bf89d13cf84fa4ae23c6f712a947fc6926bbfde9725028ed"
LAB36B_SHA = "21c7bb739779c9bf61fd4ee0d502f8a0304c9f9ac75f936128c09220c76c99c0"
LAB37B_SHA = "cd92ead859b9e7e210b96b1b811c95f91b1eba3c18c64a7cfa83e0a004c5a0b5"

OLD_CONSTANT_INIT = "Constants c={{(float)kInW,(float)kInH},{(float)kOutW,(float)kOutH}};"
NEW_CONSTANT_INIT = """Constants c={};
        if(variant==2){
            c.inputSize[0]=(float)kInW;
            c.inputSize[1]=(float)kInH;
            c.outputSize[0]=(float)kOutW;
            c.outputSize[1]=(float)kOutH;
        }else{
            c.inputSize[0]=1.0f/(float)kInW;
            c.inputSize[1]=1.0f/(float)kInH;
            c.outputSize[0]=0.5f/(float)kInW;
            c.outputSize[1]=0.5f/(float)kInH;
        }"""


def replace_exact(text: str, old: str, new: str, expected: int = 1) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"expected {expected} occurrence(s) of {old!r}, got {count}")
    return text.replace(old, new)


def main() -> None:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    text = SRC.read_text(encoding="utf-8")
    text = replace_exact(text, OLD_LAB28_SHA, LAB36B_SHA)
    text = replace_exact(text, OLD_LAB29_SHA, LAB37B_SHA)
    for old, new in (
        ("Lab28", "Lab36B"), ("Lab29", "Lab37B"),
        ("LAB28", "LAB36B"), ("LAB29", "LAB37B"),
        ("lab28", "lab36b"), ("lab29", "lab37b"),
    ):
        text = text.replace(old, new)
    text = replace_exact(text, OLD_CONSTANT_INIT, NEW_CONSTANT_INIT)

    stale = [OLD_LAB28_SHA, OLD_LAB29_SHA, "LAB28", "lab28", "LAB29", "lab29", "LAB31G", "lab31g", "LAB35A", "lab35a"]
    for token in stale:
        if token in text:
            raise SystemExit(f"stale comparator token remains: {token}")

    required = [
        LAB36B_SHA, LAB37B_SHA, "LAB36B", "LAB37B", "lab36b.cso", "lab37b.cso",
        "LAB37B_COMPARE_RAW.csv", "LAB37B_COMPARE_SUMMARY.json", "if(variant==2)",
        "1.0f/(float)kInW", "0.5f/(float)kInW",
        "c.inputSize[0]=(float)kInW", "c.outputSize[0]=(float)kOutW",
        "kWarmupTriads=60u", "kSamples=600u"
    ]
    for token in required:
        if token not in text:
            raise SystemExit(f"required comparator token missing: {token}")
    OUT.write_text(text, encoding="utf-8")
    print(f"generated {OUT}")


if __name__ == "__main__":
    main()
