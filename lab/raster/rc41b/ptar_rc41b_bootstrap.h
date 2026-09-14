#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>

struct RC41BState {
    UINT size;
    UINT active;
    UINT logicalW, logicalH;
    UINT physicalW, physicalH;
    UINT resourceBridgeInstalled;
    UINT contextInstalled;
    UINT swapchainInstalled;
    std::uint64_t viewportMapped;
    std::uint64_t scissorMapped;
    std::uint64_t getDescVirtualized;
    std::uint64_t resizeRemapped;
    std::uint64_t primaryRefreshes;
    std::uint64_t textureRemapped;
    std::uint64_t textureFallbacks;
    std::uint64_t rtvTagged;
    std::uint64_t dsvTagged;
};

struct RC41BTestHooks {
    HMODULE (WINAPI *getModuleHandleW)(LPCWSTR);
    HMODULE (WINAPI *loadLibraryW)(LPCWSTR);
    FARPROC (WINAPI *getProcAddress)(HMODULE, LPCSTR);
    DWORD (WINAPI *getLastError)();
    void (WINAPI *sleep)(DWORD);
    DWORD (WINAPI *getFileAttributesW)(LPCWSTR);
};

void RC41B_StartBootstrap(HMODULE ownerModule, HMODULE runtimeModule) noexcept;
int RC41B_RunLoaderWithPathsForTest(HMODULE runtimeModule,
                                    const wchar_t* dllPath,
                                    const wchar_t* logPath,
                                    unsigned maxAttempts,
                                    DWORD retryDelayMs) noexcept;
bool RC41B_BuildSiblingPath(const wchar_t* modulePath,
                            const wchar_t* leafName,
                            wchar_t* out,
                            size_t outCount) noexcept;
void RC41B_SetTestHooks(const RC41BTestHooks* hooks) noexcept;
void RC41B_ResetTestHooks() noexcept;
