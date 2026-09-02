// ============================================================================
// core/fuel_estimate.h
// How much fuel a lap costs, and how many laps are left in the tank. Pure, so
// the unit suite drives it directly (test_fuel_estimate.cpp).
//
// WHY IT IS HERE RATHER THAN IN FuelWidget: the spotter wants the same number,
// and a HUD is the wrong place for a core singleton to read from. Kept pure, the
// first-lap rule below is testable, which it is not inside a render path.
//
// THE FIRST LAP IS SKIPPED, and that is the whole subtlety. Lap 1 includes
// sitting on the grid with the engine running, so its consumption is inflated
// and would drag the average up for the rest of the race — the tank looks
// emptier than it is, exactly when a warning would fire. It is only skipped
// while it is still IN the buffer: once enough laps have rolled through, the
// window no longer contains it and every sample counts.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace FuelEstimate {

// Keep the last N laps. Long enough to smooth a cautious lap or a fast one,
// short enough to follow a real change in pace.
inline constexpr size_t kMaxHistory = 10;

// Average fuel per lap over the history, or 0 when there is nothing to say.
// `totalLapsRecorded` is every lap ever seen, not the buffer size: the two are
// equal only until the window rolls over, which is exactly how the first lap
// is identified without storing a flag alongside it.
inline float averagePerLap(const std::vector<float>& perLap,
                           size_t totalLapsRecorded) {
    if (perLap.empty()) return 0.0f;
    const bool firstLapInBuffer = (totalLapsRecorded == perLap.size());
    // Lap 1 alone is not an estimate: it is the least trustworthy number in the
    // whole history, and not merely inflated by grid idling. A real session: the
    // bike sits at 7.20L on the pre-start screen and goes green with far less,
    // so lap 1 "consumes" 4.48L against a true 0.21L. That is a tank change, not
    // consumption; used alone it puts the estimate at 0.6 laps with a dozen in
    // the tank, and the low-fuel warning fires on it. Returning 0 here means
    // lapsRemaining() answers "cannot be known", which is the truth until a
    // second lap confirms a rate.
    if (firstLapInBuffer && perLap.size() == 1) return 0.0f;
    const size_t startIdx = firstLapInBuffer ? 1 : 0;
    float total = 0.0f;
    for (size_t i = startIdx; i < perLap.size(); ++i) total += perLap[i];
    const size_t count = perLap.size() - startIdx;
    return count > 0 ? total / static_cast<float>(count) : 0.0f;
}

// The two thresholds, shared so the HUD's colour and the spotter's warning
// fire at the same moment. A number turning amber on screen while the voice
// says nothing (or the reverse) reads as one of them being broken.
inline constexpr float kWarnLaps = 4.0f;
inline constexpr float kCriticalLaps = 2.0f;

// Laps left in the tank, or -1 when it cannot be known yet (no history, or a
// consumption too small to divide by). Clamped at 99.9 so a near-zero average
// on an out-lap cannot produce a number nobody would believe.
//
// isfinite-guarded on BOTH inputs, per the project invariant: `fuel` is raw
// game telemetry, and NaN slips every ordering comparison while +Inf slips
// `<=` — an infinite average would otherwise return a confident 0.0 ("empty
// tank", so fuel_critical fires) instead of the -1 "cannot be known".
inline float lapsRemaining(float fuel, float avgPerLap) {
    if (!std::isfinite(fuel) || !std::isfinite(avgPerLap)) return -1.0f;
    if (avgPerLap <= 0.001f || fuel < 0.0f) return -1.0f;
    return std::min(fuel / avgPerLap, 99.9f);
}

}  // namespace FuelEstimate
