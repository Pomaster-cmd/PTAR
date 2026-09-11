from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

MASK32 = 0xFFFFFFFF
EXPECTED_BASELINE_SHA256 = "d60820f62ca69e2cb2ef995c37001f100733e179f0821d7eb83247ad2c0014d3"
EXPECTED_BASELINE_CHECKSUM = bytes.fromhex("e61dfd2c4cacec49528dc37a92092d4f")
EXPECTED_CHUNK_OFFSETS = [52, 484, 536, 588, 2372]
EXPECTED_CHUNKS = [b"RDEF", b"ISGN", b"OSGN", b"SHEX", b"STAT"]
EXPECTED_EQ_TOKENS = [
    0x07000018, 0x00100022, 0x00000000, 0x0010002A,
    0x00000000, 0x00004001, 0x00000000,
]
EXPECTED_FINAL_MOVC = [
    0x09000037,
    0x001020F2, 0x00000000,          # o0.xyzw
    0x00100556, 0x00000000,          # r0.yyyy (eq result)
    0x00100E46, 0x00000003,          # r3.xyzw (f0)
    0x00100E46, 0x00000001,          # r1.xyzw (cubic)
]
PATCHED_FINAL_MOVC = [
    0x09000037,
    0x001020F2, 0x00000000,          # o0.xyzw
    0x00100AA6, 0x00000000,          # r0.zzzz (phaseFrac)
    0x00100E46, 0x00000001,          # r1.xyzw (cubic when phaseFrac != 0)
    0x00100E46, 0x00000003,          # r3.xyzw (f0 when phaseFrac == 0)
]

# Standard MD5 round constants. DXBC uses the same compression function but a
# Microsoft-specific finalization scheme for the container checksum.
MD5_S = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
MD5_K = [int(abs(math.sin(i + 1)) * (1 << 32)) & MASK32 for i in range(64)]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rol32(x: int, n: int) -> int:
    return ((x << n) | (x >> (32 - n))) & MASK32


def md5_transform(state: list[int], words: tuple[int, ...] | list[int]) -> list[int]:
    a, b, c, d = state
    aa, bb, cc, dd = a, b, c, d
    for i in range(64):
        if i < 16:
            f = (b & c) | ((~b) & d)
            g = i
        elif i < 32:
            f = (d & b) | ((~d) & c)
            g = (5 * i + 1) & 15
        elif i < 48:
            f = b ^ c ^ d
            g = (3 * i + 5) & 15
        else:
            f = c ^ (b | (~d))
            g = (7 * i) & 15
        f &= MASK32
        a, b, c, d = d, (b + rol32((a + f + MD5_K[i] + words[g]) & MASK32, MD5_S[i])) & MASK32, b, c
    return [(aa + a) & MASK32, (bb + b) & MASK32, (cc + c) & MASK32, (dd + d) & MASK32]


def calculate_dxbc_checksum(data: bytes) -> bytes:
    if len(data) < 20 or data[:4] != b"DXBC":
        raise ValueError("not a DXBC container")
    payload = data[0x14:]
    bit_count = (len(payload) * 8) & MASK32
    state = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]

    full = len(payload) & ~63
    for off in range(0, full, 64):
        state = md5_transform(state, struct.unpack("<16I", payload[off:off + 64]))

    last = payload[full:]
    if len(last) >= 56:
        block = last + b"\x80" + b"\x00" * (63 - len(last))
        state = md5_transform(state, struct.unpack("<16I", block))
        words = [0] * 16
        words[0] = bit_count
        words[15] = ((bit_count >> 2) | 1) & MASK32
        state = md5_transform(state, words)
    else:
        block = bytearray(64)
        struct.pack_into("<I", block, 0, bit_count)
        block[4:4 + len(last)] = last
        block[4 + len(last)] = 0x80
        struct.pack_into("<I", block, 60, ((bit_count >> 2) | 1) & MASK32)
        state = md5_transform(state, struct.unpack("<16I", block))

    return struct.pack("<4I", *state)


def parse_chunks(data: bytes) -> tuple[list[int], list[tuple[bytes, bytearray]]]:
    if len(data) < 52 or data[:4] != b"DXBC":
        raise SystemExit("invalid DXBC header")
    marker, total_size, chunk_count = struct.unpack_from("<III", data, 20)
    if marker != 1 or total_size != len(data) or chunk_count != 5:
        raise SystemExit(f"unexpected DXBC header marker={marker} total={total_size} count={chunk_count}")
    offsets = list(struct.unpack_from("<5I", data, 32))
    chunks: list[tuple[bytes, bytearray]] = []
    for off in offsets:
        if off + 8 > len(data):
            raise SystemExit("chunk header out of range")
        fourcc = data[off:off + 4]
        size = struct.unpack_from("<I", data, off + 4)[0]
        end = off + 8 + size
        if end > len(data):
            raise SystemExit(f"chunk {fourcc!r} out of range")
        chunks.append((fourcc, bytearray(data[off + 8:end])))
    return offsets, chunks


def patch_dxbc(data: bytes) -> tuple[bytes, dict]:
    base_sha = sha256_bytes(data)
    if base_sha != EXPECTED_BASELINE_SHA256:
        raise SystemExit(f"LAB38A SHA mismatch: {base_sha}")
    if data[4:20] != EXPECTED_BASELINE_CHECKSUM:
        raise SystemExit(f"LAB38A stored checksum mismatch: {data[4:20].hex()}")
    recomputed = calculate_dxbc_checksum(data)
    if recomputed != EXPECTED_BASELINE_CHECKSUM:
        raise SystemExit(f"DXBC checksum implementation self-test failed: {recomputed.hex()}")

    offsets, chunks = parse_chunks(data)
    if offsets != EXPECTED_CHUNK_OFFSETS:
        raise SystemExit(f"unexpected LAB38A chunk offsets: {offsets}")
    if [fourcc for fourcc, _ in chunks] != EXPECTED_CHUNKS:
        raise SystemExit(f"unexpected LAB38A chunk sequence: {[x[0] for x in chunks]}")

    shex = chunks[3][1]
    if len(shex) != 1776:
        raise SystemExit(f"unexpected SHEX payload size {len(shex)}")
    words = list(struct.unpack("<%dI" % (len(shex) // 4), shex))
    if len(words) != 444 or words[0] != 0x50 or words[1] != 444:
        raise SystemExit("unexpected SHEX header")
    if words[295:302] != EXPECTED_EQ_TOKENS:
        raise SystemExit("expected phase-zero EQ token sequence not found at frozen location")
    if words[434:443] != EXPECTED_FINAL_MOVC:
        raise SystemExit("expected final MOVC token sequence not found at frozen location")

    del words[295:302]
    if words[427:436] != EXPECTED_FINAL_MOVC:
        raise SystemExit("MOVC did not shift to expected location after EQ removal")
    words[427:436] = PATCHED_FINAL_MOVC
    words[1] = len(words)
    if len(words) != 437:
        raise SystemExit(f"unexpected patched SHEX dword count {len(words)}")
    chunks[3] = (b"SHEX", bytearray(struct.pack("<%dI" % len(words), *words)))

    stat = chunks[4][1]
    if len(stat) != 148:
        raise SystemExit(f"unexpected STAT payload size {len(stat)}")
    stats = list(struct.unpack("<37I", stat))
    if stats[0] != 49 or stats[1] != 8 or stats[4] != 37:
        raise SystemExit(f"unexpected LAB38A STAT signature inst={stats[0]} temps={stats[1]} float={stats[4]}")
    stats[0] = 48
    stats[4] = 36
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
    if new_offsets != [52, 484, 536, 588, 2344] or len(out) != 2500:
        raise SystemExit(f"unexpected patched container layout offsets={new_offsets} size={len(out)}")

    struct.pack_into("<I", out, 24, len(out))
    struct.pack_into("<5I", out, 32, *new_offsets)
    out[4:20] = b"\x00" * 16
    out[4:20] = calculate_dxbc_checksum(bytes(out))
    if calculate_dxbc_checksum(bytes(out)) != out[4:20]:
        raise SystemExit("patched DXBC checksum verification failed")

    result = bytes(out)
    audit = {
        "protocol": "LAB47A_DXBC_PHASE_MASK_PATCH",
        "baseline": "LAB38A",
        "baseline_sha256": base_sha,
        "baseline_checksum": EXPECTED_BASELINE_CHECKSUM.hex(),
        "candidate_sha256": sha256_bytes(result),
        "candidate_checksum": result[4:20].hex(),
        "baseline_bytes": len(data),
        "candidate_bytes": len(result),
        "byte_delta": len(result) - len(data),
        "baseline_shex_dwords": 444,
        "candidate_shex_dwords": 437,
        "removed_instruction": "eq r0.y, r0.z, 0.0",
        "rewritten_final_selection": "movc o0.xyzw, r0.zzzz, r1.xyzw, r3.xyzw",
        "stat_instruction_count": stats[0],
        "stat_temp_register_count": stats[1],
        "stat_float_instruction_count": stats[4],
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
