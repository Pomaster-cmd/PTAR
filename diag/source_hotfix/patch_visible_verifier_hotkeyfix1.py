#!/usr/bin/env python3
"""Reproducible HOTKEYFIX1 patch for the existing P1FG7N-VBLANK2 visible verifier.

This does not alter the measurement/capture engine. It only removes the two
CTRL-down gates from the existing F5 edge detector and updates the ARMED text.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path

BASE_SHA256 = "cfeb9be1b1fbdae2fb9a763588a40cc2ea0b81b56469476f02dc82e3ec6c383b"
PATCHED_SHA256 = "6ac4a17bc5f37ebe386b652a911f516762e7357e6f076819f1c97d7433b2ca5a"
CODE_PATCHES = {
    0x0DDE: (bytes.fromhex("79 1d"), bytes.fromhex("90 90")),
    0x0E3F: (bytes.fromhex("79 bc"), bytes.fromhex("90 90")),
}
ARMED_OFFSET = 0x36B7
OLD_ARMED = b"ARMED: enable FG with CTRL+F6, then CTRL+F5 starts 20-second visible-marker measurement. ESC cancels.\x00"
NEW_ARMED = b"ARMED: enable FG, then F5 or CTRL+F5 starts 20-second visible-marker measurement. ESC cancels.\x00"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch(data: bytes) -> bytes:
    if sha256(data) != BASE_SHA256:
        raise ValueError("input verifier is not the locked DIAGREPAIR3 base")
    out = bytearray(data)
    for offset, (before, after) in CODE_PATCHES.items():
        if bytes(out[offset:offset + len(before)]) != before:
            raise ValueError(f"unexpected opcode at 0x{offset:X}")
        out[offset:offset + len(after)] = after
    if bytes(out[ARMED_OFFSET:ARMED_OFFSET + len(OLD_ARMED)]) != OLD_ARMED:
        raise ValueError("unexpected ARMED string")
    if len(NEW_ARMED) > len(OLD_ARMED):
        raise ValueError("replacement ARMED string does not fit")
    out[ARMED_OFFSET:ARMED_OFFSET + len(OLD_ARMED)] = NEW_ARMED + b"\0" * (len(OLD_ARMED) - len(NEW_ARMED))
    final = bytes(out)
    if sha256(final) != PATCHED_SHA256:
        raise ValueError("patched verifier hash mismatch")
    return final


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    ns = ap.parse_args()
    ns.output.write_bytes(patch(ns.input.read_bytes()))
    print(PATCHED_SHA256)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
