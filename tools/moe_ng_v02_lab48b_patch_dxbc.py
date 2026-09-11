from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from moe_ng_v02_lab47_patch_dxbc import calculate_dxbc_checksum, parse_chunks

EXPECTED_BASELINE_SHA256 = "12ab14074c5de7a91b50aa8c114f4f235aea2f6d8903f8d0e6b2813e8907cc72"
EXPECTED_BASELINE_CHECKSUM = bytes.fromhex("b0aeb8f7407cffbe9ac7e16f20f2d671")
EXPECTED_CANDIDATE_SHA256 = "499193c3bfeff4966fd5a8d03f1618d4880d16e1c2ad0fe96fd16c5523a3855a"
EXPECTED_CHUNK_OFFSETS = [52, 484, 536, 588, 2344]
EXPECTED_CHUNKS = [b"RDEF", b"ISGN", b"OSGN", b"SHEX", b"STAT"]

# Frozen LAB47A executable prefix:
#   round_ni r0.xy, v0.xyxx
#   mul      r0.zw, r0.xxxy, l(0,0,2/3,2/3)
EXPECTED_PREFIX = [
    0x05000041,
    0x00100032, 0x00000000,
    0x00101046, 0x00000000,
    0x0A000038,
    0x001000C2, 0x00000000,
    0x00100406, 0x00000000,
    0x00004002,
    0x00000000, 0x00000000, 0x3F2AAAAB, 0x3F2AAAAB,
]

# D3D11 fullscreen raster invariant: pixel shader SV_Position.xy is pixel center n+0.5.
# For integer pixel n, floor(SV_Position)=n and therefore
#   n*(2/3) == mad(SV_Position, 2/3, -1/3)
# for the exact x1.5 mapping domain, subject to the DXBC MAD arithmetic path.  This
# candidate is admitted only if the complete WARP/CPU exactness gate proves parity.
# Keep the result in r0.zw so every downstream instruction/register and texture
# issue position remains structurally identical apart from the removed prefix op.
PATCHED_PREFIX = [
    0x0F000032,
    0x001000C2, 0x00000000,
    0x00101406, 0x00000000,
    0x00004002,
    0x00000000, 0x00000000, 0x3F2AAAAB, 0x3F2AAAAB,
    0x00004002,
    0x00000000, 0x00000000, 0xBEAAAAAB, 0xBEAAAAAB,
]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch_dxbc(data: bytes) -> tuple[bytes, dict]:
    baseline_sha = sha256_bytes(data)
    if baseline_sha != EXPECTED_BASELINE_SHA256:
        raise SystemExit(f"LAB47A SHA mismatch: {baseline_sha}")
    if len(data) != 2500:
        raise SystemExit(f"unexpected LAB47A size: {len(data)}")
    if data[4:20] != EXPECTED_BASELINE_CHECKSUM:
        raise SystemExit(f"LAB47A stored checksum mismatch: {data[4:20].hex()}")
    if calculate_dxbc_checksum(data) != EXPECTED_BASELINE_CHECKSUM:
        raise SystemExit("LAB47A checksum self-test failed")

    offsets, chunks = parse_chunks(data)
    if offsets != EXPECTED_CHUNK_OFFSETS:
        raise SystemExit(f"unexpected LAB47A chunk offsets: {offsets}")
    if [fourcc for fourcc, _ in chunks] != EXPECTED_CHUNKS:
        raise SystemExit(f"unexpected LAB47A chunk sequence: {[x[0] for x in chunks]}")

    shex = chunks[3][1]
    if len(shex) != 1748:
        raise SystemExit(f"unexpected LAB47A SHEX payload size: {len(shex)}")
    words = list(struct.unpack("<%dI" % (len(shex) // 4), shex))
    if len(words) != 437 or words[0] != 0x50 or words[1] != 437:
        raise SystemExit("unexpected LAB47A SHEX header")
    if words[23:38] != EXPECTED_PREFIX:
        got = " ".join(f"0x{x:08x}" for x in words[23:38])
        raise SystemExit(f"frozen LAB47A mapping prefix mismatch: {got}")

    # Equal dword footprint: 5-dword ROUND_NI + 10-dword MUL -> one 15-dword MAD.
    # No downstream token offset changes inside SHEX.
    words[23:38] = PATCHED_PREFIX
    if len(words) != 437:
        raise SystemExit("LAB48B patch unexpectedly changed SHEX dword count")
    chunks[3] = (b"SHEX", bytearray(struct.pack("<%dI" % len(words), *words)))

    stat = chunks[4][1]
    if len(stat) != 148:
        raise SystemExit(f"unexpected STAT payload size: {len(stat)}")
    stats = list(struct.unpack("<37I", stat))
    if stats[0] != 48 or stats[1] != 8 or stats[4] != 36:
        raise SystemExit(
            f"unexpected LAB47A STAT signature inst={stats[0]} temps={stats[1]} float={stats[4]}"
        )
    stats[0] = 47
    stats[4] = 35
    chunks[4] = (b"STAT", bytearray(struct.pack("<37I", *stats)))

    out = bytearray(data[:52])
    new_offsets: list[int] = []
    body = bytearray()
    for fourcc, payload in chunks:
        new_offsets.append(52 + len(body))
        body += fourcc
        body += struct.pack("<I", len(payload))
        body += payload
    out += body
    if new_offsets != EXPECTED_CHUNK_OFFSETS or len(out) != 2500:
        raise SystemExit(f"unexpected LAB48B layout offsets={new_offsets} size={len(out)}")

    struct.pack_into("<I", out, 24, len(out))
    struct.pack_into("<5I", out, 32, *new_offsets)
    out[4:20] = b"\x00" * 16
    out[4:20] = calculate_dxbc_checksum(bytes(out))
    if calculate_dxbc_checksum(bytes(out)) != out[4:20]:
        raise SystemExit("LAB48B DXBC checksum verification failed")

    result = bytes(out)
    candidate_sha = sha256_bytes(result)
    if candidate_sha != EXPECTED_CANDIDATE_SHA256:
        raise SystemExit(f"LAB48B deterministic SHA mismatch: {candidate_sha}")

    audit = {
        "protocol": "LAB48B_DXBC_SVPOS_CENTER_FMA_PATCH",
        "baseline": "LAB47A",
        "baseline_sha256": baseline_sha,
        "baseline_checksum": EXPECTED_BASELINE_CHECKSUM.hex(),
        "candidate_sha256": candidate_sha,
        "candidate_checksum": result[4:20].hex(),
        "baseline_bytes": len(data),
        "candidate_bytes": len(result),
        "byte_delta": 0,
        "baseline_shex_dwords": 437,
        "candidate_shex_dwords": 437,
        "removed_instructions": [
            "round_ni r0.xy, v0.xyxx",
            "mul r0.zw, r0.xxxy, l(0,0,0.666667,0.666667)",
        ],
        "replacement_instruction": "mad r0.zw, v0.xxxy, l(0,0,0.666667,0.666667), l(0,0,-0.333333,-0.333333)",
        "mapping_assumption": "D3D11 fullscreen pixel shader SV_Position.xy = integer pixel coordinate + 0.5; full exactness is still mandatory before admission.",
        "stat_instruction_count": stats[0],
        "stat_temp_register_count": stats[1],
        "stat_float_instruction_count": stats[4],
        "chunk_offsets_unchanged": True,
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
