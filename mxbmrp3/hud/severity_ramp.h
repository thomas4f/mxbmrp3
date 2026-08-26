// ============================================================================
// hud/severity_ramp.h
// ONE VALUE -> ONE COLOUR, on the palette's POSITIVE -> NEUTRAL -> NEGATIVE ramp.
// Pure arithmetic: no HUD, no palette lookup, no rendering.
//
// WHY IT IS SHARED. Three gauges say the same thing -- "this reading is fine /
// getting serious / at the limit" -- and each had written the ramp out again:
// GForceWidget::getMagnitudeColor, RadarHud's proximity gradient, and (as of this
// header) LeanWidget. Two copies is a coincidence; three is a helper. The shape is
// piecewise linear with the midpoint at t = 0.5, so a gauge's colour and any other
// gauge's colour mean the same thing at the same fraction of full scale -- which is
// the property a shared ramp buys and three private ones cannot promise.
//
// ALPHA IS LERPED WITH RGB. A user may set a palette slot to a non-opaque colour,
// and interpolating only the RGB silently forces the output opaque -- so the alpha
// travels with it.
//
// NOT USED BY RadarHud, deliberately: it pre-extracts its three colours into a POD
// once per rebuild and interpolates per RIDER inside the draw loop, and it ramps the
// other way round (close = NEGATIVE). Same shape, different hot-path shape; folding
// it in here would put a palette-slot read back inside that loop.
//
// Pinned by tests/unit/test_severity_ramp.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>

namespace SeverityRamp {

// t is the reading as a fraction of full scale, clamped to [0, 1]:
//   0.0 -> lo (POSITIVE), 0.5 -> mid (NEUTRAL), 1.0 -> hi (NEGATIVE).
// Colours are the engine's 0xAABBGGRR words, in and out.
inline unsigned long at(float t, unsigned long lo, unsigned long mid, unsigned long hi) {
    t = std::min(1.0f, std::max(0.0f, t));

    auto chan = [](unsigned long v, int shift) {
        return static_cast<uint8_t>((v >> shift) & 0xFF);
    };
    // Half the ramp, and which half: below the midpoint it is lo -> mid, above it
    // mid -> hi, with u running 0..1 across whichever half t landed in.
    const unsigned long a = (t < 0.5f) ? lo : mid;
    const unsigned long b = (t < 0.5f) ? mid : hi;
    const float u = (t < 0.5f) ? (t * 2.0f) : ((t - 0.5f) * 2.0f);

    unsigned long out = 0;
    for (int shift = 0; shift <= 24; shift += 8) {
        const float ca = static_cast<float>(chan(a, shift));
        const float cb = static_cast<float>(chan(b, shift));
        // +0.5 rounds instead of truncating: without it an exact midpoint between
        // two channel values lands a step low, which is visible as a seam when two
        // gauges at the same fraction of scale are next to each other.
        const auto v = static_cast<unsigned long>(ca + u * (cb - ca) + 0.5f);
        out |= (v & 0xFF) << shift;
    }
    return out;
}

}  // namespace SeverityRamp
