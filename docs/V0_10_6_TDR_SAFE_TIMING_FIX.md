# PTAR v0.10.6 — GTX 960M TDR-safe timing correction

The first complete real-GPU run proved that K185 itself can execute correctly:
all 42 parity renders completed and passed before the timing phase began.

The subsequent NVIDIA driver recovery occurred only after entering the old
fullscreen timing loop.

The validator has therefore been corrected, not the Windows TDR settings.

v0.10.6 submits exactly one timed fullscreen draw, waits for its query result,
then submits the next. It can no longer create the old large backlog of
unresolved 1080p work.

A driver-reset HRESULT is now reported separately from a normal query timeout.
