#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include "ptar_borderless_geometry.h"

namespace ptar_lab {

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

    // Translate game TrackMouseEvent requests onto the physical presenter.
    // The caller-owned structure is never modified in-place.
    bool rewrite_track_request(const TRACKMOUSEEVENT& logical, TRACKMOUSEEVENT& physical) noexcept {
        physical = logical;
        if (logical.hwndTrack != _game) return false;
        physical.hwndTrack = _presenter;
        _trackedFlags = logical.dwFlags;
        _hoverTime = logical.dwHoverTime;
        return true;
    }

    // TME_QUERY returns the physical presenter from USER32; hide that detail
    // before returning to the game.
    void logicalize_track_result(TRACKMOUSEEVENT& result) const noexcept {
        if (result.hwndTrack == _presenter) result.hwndTrack = _game;
    }

    DWORD tracked_flags() const noexcept { return _trackedFlags; }
    DWORD hover_time() const noexcept { return _hoverTime; }

    // GetMessagePos is thread-queue based and otherwise exposes the native
    // presenter position while a routed callback is executing. Scope the
    // logical screen position only around that synchronous game callback.
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
