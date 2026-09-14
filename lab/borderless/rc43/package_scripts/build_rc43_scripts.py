from __future__ import annotations

from pathlib import Path
import re
import sys

OLD_BORDERLESS = "3fca95b75a1dbf41434dd45858300e9d64312a2138f2e0799c8ab1bae3ee3beb"
NEW_BORDERLESS = "cd38729a93b8594203196bdda89efc343db0854f024aaba3a5663009a7503612"


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
    s = exact_replace(
        s,
        f"$ExpectedBorderless='{OLD_BORDERLESS}'",
        f"$ExpectedBorderless='{NEW_BORDERLESS}'",
        "install expected RC43 carrier",
    )
    s = add_hash_to_array(s, "KnownBorderless", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("package='PTAR_RC42_PRIMARY_RASTER_RECONCILE'", "package='PTAR_RC43_WINDOWED_BORDERLESS_COEXIST'")
    s = s.replace("F 'Payload RC42 hash mismatch.'", "F 'Payload RC43 hash mismatch.'")
    s = s.replace("L 'INSTALL_RC42=PASS'", "L 'INSTALL_RC43=PASS'")
    s = s.replace(
        "L 'MODE=RC42_PRIMARY_RASTER_RECONCILE_WITH_RC41B_BOOTSTRAP'",
        "L 'MODE=RC43_WINDOWED_BORDERLESS_COEXIST_WITH_RC42_RASTER'",
    )

    # RC42 still forced WindowStyle=1. RC43 deliberately stops owning this preference:
    # the user's/game's Windowed vs Borderless choice must remain authoritative.
    forced_windowstyle = (
        "if(Test-Path -LiteralPath $key){if((-not $ws.exists) -or ([int]$ws.original -ne 1)){"
        "Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value 1;$m.windowstyle.applied=$true}}"
    )
    preserved_windowstyle = "$wsText=if($ws.exists){[string]$ws.original}else{'UNSET'};L ('WINDOWSTYLE_PRESERVED='+$wsText)"
    s = exact_replace(s, forced_windowstyle, preserved_windowstyle, "remove forced WindowStyle=1")
    write_ps(output / p.name, s)

    p = source / "verify.ps1"
    s = read_ps(p)
    s = exact_replace(
        s,
        f"$ExpectedBorderless='{OLD_BORDERLESS}'",
        f"$ExpectedBorderless='{NEW_BORDERLESS}'",
        "verify expected RC43 carrier",
    )
    s = s.replace("VERIFY_RC42_PRIMARY_RASTER_RECONCILE=PASS", "VERIFY_RC43_WINDOWED_BORDERLESS_COEXIST=PASS")
    write_ps(output / p.name, s)

    p = source / "rollback.ps1"
    s = add_hash_to_array(read_ps(p), "KnownBorder", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("[PASS] RC42 retire;", "[PASS] RC43 retire;")
    write_ps(output / p.name, s)

    p = source / "uninstall_overlay.ps1"
    s = add_hash_to_array(read_ps(p), "borderExpected", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("sidecars/additifs RC42 nettoyes", "sidecars/additifs RC43 nettoyes")
    write_ps(output / p.name, s)

    p = source / "collect_rc41.ps1"
    s = read_ps(p)
    s = s.replace("PTAR_RC42_DIAG_", "PTAR_RC43_DIAG_")
    s = s.replace("'RC42_DIAGNOSTIC=1'", "'RC43_DIAGNOSTIC=1'")
    s = s.replace("'BORDERLESS_SHA256='", "'RC43_BORDERLESS_SHA256='")
    s = s.replace("RC42_ACTIVE_PAYLOAD_SHA256.txt", "RC43_ACTIVE_PAYLOAD_SHA256.txt")
    s = s.replace("RC42_BORDERLESS_LOG_ABSENT.txt", "RC43_BORDERLESS_LOG_ABSENT.txt")
    s = s.replace("RC42_RASTER_LOG_ABSENT.txt", "RC43_RASTER_LOG_ABSENT.txt")
    s = s.replace("RC42_BOOTSTRAP_LOG_ABSENT.txt", "RC43_BOOTSTRAP_LOG_ABSENT.txt")
    s = s.replace("PTAR_RC42_RESULTS_", "PTAR_RC43_RESULTS_")
    s = s.replace("RESULTAT_RC42=", "RESULTAT_RC43=")
    s = s.replace(
        "[PASS] Collecte canonique + journaux RC40/RC41/RC42 bootstrap/raster ajoutes.",
        "[PASS] Collecte canonique + journaux RC43/RC42 raster/bootstrap ajoutes.",
    )
    write_ps(output / p.name, s)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_rc43_scripts.py RC42_SCRIPT_DIR OUTPUT_DIR")
    transform(Path(sys.argv[1]), Path(sys.argv[2]))
