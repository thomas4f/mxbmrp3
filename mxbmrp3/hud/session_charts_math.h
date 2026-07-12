// ============================================================================
// hud/session_charts_math.h
// Pure derivation math for the Session Charts HUD.
//
// The four race-progression charts (lap/position, race trace, gap-to-leader,
// pace) are all views of one primary input: each rider's per-lap lap time.
// Everything else — cumulative time, per-lap position, gap to leader, the
// reference pace for the trace chart — is derived from it here.
//
// This header is intentionally DEPENDENCY-FREE (only the C++ standard library):
// no PluginData, no game API, no Windows. That keeps the derivation logic
// unit-testable headlessly (see tests/unit/test_session_charts_math.cpp) and keeps
// the HUD's rebuildRenderData() a thin adapter that feeds collected lap times
// in and renders the results.
//
// Time is in milliseconds throughout. Lap-time vectors are OLDEST-FIRST and
// contain only completed, positive laps (the HUD reverses the newest-first
// deque and filters before calling in).
// ============================================================================
#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace SessionChartsMath {

// A rider's ordered completed lap times (oldest-first, ms).
struct RiderSeries {
    int raceNum = 0;
    std::vector<int> lapMs;
};

// Running sum of lap times. result[l] = total time after lap index l (0-based),
// i.e. the rider's cumulative race time once lap (l+1) is complete.
inline std::vector<long long> cumulative(const std::vector<int>& lapMs) {
    std::vector<long long> out;
    out.reserve(lapMs.size());
    long long sum = 0;
    for (int t : lapMs) {
        sum += t;
        out.push_back(sum);
    }
    return out;
}

// Running best (minimum) lap time up to and including each lap. This is the
// non-race analogue of cumulative time: ranking riders by best-lap-so-far gives
// the provisional qualifying/practice order, and the spread gives each rider's
// gap to the session-best lap. Feed the result into positionsPerLap() /
// gapToLeaderPerLap() exactly like cumulative() is used for a race.
inline std::vector<long long> bestLapSoFar(const std::vector<int>& lapMs) {
    std::vector<long long> out;
    out.reserve(lapMs.size());
    long long best = 0;
    bool have = false;
    for (int t : lapMs) {
        if (!have || t < best) { best = t; have = true; }
        out.push_back(best);
    }
    return out;
}

// A rider that has completed laps but none of them VALID ranks last until they set
// a clean lap. This sentinel is large (ranks last in positionsPerLap) yet bounded
// (won't overflow when a gap subtracts it) and clips on a robust axis.
constexpr long long kNoValidLap = 1LL << 40;

// Validity-aware bestLapSoFar: an INVALID lap (cut track, jump-start, penalised)
// must NOT set a provisional qualifying/practice best, so it doesn't update the
// running minimum — but the series still has one entry per completed lap so it
// lines up with cumulative() for positionsPerLap()/gapToLeaderPerLap(). Before the
// rider's first valid lap the value is kNoValidLap (ranks last). `valid[i]` is the
// validity of lap i (any missing/short entry is treated as valid).
inline std::vector<long long> bestLapSoFar(const std::vector<int>& lapMs,
                                           const std::vector<char>& valid) {
    std::vector<long long> out;
    out.reserve(lapMs.size());
    long long best = kNoValidLap;
    for (size_t i = 0; i < lapMs.size(); ++i) {
        bool isValid = (i >= valid.size()) || valid[i];
        if (isValid && lapMs[i] < best) best = lapMs[i];
        out.push_back(best);
    }
    return out;
}

// Reference pace for the race-trace chart: an "imaginary rider" holding a fixed
// lap time equal to the leader's average (leader total time / leader lap count).
// Guards divide-by-zero (returns 0 when there are no laps).
inline long long referencePaceMs(long long leaderTotalMs, int leaderLaps) {
    if (leaderLaps <= 0) return 0;
    return leaderTotalMs / leaderLaps;
}

// Race-trace Y value (ms): how far ahead of the reference pace a rider is after
// completing `lapCount` laps. Positive = ahead of the imaginary reference rider
// (plotted higher); negative = behind. Zero when the rider exactly matches the
// reference pace, which is why the leader hugs the dashed zero line.
inline long long traceValueMs(long long refPaceMs, int lapCount, long long cumulativeMs) {
    return refPaceMs * static_cast<long long>(lapCount) - cumulativeMs;
}

// Index of the leader among the given per-rider cumulative vectors, or -1 if
// there is no data. The leader is the rider who has completed the most laps and,
// among those, has the lowest cumulative time at that lap (tie-break: lowest
// raceNum for determinism). This matches "current leader" live and the winner
// post-race.
inline int leaderIndex(const std::vector<std::vector<long long>>& cumulatives,
                       const std::vector<int>& raceNums) {
    int best = -1;
    size_t bestLaps = 0;
    long long bestCum = 0;
    for (size_t i = 0; i < cumulatives.size(); ++i) {
        size_t laps = cumulatives[i].size();
        if (laps == 0) continue;
        long long cum = cumulatives[i].back();
        bool take = false;
        if (best < 0) {
            take = true;
        } else if (laps != bestLaps) {
            take = laps > bestLaps;
        } else if (cum != bestCum) {
            take = cum < bestCum;
        } else {
            take = raceNums[i] < raceNums[best];
        }
        if (take) {
            best = static_cast<int>(i);
            bestLaps = laps;
            bestCum = cum;
        }
    }
    return best;
}

// Per-rider, per-lap track position (1 = leader). positions[i][l] is the 1-based
// rank of rider i at lap index l, computed by ranking every rider that has
// completed lap l by their cumulative time (tie-break: lower raceNum). A rider
// that has not completed lap l gets position 0 for that lap (no data — the HUD
// simply doesn't plot a point there). Output rows match the input order/length.
inline std::vector<std::vector<int>> positionsPerLap(
        const std::vector<std::vector<long long>>& cumulatives,
        const std::vector<int>& raceNums) {
    const size_t n = cumulatives.size();
    std::vector<std::vector<int>> positions(n);
    size_t maxLaps = 0;
    for (const auto& c : cumulatives) maxLaps = std::max(maxLaps, c.size());
    for (size_t i = 0; i < n; ++i) positions[i].assign(cumulatives[i].size(), 0);

    for (size_t lap = 0; lap < maxLaps; ++lap) {
        // Collect riders present at this lap.
        std::vector<size_t> present;
        present.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (lap < cumulatives[i].size()) present.push_back(i);
        }
        std::sort(present.begin(), present.end(), [&](size_t a, size_t b) {
            if (cumulatives[a][lap] != cumulatives[b][lap])
                return cumulatives[a][lap] < cumulatives[b][lap];
            return raceNums[a] < raceNums[b];
        });
        for (size_t rank = 0; rank < present.size(); ++rank) {
            positions[present[rank]][lap] = static_cast<int>(rank) + 1;
        }
    }
    return positions;
}

// Per-rider, per-lap gap to the leader (ms). gaps[i][l] = rider i's cumulative
// time at lap l minus the smallest cumulative time among all riders present at
// lap l (so the leader of each lap is pinned to 0). Riders absent at a lap keep
// no entry there (row length matches that rider's completed-lap count).
inline std::vector<std::vector<long long>> gapToLeaderPerLap(
        const std::vector<std::vector<long long>>& cumulatives) {
    const size_t n = cumulatives.size();
    std::vector<std::vector<long long>> gaps(n);
    size_t maxLaps = 0;
    for (const auto& c : cumulatives) maxLaps = std::max(maxLaps, c.size());
    for (size_t i = 0; i < n; ++i) gaps[i].assign(cumulatives[i].size(), 0);

    for (size_t lap = 0; lap < maxLaps; ++lap) {
        long long leaderCum = 0;
        bool haveLeader = false;
        for (size_t i = 0; i < n; ++i) {
            if (lap < cumulatives[i].size()) {
                if (!haveLeader || cumulatives[i][lap] < leaderCum) {
                    leaderCum = cumulatives[i][lap];
                    haveLeader = true;
                }
            }
        }
        if (!haveLeader) continue;
        for (size_t i = 0; i < n; ++i) {
            if (lap < cumulatives[i].size()) {
                gaps[i][lap] = cumulatives[i][lap] - leaderCum;
            }
        }
    }
    return gaps;
}

// Median lap time (ms) of a sample, used as the baseline for outlier filtering
// on the pace chart. Returns 0 for an empty sample. Averages the two middle
// values for an even count.
inline int medianMs(std::vector<int> laps) {
    if (laps.empty()) return 0;
    std::sort(laps.begin(), laps.end());
    size_t mid = laps.size() / 2;
    if (laps.size() % 2 == 1) return laps[mid];
    return static_cast<int>((static_cast<long long>(laps[mid - 1]) + laps[mid]) / 2);
}

// Whether a lap should be excluded from the pace chart's racing-pace band.
// The opening lap (index 0) is always an outlier (standing start), as is any lap
// slower than median * factor (e.g. a crash, an off, or a neutralised lap).
// A non-positive median disables the ratio test (only the opening lap filters).
inline bool isOutlierLap(int lapIndex0Based, int lapTimeMs, int median, float factor) {
    if (lapIndex0Based <= 0) return true;
    if (median <= 0) return false;
    return static_cast<float>(lapTimeMs) > static_cast<float>(median) * factor;
}

// Linear-interpolated percentile of an ALREADY-SORTED sample (p in [0,1]).
// Returns 0 for an empty sample. Used by robustRange().
inline long long percentileSorted(const std::vector<long long>& sorted, double p) {
    if (sorted.empty()) return 0;
    if (sorted.size() == 1) return sorted.front();
    if (p <= 0.0) return sorted.front();
    if (p >= 1.0) return sorted.back();
    double pos = p * static_cast<double>(sorted.size() - 1);
    size_t lo = static_cast<size_t>(pos);
    size_t hi = std::min(lo + 1, sorted.size() - 1);
    double frac = pos - static_cast<double>(lo);
    return sorted[lo] + static_cast<long long>((sorted[hi] - sorted[lo]) * frac);
}

// A robust [lo, hi] value range for a chart's Y-axis. A single catastrophic
// outlier (a rider who crashed and lost minutes) must not stretch the axis and
// crush the readable pack into a sliver — but the outlier's line should still be
// drawn (the caller clips it to the chart edge). Uses Tukey fences: sort, take
// Q1/Q3, fence = [Q1 - k*IQR, Q3 + k*IQR], then INTERSECT with the actual data
// extent so the range never widens past real values.
//
// Key property: on well-behaved data the fence lies outside the data, so the
// result equals the plain min/max — no clipping happens unless there is a genuine
// outlier beyond the fence. Falls back to plain min/max for tiny samples (<4) or a
// degenerate IQR. `valid` is false only for an empty sample.
struct AxisRange {
    long long lo = 0;
    long long hi = 0;
    bool valid = false;
};

// Format a millisecond value as a compact seconds label for a chart axis, e.g.
// "12.3s", or "1:23.4" for magnitudes >= 60s. `showSign` prefixes '+'/'-' (used by
// the trace chart, where the sign shows ahead-of / behind-pace). Rounds to tenths of
// a second BEFORE splitting into minutes, so a value like 59.96s prints "1:00.0" and
// never "0:60.0" (the rounded tenths must carry into the minute, not overflow the
// seconds field). Mirrored by fmtChartSecs() in the web overlay (overlay-charts.js) — keep the
// two in step (same integer rounding, same field widths). Pure + stdlib-only, so it
// lives here with the derivations and is unit-tested headlessly.
inline void formatSecs(char* buf, size_t n, long long ms, bool showSign) {
    const char* sign = "";
    if (showSign) sign = (ms > 0) ? "+" : (ms < 0 ? "-" : "");
    long long am = (ms < 0) ? -ms : ms;
    long long tenths = (am + 50) / 100;            // round ms to tenths of a second
    long long whole = tenths / 10, frac = tenths % 10;
    if (whole >= 60) {
        snprintf(buf, n, "%s%lld:%02lld.%lld", sign, whole / 60, whole % 60, frac);
    } else {
        snprintf(buf, n, "%s%lld.%llds", sign, whole, frac);
    }
}

inline AxisRange robustRange(std::vector<long long> vals, double k = 1.5) {
    AxisRange r;
    if (vals.empty()) return r;
    std::sort(vals.begin(), vals.end());
    const long long dataLo = vals.front();
    const long long dataHi = vals.back();
    if (vals.size() < 4) { return AxisRange{ dataLo, dataHi, true }; }
    const long long q1 = percentileSorted(vals, 0.25);
    const long long q3 = percentileSorted(vals, 0.75);
    const long long iqr = q3 - q1;
    if (iqr <= 0) { return AxisRange{ dataLo, dataHi, true }; }
    const long long fenceLo = q1 - static_cast<long long>(k * static_cast<double>(iqr));
    const long long fenceHi = q3 + static_cast<long long>(k * static_cast<double>(iqr));
    r.lo = std::max(dataLo, fenceLo);   // intersect: never widen past real data
    r.hi = std::min(dataHi, fenceHi);
    r.valid = true;
    return r;
}

} // namespace SessionChartsMath
