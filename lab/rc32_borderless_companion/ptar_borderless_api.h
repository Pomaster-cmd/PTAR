#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>

#ifdef PTAR_BORDERLESS_EXPORTS
#define PTAR_BCTL_API extern "C" __declspec(dllexport)
#else
#define PTAR_BCTL_API extern "C" __declspec(dllimport)
#endif

// ABI deliberately fixed/simple so the RC32 proxy only needs a tiny loader stub.
// Returns 0 on success. Any non-zero result means fail-open: the original RC32
// behavior must remain usable and no partial takeover may be left behind.
PTAR_BCTL_API int WINAPI PTAR_BorderlessAttach(
    HWND gameHwnd,
    HWND presenterHwnd,
    uint32_t renderWidth,
    uint32_t renderHeight,
    uint32_t outputWidth,
    uint32_t outputHeight);

PTAR_BCTL_API void WINAPI PTAR_BorderlessDetach(void);
PTAR_BCTL_API uint32_t WINAPI PTAR_BorderlessStatus(void);

// Status bits. The product gate requires ACTIVE + GAME_SUBCLASSED +
// PRESENTER_SUBCLASSED + GEOMETRY_LOCKED + INPUT_ROUTING.
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
