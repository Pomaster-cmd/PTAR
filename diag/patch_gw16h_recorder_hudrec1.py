#!/usr/bin/env python3
"""PTAR SAFEPOINT11 HUDREC1 surgical patch.

FG-OFF recorder feed only:
  native recorder wrapper RVA 0x34FE9B9
  from game swapchain global 0x02C7E010
  to   native presenter swapchain global 0x02C7DFC8

This makes the FG-OFF recorder consume the same visible presenter BackBuffer0
selected by the native F9 capture path, i.e. the post-PTAR in-frame HUD surface.
FG-ON recorder routing is intentionally untouched.
"""
from pathlib import Path
import argparse, hashlib, struct, sys

BASE_SHA = '864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'
EXPECTED_OUT_SHA = 'e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'
PATCH_RVA = 0x34FE9B9
OLD_SWAPCHAIN_RVA = 0x02C7E010
NEW_SWAPCHAIN_RVA = 0x02C7DFC8


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_pe(data: bytearray):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('PE signature missing')
    nsec = struct.unpack_from('<H', data, pe+6)[0]
    optsz = struct.unpack_from('<H', data, pe+20)[0]
    sec_off = pe + 24 + optsz
    secs = []
    for i in range(nsec):
        o = sec_off + i*40
        name = bytes(data[o:o+8]).split(b'\0')[0].decode('ascii','replace')
        vs, va, rs, rp = struct.unpack_from('<IIII', data, o+8)
        secs.append((name, va, vs, rp, rs))
    return pe, secs


def rva_to_off(rva, secs):
    for name, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs):
            return rp + (rva-va), name
    raise ValueError('RVA not mapped: 0x%X' % rva)


def pe_checksum(data: bytearray, checksum_off: int) -> int:
    d = bytearray(data)
    d[checksum_off:checksum_off+4] = b'\0'*4
    total = 0
    i = 0
    while i+1 < len(d):
        total = (total + d[i] + (d[i+1] << 8)) & 0xffffffff
        i += 2
    if i < len(d):
        total += d[i]
    total = (total & 0xffff) + (total >> 16)
    total = total + (total >> 16)
    return ((total & 0xffff) + len(d)) & 0xffffffff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('input')
    ap.add_argument('output')
    a = ap.parse_args()
    src = Path(a.input)
    out = Path(a.output)
    data = bytearray(src.read_bytes())
    got = sha(data)
    if got != BASE_SHA:
        raise SystemExit('BASE_SHA mismatch: %s' % got)
    pe, secs = parse_pe(data)
    off, sec = rva_to_off(PATCH_RVA, secs)
    if sec != '.dwmlab':
        raise SystemExit('patch RVA not in .dwmlab')
    if bytes(data[off:off+3]) != b'\x48\x8b\x0d':
        raise SystemExit('native swapchain load signature mismatch')
    old_disp = struct.unpack_from('<i', data, off+3)[0]
    old_target = PATCH_RVA + 7 + old_disp
    if old_target != OLD_SWAPCHAIN_RVA:
        raise SystemExit('unexpected old target: 0x%X' % old_target)
    new_disp = NEW_SWAPCHAIN_RVA - (PATCH_RVA + 7)
    struct.pack_into('<i', data, off+3, new_disp)
    if PATCH_RVA + 7 + struct.unpack_from('<i', data, off+3)[0] != NEW_SWAPCHAIN_RVA:
        raise SystemExit('new target verification failed')
    checksum_off = pe + 24 + 64
    struct.pack_into('<I', data, checksum_off, 0)
    struct.pack_into('<I', data, checksum_off, pe_checksum(data, checksum_off))
    new_sha = sha(data)
    if new_sha != EXPECTED_OUT_SHA:
        raise SystemExit('OUT_SHA mismatch: %s' % new_sha)
    out.write_bytes(data)
    print('HUDREC1=PASS')
    print('BASE_SHA='+BASE_SHA)
    print('OUT_SHA='+new_sha)
    print('PATCH_RVA=0x%X' % PATCH_RVA)
    print('FG_OFF_RECORDER_SWAPCHAIN=0x%X->0x%X' % (OLD_SWAPCHAIN_RVA,NEW_SWAPCHAIN_RVA))
    print('FG_ON_RECORDER=UNCHANGED')

if __name__ == '__main__':
    main()
