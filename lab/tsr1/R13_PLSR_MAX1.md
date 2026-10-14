# R13 PLSR MAX1

Experimental maximum-practical-quality branch for the GTX 960M target.

Primary objective: make exact 1280x720 -> 1920x1080 look perceptually closer to native 1080p by prioritising native-correlated fine detail, sharpness and coherent edge regularity rather than global SSIM alone.

R13 keeps the R11 phase-lattice temporal reconstruction and R12 selective resolution AA. It spends additional budget only inside the existing full-resolution resolve pass:

- 8 additional reads from the current 720p source, no additional history surface;
- 4-tap directional Catmull support around the exact 3:2 output phase;
- wide/central gradient coherence to distinguish long geometric edges from 1-3 px texture;
- extra AA only on coherent slanted staircase geometry;
- luminance microdetail recovery guided by directional source curvature and sign agreement with the qualified R7 residual;
- no generic FXAA, no global sharpen, no second 1080p pass, no neural inference;
- FG scope unchanged: temporal SR remains FG-OFF-only until the SR path is visually and performance-qualified.

Lab budget gates:

- resolve <= 18 texld;
- resolve <= 320 ps_3_0 instruction slots;
- no extra pass;
- total synthetic TSR pipeline <= 3.20x R7 in the existing GitHub-runner harness (field F7 on GTX 960M remains authoritative).

This is intentionally an aggressive lab candidate, not a production default. It must be rejected if GTX 960M F7 (FG OFF) shows unacceptable cost or if real captures gain edge energy without native-correlated structure.
