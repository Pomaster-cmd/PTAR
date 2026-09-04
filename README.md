# PTAR

PTAR is an experimental Windows 8.1 / Direct3D 11 spatial reconstruction and frame-generation research project.

## Current `main`: GW15 LOCK30

`main` is the **hardware-validated GW15 LOCK30 baseline**. It targets an even 30 FPS visible cadence on a fixed 60 Hz display using the validated Flip Sequential 3-buffer presenter with `Present(2, 0)` and a 30 FPS FG target (15 REAL + 15 GENERATED target split).

External visible-frame validation on the Windows 8.1 / GTX 960M test platform measured:

- visible unique FPS: **29.994**
- generated FPS: **14.997**
- real FPS: **14.997**
- generated dwell median: **33.303 ms**
- real dwell median: **33.345 ms**
- pair imbalance median: **0.221%**
- pair imbalance p95: **0.771%**
- midpoint balance: **99.875%**

Runtime SHA-256:

`2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d`

Validated package SHA-256:

`b78fd4bf42f2e7d2eb9f72b0eb59c454e1fe53380d151dd430b29874f287aa40`

GW16 AUTO (automatic 60/30 selection) remains experimental and is intentionally **not** promoted to `main` until its hardware gate passes.

## Installation / validation

1. Close the game and run `01-INSTALL_GW15.bat`.
2. Run `02-VERIFY_INSTALL.bat` and require `VERIFY=PASS`.
3. Launch the test scene, enable FG with `CTRL+F6`, then wait 2–3 seconds.
4. Run `03-ARM_VISIBLE_FRAME_VERIFIER.bat` and press `F5` once.
5. The verifier uses a 900 Hz start beep and a 1400 Hz end beep.
6. Run `04-COLLECT_RESULTS.bat` to collect the evidence package.

Frame generation starts **OFF** by default.

`06-DESINSTALLER_PTAR_COMPLET.bat` uses the ownership/SHA-aware safe uninstall path.

## Repository layout

- `main` — current hardware-validated GW15 LOCK30 runtime package, installer, verifier and validation tooling.
- `SOURCE` — PTAR Project Master / source and reproducibility branch.

The product files on `main` are kept byte-identical to the validated GW15 package. Repository metadata such as this README and `LICENSE` are maintained separately from the package payload.

## License

PTAR is distributed under the GNU General Public License v3.0. See `LICENSE`.
