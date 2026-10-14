# PTAR R9 TSR2 DETAILPRESERVE1

R9 is the first field-capture-driven refinement of R8 TSR1.

R8 proved that temporal subpixel history can reduce reconstruction error against a continuous synthetic native reference, but the first real 720p -> 1080p capture pair showed excessive smoothing of detail-bearing regions: small menu text, chair weave, granular ground texture and some foliage structure. The issue was not catastrophic ghosting; it was temporal averaging reducing microcontrast that R7 already reconstructed usefully.

R9 keeps the R8 topology unchanged (one 720p history frame, coarse/refined/subpixel motion, one 1080p resolve, exact R7 fallback) and changes only the temporal blend policy:

- smooth/low-detail regions may retain up to 95% of R8 temporal strength;
- high local source contrast protects R7 detail;
- pixels where R7 materially differs from current-frame bilinear also protect R7 detail;
- fully protected detail regions cap temporal strength at 40%;
- no additional texture read;
- no additional render pass;
- exact zero-motion/failure fallback remains R7;
- FG/HF6/TEMPORALPAIR60 behavior remains isolated and unchanged.

Important performance interpretation: R8/R9 TSR is active only with FG OFF. An F7 result captured with FG ON measures the qualified FG/R7 path, not the TSR resolve. Target-hardware TSR cost must therefore be measured with FG OFF.
