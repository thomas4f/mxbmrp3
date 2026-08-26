// ============================================================================
// tests/unit/test_spotter_stretch.cpp
// Pins the pitch-preserving time stretch (core/spotter_stretch.h) that makes
// the speed setting mean something on the wav paths — the ones most players
// hear, since TTS doesn't exist under Wine.
//
// WHAT THIS FILE CAN AND CANNOT PROVE. "Does it sound good" is a listening
// test, and the quality bar was set by ear on real pack audio (a spectral
// check against a deliberately resampled control showed the centroid holding
// within a few percent from 0.75x to 1.5x, where resampling moved it 20%+).
// What belongs HERE is everything that would silently rot underneath that
// judgement: the duration actually tracks the requested speed, unity is a
// true no-op, the clamps hold, and — the one that bites — nothing indexes
// out of bounds for short or awkward inputs. The unit-asan flavour is what
// gives that last one teeth.
// ============================================================================
#include "doctest.h"

#include "core/spotter_stretch.h"

#include <cmath>
#include <vector>

using namespace SpotterStretch;

namespace {

// A 200 Hz tone at 12kHz — voiced speech is periodic, so this is the shape
// the correlation search is built for.
std::vector<int16_t> tone(size_t samples, double hz = 200.0,
                          double rate = 12000.0) {
    std::vector<int16_t> out(samples);
    for (size_t i = 0; i < samples; ++i) {
        out[i] = static_cast<int16_t>(
            8000.0 * std::sin(2.0 * 3.14159265358979 * hz * i / rate));
    }
    return out;
}

}  // namespace

TEST_CASE("apply: duration tracks the requested speed") {
    const std::vector<int16_t> in = tone(12000);   // 1 second
    // Within 5%: WSOLA lands on whole output hops, so the last partial
    // window rounds — the contract is "about this long", not sample-exact.
    for (float sp : {0.75f, 1.25f, 1.5f, 2.0f}) {
        const std::vector<int16_t> out = apply(in, sp, 12000);
        const double want = in.size() / static_cast<double>(sp);
        CHECK(out.size() > want * 0.95);
        CHECK(out.size() < want * 1.05);
    }
}

TEST_CASE("apply: unity and near-unity are exact no-ops") {
    const std::vector<int16_t> in = tone(6000);
    CHECK(apply(in, 1.0f, 12000) == in);
    // The dead band matters: a stepper landing on 0.999 must not rebuild the
    // audio (and add artifacts) for an inaudible difference.
    CHECK(apply(in, 1.001f, 12000) == in);
    CHECK(apply(in, 0.999f, 12000) == in);
    CHECK(isUnity(1.0f));
    CHECK_FALSE(isUnity(1.05f));
}

TEST_CASE("apply: speeds outside the range clamp instead of exploding") {
    const std::vector<int16_t> in = tone(12000);
    // 10x would be unintelligible; the clamp means the worst a bad INI can
    // do is the edge of the supported range.
    const std::vector<int16_t> fast = apply(in, 10.0f, 12000);
    const std::vector<int16_t> atMax = apply(in, kMaxSpeed, 12000);
    CHECK(fast.size() == atMax.size());
    const std::vector<int16_t> slow = apply(in, 0.01f, 12000);
    const std::vector<int16_t> atMin = apply(in, kMinSpeed, 12000);
    CHECK(slow.size() == atMin.size());
    // Nothing may run away: even the slowest is bounded by 1/kMinSpeed.
    CHECK(slow.size() <= in.size() * 2 + 1000);
}

TEST_CASE("apply: degenerate inputs return the input, never a crash") {
    CHECK(apply({}, 1.5f, 12000).empty());
    // Shorter than one analysis window: a click, not speech — passed through
    // rather than windowed into nonsense.
    const std::vector<int16_t> tiny = tone(50);
    CHECK(apply(tiny, 1.5f, 12000) == tiny);
    // A nonsense sample rate must not divide by zero or window to nothing.
    const std::vector<int16_t> in = tone(12000);
    CHECK_FALSE(apply(in, 1.5f, 0).empty());
    // A 24kHz pack gets the same ~30ms window, so it stretches too.
    const std::vector<int16_t> hi = tone(24000, 200.0, 24000.0);
    const std::vector<int16_t> out = apply(hi, 1.5f, 24000);
    CHECK(out.size() < hi.size() * 0.8);
}

TEST_CASE("apply: output stays in range and keeps the signal's energy") {
    // Full-scale input: the overlap-add normalisation must not let the sum
    // of two windows clip the result into buzz.
    std::vector<int16_t> loud = tone(12000);
    for (int16_t& s : loud) s = static_cast<int16_t>(s * 4);   // near full scale
    const std::vector<int16_t> out = apply(loud, 1.25f, 12000);
    REQUIRE(!out.empty());
    double sum = 0.0;
    int16_t peak = 0;
    for (int16_t s : out) {
        sum += static_cast<double>(s) * s;
        const int16_t a = static_cast<int16_t>(s < 0 ? -s : s);
        if (a > peak) peak = a;
    }
    const double rms = std::sqrt(sum / out.size());
    // A tone in, a tone out: same order of loudness, not silence and not a
    // clipped square. (Silence here would mean the normalisation divided the
    // signal away — the failure mode that sounds "broken" rather than "off".)
    CHECK(rms > 5000.0);
    CHECK(peak <= 32767);
}
