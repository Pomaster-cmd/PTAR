#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>

#ifdef PTAR_BORDERLESS_EXPORTS
#define PTAR_BCTL_API extern "C" __declspec(dllexport)
// LAB SAFETY OVERRIDE: SetWindowPos on a window created WS_EX_TOPMOST must use
// HWND_NOTOPMOST to leave the topmost z-band; merely clearing GWL_EXSTYLE is
// insufficient. Companion source uses HWND_TOP at one call site, so route that
// symbol to HWND_NOTOPMOST until the product split moves z-order into platform.cpp.
#undef HWND_TOP
#define HWND_TOP HWND_NOTOPMOST
#else
#define PTAR_BCTL_API extern "C" __declspec(dllimport)
#endif

PTAR_BCTL_API int WINAPI PTAR_BorderlessAttach(
    HWND gameHwnd,
    HWND presenterHwnd,
    uint32_t renderWidth,
    uint32_t renderHeight,
    uint32_t outputWidth,
    uint32_t outputHeight);

PTAR_BCTL_API void WINAPI PTAR_BorderlessDetach(void);
PTAR_BCTL_API uint32_t WINAPI PTAR_BorderlessStatus(void);

enum PTARBorderlessStatus : uint32_t {
    PTAR_BCTL_ACTIVE               = 1u << 0,
    PTAR_BCTL_GAME_SUBCLASSED      = 1u << 1,
    PTAR_BCTL_PRESENTER_SUBCLASSED = 1u << 2,
    PTAR_BCTL_GEOMETRY_LOCKED      = 1u << 3,
    PTAR_BCTL_INPUT_ROUTING        = 1u << 4,
    PTAR_BCTL_DPI_SAFE             = 1u << 5,
    PTAR_BCTL_IAT_INPUT_HOOKS      = 1u << 6,
    PTAR_BCTL_FAIL_OPEN            = 1u << 31
};
