#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ptar_rc43 {

enum class RequestedWindowMode : unsigned {
    Ambiguous=0,
    Windowed=1,
    Borderless=2,
};

inline RequestedWindowMode classify_style(LONG_PTR style) noexcept {
    const bool child=(style&WS_CHILD)!=0;
    const bool popup=(style&WS_POPUP)!=0;
    const bool caption=(style&WS_CAPTION)!=0;
    const bool thick=(style&WS_THICKFRAME)!=0;
    if(!child && (caption||thick)) return RequestedWindowMode::Windowed;
    if(!child && popup && !caption && !thick) return RequestedWindowMode::Borderless;
    return RequestedWindowMode::Ambiguous;
}

inline bool is_windowed_request(LONG_PTR style) noexcept {
    return classify_style(style)==RequestedWindowMode::Windowed;
}

inline bool is_borderless_request(LONG_PTR style) noexcept {
    return classify_style(style)==RequestedWindowMode::Borderless;
}

} // namespace ptar_rc43
