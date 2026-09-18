PTAR GW16I / SLATEABS1 - CLICK INPUT DIAGNOSTICS

These tools are optional and are not used during normal PTAR operation.
Production default: [INPUT] Diagnostics=0.
Expected final runtime SHA256: bc291f0f91013df7a28630ffef44983856fce6eb71d79aca597ab292012165e0

Use only when a game receives mouse movement/clicks but UI actions do not behave correctly.
1. Close the game.
2. Drag the real x64 game executable onto 01-ENABLE_CLICK_DIAGNOSTICS.bat.
3. Launch the game and reproduce the click problem.
4. Press F8 immediately after the relevant click to flush the passive trace.
5. Close the game normally.
6. Run 02-COLLECT_AND_RESTORE_CLICK_DIAGNOSTICS.bat.

The collector records Win32 button delivery, focus, foreground, capture symmetry, presenter ownership and ClientToScreen coordinate traces, then restores the original INI bit-for-bit.
03-RESTORE_DIAGNOSTICS_INI_ONLY.bat is the fallback if a diagnostic session is interrupted.
