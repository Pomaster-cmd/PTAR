PTAR GW15 LOCK30

PURPOSE
-------
Test an exact 30 FPS visible cadence on a fixed 60 Hz display by combining the already-tested GW13 all-frame SyncInterval=2 presenter with FrameGenerationTargetFPS=30.

WHY
---
GW14 restored ~31.2 visible FPS with zero marker gaps and near-perfect median 33/33 ms pacing, but the raw CSV still contains periodic 1-VBlank correction frames and an initial 3/1 phase. A fixed 60 Hz display cannot represent a perfectly even 31.2 FPS cadence. Exact 30 FPS can be represented as 2 VBlanks per visible content.

GW15 changes NO runtime bytes versus GW13. It only sets FrameGenerationTargetFPS=30. The existing FGREAL governor contract targets REAL at target/2 = 15 FPS.

TEST
----
1. Close game. Run 01-INSTALL_GW15.bat.
2. Run 02-VERIFY_INSTALL.bat and require VERIFY=PASS.
3. Launch same scene, CTRL+F6 ON, wait 2-3 s.
4. Run 03-ARM_VISIBLE_FRAME_VERIFIER.bat, press F5 once.
5. Wait for 900 Hz start beep and 1400 Hz end beep.
6. Run 04-COLLECT_RESULTS.bat and send PTAR_GW15_RESULTS_*.zip.

SUCCESS TARGET
--------------
- capture rate near 60 Hz, sampling ratio >=0.90
- visible ~30 FPS, ~15 G + ~15 R
- G dwell ~33.3 ms and R dwell ~33.3 ms
- gaps/same-type near 0
- load-shed / late midpoint drops near 0
- pair imbalance low, with no persistent 1/3 or periodic 2/1 cadence.
