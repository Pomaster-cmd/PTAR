#!/usr/bin/env python3
"""Deterministic RC17 -> RC18 PRESENTDELIVERY1 hotfix.

Scope:
- Remove the two explicit pre-Present WaitForVBlank calls in the isolated display loop.
- Change IDXGISwapChain::Present from Present(0, DXGI_PRESENT_DO_NOT_WAIT)
  to blocking/vsynced Present(1, 0).
- Keep the swapchain, mailbox, generated/real selection, FG quality, NVENC/QPC
  generation pipeline and FGVblankWaitDisplaySlot implementation byte-identical.
"""
import hashlib
import struct
import sys
from pathlib import Path

EXPECTED_BASE_SHA256 = "5bdff6f5ecce928cb62f5dbbfe0cd3562eff455054620223cc4ba268c12c92fc"
IMAGE_BASE = 0x180000000
PREWAIT_CALL_RVAS = (0x259C9, 0x25B58)
PRESENT_ARGS_RVA = 0x265CF
PRESENT_CALL_RVA = 0x265D7
WAIT_FUNC_RVA = 0x26400
WAIT_FUNC_END_RVA = 0x2653C

OLD_PREWAIT = bytes.fromhex("e8 32 0a 00 00")
OLD_PREWAIT_2 = bytes.fromhex("e8 a3 08 00 00")
OLD_PRESENT_ARGS = bytes.fromhex("31 d2 41 b8 08 00 00 00")
NEW_PRESENT_ARGS = bytes.fromhex("ba 01 00 00 00 45 31 c0")

# Exact-length status string replacement avoids stale runtime diagnostics.
OLD_PRESENT_STRING = b"P1FG7N-VBLANK2 PRESENT MODE DISCARD + PRESENT(0,DO_NOT_WAIT) - EXACT N SWAPCHAIN RETAINED"
NEW_PRESENT_STRING = b"P1FG7N-PRESENTDELIVERY1 PRESENT(1,0) VSYNC - PREWAIT REMOVED - EXACT N SWAPCHAIN RETAINED"
assert len(OLD_PRESENT_STRING) == len(NEW_PRESENT_STRING) == 89

OLD_STARTUP_INFO = b"INFO: P1FG7N preserves P1FG7L QPC-only selection, dual-backbuffer isolated display, shared FG publisher/NVENC context, mailbox, Ctrl+F6 latch and Present(0, DO_NOT_WAIT); real frames remain mandatory"
_new_start = b"INFO: RC18 PRESENTDELIVERY1 preserves QPC selection, dual-backbuffer display, FG/NVENC/mailbox and Ctrl+F6; output uses Present(1,0), pre-Present WaitForVBlank removed; real frames mandatory"
NEW_STARTUP_INFO = _new_start + b" " * (len(OLD_STARTUP_INFO) - len(_new_start))
assert len(NEW_STARTUP_INFO) == len(OLD_STARTUP_INFO) == 199

OLD_SWAPCHAIN_INFO = b"OK: P1FG7N-VBLANK2 exact two-backbuffer DISCARD swapchain retained; isolated display is paced by output VBlank when available"
_new_swap = b"OK: RC18 PRESENTDELIVERY1 two-backbuffer DISCARD swapchain retained; display is paced by blocking Present(1,0), no pre-wait"
NEW_SWAPCHAIN_INFO = _new_swap + b" " * (len(OLD_SWAPCHAIN_INFO) - len(_new_swap))
assert len(NEW_SWAPCHAIN_INFO) == len(OLD_SWAPCHAIN_INFO) == 125


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def pe_checksum(blob: bytes, checksum_off: int) -> int:
    b = bytearray(blob)
    struct.pack_into("<I", b, checksum_off, 0)
    s = 0
    for i in range(0, len(b) - 1, 2):
        s += b[i] | (b[i + 1] << 8)
        s = (s & 0xFFFF) + (s >> 16)
    if len(b) & 1:
        s += b[-1]
        s = (s & 0xFFFF) + (s >> 16)
    s = (s & 0xFFFF) + (s >> 16)
    return (s + len(b)) & 0xFFFFFFFF


def parse_pe(b: bytes):
    if b[:2] != b"MZ":
        raise RuntimeError("MZ signature missing")
    e_lfanew = struct.unpack_from("<I", b, 0x3C)[0]
    if b[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise RuntimeError("PE signature missing")
    coff = e_lfanew + 4
    machine, nsec, _ts, _ps, _ns, optsz, _ch = struct.unpack_from("<HHIIIHH", b, coff)
    if machine != 0x8664:
        raise RuntimeError("Expected x64 PE")
    opt = coff + 20
    if struct.unpack_from("<H", b, opt)[0] != 0x20B:
        raise RuntimeError("Expected PE32+")
    image_base = struct.unpack_from("<Q", b, opt + 24)[0]
    if image_base != IMAGE_BASE:
        raise RuntimeError(f"Unexpected image base {image_base:#x}")
    checksum_off = opt + 64
    shoff = opt + optsz
    sections = []
    for i in range(nsec):
        o = shoff + i * 40
        name = b[o:o+8].rstrip(b"\0").decode("ascii", "replace")
        vs, va, rs, rp = struct.unpack_from("<IIII", b, o + 8)
        sections.append((name, va, vs, rp, rs))
    return {"checksum_off": checksum_off, "sections": sections, "nsec": nsec, "image_base": image_base}


def rva_to_off(pe, rva: int) -> int:
    for name, va, vs, rp, rs in pe["sections"]:
        span = max(vs, rs)
        if va <= rva < va + span:
            return rp + (rva - va)
    raise RuntimeError(f"RVA {rva:#x} not mapped")


def assert_call_target(b: bytes, pe, rva: int, expected_target_rva: int):
    off = rva_to_off(pe, rva)
    raw = b[off:off+5]
    if len(raw) != 5 or raw[0] != 0xE8:
        raise RuntimeError(f"Expected CALL rel32 at {rva:#x}, found {raw.hex()}")
    disp = struct.unpack_from("<i", raw, 1)[0]
    target = rva + 5 + disp
    if target != expected_target_rva:
        raise RuntimeError(f"Unexpected call target at {rva:#x}: {target:#x}, expected {expected_target_rva:#x}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: patch_presentdelivery1_rc17.py INPUT_RC17_DLL OUTPUT_RC18_DLL")
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    original = src.read_bytes()
    if sha256(original) != EXPECTED_BASE_SHA256:
        raise RuntimeError("Refusing input: DLL is not the exact validated RC17 base")

    pe = parse_pe(original)
    out = bytearray(original)

    # Verify the two and only two pre-Present calls target FGVblankWaitDisplaySlot.
    assert_call_target(original, pe, PREWAIT_CALL_RVAS[0], WAIT_FUNC_RVA)
    assert_call_target(original, pe, PREWAIT_CALL_RVAS[1], WAIT_FUNC_RVA)
    for rva, expected in zip(PREWAIT_CALL_RVAS, (OLD_PREWAIT, OLD_PREWAIT_2)):
        off = rva_to_off(pe, rva)
        if original[off:off+5] != expected:
            raise RuntimeError(f"Original prewait bytes changed at {rva:#x}")
        out[off:off+5] = b"\x90" * 5

    # Verify and replace only Present arguments. The indirect Present call itself stays intact.
    arg_off = rva_to_off(pe, PRESENT_ARGS_RVA)
    if original[arg_off:arg_off+len(OLD_PRESENT_ARGS)] != OLD_PRESENT_ARGS:
        raise RuntimeError("Present argument setup bytes do not match RC17")
    call_off = rva_to_off(pe, PRESENT_CALL_RVA)
    if original[call_off:call_off+2] != b"\xff\xd0":
        raise RuntimeError("Present indirect call bytes changed")
    out[arg_off:arg_off+len(NEW_PRESENT_ARGS)] = NEW_PRESENT_ARGS

    # Replace one exact status string so F8/log diagnostics don't claim the old policy.
    string_off = original.find(OLD_PRESENT_STRING)
    if string_off < 0 or original.find(OLD_PRESENT_STRING, string_off + 1) >= 0:
        raise RuntimeError("Expected unique old Present policy string not found")
    out[string_off:string_off+len(OLD_PRESENT_STRING)] = NEW_PRESENT_STRING

    # Replace the two other runtime messages that would otherwise falsely report the old delivery policy.
    for old_msg, new_msg, label in (
        (OLD_STARTUP_INFO, NEW_STARTUP_INFO, "startup info"),
        (OLD_SWAPCHAIN_INFO, NEW_SWAPCHAIN_INFO, "swapchain info"),
    ):
        msg_off = original.find(old_msg)
        if msg_off < 0 or original.find(old_msg, msg_off + 1) >= 0:
            raise RuntimeError(f"Expected unique old {label} string not found")
        out[msg_off:msg_off+len(old_msg)] = new_msg

    # FGVblankWaitDisplaySlot implementation itself must remain untouched.
    wait_a = rva_to_off(pe, WAIT_FUNC_RVA)
    wait_b = rva_to_off(pe, WAIT_FUNC_END_RVA)
    if out[wait_a:wait_b] != original[wait_a:wait_b]:
        raise RuntimeError("WaitForVBlank helper implementation changed unexpectedly")

    # Recompute PE checksum after all deterministic changes.
    checksum_off = pe["checksum_off"]
    struct.pack_into("<I", out, checksum_off, 0)
    checksum = pe_checksum(bytes(out), checksum_off)
    struct.pack_into("<I", out, checksum_off, checksum)

    dst.write_bytes(out)
    print(f"BASE_SHA256={sha256(original)}")
    print(f"OUT_SHA256={sha256(out)}")
    print(f"PE_CHECKSUM=0x{checksum:08x}")
    print("PATCH=PRESENTDELIVERY1")
    print("PREWAIT_CALLS_NOP=2")
    print("PRESENT=SyncInterval1 Flags0")
    print("STATUS_STRING=PRESENTDELIVERY1")


if __name__ == "__main__":
    main()
