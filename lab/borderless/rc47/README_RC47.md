RC47 Windowed Presenter Owner Guard

Purpose: keep the real P1U46 presenter visibly above the opaque game HWND in Windowed mode without using TOPMOST. The presenter becomes an owned popup of the game only while Windowed; the original owner is restored before Borderless so the validated RC42/RC46 borderless path remains unchanged.

Field regression reproduced from RC46 result: the game HWND is correctly held at its real 1280x720-client window while P1U46 repeatedly raises that HWND; an unowned presenter then remains behind the opaque black game client. RC47 fixes only that ownership/z-order invariant.
