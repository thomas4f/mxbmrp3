// ============================================================================
// core/spotter_pace.h
// The spotter's pace reports: gap to the rider ahead/behind at timing points,
// with a gaining/losing trend. Pure (all inputs injected, no PluginData) so
// the unit suite drives every edge (test_spotter_pace.cpp); SpotterManager
// resolves who is ahead/behind and feeds crossings from the lap/split taps.
//
// HOW A GAP IS MEASURED HONESTLY. A gap only exists where both riders have
// crossed the SAME timing point (a split line or start/finish) on the SAME
// lap: it is the difference of the times at which each of them reached it.
//   - AHEAD at any of your crossings: the rider ahead passed this same point
//     earlier, so the gap (and its trend vs your previous gap to them) speaks
//     immediately, chained straight after the "P three" position report. Their
//     crossings are kept as a short RING, not a single slot — a rider more
//     than a sector up the road has already banked the next split by the time
//     you reach theirs, which is what used to make the trend almost never fire.
//   - BEHIND is NOT known at your crossing — they haven't reached the line
//     yet. The report is ARMED at your crossing and RESOLVED when that rider
//     next reaches a timing point you have also crossed (their next split
//     with RaceSplit data, else their S/F). Deliberately delayed: quoting a
//     live-extrapolated number would speak a guess; this speaks a stopwatch.
//
// WHICH CLOCK, precisely, because "their crossing times" reads like the game's
// and is not. The caller stamps each crossing with PluginData's session clock
// as the callback ARRIVES (getSessionElapsedTime, refreshed from the
// RaceTrackPosition batch and from RaceClassification, so ~30Hz). It is not
// the game's own splitTimeMs, which the callback also carries: that figure is
// elapsed-since-LAP-START, so subtracting two riders' split times compares
// their pace to that point and says nothing about the distance between them
// unless their laps began together, which in a race they never do. So
// splitTimeMs is spoken (the sector cue reads it back) and never differenced.
//
// The quantisation that buys is ~33ms, against a kMinGapMs of 100 and a
// kTrendMinDeltaMs of 300 — and it largely cancels, since both riders are
// stamped off the same clock at the same rate.
//
// WHY NOT THE LIVE GAP, which the standings already compute (realTimeGap, via
// LiveGap). Not because that one is "plugin-derived" and this is not — both
// are; the difference is the BASIS. LiveGap interpolates against a leader
// baseline sampled every 1% of a lap, so it answers continuously and is what
// a moving on-screen gap wants. This stopwatch fires only at real split lines
// and answers nothing in between, which is what a VOICE wants: a spotter
// speaks at the line, a trend needs two discrete points to compare, and the
// behind-report's arm-then-resolve has no equivalent in an interpolation. The
// interpolation is also the one that went visibly wrong in a logged race —
// 91.0s against an official 3.2s on the same crossing — so where a number is
// going to be read out as a fact, this is the one to read.
//
// Trend ("gaining"/"losing", "closing"/"dropping") compares against the
// previous resolved gap to the SAME rider; a change under kTrendMinDeltaMs
// (or a different neighbor) reports no trend. Gaps beyond kMaxGapMs are not
// worth airtime and report nothing.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace SpotterPace {

// A timing point identity: lap * 8 + point index, where index 0..6 are split
// lines and 7 is the start/finish that COMPLETES the lap. Unique and ordered
// within a session.
inline long long pointKey(int lap, int pointIdx) {
    return static_cast<long long>(lap) * 8 + pointIdx;
}
constexpr int kSfPoint = 7;

struct Gap {
    int gapMs = 0;
    int deltaMs = 0;       // vs the previous gap to the same rider
    bool hasTrend = false;
    // WHO it was measured against. A cached gap outlives the crossing that
    // produced it (the ambient variables serve it between timing points), and
    // the rider ahead changes constantly — so a reading is only worth
    // repeating while it still describes the rider it was taken for.
    int raceNum = -1;
};

class Tracker {
public:
    static constexpr int kMaxGapMs = 30000;      // beyond this: say nothing
    static constexpr int kMinGapMs = 100;        // below clock resolution:
                                                 // "zero point zero" is not a
                                                 // number, and the proximity
                                                 // cues own "right on you"
    static constexpr int kTrendMinDeltaMs = 300; // smaller change: no trend

    // Record the FOCUSED rider's own crossing of any timing point.
    void myPoint(long long key, int tMs) {
        m_mine[m_mineHead] = { key, tMs };
        m_mineHead = (m_mineHead + 1) % kRing;
    }

    // Record ANY OTHER rider's crossing of a timing point. Kept as a short
    // per-rider RING, not one slot, and that is the whole difference between
    // hearing a trend and never hearing one.
    //
    // With a single slot the ahead-gap only resolved when the rider ahead's
    // MOST RECENT crossing was the point you are crossing now — which fails
    // exactly when they are ahead by more than a sector, because by then they
    // have already banked the next split. A real four-lap race resolved it on
    // one crossing out of three; the other two fell back to the standings
    // estimate, so {gained_on_ahead} and {trend_ahead} never spoke at all.
    // Their crossing of your point is still in the ring a sector later.
    void otherPoint(int raceNum, long long key, int tMs) {
        for (Ring& r : m_others) {
            if (r.raceNum == raceNum) { r.push(key, tMs); return; }
        }
        m_others.push_back(Ring{raceNum, {}, 0});
        m_others.back().push(key, tMs);
    }

    // At any of the focused rider's timing points: the gap to the rider ahead,
    // if that rider has also crossed this exact point (line + lap) and it is
    // still in their ring. aheadNum identifies the neighbor so a changed
    // neighbor drops the trend rather than comparing gaps to different riders.
    bool aheadGap(int aheadNum, long long myKey, int myT, Gap& out) {
        const int aheadT = lookupPoint(aheadNum, myKey);
        if (aheadT < 0) return false;   // not the same line+lap: no gap
        const int gap = myT - aheadT;
        const int prev = lookupGap(m_lastAhead, aheadNum);
        if (!plausibleGap(gap)) { forgetGap(m_lastAhead, aheadNum); return false; }
        storeGap(m_lastAhead, aheadNum, gap);
        if (gap < kMinGapMs) return false;
        out.gapMs = gap;
        out.raceNum = aheadNum;
        out.hasTrend = prev >= 0 && absInt(gap - prev) >= kTrendMinDeltaMs;
        out.deltaMs = prev >= 0 ? gap - prev : 0;
        return true;
    }

    // Arm the behind report at the focused rider's crossing.
    void armBehind(int behindNum) { m_pendingBehindNum = behindNum; }
    int pendingBehind() const { return m_pendingBehindNum; }

    // The pending rider crossed a timing point: resolves if the focused
    // rider crossed that same point (it is in the ring). One report per arm.
    bool behindPoint(int raceNum, long long key, int tMs, Gap& out) {
        if (raceNum != m_pendingBehindNum) return false;
        int myT = -1;
        for (const Pt& p : m_mine) {
            if (p.key == key) { myT = p.tMs; break; }
        }
        if (myT < 0) return false;   // haven't crossed it myself (yet)
        m_pendingBehindNum = -1;     // resolved (or dead) either way below
        const int gap = tMs - myT;
        const int prev = lookupGap(m_lastBehind, raceNum);
        if (!plausibleGap(gap)) { forgetGap(m_lastBehind, raceNum); return false; }
        storeGap(m_lastBehind, raceNum, gap);
        if (gap < kMinGapMs) return false;
        out.gapMs = gap;
        out.raceNum = raceNum;
        out.hasTrend = prev >= 0 && absInt(gap - prev) >= kTrendMinDeltaMs;
        out.deltaMs = prev >= 0 ? gap - prev : 0;
        return true;
    }

    void reset() {
        for (Pt& p : m_mine) p = {};
        m_mineHead = 0;
        m_others.clear();
        m_lastAhead.clear();
        m_lastBehind.clear();
        m_pendingBehindNum = -1;
    }

private:
    static int absInt(int v) { return v < 0 ? -v : v; }

    // Is this measurement a rider-to-rider gap at all, and therefore fit to be
    // the baseline the NEXT one is compared against?
    //
    // A gap below kMinGapMs is: too small to say, but real, and it must move
    // the baseline or a delta spans more than one crossing. The two that are
    // not: a NEGATIVE gap means the rider "ahead" reached the point after you,
    // so they are not ahead on the road at all; and one past kMaxGapMs means
    // you are not racing them. Storing either poisoned the next real reading —
    // a logged race announced "Sector one, thirty two point five, NINETY FIVE
    // POINT ZERO gaining on rider nine oh one" one crossing after a nine-second
    // gap to that same rider, which is a delta no sector can contain.
    //
    // Forgetting rather than keeping the old value: a rider who was a hundred
    // seconds away and is now beside you is a rider you have not been tracking,
    // so the next resolution is a first sighting and reports no trend.
    static bool plausibleGap(int gap) { return gap >= 0 && gap <= kMaxGapMs; }

    // Two laps of a four-point lap (three splits + start/finish), which is far
    // more than the rider ahead can bank while you cover one sector.
    static constexpr int kRing = 8;
    struct Pt { long long key = -1; int tMs = -1; };
    Pt m_mine[kRing];
    int m_mineHead = 0;

    // Every OTHER rider's recent crossings, one ring each. Session-scoped and
    // cleared by reset(); a departed race number's ring is harmless, since a
    // reused number's gap is measured from crossings it makes itself (and a
    // stale key can only match the same lap and line).
    struct Ring {
        int raceNum;
        Pt pts[kRing];
        int head;
        void push(long long key, int tMs) {
            pts[head] = { key, tMs };
            head = (head + 1) % kRing;
        }
    };
    std::vector<Ring> m_others;

    int lookupPoint(int raceNum, long long key) const {
        for (const Ring& r : m_others) {
            if (r.raceNum != raceNum) continue;
            for (const Pt& p : r.pts) if (p.key == key) return p.tMs;
            return -1;
        }
        return -1;
    }
    // The last resolved gap PER RIDER, not one slot per side.
    //
    // One slot meant the trend only existed when the SAME rider was your
    // neighbour two crossings running — and in a real race that almost never
    // holds: a logged five-lap race had the rider ahead change on every single
    // lap (#8, #32, #247, #32), so {trend_ahead} and {gained_on_ahead} were
    // empty the whole way. Keyed by rider, a gap to somebody you are actually
    // racing survives another rider shuffling between you.
    //
    // Small and session-scoped: reset() clears it, and it holds at most one
    // entry per rider who has been your neighbour. A stale entry for a
    // departed rider is harmless — a reused race number would have to become
    // your neighbour again before it could be read, and the gap it produces is
    // recomputed from live crossings either way.
    struct LastGap { int raceNum; int gapMs; };
    std::vector<LastGap> m_lastAhead, m_lastBehind;

    // -1 = never measured, or measured and forgotten (see forgetGap): both
    // mean "no baseline", which is what a first sighting is.
    static int lookupGap(const std::vector<LastGap>& v, int raceNum) {
        for (const LastGap& g : v) if (g.raceNum == raceNum) return g.gapMs;
        return -1;
    }
    // Drop a rider's baseline without dropping the rider: -1 reads back as
    // "never measured", which is what a first sighting needs.
    static void forgetGap(std::vector<LastGap>& v, int raceNum) {
        for (LastGap& g : v) {
            if (g.raceNum == raceNum) { g.gapMs = -1; return; }
        }
    }
    static void storeGap(std::vector<LastGap>& v, int raceNum, int gapMs) {
        for (LastGap& g : v) if (g.raceNum == raceNum) { g.gapMs = gapMs; return; }
        v.push_back({raceNum, gapMs});
    }
    int m_pendingBehindNum = -1;
};

}  // namespace SpotterPace
