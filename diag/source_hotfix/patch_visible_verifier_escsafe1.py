#!/usr/bin/env python3
"""ESCSAFE1 patch for the locked HOTKEYFIX1 visible verifier.

Removes all three global ESC cancellation branches. The diagnostic can be
stopped only by manually closing its console window/process. F5/CTRL+F5
trigger and the capture/marker/20-second measurement engine are unchanged.
"""
from __future__ import annotations
import argparse, hashlib
from pathlib import Path
BASE_SHA256 = "6ac4a17bc5f37ebe386b652a911f516762e7357e6f076819f1c97d7433b2ca5a"
PATCHED_SHA256 = "67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305"
PATCHES = {
    0x0C92: (bytes.fromhex("0f8805070000"), bytes.fromhex("909090909090")),
    0x0E13: (bytes.fromhex("0f887d050000"), bytes.fromhex("909090909090")),
    0x136A: (bytes.fromhex("0f8937faffff"), bytes.fromhex("e938faffff90")),
}
ARMED_OFFSET = 0x36B7
OLD_ARMED = b"ARMED: enable FG, then F5 or CTRL+F5 starts 20-second visible-marker measurement. ESC cancels.\x00"
NEW_ARMED = b"ARMED: enable FG, then F5 or CTRL+F5 starts 20s measurement. Close window to stop.\x00"
def h(b): return hashlib.sha256(b).hexdigest()
def patch(data: bytes)->bytes:
    if h(data)!=BASE_SHA256: raise ValueError("input is not locked HOTKEYFIX1 verifier")
    out=bytearray(data)
    for off,(before,after) in PATCHES.items():
        if bytes(out[off:off+len(before)])!=before: raise ValueError(f"opcode mismatch at 0x{off:X}")
        out[off:off+len(after)]=after
    if bytes(out[ARMED_OFFSET:ARMED_OFFSET+len(OLD_ARMED)])!=OLD_ARMED: raise ValueError("ARMED string mismatch")
    out[ARMED_OFFSET:ARMED_OFFSET+len(OLD_ARMED)] = NEW_ARMED + b"\0"*(len(OLD_ARMED)-len(NEW_ARMED))
    final=bytes(out)
    if h(final)!=PATCHED_SHA256: raise ValueError("patched verifier hash mismatch")
    return final
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("input",type=Path); ap.add_argument("output",type=Path); ns=ap.parse_args()
    ns.output.write_bytes(patch(ns.input.read_bytes())); print(PATCHED_SHA256); return 0
if __name__=="__main__": raise SystemExit(main())
