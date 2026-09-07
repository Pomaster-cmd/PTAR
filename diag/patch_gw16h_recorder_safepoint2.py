#!/usr/bin/env python3
"""GW16H UNIFIEDREC3 SAFEPOINT2 / NATIVEUSR1 surgical binary patch.

Base: exact SAFEPOINT1 runtime.

Field evidence from SAFEPOINT1 proved that the active FG-OFF route was one of four
native USR presenter branches, while SAFEPOINT1 consumed the recorder request only in
the isolated/common presenter branch. This patch covers all five late presenter routes.

FG ON:
  - native wrappers are inert for recorder work;
  - the existing isolated presenter wrapper keeps feeding the final REAL+GENERATED
    texture to the original B18K18 submit function.

FG OFF:
  - the four native presenter branches route through one wrapper immediately before
    their existing F9 deferred-capture check;
  - the wrapper obtains the current swapchain backbuffer with IDXGISwapChain::GetBuffer,
    and feeds native device/context/backbuffer to the same original B18K18 submit;
  - recorder start resource/submit identity guards are EXTENDED (not removed) to accept
    this proven native D3D11 tuple only while FG is disabled.

Original QSV encoder, recorder state machine, pacing, FG algorithm, quality paths, F9,
and all unrelated native-presenter logic remain unchanged. No external recorder bridge.
"""
from pathlib import Path
import hashlib, struct, sys

BASE_SHA = '6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'
FINAL_SHA = '7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630'
SIZE = 320000

ACTION8_SITE = 0x12304
ACTION8_RETURN = 0x12309
RECORDER_HANDLER = 0x19DC0
LOGGER = 0x1E50
RECORDER_SUBMIT = 0x27760
SUBMIT_SITE = 0x25B49

FG_ENABLED = 0x2C3F6CC
ISO_DEVICE = 0x2C925E0
ISO_READY = 0x2C925C4
NATIVE_DEVICE = 0x4B118
NATIVE_CONTEXT = 0x2C7DFA8
NATIVE_SWAPCHAIN = 0x2C7E010
IID_ID3D11TEXTURE2D = 0x32058
REC_ACTIVE = 0x4B170
REC_STATE = 0x2CCE660

PENDING = 0x34FE900
REQUEST = 0x34FE910
ISO_WRAPPER = 0x34FE930
NATIVE_WRAPPER = 0x34FE980
RESOURCE_HELPER = 0x34FEA60
IDENTITY_HELPER = 0x34FEAD0
MSG_REQUEST = 0x34FEB20
MSG_NATIVE = 0x34FEB60
MSG_ISOLATED = 0x34FEBA8
CAVE_FREE_START = 0x34FE953
CAVE_FREE_END = 0x34FEC00

NATIVE_CALL_SITES = (0xC810, 0xC90E, 0xD6F0, 0xD7EE)
ORIGINAL_NATIVE_POST = 0x14930
UNTOUCHED_NATIVE_CALL = 0x150C3
F9_CHECKS = (0xC815, 0xC913, 0xD6F5, 0xD7F3, 0x25AF5)

START_RESOURCE_SITE = 0x19EEF
START_RESOURCE_LEN = 20
START_FAIL = 0x19F3A
SUBMIT_ID_SITE = 0x277BF
SUBMIT_ID_LEN = 13
SUBMIT_FAIL = 0x27C0A

START_RESOURCE_SAFEPOINT1 = bytes.fromhex('48833de986c7020074418b05c586c70285c07437')
SUBMIT_ID_SAFEPOINT1 = bytes.fromhex('48390d1aaec6020f853e040000')
REQUEST_SAFEPOINT1 = bytes.fromhex('c605e9ffffff01e9ed39b1fc')
ISO_WRAPPER_SAFEPOINT1 = bytes.fromhex('4883ec28e8278eb2fc803dc0ffffff00740cc605b7ffffff00e872b4b1fc4883c428c3')

MSG_REQUEST_BYTES = b'HOTKEY B18K18 SAFEPOINT2 request queued\0'
MSG_NATIVE_BYTES = b'HOTKEY B18K18 SAFEPOINT2 consumed native USR\0'
MSG_ISOLATED_BYTES = b'HOTKEY B18K18 SAFEPOINT2 consumed isolated\0'


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


def rel32(src_rva, insn_len, dst_rva):
    d = dst_rva - (src_rva + insn_len)
    if not (-0x80000000 <= d <= 0x7fffffff):
        raise RuntimeError('rel32 out of range: %#x -> %#x' % (src_rva, dst_rva))
    return struct.pack('<i', d)


def call_target(b, site):
    raw = b[roff(b, site):roff(b, site)+5]
    if len(raw) != 5 or raw[0] != 0xE8:
        return None
    return site + 5 + struct.unpack_from('<i', raw, 1)[0]


def jmp_target(b, site):
    raw = b[roff(b, site):roff(b, site)+5]
    if len(raw) != 5 or raw[0] != 0xE9:
        return None
    return site + 5 + struct.unpack_from('<i', raw, 1)[0]


def checksum(blob, off):
    x = bytearray(blob)
    struct.pack_into('<I', x, off, 0)
    s = 0
    for i in range(0, len(x)-1, 2):
        s += x[i] | (x[i+1] << 8)
        s = (s & 0xffff) + (s >> 16)
    if len(x) & 1:
        s += x[-1]
    s = (s & 0xffff) + (s >> 16)
    s = (s & 0xffff) + (s >> 16)
    return (s + len(x)) & 0xffffffff


class Emitter:
    def __init__(self, base):
        self.base = base
        self.buf = bytearray()
        self.labels = {}
        self.fixups = []

    @property
    def rva(self):
        return self.base + len(self.buf)

    def emit(self, data):
        self.buf += bytes(data)

    def label(self, name):
        self.labels[name] = self.rva

    def call(self, target):
        s = self.rva
        self.emit(b'\xE8' + rel32(s, 5, target))

    def jcc(self, cc, label):
        op = {'e': b'\x0F\x84', 'ne': b'\x0F\x85', 's': b'\x0F\x88'}[cc]
        self.emit(op)
        pos = len(self.buf)
        self.emit(b'\0' * 4)
        self.fixups.append((pos, label))

    def rip(self, prefix, target, ilen):
        s = self.rva
        self.emit(prefix + struct.pack('<i', target - (s + ilen)))

    def finish(self):
        for pos, label in self.fixups:
            if label not in self.labels:
                raise RuntimeError('undefined label: ' + label)
            struct.pack_into('<i', self.buf, pos, self.labels[label] - (self.base + pos + 4))
        return bytes(self.buf)


def build_request():
    # Reached by JMP from a site that was originally a CALL. RSP is already caller-call
    # aligned, so allocate 0x20 shadow space (NOT 0x28) before calling the logger.
    e = Emitter(REQUEST)
    e.rip(b'\xC6\x05', PENDING, 7); e.emit(b'\x01')
    e.emit(b'\x48\x83\xEC\x20')
    e.rip(b'\x48\x8D\x0D', MSG_REQUEST, 7)
    e.call(LOGGER)
    e.emit(b'\x48\x83\xC4\x20')
    s = e.rva; e.emit(b'\xE9' + rel32(s, 5, ACTION8_RETURN))
    out = e.finish()
    if len(out) != 32:
        raise RuntimeError('request size drift: %d' % len(out))
    return out


def build_isolated_wrapper():
    e = Emitter(ISO_WRAPPER)
    e.emit(b'\x48\x83\xEC\x28')
    e.call(RECORDER_SUBMIT)
    e.rip(b'\x80\x3D', PENDING, 7); e.emit(b'\x00')
    e.jcc('e', 'done')
    e.rip(b'\xC6\x05', PENDING, 7); e.emit(b'\x00')
    e.rip(b'\x48\x8D\x0D', MSG_ISOLATED, 7)
    e.call(LOGGER)
    e.call(RECORDER_HANDLER)
    e.label('done')
    e.emit(b'\x48\x83\xC4\x28\xC3')
    return e.finish()


def build_native_wrapper():
    e = Emitter(NATIVE_WRAPPER)
    e.emit(b'\x48\x83\xEC\x38')
    e.call(ORIGINAL_NATIVE_POST)

    # The native feed must never compete with the isolated REAL+GENERATED feed.
    e.rip(b'\x83\x3D', FG_ENABLED, 7); e.emit(b'\x00')
    e.jcc('ne', 'done')

    # Avoid GetBuffer overhead while the recorder is inactive. State 3 is the existing
    # drain/transition path accepted by B18K18 submit even when REC_ACTIVE is clear.
    e.rip(b'\x83\x3D', REC_ACTIVE, 7); e.emit(b'\x00')
    e.jcc('ne', 'submit_try')
    e.rip(b'\x83\x3D', REC_STATE, 7); e.emit(b'\x03')
    e.jcc('ne', 'toggle')

    e.label('submit_try')
    e.emit(b'\x48\xC7\x44\x24\x30\x00\x00\x00\x00')  # local texture = NULL
    e.rip(b'\x48\x8B\x0D', NATIVE_SWAPCHAIN, 7)
    e.emit(b'\x48\x85\xC9')
    e.jcc('e', 'toggle')
    e.emit(b'\x48\x8B\x01\x48\x8B\x40\x48')  # vtbl; IDXGISwapChain::GetBuffer
    e.emit(b'\x31\xD2')                         # Buffer=0
    e.rip(b'\x4C\x8D\x05', IID_ID3D11TEXTURE2D, 7)
    e.emit(b'\x4C\x8D\x4C\x24\x30\xFF\xD0')
    e.emit(b'\x85\xC0')
    e.jcc('s', 'toggle')
    e.emit(b'\x4C\x8B\x44\x24\x30\x4D\x85\xC0')
    e.jcc('e', 'toggle')
    e.rip(b'\x48\x8B\x0D', NATIVE_DEVICE, 7)
    e.rip(b'\x48\x8B\x15', NATIVE_CONTEXT, 7)
    e.emit(b'\x48\x85\xC9'); e.jcc('e', 'release')
    e.emit(b'\x48\x85\xD2'); e.jcc('e', 'release')
    e.call(RECORDER_SUBMIT)

    e.label('release')
    e.emit(b'\x48\x8B\x4C\x24\x30\x48\x85\xC9')
    e.jcc('e', 'toggle')
    e.emit(b'\x48\x8B\x01\xFF\x50\x10')  # IUnknown::Release

    e.label('toggle')
    e.rip(b'\x80\x3D', PENDING, 7); e.emit(b'\x00')
    e.jcc('e', 'done')
    e.rip(b'\xC6\x05', PENDING, 7); e.emit(b'\x00')
    e.rip(b'\x48\x8D\x0D', MSG_NATIVE, 7)
    e.call(LOGGER)
    e.call(RECORDER_HANDLER)

    e.label('done')
    e.emit(b'\x48\x83\xC4\x38\xC3')
    return e.finish()


def build_resource_helper():
    # Return EAX=1 only for the resource family matching the active presenter mode.
    e = Emitter(RESOURCE_HELPER)
    e.rip(b'\x83\x3D', FG_ENABLED, 7); e.emit(b'\x00')
    e.jcc('e', 'native')
    e.rip(b'\x48\x8B\x05', ISO_DEVICE, 7); e.emit(b'\x48\x85\xC0'); e.jcc('e', 'fail')
    e.rip(b'\x83\x3D', ISO_READY, 7); e.emit(b'\x00'); e.jcc('e', 'fail')
    e.emit(b'\xB8\x01\x00\x00\x00\xC3')
    e.label('native')
    e.rip(b'\x48\x8B\x05', NATIVE_DEVICE, 7); e.emit(b'\x48\x85\xC0'); e.jcc('e', 'fail')
    e.rip(b'\x48\x8B\x05', NATIVE_CONTEXT, 7); e.emit(b'\x48\x85\xC0'); e.jcc('e', 'fail')
    e.rip(b'\x48\x8B\x05', NATIVE_SWAPCHAIN, 7); e.emit(b'\x48\x85\xC0'); e.jcc('e', 'fail')
    e.emit(b'\xB8\x01\x00\x00\x00\xC3')
    e.label('fail'); e.emit(b'\x31\xC0\xC3')
    return e.finish()


def build_identity_helper():
    # RCX is the submit device. Accept isolated always; accept native only while FG OFF.
    e = Emitter(IDENTITY_HELPER)
    e.rip(b'\x48\x8B\x05', ISO_DEVICE, 7); e.emit(b'\x48\x39\xC1'); e.jcc('e', 'ok')
    e.rip(b'\x83\x3D', FG_ENABLED, 7); e.emit(b'\x00'); e.jcc('ne', 'fail')
    e.rip(b'\x48\x8B\x05', NATIVE_DEVICE, 7); e.emit(b'\x48\x39\xC1'); e.jcc('ne', 'fail')
    e.label('ok'); e.emit(b'\xB8\x01\x00\x00\x00\xC3')
    e.label('fail'); e.emit(b'\x31\xC0\xC3')
    return e.finish()


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: patch_gw16h_recorder_safepoint2.py SAFEPOINT1_DLL OUT_DLL')
    src, dst = map(Path, sys.argv[1:])
    base = src.read_bytes()
    if len(base) != SIZE or sha_bytes(base) != BASE_SHA:
        raise RuntimeError('exact SAFEPOINT1 runtime required; got size=%d sha=%s' % (len(base), sha_bytes(base)))

    _opt, secs = parse(base)
    dwm = [s for s in secs if s[0] == '.dwmlab']
    if len(dwm) != 1 or (dwm[0][5] & 0xE0000000) != 0xE0000000:
        raise RuntimeError('.dwmlab RWX topology lock failed: %r' % (dwm,))

    if jmp_target(base, ACTION8_SITE) != REQUEST:
        raise RuntimeError('SAFEPOINT1 action8 request lock failed')
    if call_target(base, SUBMIT_SITE) != ISO_WRAPPER:
        raise RuntimeError('SAFEPOINT1 isolated wrapper lock failed')
    if base[roff(base, REQUEST):roff(base, REQUEST)+len(REQUEST_SAFEPOINT1)] != REQUEST_SAFEPOINT1:
        raise RuntimeError('SAFEPOINT1 request bytes lock failed')
    if base[roff(base, ISO_WRAPPER):roff(base, ISO_WRAPPER)+len(ISO_WRAPPER_SAFEPOINT1)] != ISO_WRAPPER_SAFEPOINT1:
        raise RuntimeError('SAFEPOINT1 isolated wrapper bytes lock failed')
    for site in NATIVE_CALL_SITES:
        if call_target(base, site) != ORIGINAL_NATIVE_POST:
            raise RuntimeError('native presenter call lock failed at %#x' % site)
    if call_target(base, UNTOUCHED_NATIVE_CALL) != ORIGINAL_NATIVE_POST:
        raise RuntimeError('unrelated native call lock failed')
    if base[roff(base, START_RESOURCE_SITE):roff(base, START_RESOURCE_SITE)+START_RESOURCE_LEN] != START_RESOURCE_SAFEPOINT1:
        raise RuntimeError('start resource guard lock failed')
    if base[roff(base, SUBMIT_ID_SITE):roff(base, SUBMIT_ID_SITE)+SUBMIT_ID_LEN] != SUBMIT_ID_SAFEPOINT1:
        raise RuntimeError('submit device identity lock failed')

    free = base[roff(base, CAVE_FREE_START):roff(base, CAVE_FREE_END)]
    if free != b'\xCC' * (CAVE_FREE_END - CAVE_FREE_START):
        bad = next(i for i, x in enumerate(free) if x != 0xCC)
        raise RuntimeError('SAFEPOINT2 cave lock failed at %#x' % (CAVE_FREE_START + bad))

    req = build_request()
    iso = build_isolated_wrapper()
    native = build_native_wrapper()
    resource = build_resource_helper()
    identity = build_identity_helper()
    payloads = [
        (REQUEST, req, 'request'), (ISO_WRAPPER, iso, 'isolated wrapper'),
        (NATIVE_WRAPPER, native, 'native wrapper'), (RESOURCE_HELPER, resource, 'resource helper'),
        (IDENTITY_HELPER, identity, 'identity helper'),
        (MSG_REQUEST, MSG_REQUEST_BYTES, 'request log'), (MSG_NATIVE, MSG_NATIVE_BYTES, 'native log'),
        (MSG_ISOLATED, MSG_ISOLATED_BYTES, 'isolated log')
    ]
    for i, (a, data, name) in enumerate(payloads):
        z = a + len(data)
        if not (0x34FE900 <= a < z <= CAVE_FREE_END):
            raise RuntimeError('%s out of reserved cave: %#x-%#x' % (name, a, z))
        for b_rva, b_data, b_name in payloads[i+1:]:
            bz = b_rva + len(b_data)
            if not (z <= b_rva or bz <= a):
                raise RuntimeError('cave overlap: %s / %s' % (name, b_name))

    out = bytearray(base)
    for rva, data, _name in payloads:
        out[roff(base, rva):roff(base, rva)+len(data)] = data

    for site in NATIVE_CALL_SITES:
        out[roff(base, site):roff(base, site)+5] = b'\xE8' + rel32(site, 5, NATIVE_WRAPPER)

    # Replace only the isolated-device/ready start guard pair with a mode-aware helper.
    p = bytearray(b'\xE8' + rel32(START_RESOURCE_SITE, 5, RESOURCE_HELPER) + b'\x84\xC0')
    j = START_RESOURCE_SITE + len(p)
    p += b'\x0F\x84' + rel32(j, 6, START_FAIL)
    p += b'\x90' * (START_RESOURCE_LEN - len(p))
    if len(p) != START_RESOURCE_LEN:
        raise RuntimeError('start resource patch size drift')
    out[roff(base, START_RESOURCE_SITE):roff(base, START_RESOURCE_SITE)+START_RESOURCE_LEN] = p

    # Preserve the submit safety check, extending accepted identity to native device only FG OFF.
    p = bytearray(b'\xE8' + rel32(SUBMIT_ID_SITE, 5, IDENTITY_HELPER) + b'\x84\xC0')
    j = SUBMIT_ID_SITE + len(p)
    p += b'\x0F\x84' + rel32(j, 6, SUBMIT_FAIL)
    if len(p) != SUBMIT_ID_LEN:
        raise RuntimeError('submit identity patch size drift')
    out[roff(base, SUBMIT_ID_SITE):roff(base, SUBMIT_ID_SITE)+SUBMIT_ID_LEN] = p

    opt, _secs = parse(out)
    csoff = opt + 64
    struct.pack_into('<I', out, csoff, 0)
    struct.pack_into('<I', out, csoff, checksum(out, csoff))

    got = sha_bytes(out)
    if got != FINAL_SHA:
        raise RuntimeError('deterministic output SHA drift: got %s expected %s' % (got, FINAL_SHA))
    dst.write_bytes(out)
    print('BASE_SHA256=' + BASE_SHA)
    print('OUT_SHA256=' + got)
    print('NATIVE_CALL_SITES=' + ','.join('0x%X' % x for x in NATIVE_CALL_SITES))
    print('NATIVE_WRAPPER_RVA=0x%X SIZE=%d' % (NATIVE_WRAPPER, len(native)))
    print('RESOURCE_HELPER_RVA=0x%X SIZE=%d' % (RESOURCE_HELPER, len(resource)))
    print('IDENTITY_HELPER_RVA=0x%X SIZE=%d' % (IDENTITY_HELPER, len(identity)))
    print('DELTA=MULTIPATH_NATIVE_USR_FEED + MODE_AWARE_RESOURCE_GUARD + MODE_AWARE_DEVICE_IDENTITY + DIAGNOSTIC_LOGS + PE_CHECKSUM')


if __name__ == '__main__':
    main()
