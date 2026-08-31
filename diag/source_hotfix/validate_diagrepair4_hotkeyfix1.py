#!/usr/bin/env python3
"""Static package gate for RC17 DIAGREPAIR4 / HOTKEYFIX1."""
from __future__ import annotations
import hashlib
import struct
import sys
from pathlib import Path

EXPECTED_DLL = "5bdff6f5ecce928cb62f5dbbfe0cd3562eff455054620223cc4ba268c12c92fc"
EXPECTED_INI = "a7673260e016ab2d6b8923850ef53e40e4ba8da2c0a224703140c1ed7ab21499"
EXPECTED_VERIFIER = "6ac4a17bc5f37ebe386b652a911f516762e7357e6f076819f1c97d7433b2ca5a"
BASE_VERIFIER = "cfeb9be1b1fbdae2fb9a763588a40cc2ea0b81b56469476f02dc82e3ec6c383b"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def passfail(cond: bool, label: str, out: list[str]) -> None:
    out.append(("PASS " if cond else "FAIL ") + label)
    if not cond:
        raise AssertionError(label)


def pe_machine_subsystem(data: bytes) -> tuple[int, int]:
    if data[:2] != b"MZ":
        raise ValueError("not MZ")
    peoff = struct.unpack_from("<I", data, 0x3C)[0]
    if data[peoff:peoff+4] != b"PE\0\0":
        raise ValueError("not PE")
    machine = struct.unpack_from("<H", data, peoff + 4)[0]
    opt = peoff + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x20B:
        raise ValueError("not PE32+")
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]
    return machine, subsystem


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]).resolve()
    out: list[str] = []
    dll = root / "win81_nis_dx11_x64.dll"
    ini = root / "win81_nis.ini"
    verifier = root / "diag/visible_verifier/win81_vblank2_visible_marker_x64.exe"
    bat = root / "diag/04-ARM_VISIBLE_FRAME_VERIFIER.bat"
    verify = root / "VERIFY_FULLSTACK1_INSTALL.bat"

    passfail(sha(dll) == EXPECTED_DLL, "runtime DLL unchanged RC17", out)
    passfail(sha(ini) == EXPECTED_INI, "win81_nis.ini unchanged", out)
    passfail(sha(verifier) == EXPECTED_VERIFIER, "visible verifier HOTKEYFIX1 exact hash", out)
    data = verifier.read_bytes()
    passfail(len(data) == 0x4000, "visible verifier size unchanged 16384", out)
    machine, subsystem = pe_machine_subsystem(data)
    passfail(machine == 0x8664, "visible verifier PE x64", out)
    passfail(subsystem == 3, "visible verifier Windows CUI", out)
    passfail(data[0x0DDE:0x0DE0] == b"\x90\x90", "CTRL gate 1 bypassed", out)
    passfail(data[0x0E3F:0x0E41] == b"\x90\x90", "CTRL gate 2 bypassed", out)
    passfail(b"F5 or CTRL+F5 starts 20-second visible-marker measurement" in data, "ARMED text advertises F5/CTRL+F5", out)
    passfail(b"then CTRL+F5 starts 20-second visible-marker measurement" not in data, "old CTRL-only ARMED text absent", out)

    raw = bat.read_bytes()
    passfail(raw.isascii(), "04 batch ASCII", out)
    passfail(b"\r\n" in raw and raw.replace(b"\r\n", b"").find(b"\n") < 0, "04 batch CRLF", out)
    txt = raw.decode("ascii")
    passfail("DIAGREPAIR4 HOTKEYFIX1" in txt, "04 DIAGREPAIR4 marker", out)
    passfail("-Verb RunAs" in txt, "04 requests elevation", out)
    passfail("__PTAR_DIAG_INNER__" in txt, "04 inner elevated mode", out)
    passfail('"%VERIFIER%"' in txt, "04 direct verifier execution", out)
    passfail("PTAR_INSTALL_TEE_RAW" not in txt, "04 no installer Tee", out)
    passfail(EXPECTED_VERIFIER in txt, "04 verifier hash gate", out)
    passfail("Appuyer UNE FOIS sur F5" in txt, "04 F5 procedure", out)

    vraw = verify.read_bytes()
    passfail(vraw.isascii(), "FULLSTACK verifier ASCII", out)
    passfail(EXPECTED_VERIFIER.encode() in vraw, "FULLSTACK verifier checks HOTKEYFIX1 hash", out)
    passfail(b"DIAGREPAIR4 HOTKEYFIX1" in vraw, "FULLSTACK verifier checks DIAGREPAIR4", out)

    print("\n".join(out))
    print(f"RESULT {len(out)}/{len(out)} PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL {exc}")
        raise
