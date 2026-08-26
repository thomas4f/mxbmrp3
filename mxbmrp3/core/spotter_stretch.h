// ============================================================================
// core/spotter_stretch.h
// Pitch-preserving time stretch for the spotter's wav backends (WSOLA), so
// the speed setting means something on the path most players hear.
//
// WHY NOT JUST RESAMPLE. Playing samples faster is one line and gives you
// chipmunks: resampling scales time and pitch together. Speech has to keep
// its pitch to stay the same voice, which means resynthesising the timeline —
// overlap-add, not interpolation.
//
// WSOLA IN ONE PARAGRAPH. Cut the input into overlapping windows, lay them
// back down at a different spacing, and cross-fade the overlaps. Output hop
// (kHopOut) is fixed; input hop is kHopOut * speed, so 1.25 reads further per
// window and the clip gets shorter. Plain OLA does exactly that and sounds
// "fluttery" on vowels, because consecutive windows land out of phase with
// each other. WSOLA fixes it by letting each window start slide within a
// small search range to the offset that best CORRELATES with what has
// already been written — the waveform stays continuous through the join.
// That search is the whole difference between this and the naive version,
// and it is why it is worth the ~30 extra lines.
//
// TUNED FOR SHORT RADIO CALLOUTS, not music: a ~30ms window at pack rate
// (12kHz -> 360 samples) is long enough to carry a pitch period and short
// enough that a one-second cue still gets ~60 windows. Speed is clamped to
// [kMinSpeed, kMaxSpeed]; past that, stretch artifacts stop being subtle no
// matter how good the search is, and the setting has no business there.
//
// Pure and header-only: tests/unit/test_spotter_stretch.cpp drives it with
// synthetic tones, and the ASan flavour is what proves the indexing.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace SpotterStretch {

constexpr float kMinSpeed = 0.5f;
constexpr float kMaxSpeed = 2.0f;

// Windowing, in samples at a nominal 12kHz pack rate. Scaled by the caller's
// real rate so a 24kHz pack gets the same ~30ms window rather than half of
// one.
constexpr int kWindowAt12k = 360;   // 30ms
constexpr int kSearchAt12k = 60;    // +/-5ms of slide for the correlation

inline bool isUnity(float speed) { return speed > 0.995f && speed < 1.005f; }

// Time-stretch `in` by `speed` (>1 = shorter/faster, <1 = longer/slower)
// WITHOUT changing pitch. Returns the input unchanged for unity speed, an
// empty vector for empty input. Never allocates unboundedly: the output is
// at most in.size()/kMinSpeed + one window.
inline std::vector<int16_t> apply(const std::vector<int16_t>& in, float speed,
                                  int sampleRate) {
    if (in.empty() || isUnity(speed)) return in;
    if (speed < kMinSpeed) speed = kMinSpeed;
    if (speed > kMaxSpeed) speed = kMaxSpeed;
    if (sampleRate <= 0) sampleRate = 12000;

    const int window = kWindowAt12k * sampleRate / 12000;
    const int search = kSearchAt12k * sampleRate / 12000;
    const int hopOut = window / 2;                       // 50% overlap
    const int hopIn = static_cast<int>(hopOut * speed + 0.5f);
    // Too short to window: a clip this small is a click, not speech.
    if (window < 8 || hopIn < 1 ||
        static_cast<int>(in.size()) < window + search * 2) {
        return in;
    }

    // Hann window, built once.
    std::vector<float> hann(static_cast<size_t>(window));
    for (int i = 0; i < window; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(window - 1);
        hann[static_cast<size_t>(i)] =
            0.5f - 0.5f * std::cos(6.28318530718f * x);
    }

    const size_t outCap = in.size() * 2 + static_cast<size_t>(window) + 1;
    std::vector<float> acc(outCap, 0.0f);
    std::vector<float> norm(outCap, 0.0f);

    size_t outPos = 0;
    int readPos = 0;
    // The tail of what we have already written, used as the correlation
    // template for the next window's slide.
    while (readPos + window < static_cast<int>(in.size())) {
        int best = readPos;
        if (outPos > 0 && search > 0) {
            // Slide within +/-search for the offset whose head best matches
            // the overlap region already written. Plain sum-of-products; the
            // magnitudes are comparable because the region is fixed.
            float bestScore = -1e30f;
            const int lo = (readPos - search < 0) ? 0 : readPos - search;
            const int hi = (readPos + search + window <
                            static_cast<int>(in.size()))
                               ? readPos + search
                               : static_cast<int>(in.size()) - window - 1;
            for (int cand = lo; cand <= hi; ++cand) {
                float score = 0.0f;
                for (int i = 0; i < hopOut; i += 4) {   // stride: 4x cheaper,
                                                        // same peak in practice
                    const size_t o = outPos + static_cast<size_t>(i);
                    if (o >= acc.size()) break;
                    score += acc[o] * static_cast<float>(
                                          in[static_cast<size_t>(cand + i)]);
                }
                if (score > bestScore) {
                    bestScore = score;
                    best = cand;
                }
            }
        }

        for (int i = 0; i < window; ++i) {
            const size_t o = outPos + static_cast<size_t>(i);
            if (o >= acc.size()) break;
            const float w = hann[static_cast<size_t>(i)];
            acc[o] += w * static_cast<float>(in[static_cast<size_t>(best + i)]);
            norm[o] += w;
        }
        outPos += static_cast<size_t>(hopOut);
        readPos += hopIn;
    }

    const size_t outLen = outPos + static_cast<size_t>(hopOut);
    std::vector<int16_t> out;
    out.reserve(outLen);
    for (size_t i = 0; i < outLen && i < acc.size(); ++i) {
        // Normalise by the summed window so the 50%-overlap crossfades do
        // not double the level; a gap with no coverage stays silent.
        float v = norm[i] > 0.001f ? acc[i] / norm[i] : 0.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        out.push_back(static_cast<int16_t>(v));
    }
    return out;
}

}  // namespace SpotterStretch
