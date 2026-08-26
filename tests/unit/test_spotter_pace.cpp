// ============================================================================
// tests/unit/test_spotter_pace.cpp
// Pins the pace-report tracker (core/spotter_pace.h) — honest gaps only:
//  - a gap exists only where BOTH riders crossed the SAME timing point on
//    the same lap; anything else reports nothing, never a guess;
//  - the behind report is armed at the focused rider's crossing and stays
//    pending until the behind rider reaches a point the focused rider has
//    also crossed (their next split with data, else their S/F) — one report
//    per arm;
//  - trend compares gaps to the SAME neighbor and stays silent for changes
//    under the threshold or when the neighbor changed (comparing gaps to
//    two different riders would call a lie a trend);
//  - gaps beyond the cap are not worth airtime.
// ============================================================================
#include "doctest.h"

#include "core/spotter_pace.h"

using namespace SpotterPace;

TEST_CASE("ahead: same-point gap, trend vs same neighbor only") {
    Tracker t;
    Gap g;
    const long long lap1 = pointKey(1, kSfPoint);
    const long long lap2 = pointKey(2, kSfPoint);

    // Rider ahead (#56) crossed lap 1 S/F at 90.0s; I cross at 92.1s.
    t.otherPoint(56, lap1, 90000);
    CHECK(t.aheadGap(56, lap1, 92100, g));
    CHECK(g.gapMs == 2100);
    CHECK_FALSE(g.hasTrend);   // first measurement: no trend yet

    // Next lap: gap shrank to 1.4s — gaining, delta negative.
    t.otherPoint(56, lap2, 117600);
    CHECK(t.aheadGap(56, lap2, 119000, g));
    CHECK(g.gapMs == 1400);
    CHECK(g.hasTrend);
    CHECK(g.deltaMs == -700);

    // Lap 3: different rider ahead — no trend against #56's gaps.
    const long long lap3 = pointKey(3, kSfPoint);
    t.otherPoint(99, lap3, 145000);
    CHECK(t.aheadGap(99, lap3, 146000, g));
    CHECK_FALSE(g.hasTrend);

    // A sub-threshold change is not a trend worth calling.
    const long long lap4 = pointKey(4, kSfPoint);
    t.otherPoint(99, lap4, 171900);
    CHECK(t.aheadGap(99, lap4, 173000, g));
    CHECK(g.gapMs == 1100);
    CHECK_FALSE(g.hasTrend);   // 100ms drift < 300ms threshold
}

TEST_CASE("ahead: mismatched point or absurd gap reports nothing") {
    Tracker t;
    Gap g;
    // They have only crossed a lap I have not reached: not the same line+lap.
    t.otherPoint(56, pointKey(1, kSfPoint), 90000);
    CHECK_FALSE(t.aheadGap(56, pointKey(2, kSfPoint), 119000, g));
    // Same point but past the cap: a 35s "gap to the rider ahead" is noise.
    t.otherPoint(56, pointKey(2, kSfPoint), 90000);
    CHECK_FALSE(t.aheadGap(56, pointKey(2, kSfPoint), 125000, g));
    // Below the clock's resolution: "zero point zero" is not a number.
    t.otherPoint(56, pointKey(3, kSfPoint), 145950);
    CHECK_FALSE(t.aheadGap(56, pointKey(3, kSfPoint), 146000, g));
    // A rider we have never seen cross anything.
    CHECK_FALSE(t.aheadGap(77, pointKey(3, kSfPoint), 146000, g));
}

TEST_CASE("behind: armed at my crossing, resolves at their next shared point") {
    Tracker t;
    Gap g;
    // My lap-2 crossing: record my point, arm the rider behind (#90).
    t.myPoint(pointKey(2, kSfPoint), 119000);
    t.armBehind(90);
    CHECK(t.pendingBehind() == 90);

    // Another rider's crossing does not resolve it.
    CHECK_FALSE(t.behindPoint(56, pointKey(2, kSfPoint), 120000, g));
    CHECK(t.pendingBehind() == 90);

    // #90 reaches a point I have NOT crossed (their split of the next lap
    // before I logged mine): still pending, no guess.
    CHECK_FALSE(t.behindPoint(90, pointKey(3, 0), 121000, g));
    CHECK(t.pendingBehind() == 90);

    // #90 crosses MY lap-2 S/F 2.8s after me: the report, exactly once.
    CHECK(t.behindPoint(90, pointKey(2, kSfPoint), 121800, g));
    CHECK(g.gapMs == 2800);
    CHECK_FALSE(g.hasTrend);
    CHECK(t.pendingBehind() == -1);
    CHECK_FALSE(t.behindPoint(90, pointKey(2, kSfPoint), 121800, g));

    // Next lap: the same rider closes to 1.9s — "closing" (negative delta).
    t.myPoint(pointKey(3, kSfPoint), 146000);
    t.armBehind(90);
    CHECK(t.behindPoint(90, pointKey(3, kSfPoint), 147900, g));
    CHECK(g.gapMs == 1900);
    CHECK(g.hasTrend);
    CHECK(g.deltaMs == -900);
}

TEST_CASE("behind: splits resolve earlier than the full lap") {
    Tracker t;
    Gap g;
    // I cross split 1 of lap 3, then S/F is still ahead of me; the pending
    // rider reaches that split next — the gap resolves there, laps sooner
    // than waiting for their S/F.
    t.myPoint(pointKey(3, 0), 130000);
    t.armBehind(56);
    CHECK(t.behindPoint(56, pointKey(3, 0), 131500, g));
    CHECK(g.gapMs == 1500);
}

TEST_CASE("reset wipes crossings, trends and the pending arm") {
    Tracker t;
    Gap g;
    t.myPoint(pointKey(2, kSfPoint), 119000);
    t.otherPoint(56, pointKey(2, kSfPoint), 117000);
    t.armBehind(90);
    t.reset();
    CHECK(t.pendingBehind() == -1);
    CHECK_FALSE(t.behindPoint(90, pointKey(2, kSfPoint), 121800, g));
    CHECK_FALSE(t.aheadGap(56, pointKey(2, kSfPoint), 119000, g));
}

// The rider ahead is normally MORE than a sector up the road, so by the time
// you reach a point they have already banked the next one — and with a single
// stored crossing per rider that is a miss, every time. A real four-lap race
// resolved the ahead gap on one crossing out of three, which is why the trend
// and the delta were never heard: they need two resolutions to the same rider.
TEST_CASE("ahead: their older crossings still resolve a gap") {
    Tracker t;
    Gap g;
    // #32 crosses S1, S2 and S/F of lap 1 while I am still working through the
    // lap; their newest crossing is nowhere near the point I reach next.
    t.otherPoint(32, pointKey(1, 0), 30000);
    t.otherPoint(32, pointKey(1, 1), 60000);
    t.otherPoint(32, pointKey(1, kSfPoint), 90000);

    // I reach S1: their S1 is three crossings old and still exactly the right
    // stopwatch reading.
    REQUIRE(t.aheadGap(32, pointKey(1, 0), 32000, g));
    CHECK(g.gapMs == 2000);
    CHECK_FALSE(g.hasTrend);

    // And at S2 the trend against the same rider finally has something to
    // compare with — the whole point of the exercise.
    REQUIRE(t.aheadGap(32, pointKey(1, 1), 63000, g));
    CHECK(g.gapMs == 3000);
    CHECK(g.hasTrend);
    CHECK(g.deltaMs == 1000);   // a second lost in one sector

    // The ring is finite: crossings older than kRing entries are gone rather
    // than growing without bound.
    for (int i = 0; i < 8; ++i) t.otherPoint(32, pointKey(2, i % 8), 100000 + i);
    CHECK_FALSE(t.aheadGap(32, pointKey(1, kSfPoint), 92000, g));
}

// The trend used to live in ONE slot per side, so it only existed when the
// same rider was your neighbour two crossings running. A logged five-lap race
// had the rider ahead change on every single lap — #8, #32, #247, #32 — so
// {trend_ahead} and {gained_on_ahead} were empty from flag to flag, which is
// the whole reason nobody had ever heard them.
TEST_CASE("pace: a gap trend survives another rider shuffling between you") {
    using namespace SpotterPace;
    Tracker t;
    Gap g;
    const long long k1 = pointKey(1, kSfPoint);
    const long long k2 = pointKey(2, kSfPoint);
    const long long k3 = pointKey(3, kSfPoint);

    // Lap 1: #32 is ahead by 2.0s. First sighting, so no trend yet.
    t.myPoint(k1, 100000);
    t.otherPoint(32, k1, 98000);
    REQUIRE(t.aheadGap(32, k1, 100000, g));
    CHECK_FALSE(g.hasTrend);

    // Lap 2: #247 has shuffled in between. Different rider, no trend of its
    // own — and crucially this must not DESTROY what we know about #32.
    t.myPoint(k2, 200000);
    t.otherPoint(247, k2, 199000);
    REQUIRE(t.aheadGap(247, k2, 200000, g));
    CHECK_FALSE(g.hasTrend);

    // Lap 3: #32 is ahead again, now 3.5s up. That is a real 1.5s lost to a
    // rider we are actually racing, and it is reported.
    t.myPoint(k3, 300000);
    t.otherPoint(32, k3, 296500);
    REQUIRE(t.aheadGap(32, k3, 300000, g));
    CHECK(g.hasTrend);
    CHECK(g.deltaMs == 1500);   // positive = the gap grew = losing ground
}

// The delta means "since the last shared timing point", so the baseline has to
// be the last point — not the last one that happened to be inside the range
// worth speaking. A measurement outside it returned before storing, so the
// next in-range gap silently compared against something older than one
// crossing and reported a change smaller than the one that happened.
TEST_CASE("ahead: an unspoken gap still moves the trend baseline") {
    Tracker t;
    Gap g;
    // Lap 1: half a second back, spoken and stored.
    t.otherPoint(32, pointKey(1, kSfPoint), 100000);
    REQUIRE(t.aheadGap(32, pointKey(1, kSfPoint), 100500, g));
    CHECK(g.gapMs == 500);

    // Lap 2: right on their wheel — below the floor, so nothing is said. The
    // proximity cues own that moment; the stopwatch still saw it.
    t.otherPoint(32, pointKey(2, kSfPoint), 200000);
    CHECK_FALSE(t.aheadGap(32, pointKey(2, kSfPoint), 200050, g));

    // Lap 3: nine tenths back. That is 0.85s lost since the last point, not
    // the 0.4s the stale half-second baseline would have reported.
    t.otherPoint(32, pointKey(3, kSfPoint), 300000);
    REQUIRE(t.aheadGap(32, pointKey(3, kSfPoint), 300900, g));
    CHECK(g.gapMs == 900);
    CHECK(g.hasTrend);
    CHECK(g.deltaMs == 850);
}

// ...but only a measurement that IS a rider-to-rider gap may become the
// baseline. Storing every one of them, which is how the case above was first
// written, meant an unspeakable reading poisoned the next speakable one: a
// logged race announced "Sector one, thirty two point five, NINETY FIVE POINT
// ZERO gaining on rider nine oh one" one crossing after a nine-second gap to
// that same rider — a delta no sector can contain.
TEST_CASE("ahead: an implausible gap forgets the baseline, it does not become one") {
    Tracker t;
    Gap g;
    // A rider a hundred seconds up the road. Past the cap, so nothing is said
    // — and nothing is remembered either, because that is not a rival you are
    // racing.
    t.otherPoint(901, pointKey(1, kSfPoint), 100000);
    CHECK_FALSE(t.aheadGap(901, pointKey(1, kSfPoint), 200000, g));

    // The field shuffles and they are now nine seconds ahead. FIRST sighting:
    // a real gap, and no trend, rather than "ninety one seconds gaining".
    t.otherPoint(901, pointKey(2, kSfPoint), 291000);
    REQUIRE(t.aheadGap(901, pointKey(2, kSfPoint), 300000, g));
    CHECK(g.gapMs == 9000);
    CHECK_FALSE(g.hasTrend);

    // And from there the trend works normally again.
    t.otherPoint(901, pointKey(3, kSfPoint), 393000);
    REQUIRE(t.aheadGap(901, pointKey(3, kSfPoint), 400000, g));
    CHECK(g.gapMs == 7000);
    CHECK(g.hasTrend);
    CHECK(g.deltaMs == -2000);

    // A NEGATIVE gap is the other implausible one: the rider "ahead" reached
    // the point after you, so they are not ahead on the road at all. Same
    // treatment — say nothing, remember nothing.
    Tracker u;
    u.otherPoint(7, pointKey(1, kSfPoint), 105000);
    CHECK_FALSE(u.aheadGap(7, pointKey(1, kSfPoint), 100000, g));   // -5s
    u.otherPoint(7, pointKey(2, kSfPoint), 198000);
    REQUIRE(u.aheadGap(7, pointKey(2, kSfPoint), 200000, g));
    CHECK(g.gapMs == 2000);
    CHECK_FALSE(g.hasTrend);
}
