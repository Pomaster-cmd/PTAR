PTAR DISPLAY DELIVERY LAB DWMPHASE2 AUTOCOLLECT1
=================================================

Cette branche diagnostique repart exactement de RC18 PRESENTDELIVERY1. Le run DWMPHASE1 a confirme un pipeline interne equilibre, mais la collecte a perdu les deux sorties decisives : snapshot DWM manuel non persiste et stdout du visible verifier non archive.

DWMPHASE2 ne change ni Present(1,0), ni le swapchain, ni le pacing, ni NVENC, ni le mailbox. Il ajoute seulement :
- un dump automatique des histogrammes DWM au prochain F8, apres le statut normal ;
- la sauvegarde automatique de la sortie du visible verifier ;
- CTRL+F4 reste le snapshot manuel de secours.

Voir diag\DWMPHASE2_AUTOCOLLECT1_HARDWARE_PROTOCOL.txt.

--- BASE RC18 / HISTORIQUE ---

PTAR RC18 - PRESENTDELIVERY1 (CURRENT CANDIDATE)
================================================

RC18 is a targeted presentation correction built from RC17. RC17 CONSERVATIVE quality was field-satisfactory, but the external visible-marker verifier proved the FG output was not actually reaching the desktop: ~15.578 visible REAL FPS, only 0.793 visible GENERATED FPS and 16.372 visible UNIQUE FPS while the internal HUD was near 30 FPS.

PRESENTDELIVERY1 changes the isolated display delivery contract only:
- the two explicit WaitForVBlank calls immediately before Present are bypassed;
- the existing swapchain Present changes from Present(0, DXGI_PRESENT_DO_NOT_WAIT) to Present(1, 0);
- FrameGenerationRequireVSync=0 and FrameGenerationPresentSync=1 mirror the new runtime policy.

This is intentionally a real runtime revision (RC18), not another diagnostic patch. Generated-frame synthesis, FGQUALITY3/CONSERVATIVE, QUALITYMENU1, SHARELEGACY3, RESIZEGUARD1, GATE1, INPUTMAP2 and recorder logic are otherwise retained. The success criterion is external VISIBLE GENERATED/UNIQUE FPS, not the HUD counter.

See diag\RC18_FIELD_FINDING_VISIBLE_DELIVERY.txt and diag\PRESENTDELIVERY1_HARDWARE_PROTOCOL.txt.

--- HISTORICAL RC17 NOTES BELOW ---

PTAR RC17 ? FGQUALITY3 + QUALITYMENU1
====================================

RC17 is built from the field-working RC16. RC16 restored the normal Inquisitor startup image and HUD after the rejected RC15 HUD experiment, but profile 3 still showed a visible flicker/halo around the moving character. RC17 treats that remaining FG image-quality defect as the primary change.

FG QUALITY
----------
FrameGenerationQuality remains the authoritative INI setting:
0 LEGACY       = ME /3, Guard65, historical SatGat-style parameters
1 BALANCED     = ME /3, Guard50
2 QUALITY      = ME /2, Guard35, RC13/Inquisitor baseline
3 CONSERVATIVE = ME /2, Guard25 + RC17 motion-discontinuity stabilizer

Profile 3 now lowers warp trust at local motion-field discontinuities. The midpoint shader compares the center MV with right/down neighbors; when they disagree strongly, the generated pixel falls back more consistently toward the existing blend of the two real frames. This is intended to reduce the unstable halo/flicker around independently moving 3D objects without increasing ME resolution. Profiles 0/1/2 do not enter this new branch.

CTRL+F8 MENU
------------
F8 remains Status.

CTRL+F8 now behaves as a small quality menu:
- first press: display the current selected profile NAME only; no profile change;
- release the keys, then press CTRL+F8 again while that name is still visible: select the next profile and refresh the name;
- repeat while the notice remains visible to continue cycling;
- after the notice disappears, the next press only displays the current name again.

The notice lasts 3000 ms and displays LEGACY, BALANCED, QUALITY or CONSERVATIVE. No numeric profile is shown in this CTRL+F8 notice.

A /3 <-> /2 profile crossing while FG is active still requires CTRL+F6 OFF then CTRL+F6 ON for the ME allocation. RC17 does not rebuild FG automatically. Same-tier changes continue to apply live.

REGRESSION LOCKS
----------------
RC17 leaves the RC16/RC14 startup parser path byte-identical. SHARELEGACY3, RESIZEGUARD1, GATE1, INPUTMAP2, 30->60 cadence, NVENC ring, QPC/VBlank, mailbox topology, PTAR spatial path and recorder behavior are not redesigned. Auto-VSync remains deferred and recorder testing is not part of this iteration.

Hardware validation is still required for the new Q3 shader and the new CTRL+F8 interaction. See diag\FGQUALITY3_QUALITYMENU1_HARDWARE_PROTOCOL.txt.

VISIBLE FPS DIAGNOSTIC - DIAGREPAIR4 / HOTKEYFIX1
----------------------------------------------------
The existing diag workflow is retained. The measurement engine remains at:
  diag\visible_verifier\win81_vblank2_visible_marker_x64.exe

DIAGREPAIR4 fixes the trigger path instead of adding a parallel diagnostic.
diag\04-ARM_VISIBLE_FRAME_VERIFIER.bat now:
- relaunches the same existing entry point in an elevated CMD /K console through the normal Windows UAC prompt;
- enables and confirms VBlankDiagnostics=1 with the existing Win32 INI helper;
- verifies the exact HOTKEYFIX1 verifier SHA-256 before launch;
- launches the verifier directly, with no Tee, stdout pipe or detached verifier process;
- leaves the elevated CMD /K console open after return/failure.

HOTKEYFIX1 changes only the verifier trigger. The two CTRL-down gates around the existing F5 edge detector are bypassed. F5 alone therefore starts the measurement, and CTRL+F5 remains compatible because F5 is still the actual edge. The capture/marker/20-second measurement code is unchanged. The ARMED line now explicitly says F5 or CTRL+F5.

Procedure: run diag\04-ARM_VISIBLE_FRAME_VERIFIER.bat, accept UAC, launch Inquisitor, enable FG (OFF then ON if FG was already active when the diagnostic was enabled), wait 2-3 seconds, then press F5 once. The verifier must immediately print MEASUREMENT STARTED: 20 seconds. ESC still cancels.

INSTALL / VERIFY - INSTALLVERIFY3 / RECORDERFIX1 / HASHFIX1
----------------------------------
01-INSTALL_FULLSTACK1.bat still performs VERIFY_FULLSTACK1_INSTALL.bat /AUTO after a successful core install. INSTALLVERIFY3 additionally fixes the two field failures seen immediately after a fresh install: RECORDERFIX1 makes VideoRecordProfile=3 authoritative during the core merge, and HASHFIX1 makes SHA-256 verification Windows-8.1-safe by capturing the PowerShell helper through a temp file with native CertUtil fallback. The complete verifier also checks DIAGREPAIR4/HOTKEYFIX1: the 04 entry-point marker, the Win32 VBlank helper, and the exact visible-verifier SHA-256. A final install PASS therefore covers both the RC17 runtime and the diagnostic system shipped beside it.

VERIFY_FULLSTACK1_INSTALL.bat continues to write PTAR_VERIFY_LAST.log and repeats every [FAIL] line at the bottom. Direct manual verification still pauses before closing.


VISIBLE FPS DIAGNOSTIC - DIAGREPAIR5 / ESCSAFE1
------------------------------------------------
Field finding: ESC is used normally inside Inquisitor menus. The external verifier was globally polling VK_ESCAPE in three places: while arming, while waiting for the trigger, and during the 20-second measurement. Therefore an ESC press intended only for the game could silently cancel the diagnostic before any useful result appeared.

ESCSAFE1 neutralizes those three cancellation branches. ESC is now ignored by the verifier. The only supported stop/cancel action is manual closure of the diagnostic console window/process. F5 and CTRL+F5 triggering, marker decoding, GDI capture and the 20-second measurement engine are unchanged.

Locked visible-verifier SHA-256: 67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305
