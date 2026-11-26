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

// An average worth dividing by: under this, a consumption too small to say
// anything (an out-lap, a stand). Every reader shows "no average yet" below it.
inline constexpr float kMinAverage = 0.001f;
inline bool hasAverage(float avgPerLap) { return avgPerLap > kMinAverage; }

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
    if (!hasAverage(avgPerLap) || fuel < 0.0f) return -1.0f;
    return std::min(fuel / avgPerLap, 99.9f);
}

// THE PLAYER'S FUEL HISTORY, one per session (PluginData owns it), so every
// reader -- the Fuel widget, the Dash, the Timing HUD, the Fuel deck and the
// spotter -- reads the same laps. Fed by two events: each fuel sample of the
// player's own bike, and each lap the player completes.
class Tracker {
public:
    Tracker() { m_perLap.reserve(kMaxHistory + 1); }

    void reset() {
        m_perLap.clear();
        m_totalLaps = 0;
        m_atRunStart = 0.0f;
        m_atLapStart = 0.0f;
        m_lastLap = -1;
        m_active = false;
    }

    // A fuel sample. The first starts tracking (the fuel at the start); one
    // above the run's start is a refuel, which restarts the run's reference
    // and keeps the lap history.
    void sample(float fuel) {
        if (!m_active || fuel > m_atRunStart) {
            m_atRunStart = fuel;
            m_atLapStart = fuel;
            m_active = true;
        }
    }

    // A lap completed (its number, once), with the fuel now. Only a timed lap
    // counts (a pit-in or a cut one would skew the average), and only a
    // plausible use; either way the next lap measures from here. The fuel
    // used, or -1 when nothing was recorded.
    float lapCompleted(int lapNum, bool timed, float fuel, float maxFuel) {
        if (!m_active || lapNum <= m_lastLap) return -1.0f;
        float used = -1.0f;
        const float u = m_atLapStart - fuel;
        if (timed && u > 0.0f && u < maxFuel) {
            m_perLap.push_back(u);
            ++m_totalLaps;
            if (m_perLap.size() > kMaxHistory) m_perLap.erase(m_perLap.begin());
            used = u;
        }
        m_atLapStart = fuel;
        m_lastLap = lapNum;
        return used;
    }

    float averagePerLap() const { return FuelEstimate::averagePerLap(m_perLap, m_totalLaps); }
    float lapsRemaining(float fuel) const { return FuelEstimate::lapsRemaining(fuel, averagePerLap()); }
    // The fuel used this run (below 0 after a refuel), once there is a start
    bool hasFuelUsed() const { return m_active && m_atRunStart > 0.0f; }
    float fuelUsed(float fuel) const { return m_atRunStart - fuel; }

private:
    std::vector<float> m_perLap;   // fuel used per lap, newest last
    size_t m_totalLaps = 0;        // every lap recorded (averagePerLap's first-lap rule)
    float m_atRunStart = 0.0f;
    float m_atLapStart = 0.0f;
    int m_lastLap = -1;
    bool m_active = false;
};

}  // namespace FuelEstimate
