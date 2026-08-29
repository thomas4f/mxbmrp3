// ============================================================================
// core/ui_viewport.h
// THE ONE 16:9 UI-RECT COMPUTATION: the largest 16:9 rectangle centered in a
// client area, in integer client pixels.
//
// Three call sites consume this rect and MUST agree on it, or a click lands
// beside the thing it clicked:
//   - CompanionWindow's paint loop (companion_window.cpp): the rect sets the
//     HUD's scale viewport (rendering still fills the whole client — elements
//     outside [0,1] use the surrounding area, deliberately not a letterbox);
//   - InputManager::updateCursorPosition: the inverse map, client cursor px ->
//     normalized UI coordinates, for hit-testing on either surface;
//   - InputManager's window-bounds update: the visible client area expressed
//     in UI coordinates, for HUD position validation.
// Before this header each site carried its own copy of the arithmetic — the
// paint loop in integer math, the input ones via float UI_ASPECT_RATIO with
// truncation — which could disagree by a pixel at odd client sizes. One
// function, one rounding rule; viewport_test.cpp pins it (degenerate sizes,
// the exact-16:9 boundary, and inverse-map round-trips).
//
// PURE ON PURPOSE (no Win32, no singletons, no other project headers) so the
// unit suite compiles it directly. The 16:9 is spelled as integers here;
// input_manager.cpp static_asserts it against PluginConstants::UI_ASPECT_RATIO,
// whose include graph is too heavy for a leaf header like this one.
// ============================================================================
#pragma once

#include <algorithm>

namespace UiViewport {

// Aspect as integers: exact comparisons, no float truncation differences
// between forward (paint) and inverse (cursor) users.
inline constexpr int kAspectW = 16;
inline constexpr int kAspectH = 9;

struct Rect {
    int x = 0, y = 0;   // top-left offset of the UI rect within the client
    int w = 1, h = 1;   // UI rect size; never below 1 (division guard for the
                        // inverse map — a 0x0 client must not divide by zero)
};

// The largest kAspectW:kAspectH rect centered in a clientW x clientH client.
// A client wider than 16:9 pillarboxes (full height), narrower letterboxes
// (full width); exactly 16:9 fills it. All divisions truncate toward zero,
// the same rule every prior copy used.
inline Rect compute(int clientW, int clientH) {
    clientW = std::max(1, clientW);
    clientH = std::max(1, clientH);
    Rect r;
    if (static_cast<long long>(clientW) * kAspectH >
        static_cast<long long>(clientH) * kAspectW) {
        r.h = clientH;
        r.w = std::max(1, clientH * kAspectW / kAspectH);
    } else {
        r.w = clientW;
        r.h = std::max(1, clientW * kAspectH / kAspectW);
    }
    r.x = (clientW - r.w) / 2;
    r.y = (clientH - r.h) / 2;
    return r;
}

}  // namespace UiViewport
