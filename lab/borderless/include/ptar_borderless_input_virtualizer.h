#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include "ptar_borderless_geometry.h"

namespace ptar_lab {

// Lab model for the bounded USER32 surface PTAR needs when the native presenter
// physically owns hit-tested pointer input but the game must keep logical input
// semantics. It deliberately leaves WM_INPUT / DirectInput relative motion alone.
class InputVirtualizer {
public:
    void configure(HWND game, HWND presenter, const Geometry& geometry) noexcept {
        _game = game;
        _presenter = presenter;
        _geometry = geometry;
    }

    HWND game() const noexcept { return _game; }
    HWND presenter() const noexcept { return _presenter; }
    const Geometry& geometry() const noexcept { return _geometry; }

    POINT physical_screen_to_logical_client(POINT p) const noexcept {
        return _geometry.physical_to_logical(p);
    }

    POINT physical_screen_to_logical_screen(POINT p) const noexcept {
        POINT q = _geometry.physical_to_logical(p);
        q.x += _geometry.output.left;
        q.y += _geometry.output.top;
        return q;
    }

    POINT logical_screen_to_physical_screen(POINT p) const noexcept {
        p.x -= _geometry.output.left;
        p.y -= _geometry.output.top;
        return _geometry.logical_to_physical(p);
    }

    // Logical game capture -> physical presenter capture. The game-facing query
    // still reports the game HWND so engines do not discover the presenter.
    HWND set_capture(HWND requested) noexcept {
        if (requested != _game) {
            _logicalCapture = false;
            return ::SetCapture(requested);
        }
        HWND oldPhysical = ::SetCapture(_presenter);
        _logicalCapture = true;
        return logicalize_capture(oldPhysical);
    }

    HWND get_capture() const noexcept {
        return logicalize_capture(::GetCapture());
    }

    BOOL release_capture() noexcept {
        const BOOL ok = ::ReleaseCapture();
        if (ok) _logicalCapture = false;
        return ok;
    }

    bool logical_capture_active() const noexcept { return _logicalCapture; }

    // Prepare a TrackMouseEvent request issued by the game for the presenter.
    // We return the rewritten request separately so production code can call the
    // real API without mutating the game's caller-owned structure.
    bool rewrite_track_request(const TRACKMOUSEEVENT& logical, TRACKMOUSEEVENT& physical) noexcept {
        physical = logical;
        if (logical.hwndTrack != _game) return false;
        physical.hwndTrack = _presenter;
        _trackedFlags = logical.dwFlags;
        _hoverTime = logical.dwHoverTime;
        return true;
    }

    DWORD tracked_flags() const noexcept { return _trackedFlags; }
    DWORD hover_time() const noexcept { return _hoverTime; }

    // Thread-local message-position override used only while a routed pointer
    // callback is executing on the game WndProc. Outside that scope it falls
    // through to USER32 GetMessagePos.
    class RoutedMessageScope {
    public:
        explicit RoutedMessageScope(POINT logicalScreen) noexcept
            : _oldActive(tlsActive), _oldPos(tlsPos) {
            tlsActive = true;
            tlsPos = pack_point(logicalScreen);
        }
        ~RoutedMessageScope() noexcept {
            tlsActive = _oldActive;
            tlsPos = _oldPos;
        }
        RoutedMessageScope(const RoutedMessageScope&) = delete;
        RoutedMessageScope& operator=(const RoutedMessageScope&) = delete;
    private:
        bool _oldActive;
        DWORD _oldPos;
    };

    static DWORD get_message_pos() noexcept {
        return tlsActive ? tlsPos : ::GetMessagePos();
    }

    static POINT unpack_message_pos(DWORD v) noexcept {
        return POINT{GET_X_LPARAM(v), GET_Y_LPARAM(v)};
    }

private:
    static DWORD pack_point(POINT p) noexcept {
        return MAKELONG(WORD(SHORT(p.x)), WORD(SHORT(p.y)));
    }

    HWND logicalize_capture(HWND physical) const noexcept {
        if (_logicalCapture && physical == _presenter) return _game;
        return physical;
    }

    HWND _game = nullptr;
    HWND _presenter = nullptr;
    Geometry _geometry{};
    bool _logicalCapture = false;
    DWORD _trackedFlags = 0;
    DWORD _hoverTime = HOVER_DEFAULT;

    inline static thread_local bool tlsActive = false;
    inline static thread_local DWORD tlsPos = 0;
};

} // namespace ptar_lab
