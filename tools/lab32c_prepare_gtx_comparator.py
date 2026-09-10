from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "hardware_validation" / "ptar_moe_ng_v02_lab29_compare.cpp"
OUTDIR = ROOT / "lab_results" / "lab32c_gtx_pack"
OUT = OUTDIR / "ptar_lab32c_compare.cpp"

OLD_LAB28_SHA = "fb77c785fd79891c321a4abf5d517367a7d873574c04046fe6c44b45a0df9a77"
OLD_LAB29_SHA = "6a407b37938ec6a0bf89d13cf84fa4ae23c6f712a947fc6926bbfde9725028ed"
LAB31G_SHA = "ead1e18944b392ef23562d9a45a9429d5bdee77ae914e313b415c3143c39aed1"
LAB32C_SHA = "185efb1dc7899df178352248a17a9d71826ea509ea9d94562cf89bc92dbb9fa3"

OLD_STRUCT = "struct Constants{float inputSize[2];float outputSize[2];};"
NEW_STRUCT = "struct Constants{float inputSize[2];float outputSize[2];float axisUVPair[4];};"
OLD_BUFFER_SIZE = "bd.ByteWidth=16;"
NEW_BUFFER_SIZE = "bd.ByteWidth=sizeof(Constants);"
OLD_CONSTANT_INIT = "Constants c={{(float)kInW,(float)kInH},{(float)kOutW,(float)kOutH}};"
NEW_CONSTANT_INIT = """Constants c={};
        const float invW=1.0f/(float)kInW;
        const float invH=1.0f/(float)kInH;
        if(variant==0){
            // LAB31G ABI: inverse input size + half inverse input size.
            c.inputSize[0]=invW;
            c.inputSize[1]=invH;
            c.outputSize[0]=0.5f*invW;
            c.outputSize[1]=0.5f*invH;
        }else if(variant==1){
            // LAB32C ABI: LAB31G constants plus host-prepacked X/Y axis UV vectors.
            c.inputSize[0]=invW;
            c.inputSize[1]=invH;
            c.outputSize[0]=0.5f*invW;
            c.outputSize[1]=0.5f*invH;
            c.axisUVPair[0]=invW;
            c.axisUVPair[1]=0.0f;
            c.axisUVPair[2]=0.0f;
            c.axisUVPair[3]=invH;
        }else{
            // K185 retains the legacy ABI in the first 16 bytes.
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

    text = replace_exact(text, OLD_LAB28_SHA, LAB31G_SHA)
    text = replace_exact(text, OLD_LAB29_SHA, LAB32C_SHA)

    for old, new in (
        ("Lab28", "Lab31G"),
        ("Lab29", "Lab32C"),
        ("LAB28", "LAB31G"),
        ("LAB29", "LAB32C"),
        ("lab28", "lab31g"),
        ("lab29", "lab32c"),
    ):
        text = text.replace(old, new)

    text = replace_exact(text, OLD_STRUCT, NEW_STRUCT)
    text = replace_exact(text, OLD_BUFFER_SIZE, NEW_BUFFER_SIZE)
    text = replace_exact(text, OLD_CONSTANT_INIT, NEW_CONSTANT_INIT)

    stale = [OLD_LAB28_SHA, OLD_LAB29_SHA, "LAB28", "lab28", "LAB29", "lab29"]
    for token in stale:
        if token in text:
            raise SystemExit(f"stale comparator token remains: {token}")

    required = [
        LAB31G_SHA, LAB32C_SHA,
        "LAB31G", "LAB32C", "lab31g.cso", "lab32c.cso",
        "LAB32C_COMPARE_RAW.csv", "LAB32C_COMPARE_SUMMARY.json",
        "float axisUVPair[4]", "bd.ByteWidth=sizeof(Constants)",
        "if(variant==0)", "else if(variant==1)",
        "c.axisUVPair[0]=invW", "c.axisUVPair[3]=invH",
        "c.inputSize[0]=(float)kInW", "c.outputSize[0]=(float)kOutW",
    ]
    for token in required:
        if token not in text:
            raise SystemExit(f"required comparator token missing: {token}")

    OUT.write_text(text, encoding="utf-8")
    print(f"generated {OUT}")


if __name__ == "__main__":
    main()
