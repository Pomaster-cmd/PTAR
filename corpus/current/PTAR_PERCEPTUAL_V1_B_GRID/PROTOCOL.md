# PTAR-PERCEPTUAL-V1-B-GRID

This corpus reuses the immutable 42 HR visual cases from Protocol A but derives LR inputs with an explicit grid-aligned 1.5x sampling contract.

LR coordinate j corresponds to HR continuous coordinate 1.5*j. Reconstruction uses source_coord = output_index / 1.5, giving exact integer / 1/3 / 2/3 phases.

Downsampling is separable scaled Lanczos-3, reflect boundary, normalized weights, sRGB/code-value domain, clipped to [0,1].

Changing kernel, origin, scale, boundary, color domain or clipping requires a new corpus ID.
