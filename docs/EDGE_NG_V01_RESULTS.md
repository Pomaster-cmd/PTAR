# EDGE-NG v01 — Protocol A results

Historical EDGE v03 is unchanged. These are new NG-branch results.

EDGE_CORE ranking:
1. BASELINE epsilon=NA — SSIM 0.864928652, PSNR 22.710568 dB
2. SW3 epsilon=0.01 — SSIM 0.862948231, PSNR 22.647960 dB
3. SW1 epsilon=0.1 — SSIM 0.862552870, PSNR 22.615847 dB
4. SW2 epsilon=0.1 — SSIM 0.859078405, PSNR 22.501219 dB

Observation: the internal cubic BASELINE is ahead of SW1/SW2/SW3 on Protocol A.
STEP-WENO is therefore not promoted on the NG branch from this campaign.

Against the Protocol A generic bicubic control:
- EDGE_NG_V01_BASELINE_eps0.001: SSIM wins 1/15, PSNR wins 0/15
- EDGE_NG_V01_SW1_eps0.1: SSIM wins 1/15, PSNR wins 0/15
- EDGE_NG_V01_SW2_eps0.1: SSIM wins 1/15, PSNR wins 0/15
- EDGE_NG_V01_SW3_eps0.01: SSIM wins 1/15, PSNR wins 0/15

Protocol caveat: Protocol A uses a generic resize geometry. A grid-aligned Protocol B is required before making the final NG architectural decision.
