from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig").replace("\r\n", "\n")


def fail(msg: str) -> None:
    print("RC51_INHERITANCE=FAIL " + msg)
    raise SystemExit(1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_rc51_inheritance.py <generated_rc56_prod.cpp>")
    canonical = read(ROOT / "lab/presenter/rc51/ptar_borderless_rc51_prod.cpp")
    generated = read(Path(sys.argv[1]))

    canonical_comment = """// RC51 keeps RC46's field-proven window geometry/input policy byte-for-byte at
// source level. The only new behaviour is a presenter-swapchain backing-size
// reconciliation executed on the P1U46 presenter Present thread after a frame
// has been submitted. No SetParent/owner/WS_CHILD policy is introduced."""
    rc56_comment = """// RC56 retains RC51 presenter swapchain reconciliation and RC55 raster behavior,
// but changes Borderless authority only: P1U46 keeps the game HWND/windowed DXGI
// contract, while the separate presenter alone occupies the native monitor.
// No SetParent/owner/WS_CHILD policy is introduced."""

    if generated.count('#include "ptar_borderless_rc46_presenter_only.cpp"') != 1:
        fail("generated include marker missing")
    if generated.count(rc56_comment) != 1:
        fail("RC56 header comment marker missing")

    normalized = generated.replace(
        '#include "ptar_borderless_rc46_presenter_only.cpp"',
        '#include "../../borderless/rc46/ptar_borderless_rc46_prod.cpp"',
        1,
    ).replace(rc56_comment, canonical_comment, 1)

    if normalized != canonical:
        a = canonical.splitlines()
        b = normalized.splitlines()
        for i, (x, y) in enumerate(zip(a, b), 1):
            if x != y:
                fail(f"first_diff_line={i} canonical={x!r} generated={y!r}")
        fail(f"length_mismatch canonical_lines={len(a)} generated_lines={len(b)}")

    forbidden = ("SetParent(", "GWLP_HWNDPARENT")
    for token in forbidden:
        if token in generated:
            fail("forbidden_token=" + token)

    print("RC51_INHERITANCE=PASS")
    print("RC51_PRESENTER_SYNC_BODY=BYTE_FOR_BYTE_SOURCE_EQUIVALENT")
    print("RC56_DELTA_AT_RC51_LAYER=INCLUDE_AND_COMMENT_ONLY")


if __name__ == "__main__":
    main()
