// ============================================================================
// hud/settings/settings_controls.h
// The settings panel's standard components beyond the cycler: a SLIDER for a
// bounded number and a DROPDOWN for a choice of three or more. Both are drawn
// from plain quads and strings, so they look the same with or without a panel
// theme, and both are descriptors rebuilt in lockstep with the click regions
// (like SteppedControl / CycleControl): a region carries an index into the
// vector, which stays valid exactly as long as the region does.
// ============================================================================
#pragma once

#include <functional>

class BaseHud;

// A bounded number, set by clicking or dragging along a track. get/set speak
// the control's own unit (a percentage as 0..1, a count as a whole number);
// set() receives a value already snapped to `step` and clamped to [lo, hi].
struct SliderControl {
    float lo = 0.0f, hi = 1.0f, step = 0.01f;
    std::function<float()> get;
    std::function<void(float)> set;
    // Optional extra work after set() (a SteppedControl's postStep), and the HUD
    // to mark dirty.
    std::function<void()> postStep;
    BaseHud* dirtyHud = nullptr;
    // Optional, run once when the drag ends (SteppedControl::onRelease); while
    // set, postStep runs only then, after it.
    std::function<void()> onRelease;
    // Optional validity predicate, as SteppedControl::valid: false swallows the
    // drag and rebuilds against the right target.
    std::function<bool()> valid;

    // The fraction of the track a value sits at, and the snapped value at a
    // fraction -- the two halves of the mapping, pure so a test can pin them.
    float fractionOf(float value) const {
        if (hi <= lo) return 0.0f;
        const float f = (value - lo) / (hi - lo);
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }
    float valueAt(float fraction) const {
        const float f = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        float v = lo + f * (hi - lo);
        if (step > 0.0f) {
            const float steps = (v - lo) / step;
            v = lo + static_cast<float>(static_cast<long>(steps + 0.5f)) * step;
        }
        return v < lo ? lo : (v > hi ? hi : v);
    }
};

// The slider being dragged: its index and the track it was grabbed on, kept
// across rebuilds (the layout is deterministic, so the index and the track's
// geometry are the same next frame).
struct SliderDrag {
    int index = -1;
    float trackX = 0.0f, trackW = 0.0f;
    float lastValue = 0.0f;
};

// The open dropdown list. `open` is the CycleControl index, `tab` the tab it was
// opened on (another tab's control can share the index). The anchor is the box,
// recorded by the layout when it draws the open control; a rebuild that does not
// draw it closes the list.
struct DropdownState {
    int open = -1;
    int tab = -1;
    bool anchored = false;
    int firstRegion = -1;   // the list's first entry in the click regions (they come last), -1 = shut
    float x = 0.0f, y = 0.0f, w = 0.0f, textX = 0.0f, rowH = 0.0f, fontSize = 0.0f;
};

// The name of state `i` in a dropdown's names array, "" when out of range (an
// INI edited by hand). The array a row names its value from is the one it hands
// CycleControl::names, so the field and the list cannot spell a state apart.
template <int N>
inline const char* cycleName(const char* const (&names)[N], int i) {
    return (i >= 0 && i < N) ? names[i] : "";
}
