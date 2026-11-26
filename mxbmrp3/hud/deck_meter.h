// ============================================================================
// hud/deck_meter.h
// What a deck's meter shows, and which state each block or part of it
// is in. Header-only and pure so a unit test can pin it (the blue_flag_detect.h
// pattern: BaseHud-derived code cannot link into a unit TU).
//
// A deck fills one Data per meter (deck_hud_gather.cpp); the layout decides
// whether it is drawn as a bar, a row of blocks or blocks along an arc
// (deck_layout.h). Positions along the meter are 0..1 from its near end. A
// CENTRED meter (the slide) runs -1..1 with 0 in the middle, and its hot point
// and zone are measured from the middle outwards, the same on both sides; a
// SIDED one lights its left side bad and its right side good (the Gap deck:
// behind the reference to the left, ahead of it to the right). MARKS are
// points on a centred meter (-1..1), each lit or not (the Rival deck's riders
// either side). A MIRRORED centred meter lights both sides out to |value|.
// A BAND meter (the riding gauges) lights only the band
// holding `value`: the zone, the hot band or the danger band past it, never
// the plain run between them; its parts are the bands' edges (bandParts), and
// an arc draws a needle at `value`. Pinned by tests/unit/test_deck_meter.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DeckMeter {

inline constexpr int MAX_PARTS = 30;

enum State : uint8_t { DIM = 0, LIT, HOT_DIM, HOT_LIT, ZONE_DIM, ZONE_LIT, GOOD, BAD, DANGER_DIM, DANGER_LIT, STATE_COUNT };

struct Data {
    bool show = false;       // false: the meter is not drawn at all
    float value = 0.0f;      // lit up to here (0..1; centred: -1..1)
    bool centered = false;
    bool sided = false;      // centred: lit left is BAD, lit right is GOOD
    bool mirrored = false;   // centred: lit out from the middle on BOTH sides,
                             // to |value| (the Combo deck's chain shrinks to it)
    State litAs = LIT;       // what a plainly lit block is drawn as: the Combo
                             // deck's chain is HOT_LIT while it runs, DANGER_LIT
                             // (blinking to DANGER_DIM) as it runs out
    float hotFrom = 2.0f;    // past this the meter is hot (> 1: never)
    float dangerFrom = 2.0f; // past this, further still: the danger band (> 1: never)
    bool band = false;       // only the band holding `value` is lit (see above)
    bool idle = false;       // nothing happening: the needle is drawn dim
    float zoneFrom = -1.0f;  // a zone to mark (both < 0: none)
    float zoneTo = -1.0f;
    // Parts: where each ends (the last is 1). 0 parts = one unbroken run.
    int parts = 0;
    float partEnd[MAX_PARTS] = {};
    // A part's own tone (DeckLayout::Tone): a finished sector is good or bad
    // as a whole, whatever the fill says.
    uint8_t partTone[MAX_PARTS] = {};
    // Marks on a centred meter: where (-1..1) and whether each is lit.
    static constexpr int MAX_MARKS = 2;
    int marks = 0;
    float markAt[MAX_MARKS] = {};
    bool markLit[MAX_MARKS] = {};
    // A knob at `value` (the layout's `knob`): the meter drags; hot = under the
    // pointer or being dragged
    bool knob = false;
    bool knobHot = false;
};

// Tone values deck_layout.h names; repeated here so this header stands alone.
inline constexpr uint8_t TONE_GOOD = 3;
inline constexpr uint8_t TONE_BAD = 4;

// The band a point `along` the scale (centred: from the middle out) sits in.
enum Band : uint8_t { BAND_PLAIN = 0, BAND_ZONE, BAND_HOT, BAND_DANGER };
inline Band bandAt(const Data& d, float along) {
    if (d.zoneTo > d.zoneFrom && along >= d.zoneFrom && along <= d.zoneTo) return BAND_ZONE;
    if (along >= d.dangerFrom) return BAND_DANGER;
    if (along >= d.hotFrom) return BAND_HOT;
    return BAND_PLAIN;
}

// The state of a block spanning a..b along the meter (0..1). `tone` is the
// tone of the part the block belongs to.
inline State blockState(const Data& d, float a, float b, uint8_t tone = 0) {
    if (tone == TONE_GOOD) return GOOD;
    if (tone == TONE_BAD) return BAD;
    const float mid = (a + b) * 0.5f;
    if (d.band) {
        // Lit: the block's band is the value's, on the value's side (the
        // zone at a centred meter's middle spans both sides)
        const float c = d.centered ? mid * 2.0f - 1.0f : mid;
        const Band mine = bandAt(d, std::fabs(c));
        const Band at = bandAt(d, std::fabs(d.value));
        const bool side = !d.centered || mine == BAND_ZONE || (c < 0.0f) == (d.value < 0.0f);
        const bool lit = !d.idle && mine == at && side;
        switch (mine) {
        case BAND_ZONE:   return lit ? ZONE_LIT : ZONE_DIM;
        case BAND_HOT:    return lit ? HOT_LIT : HOT_DIM;
        case BAND_DANGER: return lit ? DANGER_LIT : DANGER_DIM;
        case BAND_PLAIN:  break;
        }
        return DIM;
    }
    bool lit;
    float along;   // where the block sits on the scale the hot point and zone use
    if (d.centered) {
        const float c = mid * 2.0f - 1.0f;
        along = std::fabs(c);
        lit = d.value != 0.0f && (d.mirrored || (c < 0.0f) == (d.value < 0.0f)) && along <= std::fabs(d.value);
    } else {
        along = mid;
        lit = mid < d.value;
    }
    if (d.centered && d.sided && lit) return mid < 0.5f ? BAD : GOOD;
    switch (bandAt(d, along)) {
    case BAND_ZONE:   return lit ? ZONE_LIT : ZONE_DIM;
    case BAND_DANGER: return lit ? DANGER_LIT : DANGER_DIM;
    case BAND_HOT:    return lit ? HOT_LIT : HOT_DIM;
    case BAND_PLAIN:  break;
    }
    return lit ? d.litAs : DIM;
}

// The colour state of a band meter's needle: the lit state of the band it is
// in (LIT in the plain run: the layout's needle colour), DIM when idle.
inline State needleState(const Data& d) {
    if (d.idle) return DIM;
    switch (bandAt(d, std::fabs(d.value))) {
    case BAND_ZONE:   return ZONE_LIT;
    case BAND_HOT:    return HOT_LIT;
    case BAND_DANGER: return DANGER_LIT;
    case BAND_PLAIN:  break;
    }
    return LIT;
}

// A band meter's parts: one per band, cut at the zone's, the hot point's and
// the danger point's edges (both sides of a centred meter's middle), so an
// arc's segments follow the bands exactly.
inline void bandParts(Data& d) {
    float cuts[8];
    int n = 0;
    const auto add = [&](float f) {
        if (!std::isfinite(f) || f <= 0.0f || f >= 1.0f) return;
        if (d.centered) {
            cuts[n++] = 0.5f - f * 0.5f;
            cuts[n++] = 0.5f + f * 0.5f;
        } else {
            cuts[n++] = f;
        }
    };
    if (d.zoneTo > d.zoneFrom) {
        add(d.zoneFrom);
        add(d.zoneTo);
    }
    add(d.hotFrom);
    add(d.dangerFrom);
    std::sort(cuts, cuts + n);
    d.parts = 0;
    for (int i = 0; i < n; ++i) {
        if (d.parts > 0 && cuts[i] - d.partEnd[d.parts - 1] < 1e-4f) continue;
        d.partEnd[d.parts++] = cuts[i];
    }
    d.partEnd[d.parts++] = 1.0f;
}

// Which part (0-based) the position x (0..1) falls in; 0 when there are none.
inline int partAt(const Data& d, float x) {
    for (int i = 0; i < d.parts; ++i) {
        if (x < d.partEnd[i]) return i;
    }
    return d.parts > 0 ? d.parts - 1 : 0;
}

// The tone of the part holding x, 0 without parts.
inline uint8_t toneAt(const Data& d, float x) {
    return d.parts > 0 ? d.partTone[partAt(d, x)] : 0;
}

// A lap race's progress: laps done plus the way round this one, over the laps.
inline float raceProgress(int lapsDone, float lapFraction, int totalLaps) {
    if (totalLaps <= 0) return 0.0f;
    return std::clamp((static_cast<float>(std::max(lapsDone, 0)) + std::clamp(lapFraction, 0.0f, 1.0f))
                      / static_cast<float>(totalLaps), 0.0f, 1.0f);
}

// A gap on a sided meter: behind (positive ms) to the left, ahead to the
// right, full at rangeMs either way.
inline float gapPosition(int gapMs, int rangeMs) {
    if (rangeMs <= 0) return 0.0f;
    return std::clamp(-static_cast<float>(gapMs) / static_cast<float>(rangeMs), -1.0f, 1.0f);
}

// Even parts, one per lap (or anything counted), into d. More than MAX_PARTS
// would be slivers, so that many stays one run.
inline void evenParts(Data& d, int count) {
    d.parts = 0;
    if (count <= 1 || count > MAX_PARTS) return;
    d.parts = count;
    for (int i = 0; i < count; ++i) d.partEnd[i] = static_cast<float>(i + 1) / static_cast<float>(count);
}

}  // namespace DeckMeter
