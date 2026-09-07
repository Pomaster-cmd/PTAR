#!/usr/bin/env python3
"""GW16H UNIFIEDREC3 SAFEPOINT1 minimal binary patch.

Base: exact PTAR_GW16H_UNIFIEDREC2_INSTALLFIX1 runtime.

The prior unified-recorder patch removed the two explicit FG-only gates, but action 8
still called the original B18K18 toggle handler directly from the early hotkey phase of
the Present hook. The native presenter resources used by the recorder are consumed
later in the common presenter path. F9 already uses this deferred model.

This patch changes only recorder *toggle timing*:
  - action 8 writes a private pending byte and returns to the dispatcher;
  - the existing common presenter recorder-submit call is redirected to a tiny wrapper;
  - the wrapper calls the original B18K18 submit first, then consumes the pending toggle
    and calls the original B18K18 toggle handler at that presenter-safe point.

All original B18K18 start/stop/QSV/HUD/resource guards are retained. The explicit FG
start/submit gates already removed by UNIFIEDREC2 remain removed. No second recorder,
external bridge, pacing change, FG change, or quality-path change is introduced.
"""
from pathlib import Path
import hashlib, struct, sys

BASE_SHA = '4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'
SIZE = 320000
IMAGE_BASE = 0x180000000

ACTION8_SITE = 0x12304
ACTION8_RETURN = 0x12309
RECORDER_HANDLER = 0x19DC0
SUBMIT_SITE = 0x25B49
RECORDER_SUBMIT = 0x27760
START_FG_GATE = 0x19EE3
SUBMIT_FG_GATE = 0x277AB

# Locked unused part of the pre-existing RWX .dwmlab section.
PENDING_RVA = 0x34FE900
REQUEST_STUB_RVA = 0x34FE910
WRAPPER_RVA = 0x34FE930
CAVE_LOCK_START = 0x34FE900
CAVE_LOCK_END = 0x34FE980


def sha_bytes(b):
    return hashlib.sha256(bytes(b)).hexdigest()


def parse(b):
    e = struct.unpack_from('<I', b, 0x3C)[0]
    coff = e + 4
    n = struct.unpack_from('<H', b, coff + 2)[0]
    optsz = struct.unpack_from('<H', b, coff + 16)[0]
    opt = coff + 20
    sh = opt + optsz
    secs = []
    for i in range(n):
        o = sh + i * 40
        name = b[o:o+8].rstrip(b'\0').decode('ascii')
        vs, va, rs, rp = struct.unpack_from('<IIII', b, o + 8)
        ch = struct.unpack_from('<I', b, o + 36)[0]
        secs.append((name, vs, va, rs, rp, ch))
    return opt, secs


def roff(b, rva):
    for _name, vs, va, rs, rp, _ch in parse(b)[1]:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    raise RuntimeError('RVA not mapped: %#x' % rva)


def checksum(blob, off):
    b = bytearray(blob)
    struct.pack_into('<I', b, off, 0)
    s = 0
    for i in range(0, len(b)-1, 2):
        s += b[i] | (b[i+1] << 8)
        s = (s & 0xFFFF) + (s >> 16)
    if len(b) & 1:
        s += b[-1]
    s = (s & 0xFFFF) + (s >> 16)
    s = (s & 0xFFFF) + (s >> 16)
    return (s + len(b)) & 0xFFFFFFFF


def rel32(src_rva, insn_len, dst_rva):
    d = dst_rva - (src_rva + insn_len)
    if not (-0x80000000 <= d <= 0x7FFFFFFF):
        raise RuntimeError('rel32 out of range: %#x -> %#x' % (src_rva, dst_rva))
    return struct.pack('<i', d)


def expect_call(b, site, target, label):
    o = roff(b, site)
    raw = b[o:o+5]
    if len(raw) != 5 or raw[0] != 0xE8:
        raise RuntimeError('%s opcode lock failed: %s' % (label, raw.hex()))
    got = site + 5 + struct.unpack_from('<i', raw, 1)[0]
    if got != target:
        raise RuntimeError('%s target lock failed: got %#x expected %#x' % (label, got, target))


def build_request_stub():
    # mov byte ptr [rip+PENDING], 1 ; jmp ACTION8_RETURN
    r = REQUEST_STUB_RVA
    out = bytearray(b'\xC6\x05' + rel32(r, 7, PENDING_RVA) + b'\x01')
    j = r + len(out)
    out += b'\xE9' + rel32(j, 5, ACTION8_RETURN)
    return bytes(out)


def build_wrapper():
    # Preserve Win64 stack alignment/shadow space for nested calls. RCX/RDX/R8 reach
    # original submit untouched. After it returns they are no longer needed.
    out = bytearray()
    r = WRAPPER_RVA
    out += b'\x48\x83\xEC\x28'                        # sub rsp, 0x28
    call_submit = r + len(out)
    out += b'\xE8' + rel32(call_submit, 5, RECORDER_SUBMIT)
    cmp_site = r + len(out)
    out += b'\x80\x3D' + rel32(cmp_site, 7, PENDING_RVA) + b'\x00'  # cmp byte [rip+pending],0
    out += b'\x74\x0C'                                # je done (12 bytes ahead)
    clear_site = r + len(out)
    out += b'\xC6\x05' + rel32(clear_site, 7, PENDING_RVA) + b'\x00'
    call_toggle = r + len(out)
    out += b'\xE8' + rel32(call_toggle, 5, RECORDER_HANDLER)
    out += b'\x48\x83\xC4\x28\xC3'                # done: add rsp,0x28 ; ret
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: patch_gw16h_recorder_safepoint.py INSTALLFIX1_DLL OUT_DLL')
    src, dst = map(Path, sys.argv[1:])
    base = src.read_bytes()
    if len(base) != SIZE:
        raise RuntimeError('size lock failed: %d' % len(base))
    if sha_bytes(base) != BASE_SHA:
        raise RuntimeError('exact INSTALLFIX1 runtime required; got ' + sha_bytes(base))

    # Topology/section lock: the selected cave must still be RWX .dwmlab.
    _opt, secs = parse(base)
    dwm = [s for s in secs if s[0] == '.dwmlab']
    if len(dwm) != 1 or not (dwm[0][5] & 0xE0000000) == 0xE0000000:
        raise RuntimeError('.dwmlab RWX topology lock failed: %r' % (dwm,))

    expect_call(base, ACTION8_SITE, RECORDER_HANDLER, 'action8 direct recorder call')
    expect_call(base, SUBMIT_SITE, RECORDER_SUBMIT, 'common presenter submit call')
    if base[roff(base, START_FG_GATE):roff(base, START_FG_GATE)+2] != b'\x90\x90':
        raise RuntimeError('UNIFIEDREC2 start-FG NOP lock failed')
    if base[roff(base, SUBMIT_FG_GATE):roff(base, SUBMIT_FG_GATE)+6] != b'\x90' * 6:
        raise RuntimeError('UNIFIEDREC2 submit-FG NOP lock failed')

    cave = base[roff(base, CAVE_LOCK_START):roff(base, CAVE_LOCK_END)]
    if cave != b'\xCC' * (CAVE_LOCK_END - CAVE_LOCK_START):
        bad = next((i for i, x in enumerate(cave) if x != 0xCC), None)
        raise RuntimeError('code-cave lock failed at +%#x byte=%02x' % (bad, cave[bad] if bad is not None else -1))

    req = build_request_stub()
    wrap = build_wrapper()
    if REQUEST_STUB_RVA + len(req) > WRAPPER_RVA:
        raise RuntimeError('request stub overlaps wrapper')
    if WRAPPER_RVA + len(wrap) > CAVE_LOCK_END:
        raise RuntimeError('wrapper exceeds locked cave')

    out = bytearray(base)
    # Pending flag starts cleared.
    out[roff(base, PENDING_RVA)] = 0
    out[roff(base, REQUEST_STUB_RVA):roff(base, REQUEST_STUB_RVA)+len(req)] = req
    out[roff(base, WRAPPER_RVA):roff(base, WRAPPER_RVA)+len(wrap)] = wrap

    # Replace direct action8 CALL with tail JMP to the request stub. The request stub
    # returns to ACTION8_RETURN with a JMP, so dispatcher stack depth is unchanged.
    a = roff(base, ACTION8_SITE)
    out[a:a+5] = b'\xE9' + rel32(ACTION8_SITE, 5, REQUEST_STUB_RVA)
    # Keep CALL semantics at the presenter site but target our wrapper.
    s = roff(base, SUBMIT_SITE)
    out[s:s+5] = b'\xE8' + rel32(SUBMIT_SITE, 5, WRAPPER_RVA)

    opt, _ = parse(out)
    csoff = opt + 64
    struct.pack_into('<I', out, csoff, 0)
    struct.pack_into('<I', out, csoff, checksum(out, csoff))
    dst.write_bytes(out)

    print('BASE_SHA256=' + BASE_SHA)
    print('OUT_SHA256=' + sha_bytes(out))
    print('REQUEST_STUB_RVA=%#x SIZE=%d' % (REQUEST_STUB_RVA, len(req)))
    print('WRAPPER_RVA=%#x SIZE=%d' % (WRAPPER_RVA, len(wrap)))
    print('PENDING_RVA=%#x' % PENDING_RVA)
    print('DELTA=DEFER_ACTION8_TOGGLE_TO_COMMON_PRESENTER_SAFEPOINT + ORIGINAL_SUBMIT_FIRST + ORIGINAL_B18K18_TOGGLE + PE_CHECKSUM')


if __name__ == '__main__':
    main()
