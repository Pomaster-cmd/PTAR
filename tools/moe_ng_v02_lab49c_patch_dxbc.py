from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from moe_ng_v02_lab47_patch_dxbc import calculate_dxbc_checksum, parse_chunks

EXPECTED_BASELINE_SHA256 = "12ab14074c5de7a91b50aa8c114f4f235aea2f6d8903f8d0e6b2813e8907cc72"
EXPECTED_BASELINE_CHECKSUM = bytes.fromhex("b0aeb8f7407cffbe9ac7e16f20f2d671")
EXPECTED_CHUNK_OFFSETS = [52, 484, 536, 588, 2344]
EXPECTED_CHUNKS = [b"RDEF", b"ISGN", b"OSGN", b"SHEX", b"STAT"]
EXPECTED_FIRST_ROUND = [
    0x05000041, 0x00100032, 0x00000000, 0x00101046, 0x00000000,
]
PATCHED_FIRST_ADD = [
    0x0A000000,
    0x00100032, 0x00000000,          # r0.xy
    0x00101046, 0x00000000,          # v0.xyxx
    0x00004002,                       # immediate float4
    0xBF000000, 0xBF000000, 0x00000000, 0x00000000,  # (-0.5,-0.5,0,0)
]
EXPECTED_CANDIDATE_SHA256 = "1e9692db2bf685fab74b3b1bf88afaf29b5ba21ac3cc86f4a2949b64da7f4f82"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch_dxbc(data: bytes) -> tuple[bytes, dict]:
    base_sha = sha256_bytes(data)
    if base_sha != EXPECTED_BASELINE_SHA256:
        raise SystemExit(f"LAB47A SHA mismatch: {base_sha}")
    if data[4:20] != EXPECTED_BASELINE_CHECKSUM:
        raise SystemExit(f"LAB47A stored checksum mismatch: {data[4:20].hex()}")
    if calculate_dxbc_checksum(data) != EXPECTED_BASELINE_CHECKSUM:
        raise SystemExit("LAB47A checksum self-test failed")

    offsets, chunks = parse_chunks(data)
    if offsets != EXPECTED_CHUNK_OFFSETS:
        raise SystemExit(f"unexpected LAB47A chunk offsets: {offsets}")
    if [fourcc for fourcc, _ in chunks] != EXPECTED_CHUNKS:
        raise SystemExit("unexpected LAB47A chunk sequence")

    shex = chunks[3][1]
    words = list(struct.unpack("<%dI" % (len(shex) // 4), shex))
    if len(words) != 437 or words[0] != 0x50 or words[1] != 437:
        raise SystemExit("unexpected LAB47A SHEX header")
    if words[23:28] != EXPECTED_FIRST_ROUND:
        raise SystemExit("frozen leading ROUND_NI token sequence not found")

    words[23:28] = PATCHED_FIRST_ADD
    words[1] = len(words)
    if len(words) != 442:
        raise SystemExit(f"unexpected LAB49C SHEX dword count {len(words)}")
    chunks[3] = (b"SHEX", bytearray(struct.pack("<%dI" % len(words), *words)))

    # Instruction count and float-instruction count are unchanged: one float
    # ROUND_NI is replaced by one float ADD. Temp count is unchanged.
    stat = chunks[4][1]
    stats = list(struct.unpack("<37I", stat))
    if stats[0] != 48 or stats[1] != 8 or stats[4] != 36:
        raise SystemExit(f"unexpected LAB47A STAT signature inst={stats[0]} temps={stats[1]} float={stats[4]}")

    out = bytearray(data[:52])
    new_offsets: list[int] = []
    body = bytearray()
    for fourcc, payload in chunks:
        new_offsets.append(52 + len(body))
        body += fourcc
        body += struct.pack("<I", len(payload))
        body += payload
    out += body
    if new_offsets != [52, 484, 536, 588, 2364] or len(out) != 2520:
        raise SystemExit(f"unexpected LAB49C layout offsets={new_offsets} size={len(out)}")

    struct.pack_into("<I", out, 24, len(out))
    struct.pack_into("<5I", out, 32, *new_offsets)
    out[4:20] = b"\x00" * 16
    out[4:20] = calculate_dxbc_checksum(bytes(out))
    if calculate_dxbc_checksum(bytes(out)) != out[4:20]:
        raise SystemExit("LAB49C checksum verification failed")

    result = bytes(out)
    candidate_sha = sha256_bytes(result)
    if candidate_sha != EXPECTED_CANDIDATE_SHA256:
        raise SystemExit(f"LAB49C deterministic SHA mismatch: {candidate_sha}")

    audit = {
        "protocol": "LAB49C_DXBC_SVPOS_MINUS_HALF_PATCH",
        "baseline": "LAB47A",
        "baseline_sha256": base_sha,
        "baseline_checksum": EXPECTED_BASELINE_CHECKSUM.hex(),
        "candidate_sha256": candidate_sha,
        "candidate_checksum": result[4:20].hex(),
        "baseline_bytes": len(data),
        "candidate_bytes": len(result),
        "byte_delta": len(result) - len(data),
        "baseline_shex_dwords": 437,
        "candidate_shex_dwords": 442,
        "replaced_instruction": "round_ni r0.xy, v0.xyxx",
        "replacement_instruction": "add r0.xy, v0.xyxx, l(-0.5,-0.5,0,0)",
        "instruction_count_unchanged": 48,
        "temp_register_count_unchanged": 8,
        "mapping_claim": "D3D11 pixel-center equivalence only; WARP bit-exact admission is mandatory.",
        "new_chunk_offsets": new_offsets,
        "checksum_self_test_pass": True,
        "pass": True,
    }
    return result, audit


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--audit", required=True)
    args = ap.parse_args()
    src = Path(args.input)
    dst = Path(args.output)
    audit_path = Path(args.audit)
    result, audit = patch_dxbc(src.read_bytes())
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(result)
    audit_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(audit, indent=2))


if __name__ == "__main__":
    main()
