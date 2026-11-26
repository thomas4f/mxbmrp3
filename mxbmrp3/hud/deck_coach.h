// ============================================================================
// hud/deck_coach.h
// The riding decks' game rules: the Wheelie deck's hold timer, balance zone and
// cue, the Slide deck's warning bands and the timer every riding gauge shows
// (a Spell).
// Header-only and pure so a unit test can pin them (the blue_flag_detect.h
// pattern).
//
// DISPLAY ONLY. Nothing here feeds the FMX score, stats or achievements: the
// decks keep their own session numbers and lose them with the session.
//
// The thresholds are first values, chosen from what the signals read in normal
// riding; each is a named constant so tuning one is one line. Pinned by
// tests/unit/test_deck_coach.cpp.
// ============================================================================
#pragma once

#include "../core/wheelie_angle.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DeckCoach {

// ---- Wheelie -----------------------------------------------------------------
//
// A hold is a wheelie as FMX counts one (WheelieAngle::held, core/
// wheelie_angle.h): it starts at FROM_DEG on the back wheel and ends when the
// front wheel touches or the raw pitch comes back over half that. The BALANCE ZONE is learned from the rider's own long holds:
// the median of their mean angles, +- ZONE_HALF. Until LEARN_HOLDS of them
// exist the default zone applies, which suits most bikes' balance point.
struct Wheelie {
    static constexpr float LEARN_MIN_HOLD = 2.0f;     // seconds: shorter holds teach nothing
    static constexpr int LEARN_HOLDS = 3;             // holds before the zone is the rider's own
    static constexpr float DEFAULT_LO = 25.0f, DEFAULT_HI = 45.0f;
    static constexpr float ZONE_HALF = 7.0f;          // degrees either side of the learned centre
    static constexpr float LOOP_FROM = 60.0f;         // degrees: past the zone is too much, past this a loop-out
    static constexpr int MAX_SAMPLES = 15;

    bool active = false;
    float hold = 0.0f;       // this hold so far, or the last one once it ended
    float best = 0.0f;       // the session's longest hold
    uint32_t bestSeq = 0;    // bumped when a hold beats the best (the NEW BEST stamp)
    float sum = 0.0f;        // angle * seconds over this hold, for its mean
    float samples[MAX_SAMPLES] = {};
    int sampleCount = 0;
    int sampleNext = 0;
    float median = 0.0f;     // the samples' median, worked out as each is added

    void reset() { *this = Wheelie{}; }

    // angleDeg: WheelieAngle::of; pitch: the raw world pitch (nose up negative)
    void step(float angleDeg, bool frontWheelDown, float pitch, float dt) {
        if (!(dt > 0.0f) || !std::isfinite(angleDeg)) return;
        if (WheelieAngle::held(active, angleDeg, frontWheelDown, pitch)) {
            if (!active) {
                active = true;
                hold = 0.0f;
                sum = 0.0f;
            }
            hold += dt;
            sum += angleDeg * dt;
            return;
        }
        if (!active) return;
        active = false;
        if (hold > best) {
            // The first hold of a session sets a best without a stamp: there
            // was nothing to beat.
            if (best > 0.0f) ++bestSeq;
            best = hold;
        }
        if (hold >= LEARN_MIN_HOLD) {
            samples[sampleNext] = sum / hold;
            sampleNext = (sampleNext + 1) % MAX_SAMPLES;
            sampleCount = std::min(sampleCount + 1, MAX_SAMPLES);
            float sorted[MAX_SAMPLES];
            for (int i = 0; i < sampleCount; ++i) sorted[i] = samples[i];
            std::sort(sorted, sorted + sampleCount);
            median = sampleCount % 2 ? sorted[sampleCount / 2]
                                     : (sorted[sampleCount / 2 - 1] + sorted[sampleCount / 2]) * 0.5f;
        }
    }

    bool learned() const { return sampleCount >= LEARN_HOLDS; }

    float centre() const { return median; }
    float zoneLo() const { return learned() ? std::max(centre() - ZONE_HALF, WheelieAngle::FROM_DEG) : DEFAULT_LO; }
    float zoneHi() const { return learned() ? centre() + ZONE_HALF : DEFAULT_HI; }
};

// ---- Spell -------------------------------------------------------------------
//
// How long something has been going on: the riding gauges' timer (a slide, a
// spin). `t` is this spell so far, or the last one once it ended; `max` the
// session's longest, rising with the spell in progress.
struct Spell {
    bool active = false;
    float t = 0.0f;
    float max = 0.0f;

    void reset() { *this = Spell{}; }

    void step(bool on, float dt) {
        if (!(dt > 0.0f)) return;
        if (!on) {
            active = false;
            return;
        }
        if (!active) {
            active = true;
            t = 0.0f;
        }
        t += dt;
        max = std::max(max, t);
    }
};

// ---- Slide warning -------------------------------------------------------------
//
// The Slide deck warns: sliding costs time, so only grip is good. Under GRIP_BELOW
// degrees the bike grips; to SAVE_FROM it slides (too much); past that, save
// it (way too much). A slide (the deck's timer) is GRIP_BELOW or more. The
// three are the riding gauges' one colour rule: green, amber, red.
enum class SlideBand : uint8_t { GRIP = 0, SLIDING, SAVE };
inline constexpr float GRIP_BELOW = 6.0f;
inline constexpr float SAVE_FROM = 35.0f;

inline SlideBand slideBand(float signedDeg) {
    const float a = std::isfinite(signedDeg) ? std::fabs(signedDeg) : 0.0f;
    if (a < GRIP_BELOW) return SlideBand::GRIP;
    if (a <= SAVE_FROM) return SlideBand::SLIDING;
    return SlideBand::SAVE;
}

}  // namespace DeckCoach
