from __future__ import annotations

from pathlib import Path
import sys

OLD = "15a8f276816f3123a79a2d8e6932b6bf42b3826288ea5f016c124afec99fbbcf"
NEW = "7ffe7aa1b9afa42d5f22557cae29a4f65e17379c2a2f181f879427277839f1ca"


def read_ps(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_ps(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8-sig", newline="")


def exact_replace(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f"missing transform anchor: {label}")
    return text.replace(old, new, 1)


def transform(source: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)

    p = source / "install.ps1"
    s = read_ps(p)
    s = exact_replace(s, f"$ExpectedRaster='{OLD}'", f"$ExpectedRaster='{NEW}'", "install expected raster")
    s = exact_replace(s, f"$KnownRaster=@('{OLD}')", f"$KnownRaster=@('{OLD}','{NEW}')", "install known raster")
    s = s.replace("package='PTAR_RC41B_BOOTSTRAP_DIAGNOSTICS'", "package='PTAR_RC42_PRIMARY_RASTER_RECONCILE'")
    s = s.replace("F 'Payload RC41B hash mismatch.'", "F 'Payload RC42 hash mismatch.'")
    s = s.replace("L 'INSTALL_RC41B=PASS'", "L 'INSTALL_RC42=PASS'")
    s = s.replace("L ('RC41_SIDECAR_SHA256='+$ExpectedRaster)", "L ('RC42_RASTER_SIDECAR_SHA256='+$ExpectedRaster)")
    s = s.replace("L 'MODE=RC41B_LOGICAL_NATIVE_SUBRASTER_WITH_DIAGNOSTIC_BOOTSTRAP'", "L 'MODE=RC42_PRIMARY_RASTER_RECONCILE_WITH_RC41B_BOOTSTRAP'")
    write_ps(output / p.name, s)

    p = source / "verify.ps1"
    s = read_ps(p)
    s = exact_replace(s, f"$ExpectedRaster='{OLD}'", f"$ExpectedRaster='{NEW}'", "verify expected raster")
    s = s.replace("VERIFY_RC41B_BOOTSTRAP_DIAGNOSTICS=PASS", "VERIFY_RC42_PRIMARY_RASTER_RECONCILE=PASS")
    write_ps(output / p.name, s)

    p = source / "rollback.ps1"
    s = read_ps(p)
    old_line = f"$raster=Join-Path $g 'ptar_rc41.dll';$rh=Sha $raster;if($rh -and $rh -ne '{OLD}'){{Write-Host ('[FAIL] Sidecar RC41 inconnu/modifie, rollback refuse: '+$rh);exit 14}}"
    new_line = f"$raster=Join-Path $g 'ptar_rc41.dll';$rh=Sha $raster;$KnownRaster=@('{OLD}','{NEW}');if($rh -and ($KnownRaster -notcontains $rh)){{Write-Host ('[FAIL] Sidecar raster RC41/RC42 inconnu/modifie, rollback refuse: '+$rh);exit 14}}"
    s = exact_replace(s, old_line, new_line, "rollback raster guard")
    s = exact_replace(s, f"if((Sha $raster) -eq '{OLD}'){{Remove-Item -LiteralPath $raster -Force}}", "if($KnownRaster -contains (Sha $raster)){Remove-Item -LiteralPath $raster -Force}", "rollback raster remove")
    s = s.replace("[PASS] RC41B retire; payload GitHub main exact restaure; sidecars RC40/RC41 connus retires.", "[PASS] RC42 retire; payload GitHub main exact restaure; sidecars connus retires.")
    write_ps(output / p.name, s)

    p = source / "uninstall_overlay.ps1"
    s = read_ps(p)
    s = exact_replace(s, f");$rasterExpected='{OLD}'", f");$rasterExpected=@('{OLD}','{NEW}')", "uninstall raster allowlist")
    s = exact_replace(s, "if($h -eq $rasterExpected){Remove-Item -LiteralPath $rasterPath -Force;Write-Host '[OK] Sidecar RC41 connu retire.'}", "if($rasterExpected -contains $h){Remove-Item -LiteralPath $rasterPath -Force;Write-Host '[OK] Sidecar raster RC41/RC42 connu retire.'}", "uninstall raster remove")
    s = s.replace("sidecars/additifs RC41B nettoyes", "sidecars/additifs RC42 nettoyes")
    write_ps(output / p.name, s)

    p = source / "collect_rc41.ps1"
    s = read_ps(p)
    s = s.replace("PTAR_RC41_DIAG_", "PTAR_RC42_DIAG_")
    s = s.replace("'RC41B_DIAGNOSTIC=1'", "'RC42_DIAGNOSTIC=1'")
    s = s.replace("'RC41_SIDECAR_SHA256='", "'RC42_RASTER_SIDECAR_SHA256='")
    s = s.replace("RC41B_ACTIVE_PAYLOAD_SHA256.txt", "RC42_ACTIVE_PAYLOAD_SHA256.txt")
    s = s.replace("RC41B_BORDERLESS_LOG_ABSENT.txt", "RC42_BORDERLESS_LOG_ABSENT.txt")
    s = s.replace("RC41B_RASTER_LOG_ABSENT.txt", "RC42_RASTER_LOG_ABSENT.txt")
    s = s.replace("RC41B_BOOTSTRAP_LOG_ABSENT.txt", "RC42_BOOTSTRAP_LOG_ABSENT.txt")
    s = s.replace("PTAR_RC41B_RESULTS_", "PTAR_RC42_RESULTS_")
    s = s.replace("RESULTAT_RC41B=", "RESULTAT_RC42=")
    s = s.replace("[PASS] Collecte canonique + journaux RC40/RC41/RC41B bootstrap ajoutes.", "[PASS] Collecte canonique + journaux RC40/RC41/RC42 bootstrap/raster ajoutes.")
    write_ps(output / p.name, s)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_rc42_scripts.py RC41B_SCRIPT_DIR OUTPUT_DIR")
    transform(Path(sys.argv[1]), Path(sys.argv[2]))
