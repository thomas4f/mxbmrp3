// ============================================================================
// core/impact_vignette.h
// Impact vignette ([HelmetOverlay] impactVignette, Settings > Helmet > Impact
// vignette): with Settings > Helmet on, the screen edges darken when the
// rider takes a hard hit, the harder the hit the further in, then clear
// again. Pure envelope and geometry maths - the HUD that draws it is
// hud/impact_vignette_hud.h.
//
// THE SIGNAL is the magnitude of the chassis acceleration vector, in g, as the
// engine reports it (averaged over 10 ms, gravity included, so a bike at rest
// reads about 1g). Telemetry arrives at 100 Hz and a landing spike can be a
// single sample, which a 60 fps frame can step straight over, so PluginData
// keeps the largest sample since the HUD last asked (takeImpactPeakG).
//
// TWO STAGES, so a one-sample spike still reads as a full hit and the effect
// stays smooth: the PEAK jumps to the hit at once and falls at a steady rate;
// the LEVEL eases toward the peak quickly. A hit therefore swells in over a few
// frames and fades out, and a second hit during the fade swells from wherever
// the fade has got to.
//
// THE FADE IS LINEAR, so it lasts as long as the hit was big: a full hit clears
// in the player's Fade time, a hit half that size in half of it. An exponential
// fade (the first version) clears every hit in nearly the same time, so a small
// knock lingered as long as a heavy landing.
//
// Pinned by tests/unit/test_impact_vignette.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace ImpactVignette {

// Below ONSET nothing shows: everyday riding, braking, a kart's hardest
// cornering and ordinary jump landings stay clear of it (the reading includes
// gravity). FULL is where the G-force widget's ring ends: the game survives
// hits past it, so the range runs that far for a big landing to look bigger
// than a firm one. Readings past PlausibleG::MAX never reach here (PluginData
// drops them).
constexpr float G_ONSET = 5.0f;
constexpr float G_FULL = 20.0f;

constexpr float RISE_RATE = 30.0f;     // per second: level eases to the peak (~35 ms to 63%)
// The fade: how long a full hit takes to clear, the player's setting; a
// smaller hit clears in its share of it.
constexpr float FADE_DEFAULT_S = 1.5f;
constexpr float FADE_MIN_S = 0.3f;
constexpr float FADE_MAX_S = 5.0f;
constexpr float FADE_STEP_S = 0.1f;
// How far the peak falls per second for a fade of `seconds`: a full hit (1)
// in exactly that time.
inline float releaseRate(float seconds) {
    return 1.0f / std::clamp(seconds, FADE_MIN_S, FADE_MAX_S);
}
constexpr float IDLE_EPSILON = 0.002f; // both below this = nothing to draw

// The dark ring sits further out for a light hit: the full-screen quad grows by
// up to this fraction, so the texture's dark edge moves off screen.
constexpr float SPREAD = 0.6f;

// The settings stepper: 0 = Off. On by default: it shows only while
// Settings > Helmet is on, which starts off.
constexpr float STRENGTH_STEP = 0.1f;
constexpr float STRENGTH_MAX = 1.0f;
constexpr float STRENGTH_DEFAULT = 0.5f;

// 0..1 hit size for a sample of `g` total acceleration.
inline float hitFromG(float g) {
    if (!(g == g)) return 0.0f;   // NaN
    return std::clamp((g - G_ONSET) / (G_FULL - G_ONSET), 0.0f, 1.0f);
}

struct Envelope {
    float peak = 0.0f;
    float level = 0.0f;

    void reset() { peak = 0.0f; level = 0.0f; }
    bool idle() const { return peak < IDLE_EPSILON && level < IDLE_EPSILON; }

    // One frame: `hit` is the largest hit since the last step, dt in seconds,
    // fadeS the player's fade setting.
    void step(float hit, float dt, float fadeS = FADE_DEFAULT_S) {
        dt = std::clamp(dt, 0.0f, 0.1f);   // a stall must not jump the fade
        peak = std::max(peak - releaseRate(fadeS) * dt, std::clamp(hit, 0.0f, 1.0f));
        level += (peak - level) * (1.0f - std::exp(-RISE_RATE * dt));
        if (idle()) reset();
    }
};

// Half the quad's size in screen units (0.5 = exactly the screen) at `level`.
inline float halfExtent(float level) {
    return 0.5f * (1.0f + (1.0f - std::clamp(level, 0.0f, 1.0f)) * SPREAD);
}

// The quad's corners at `level`, centred on and scaled to the visible screen
// (left/top/right/bottom in UI units, InputManager::getWindowBounds()). The
// UI's 0..1 is the centred 16:9 area, so a quad sized to it leaves the sides
// of an ultrawide screen (and the top and bottom of a narrower one) clear; the
// vignette instead stretches to whatever the screen is.
struct Corners {
    float l, t, r, b;
};
inline Corners corners(float level, float left, float top, float right, float bottom) {
    const float half = halfExtent(level);
    const float cx = 0.5f * (left + right), cy = 0.5f * (top + bottom);
    const float hw = half * (right - left), hh = half * (bottom - top);
    return {cx - hw, cy - hh, cx + hw, cy + hh};
}

// The quad's opacity at `level` for the player's strength.
inline float opacity(float level, float strength) {
    return std::clamp(level * strength, 0.0f, 1.0f);
}

}  // namespace ImpactVignette
