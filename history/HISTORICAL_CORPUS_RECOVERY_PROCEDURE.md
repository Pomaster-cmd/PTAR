# Historical Corpus Recovery Procedure

When a candidate historical file is found:

1. Copy it into a quarantine/recovery area; do not modify the original.
2. Compute SHA-256.
3. Record source location and discovery date.
4. Record why it is believed to belong to EDGE / horse / naturel / RASTER.
5. Determine whether dimensions, pixel format and preprocessing can be proven.
6. Assign one status:
   - VERIFIED_ORIGINAL
   - PROBABLE_ORIGINAL
   - DERIVED_OR_UNKNOWN
7. Never upgrade a file to VERIFIED_ORIGINAL without evidence.
8. Add a new row to the recovery manifest; never overwrite the old missing-artifact record.
9. Only a VERIFIED_ORIGINAL or explicitly accepted recovered corpus may be used to reproduce the historical benchmark.
10. A rerun must receive a new execution ID and timestamp even if it reproduces the old aggregate score.
