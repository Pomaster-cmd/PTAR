# PTAR R8 TSR1

Experimental temporal super-resolution path for D3D9 Q720 -> 1080p.

R8 changes the reconstruction model rather than adding another sharpening pass. It retains the qualified R7 EDGECUBIC1 spatial reconstruction as an exact fallback, stores one source-resolution history frame, estimates current->history motion in source-pixel units, refines that motion subpixel, then performs confidence-gated irregular-sample fusion on the 1080p target grid.

Scope boundaries:

- exact 1.5x Q720 spatial path only;
- TSR active only while FG is OFF;
- R7 output remains the fallback for first frame, invalid motion, integer-grid history, native 1x1, and any TSR failure;
- existing FG, HF6, TEMPORALPAIR60, F7 and POINT A/B behavior must remain unchanged.
