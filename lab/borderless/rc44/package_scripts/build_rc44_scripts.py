from __future__ import annotations

from pathlib import Path
import re
import sys

OLD_BORDERLESS = "cd38729a93b8594203196bdda89efc343db0854f024aaba3a5663009a7503612"
NEW_BORDERLESS = "13f544d39bbe39811a83c1f5b3bf823d18ef00edf6c9d8cc8ac891208bdc8551"


def read_ps(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_ps(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8-sig", newline="")


def exact_replace(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f"missing transform anchor: {label}")
    return text.replace(old, new, 1)


def add_hash_to_array(text: str, variable: str, old_hash: str, new_hash: str) -> str:
    pattern = r"(\$" + re.escape(variable) + r"=@\([\s\S]*?'" + re.escape(old_hash) + r"')([\s\S]*?\))"
    match = re.search(pattern, text)
    if not match:
        raise RuntimeError(f"array anchor missing: {variable}")
    block = match.group(0)
    if new_hash in block:
        return text
    changed = block.replace(f"'{old_hash}'", f"'{old_hash}',\n '{new_hash}'", 1)
    return text[: match.start()] + changed + text[match.end() :]


def transform(source: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)

    p = source / "install.ps1"
    s = read_ps(p)
    s = exact_replace(s, f"$ExpectedBorderless='{OLD_BORDERLESS}'", f"$ExpectedBorderless='{NEW_BORDERLESS}'", "install expected RC44 carrier")
    s = add_hash_to_array(s, "KnownBorderless", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("Payload RC43 hash mismatch.", "Payload RC44 hash mismatch.")
    s = s.replace("package='PTAR_RC43_WINDOWED_BORDERLESS_COEXIST'", "package='PTAR_RC44_INQUISITOR_FRAMELESS_BORDERLESS_FIX'")
    s = s.replace("INSTALL_RC43=PASS", "INSTALL_RC44=PASS")
    s = s.replace("MODE=RC43_WINDOWED_BORDERLESS_COEXIST_WITH_RC42_RASTER", "MODE=RC44_INQUISITOR_FRAMELESS_BORDERLESS_FIX_WITH_RC42_RASTER")
    write_ps(output / p.name, s)

    p = source / "verify.ps1"
    s = read_ps(p)
    s = exact_replace(s, f"$ExpectedBorderless='{OLD_BORDERLESS}'", f"$ExpectedBorderless='{NEW_BORDERLESS}'", "verify expected RC44 carrier")
    s = s.replace("VERIFY_RC43_WINDOWED_BORDERLESS_COEXIST=PASS", "VERIFY_RC44_INQUISITOR_FRAMELESS_BORDERLESS_FIX=PASS")
    write_ps(output / p.name, s)

    p = source / "rollback.ps1"
    s = add_hash_to_array(read_ps(p), "KnownBorder", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("[PASS] RC43 retire;", "[PASS] RC44 retire;")
    write_ps(output / p.name, s)

    p = source / "uninstall_overlay.ps1"
    s = add_hash_to_array(read_ps(p), "borderExpected", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("sidecars/additifs RC43 nettoyes", "sidecars/additifs RC44 nettoyes")
    write_ps(output / p.name, s)

    p = source / "collect_rc41.ps1"
    s = read_ps(p)
    s = s.replace("PTAR_RC43_DIAG_", "PTAR_RC44_DIAG_")
    s = s.replace("'RC43_DIAGNOSTIC=1'", "'RC44_DIAGNOSTIC=1'")
    s = s.replace("'RC43_BORDERLESS_SHA256='", "'RC44_BORDERLESS_SHA256='")
    s = s.replace("RC43_ACTIVE_PAYLOAD_SHA256.txt", "RC44_ACTIVE_PAYLOAD_SHA256.txt")
    s = s.replace("RC43_BORDERLESS_LOG_ABSENT.txt", "RC44_BORDERLESS_LOG_ABSENT.txt")
    s = s.replace("RC43_RASTER_LOG_ABSENT.txt", "RC44_RASTER_LOG_ABSENT.txt")
    s = s.replace("RC43_BOOTSTRAP_LOG_ABSENT.txt", "RC44_BOOTSTRAP_LOG_ABSENT.txt")
    s = s.replace("PTAR_RC43_RESULTS_", "PTAR_RC44_RESULTS_")
    s = s.replace("RESULTAT_RC43=", "RESULTAT_RC44=")
    s = s.replace("journaux RC43/RC42 raster/bootstrap", "journaux RC44/RC42 raster/bootstrap")
    write_ps(output / p.name, s)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_rc44_scripts.py RC43_SCRIPT_DIR OUTPUT_DIR")
    transform(Path(sys.argv[1]), Path(sys.argv[2]))
