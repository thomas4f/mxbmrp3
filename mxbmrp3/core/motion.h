// ============================================================================
// core/motion.h
// Motion ([Display] motion, Settings > Appearance > Motion): a HUD fades and
// rises into place when it appears and fades and sinks out when it goes,
// instead of popping. Pure timing and easing - no HUD, no renderer.
//
// WHERE IT IS APPLIED, and why there: HudManager::collectSurface, on the copy of
// each HUD's primitives it hands the renderer (hud_manager_motion.cpp). So no HUD
// rebuilds to animate and no HUD knows about it: the classic renderer, the
// in-game GL path and the companion window all draw the same faded, offset
// quads, and a future GPU path can take the same alpha and offset as a
// transform. "Appears" is judged there too: shown, AND drawing something - a
// notice or a toast draws nothing until it has something to say, so it fades in
// when it does and out when it empties.
//
// ONE PROGRESS VALUE PER HUD, moved toward its target at the profile's rate, not
// a start time. A HUD shown again halfway through fading out turns round from
// where it is, so a one-frame gap in a HUD's content costs a frame's worth of
// fade rather than a blink back to zero.
//
// A HUD seen for the first time starts SETTLED in whatever state it is in, so
// switching Motion on (or loading the plugin) does not fade every HUD in at once.
//
// Pinned by tests/unit/test_motion.cpp; the collect side (Off is the frame it
// always was, a hidden HUD keeps drawing while it fades) by
// tests/integration/tests/motion_test.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>

namespace Motion {

// Persisted by NAME ([Display] motion=Subtle); the cycle order is the numeric order.
enum class Level : uint8_t { OFF = 0, SUBTLE = 1, NORMAL = 2 };
constexpr int LEVEL_COUNT = 3;
// The test build defaults to Off: the integration suite reads frames by count
// and hash right after toggling HUDs, on the real clock, and a fade in flight
// would make those depend on timing. motion_test sets its levels explicitly.
#if defined(MXBMRP3_TEST_BUILD)
constexpr Level DEFAULT_LEVEL = Level::OFF;
#else
constexpr Level DEFAULT_LEVEL = Level::SUBTLE;
#endif

constexpr const char* levelName(Level level) {
    return level == Level::OFF ? "Off" : level == Level::NORMAL ? "Normal" : "Subtle";
}

struct Profile {
    float inMs;        // hidden -> shown
    float outMs;       // shown -> hidden
    float risePx;      // 1080p pixels travelled from below on the way in
    float overshoot;   // ease-out-back strength on the way in; 0 = no spring
};

constexpr Profile profileFor(Level level) {
    return level == Level::NORMAL ? Profile{ 240.0f, 180.0f, 14.0f, 1.4f }
         : level == Level::SUBTLE ? Profile{ 160.0f, 130.0f, 6.0f, 0.0f }
                                  : Profile{ 0.0f, 0.0f, 0.0f, 0.0f };
}

// The longest step one frame may take. A frame after a long gap (the game draws
// nothing in menus or while loading) would otherwise finish every fade at once,
// so what changed while nothing was drawn still animates when drawing resumes.
constexpr float MAX_STEP_MS = 50.0f;

// A tab's content fading in over its unchanged panel rises by this fraction of
// the profile's rise.
constexpr float PART_RISE_SCALE = 0.5f;

// 0..1 -> 0..1, flat at both ends.
constexpr float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Ease-out-back: 1 + (k+1)u^3 + k u^2 with u = t-1. k = 0 is a plain ease-out
// cubic; k > 0 travels past 1 and settles back.
constexpr float easeOut(float t, float k) {
    const float u = t - 1.0f;
    return 1.0f + (k + 1.0f) * u * u * u + k * u * u;
}

// One HUD's motion state on one surface.
struct Track {
    float p = 0.0f;           // 0 hidden .. 1 shown
    float partP = 1.0f;       // a sub-range's own fade in (the settings tab body)
    uint32_t partEpoch = 0;   // the HUD's part epoch this track last saw
    bool seen = false;        // false: settle on the next step instead of animating
};

// What to do with a HUD this frame.
struct Frame {
    bool emit = false;        // draw it (shown, or still fading out)
    float alpha = 1.0f;       // multiplies every primitive's alpha
    float dy = 0.0f;          // added to every primitive's y (normalized, +down)
    float partAlpha = 1.0f;   // the same for the HUD's marked sub-range, on top
    float partDy = 0.0f;
    bool moving() const { return alpha < 1.0f || dy != 0.0f; }
    bool partMoving() const { return partAlpha < 1.0f || partDy != 0.0f; }
};

// Advance `t` by dtMs given whether the HUD draws this frame. partEpoch is the
// HUD's counter for "my marked sub-range just changed" (0 for HUDs without one).
inline Frame step(Track& t, bool drawnNow, uint32_t partEpoch, float dtMs, const Profile& pr) {
    if (!t.seen) {
        t.seen = true;
        t.p = drawnNow ? 1.0f : 0.0f;
        t.partP = 1.0f;
        t.partEpoch = partEpoch;
    }
    const float dt = std::clamp(dtMs, 0.0f, MAX_STEP_MS);
    if (drawnNow) t.p = pr.inMs > 0.0f ? std::min(1.0f, t.p + dt / pr.inMs) : 1.0f;
    else          t.p = pr.outMs > 0.0f ? std::max(0.0f, t.p - dt / pr.outMs) : 0.0f;

    // A new sub-range restarts its own fade only over a settled panel: while the
    // whole HUD is still coming in, the HUD's own fade already covers it.
    if (partEpoch != t.partEpoch) {
        t.partEpoch = partEpoch;
        if (drawnNow && t.p >= 1.0f) t.partP = 0.0f;
    }
    if (t.partP < 1.0f) t.partP = pr.inMs > 0.0f ? std::min(1.0f, t.partP + dt / pr.inMs) : 1.0f;

    Frame f;
    f.emit = drawnNow || t.p > 0.0f;
    const float rise = pr.risePx / 1080.0f;
    if (t.p < 1.0f) {
        f.alpha = smooth(t.p);
        // The spring belongs to the way in; going out it just sinks.
        f.dy = (1.0f - easeOut(t.p, drawnNow ? pr.overshoot : 0.0f)) * rise;
    }
    if (t.partP < 1.0f) {
        f.partAlpha = smooth(t.partP);
        f.partDy = (1.0f - easeOut(t.partP, 0.0f)) * rise * PART_RISE_SCALE;
    }
    return f;
}

// One colour's alpha times `alpha` (0..1), RGB kept. Colours are 0xAABBGGRR.
constexpr unsigned long scaleAlpha(unsigned long color, float alpha) {
    const float a = static_cast<float>((color >> 24) & 0xFFul) * alpha + 0.5f;
    const unsigned long ai = a <= 0.0f ? 0ul : a >= 255.0f ? 255ul : static_cast<unsigned long>(a);
    return (color & 0x00FFFFFFul) | (ai << 24);
}

}  // namespace Motion
