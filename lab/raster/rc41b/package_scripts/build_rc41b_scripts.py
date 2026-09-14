from __future__ import annotations

from pathlib import Path
import re
import sys

OLD_BORDERLESS = "ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416"
NEW_BORDERLESS = "3fca95b75a1dbf41434dd45858300e9d64312a2138f2e0799c8ab1bae3ee3beb"


def read_ps(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_ps(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8-sig", newline="")


def add_after_hash_in_array(text: str, variable: str, old_hash: str, new_hash: str) -> str:
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
    s = s.replace(f"$ExpectedBorderless='{OLD_BORDERLESS}'", f"$ExpectedBorderless='{NEW_BORDERLESS}'")
    s = add_after_hash_in_array(s, "KnownBorderless", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("package='PTAR_RC41_LOGICAL_NATIVE_SUBRASTER'", "package='PTAR_RC41B_BOOTSTRAP_DIAGNOSTICS'")
    s = s.replace("L 'MODE=RC41_LOGICAL_NATIVE_SUBRASTER_PLUS_RC40_BORDERLESS'", "L 'MODE=RC41B_LOGICAL_NATIVE_SUBRASTER_WITH_DIAGNOSTIC_BOOTSTRAP'")
    s = s.replace("F 'Payload RC41 hash mismatch.'", "F 'Payload RC41B hash mismatch.'")
    s = s.replace("L 'INSTALL=PASS'", "L 'INSTALL_RC41B=PASS'")
    write_ps(output / p.name, s)

    p = source / "verify.ps1"
    s = read_ps(p).replace(f"$ExpectedBorderless='{OLD_BORDERLESS}'", f"$ExpectedBorderless='{NEW_BORDERLESS}'")
    s = s.replace("VERIFY_RC41_LOGICAL_NATIVE_SUBRASTER=PASS", "VERIFY_RC41B_BOOTSTRAP_DIAGNOSTICS=PASS")
    write_ps(output / p.name, s)

    p = source / "rollback.ps1"
    s = add_after_hash_in_array(read_ps(p), "KnownBorder", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace("[PASS] RC41 retire;", "[PASS] RC41B retire;")
    write_ps(output / p.name, s)

    p = source / "uninstall_overlay.ps1"
    s = add_after_hash_in_array(read_ps(p), "borderExpected", OLD_BORDERLESS, NEW_BORDERLESS)
    s = s.replace(
        "$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log'",
        "$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log';$boot=if($gameRoot){Join-Path $gameRoot 'ptar_rc41_bootstrap.log'}else{$null}",
    )
    s = s.replace(
        "if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue}",
        "if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue};if($boot -and (Test-Path -LiteralPath $boot -PathType Leaf)){Remove-Item -LiteralPath $boot -Force -ErrorAction SilentlyContinue}",
    )
    s = s.replace("additifs RC41 nettoyes", "additifs RC41B nettoyes")
    write_ps(output / p.name, s)

    p = source / "collect_rc41.ps1"
    s = read_ps(p)
    s = s.replace("'RC41_DIAGNOSTIC=1'", "'RC41B_DIAGNOSTIC=1'")
    s = s.replace(
        "@($tmp,'RC41_ACTIVE_PAYLOAD_SHA256.txt')",
        "@((Join-Path $g 'ptar_rc41_bootstrap.log'),'ptar_rc41_bootstrap.log'),@($tmp,'RC41B_ACTIVE_PAYLOAD_SHA256.txt')",
    )
    s = s.replace(
        "@((Join-Path $g 'ptar_rc41.log'),'RC41_RASTER_LOG_ABSENT.txt')",
        "@((Join-Path $g 'ptar_rc41.log'),'RC41B_RASTER_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41_bootstrap.log'),'RC41B_BOOTSTRAP_LOG_ABSENT.txt')",
    )
    s = s.replace("'RC41_BORDERLESS_LOG_ABSENT.txt'", "'RC41B_BORDERLESS_LOG_ABSENT.txt'")
    s = s.replace("('PTAR_RC41_RESULTS_'+$stamp+'.zip')", "('PTAR_RC41B_RESULTS_'+$stamp+'.zip')")
    s = s.replace("RESULTAT_RC41=", "RESULTAT_RC41B=")
    s = s.replace("journaux RC40/RC41 ajoutes", "journaux RC40/RC41/RC41B bootstrap ajoutes")
    write_ps(output / p.name, s)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_rc41b_scripts.py SOURCE_DIR OUTPUT_DIR")
    transform(Path(sys.argv[1]), Path(sys.argv[2]))
