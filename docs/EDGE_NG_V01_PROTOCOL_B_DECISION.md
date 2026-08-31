# EDGE-NG v01 — Protocol B decision

Protocol B is grid-aligned to the exact PTAR x1.5 phase geometry.

EDGE-NG ranking:
1. BASELINE epsilon=NA — SSIM 0.902033709, PSNR 24.067823 dB
2. SW1 epsilon=0.1 — SSIM 0.899412734, PSNR 23.902584 dB
3. SW3 epsilon=0.1 — SSIM 0.897229698, PSNR 23.799065 dB
4. SW2 epsilon=0.1 — SSIM 0.896587573, PSNR 23.767063 dB

Grid-aligned controls:
- nearest: SSIM 0.872339782, PSNR 22.372546 dB
- bilinear: SSIM 0.890223931, PSNR 23.565777 dB
- cubic4_separable: SSIM 0.908966544, PSNR 24.322724 dB

Decision: STEP-WENO is NOT PROMOTED into the EDGE-NG runtime branch from v01. The cubic directional baseline remains ahead of all SW variants, while cubic4_separable is the quality control to beat. STEP-WENO remains preserved as a documented experimental/historical branch and can be revisited with a different role or recovered EDGE v03.

Next: EDGE-NG v02 will focus on direction/orientation quality and closing the gap to cubic4_separable under a low-fetch single-pass constraint.
