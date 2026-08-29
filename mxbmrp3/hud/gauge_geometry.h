// ============================================================================
// hud/gauge_geometry.h
// One gauges pack's geometry: what each dial's FACE reads and where its needle
// sits on it. Pure data and arithmetic -- no rendering, no PluginData.
//
// THE DIAL FACE IS A PICTURE and its scale is data about that picture, which is
// the whole reason this file exists. The numbers a rider reads are painted into
// the .tga; the needle is placed by the range and sweep below. Have those
// compiled in and the two can disagree with nothing to say so: before this,
// TachoWidget carried MAX_RPM = 15000 and MIN/MAX_ANGLE_DEG, so a custom face
// drawn to any other ceiling got a needle that lied at every point but zero, and
// the only "correct" custom art was art that reproduced the shipped tick layout
// exactly. That is not a limitation of the texture hook, it is a trap in it.
//
// It was already wrong without a modder involved: neither gauge is game-gated,
// so the same 230 km/h face ships to GP Bikes (where the needle pins on the
// straight) and Kart Racing Pro (where it never leaves the first half of the
// dial). A pack is what lets a face and its scale travel together, per game and
// per bike -- the same argument pitboard_geometry.h makes for row offsets and
// gamepad_geometry.h for button positions, which this deliberately mirrors.
//
// Needle LENGTH and WIDTH are here for the same reason: they are fractions of
// the dial's height, so they describe how a needle looks against THIS face --
// a long fine pointer on an open face, a stubby one on a busy one.
//
// Needle COLOUR is here rather than in the user's settings file because it has
// to contrast with this pack's own artwork, exactly as a pit board's [text]
// color does. A dark carbon face states white; the shipped chrome face states
// red. The user's own [TachoWidget] needleColor still wins when they set one --
// this is the default the pack ships, not a lock.
//
// What is NOT here: needle smoothing. That is feel, not art -- the same physical
// inertia whatever the face looks like -- so it stays compiled in the widgets.
// ============================================================================
#pragma once

#include <cstdint>   // uint32_t needleColor -- the unit build compiles this standalone

namespace GaugeLayout {

// Miles to kilometres, for the `max-mph` convenience key below. Stated here
// rather than reached for from UnitConversion so this header keeps its "no
// includes but <cstdint>" property, which is what lets AssetManager hold a
// GaugeGeometry without pulling the HUD layer in behind it.
inline constexpr float MI_TO_KM = 1.609344f;

// One dial: what its face reads end to end, where those ends sit as angles, and
// what the needle over it looks like.
struct Dial {
    // The value at each end of the sweep, in the gauge's own unit (RPM for the
    // tacho, km/h for the speedo -- see GaugeGeometry). `min` is almost always
    // zero; a face whose scale starts partway up states it rather than pretending
    // the first tick is nothing.
    float min = 0.0f;
    float max = 0.0f;

    // Where those two ends sit, in degrees, 0 straight up and increasing
    // clockwise. The shipped faces sweep -158 to 142, i.e. 300 degrees with the
    // gap at the bottom; a two-thirds face states something much narrower.
    float minAngle = -158.0f;
    float maxAngle = 142.0f;

    // The needle, as fractions of the dial's drawn height: how far it reaches
    // from the centre, and how thick it is.
    float needleLength = 0.42f;
    float needleWidth = 0.025f;

    // The game's ABGR word. Authored as #rrggbb in the ini, like a theme's
    // colours and a pit board's text (parseRgbHex converts), so the packing
    // stays out of hand-written files. Default is the red both shipped faces
    // were drawn for.
    uint32_t needleColor = 0xFF0000FF;

    // The needle's angle for a reading, in degrees. Clamps to the face: a value
    // past either end parks the needle at that end rather than swinging off the
    // dial, which is what a real gauge does and what the widgets did before the
    // range was data.
    //
    // Guarded against a hand-edited ini describing no span at all (max <= min):
    // the honest answer there is "the needle does not move", not a division by
    // zero that would put it at a NaN angle and take the quad with it.
    float angleFor(float value) const {
        const float span = max - min;
        if (!(span > 0.0f)) return minAngle;
        float v = value;
        if (v < min) v = min;
        if (v > max) v = max;
        return minAngle + ((v - min) / span) * (maxAngle - minAngle);
    }
};

// Named construction of a dial's range. Positional aggregate init would read
// `Dial{0.0f, 15000.0f}` at the two use sites below, which is exactly the shape
// that goes silently wrong the day a field is inserted above `max`.
constexpr Dial dialRange(float lo, float hi) {
    Dial d{};
    d.min = lo;
    d.max = hi;
    return d;
}

// Both dials of one pack. They travel together because they are drawn together:
// a "carbon" set is one act of authorship, and splitting them across two pack
// directories would mean picking the same name twice for the matched case and
// buying nothing the `base =` skin rule does not already give the mixed one.
struct GaugeGeometry {
    // Engine speed, in RPM. 15000 is the ceiling the shipped face was drawn to.
    Dial tacho = dialRange(0.0f, 15000.0f);

    // Road speed, in KM/H -- always, whatever unit the face is printed in,
    // because that is the unit the widget clamps and smooths in. A face drawn in
    // mph states `max-mph` instead and the reader converts, so nobody has to do
    // the arithmetic in their head and be silently 60% out.
    Dial speedo = dialRange(0.0f, 230.0f);

    // Where the odometer and trip-meter readouts sit, as a fraction of the
    // dial's height from its top edge. Part of the pack because the shipped face
    // has a window drawn for them: art that puts its window elsewhere moves the
    // digits to match instead of having them float over the ticks.
    float odometerY = 0.33f;
    float tripmeterY = 0.66f;
};

// The gauge ini's numeric surface, in two tables because the keys address two
// levels. Scoped "section.property" exactly as layoutForEachIniPairRaw hands
// them over, and living here beside the structs rather than in AssetManager's
// discovery walk for the same reason kBoardGeometryIni does: a unit test can
// then drive the real mapping over the real shipped ini without a filesystem.
struct GaugeDialIniEntry {
    const char* key;
    Dial GaugeGeometry::* dial;
    float Dial::* field;
};

inline constexpr GaugeDialIniEntry kGaugeDialIni[] = {
    { "tacho.min",           &GaugeGeometry::tacho,  &Dial::min },
    { "tacho.max",           &GaugeGeometry::tacho,  &Dial::max },
    { "tacho.min-angle",     &GaugeGeometry::tacho,  &Dial::minAngle },
    { "tacho.max-angle",     &GaugeGeometry::tacho,  &Dial::maxAngle },
    { "tacho.needle-length", &GaugeGeometry::tacho,  &Dial::needleLength },
    { "tacho.needle-width",  &GaugeGeometry::tacho,  &Dial::needleWidth },
    { "speedo.min",           &GaugeGeometry::speedo, &Dial::min },
    { "speedo.max",           &GaugeGeometry::speedo, &Dial::max },
    { "speedo.min-angle",     &GaugeGeometry::speedo, &Dial::minAngle },
    { "speedo.max-angle",     &GaugeGeometry::speedo, &Dial::maxAngle },
    { "speedo.needle-length", &GaugeGeometry::speedo, &Dial::needleLength },
    { "speedo.needle-width",  &GaugeGeometry::speedo, &Dial::needleWidth },
};

struct GaugeOwnIniEntry {
    const char* key;
    float GaugeGeometry::* field;
};

inline constexpr GaugeOwnIniEntry kGaugeOwnIni[] = {
    { "speedo.odometer-y",  &GaugeGeometry::odometerY },
    { "speedo.tripmeter-y", &GaugeGeometry::tripmeterY },
};

// Apply one scoped key to `g`. False when the key names nothing, so the caller
// can report the typo -- a silently ignored range looks exactly like art that
// will not line up, which is the failure this whole file exists to remove.
//
// `speedo.max-mph` is the one key that is not a straight store: it writes the
// SAME field as `speedo.max`, converted, so a face printed in mph is authored in
// mph. Deliberately a second key rather than a `unit =` line, because a unit
// line makes the meaning of `max` depend on whether it was read first.
//
// A non-finite value is rejected rather than stored, for the same reason the
// settings loader guards persisted floats.
inline bool applyGaugeGeometryIni(GaugeGeometry& g, const char* key, float value) {
    // isfinite without <cmath>: NaN is the only value unequal to itself, and the
    // infinities are the only finite-comparison failures. Same test, and same
    // reason, as applyBoardGeometryIni's.
    const bool finite = !(value != value || value > 3.4e38f || value < -3.4e38f);

    auto same = [](const char* a, const char* b) {
        for (int i = 0; ; ++i) {
            if (a[i] != b[i]) return false;
            if (a[i] == '\0') return true;
        }
    };

    for (const GaugeDialIniEntry& e : kGaugeDialIni) {
        if (!same(e.key, key)) continue;
        if (finite) (g.*(e.dial)).*(e.field) = value;
        return true;
    }
    for (const GaugeOwnIniEntry& e : kGaugeOwnIni) {
        if (!same(e.key, key)) continue;
        if (finite) g.*(e.field) = value;
        return true;
    }
    if (same("speedo.max-mph", key)) {
        if (finite) g.speedo.max = value * MI_TO_KM;
        return true;
    }
    if (same("speedo.min-mph", key)) {
        if (finite) g.speedo.min = value * MI_TO_KM;
        return true;
    }
    return false;
}

}  // namespace GaugeLayout
