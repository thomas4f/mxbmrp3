// ============================================================================
// tests/unit/test_spotter_hazard.cpp
// Pins the spotter's proximity/hazard cue state machine
// (core/spotter_hazard.h) — edges and restraint over detection that already
// exists elsewhere. The behaviors a plausible edit breaks silently:
//
//  - hysteresis: a rival oscillating between behindOn and clearMeters is IN
//    the hold band and must produce neither behind nor clear chatter;
//  - "clear" only ever follows a behind that was tracked, and a clear
//    suppressed by a higher cue is DROPPED, not deferred (a late "clear"
//    with nobody around reads as a glitch);
//  - a blue-flag edge suppressed by a hazard cue retries the next tick,
//    but an edge inside the cooldown is consumed silently;
//  - a session restart (clock rewind) resets the machine — stale timestamps
//    would otherwise sit a whole cooldown in the future and mute everything.
// ============================================================================
#include "doctest.h"

#include "core/spotter_hazard.h"

using namespace SpotterHazard;

namespace {
Inputs at(int nowMs, float behind = -1.0f, bool blue = false,
          bool hazard = false, bool wrongWay = false) {
    Inputs in;
    in.nowMs = nowMs;
    in.nearestBehindMeters = behind;
    in.blueFlagged = blue;
    in.hazardAhead = hazard;
    in.hazardIsWrongWay = wrongWay;
    return in;
}
}  // namespace

TEST_CASE("behind/clear: edge, repeat cooldown, hysteresis hold band") {
    Detector d;
    Config cfg;  // behindOn 12, clear 30, repeat 10000
    // Pairing, not episode length: these timelines are compressed to
    // fractions of a second, which the clear-min-episode gate would
    // silence. Its own case is below.
    cfg.clearMinEpisodeMs = 0;

    CHECK(d.update(at(0, 50.0f), cfg) == Cue::None);      // far: nothing
    CHECK(d.update(at(1000, 10.0f), cfg) == Cue::RiderBehind);
    CHECK(d.update(at(1500, 8.0f), cfg) == Cue::None);    // still there: quiet
    CHECK(d.update(at(5000, 20.0f), cfg) == Cue::None);   // hold band: quiet
    CHECK(d.update(at(6000, 9.0f), cfg) == Cue::None);    // back in: no re-edge
    CHECK(d.update(at(11500, 9.0f), cfg) == Cue::RiderBehind);  // camped: repeat
    CHECK(d.update(at(12000, 31.0f), cfg) == Cue::Clear);
    CHECK(d.update(at(12500, 40.0f), cfg) == Cue::None);  // already cleared
    // Gone entirely (no rider in scan range) also releases.
    CHECK(d.update(at(13000, 5.0f), cfg) == Cue::RiderBehind);
    CHECK(d.update(at(13500, -1.0f), cfg) == Cue::Clear);
}

// A race log said "Rider behind." / "All clear behind." 215ms apart -- a rival
// transiting the band while passing wide of the alongside window, which releases
// through the left-the-scan path that the spatial hysteresis cannot see. The
// announce is right; a clear that fast is chatter about nothing.
TEST_CASE("behind/clear: a release too soon after contact stays silent") {
    Config cfg;   // clearMinEpisodeMs defaults to 3000
    {
        Detector d;
        CHECK(d.update(at(1000, 10.0f), cfg) == Cue::RiderBehind);
        // Gone 500ms later: the state resets, but nothing is said.
        CHECK(d.update(at(1500, -1.0f), cfg) == Cue::None);
        // ...and the reset is real: the next contact announces normally, rather
        // than being swallowed as "already active".
        CHECK(d.update(at(2000, 10.0f), cfg) == Cue::RiderBehind);
    }
    {
        // The same release, once the episode has lasted: voiced.
        Detector d;
        CHECK(d.update(at(1000, 10.0f), cfg) == Cue::RiderBehind);
        CHECK(d.update(at(4500, -1.0f), cfg) == Cue::Clear);
    }
    {
        // 0 restores the old behaviour: every release is voiced.
        Config off = cfg;
        off.clearMinEpisodeMs = 0;
        Detector d;
        CHECK(d.update(at(1000, 10.0f), off) == Cue::RiderBehind);
        CHECK(d.update(at(1500, -1.0f), off) == Cue::Clear);
    }
}

TEST_CASE("behind: hold band on approach does not announce early") {
    Detector d;
    Config cfg;
    // Approaching through the band: 40 -> 25 (band) -> announce only at <= 12.
    CHECK(d.update(at(0, 40.0f), cfg) == Cue::None);
    CHECK(d.update(at(500, 25.0f), cfg) == Cue::None);
    CHECK(d.update(at(1000, 12.0f), cfg) == Cue::RiderBehind);
}

TEST_CASE("blue flag: edge once per cooldown; re-raise inside is silent") {
    Detector d;
    Config cfg;  // blue cooldown 30000
    CHECK(d.update(at(0, -1.0f, true), cfg) == Cue::BlueFlag);
    CHECK(d.update(at(1000, -1.0f, true), cfg) == Cue::None);   // held
    CHECK(d.update(at(5000, -1.0f, false), cfg) == Cue::None);  // dropped
    CHECK(d.update(at(6000, -1.0f, true), cfg) == Cue::None);   // edge in cooldown: consumed
    CHECK(d.update(at(7000, -1.0f, true), cfg) == Cue::None);   // and stays consumed
    CHECK(d.update(at(40000, -1.0f, false), cfg) == Cue::None);
    CHECK(d.update(at(41000, -1.0f, true), cfg) == Cue::BlueFlag);  // past cooldown
}

TEST_CASE("hazard: kind selects the cue; shared cooldown; wrong-way outranks") {
    Detector d;
    Config cfg;  // hazard cooldown 20000
    CHECK(d.update(at(0, -1.0f, false, true, false), cfg) == Cue::HazardAhead);
    CHECK(d.update(at(1000, -1.0f, false, true, false), cfg) == Cue::None);
    // Falls and re-raises as wrong-way inside the shared cooldown: silent.
    CHECK(d.update(at(2000), cfg) == Cue::None);
    CHECK(d.update(at(3000, -1.0f, false, true, true), cfg) == Cue::None);
    // Past the cooldown, a fresh edge speaks with the wrong-way phrase.
    CHECK(d.update(at(25000), cfg) == Cue::None);
    CHECK(d.update(at(26000, -1.0f, false, true, true), cfg) == Cue::WrongWayAhead);
}

TEST_CASE("priority: one cue per tick, suppressed blue retries, clear drops") {
    Detector d;
    Config cfg;
    // Hazard and blue rise together: hazard wins the tick...
    CHECK(d.update(at(0, -1.0f, true, true, false), cfg) == Cue::HazardAhead);
    // ...and the un-consumed blue edge fires on the next tick.
    CHECK(d.update(at(500, -1.0f, true, true, false), cfg) == Cue::BlueFlag);

    // Rider tracked behind; a hazard edge and the release land together:
    // the hazard speaks, the clear is dropped (not deferred).
    Detector d2;
    CHECK(d2.update(at(0, 10.0f), cfg) == Cue::RiderBehind);
    CHECK(d2.update(at(25000, 40.0f, false, true, false), cfg) == Cue::HazardAhead);
    CHECK(d2.update(at(25500, 40.0f, false, true, false), cfg) == Cue::None);
    // The pairing state is reset: a NEW approach announces again.
    CHECK(d2.update(at(26000, 10.0f, false, true, false), cfg) == Cue::RiderBehind);
}

TEST_CASE("session restart: clock rewind resets state and cooldowns") {
    Detector d;
    Config cfg;
    CHECK(d.update(at(100000, -1.0f, true), cfg) == Cue::BlueFlag);
    CHECK(d.update(at(101000, 10.0f), cfg) == Cue::RiderBehind);
    // Restart: session time jumps back to zero. Without the reset, the blue
    // cooldown would run to 130000 and the behind state would be stale.
    CHECK(d.update(at(0, -1.0f, true), cfg) == Cue::BlueFlag);
    // The pre-restart behind must not produce a post-restart "clear".
    CHECK(d.update(at(1000, 40.0f), cfg) == Cue::None);
}

namespace {
// One rival alongside: `side` is -1 left, +1 right, 0 nobody (or directly in
// line, which the scan reports as neither side). The offset is SIGNED —
// positive is behind you — and defaults here are positive, i.e. the blind-spot
// half of the window. `both` puts a rival at the same offset on each side.
Inputs alongside(int nowMs, float along, int side, float behind = -1.0f) {
    Inputs in;
    in.nowMs = nowMs;
    in.nearestBehindMeters = behind;
    if (side < 0) in.alongsideLeftMeters = along;
    if (side > 0) in.alongsideRightMeters = along;
    return in;
}
Inputs both(int nowMs, float along, float behind = -1.0f) {
    Inputs in;
    in.nowMs = nowMs;
    in.nearestBehindMeters = behind;
    in.alongsideLeftMeters = along;
    in.alongsideRightMeters = along;
    return in;
}
}  // namespace

TEST_CASE("alongside: side edge, immediate flip, hold band, repeat") {
    Detector d;
    Config cfg;  // alongsideOn 5, alongsideClear 9, repeat 10000

    // Rival pulls alongside on the right: announce.
    CHECK(d.update(alongside(0, 2.0f, +1), cfg) == Cue::RiderRight);
    // Held same side: quiet.
    CHECK(d.update(alongside(500, 1.5f, +1), cfg) == Cue::None);
    // Crosses to the left: the flip re-announces IMMEDIATELY — the side is
    // the cue's whole value, waiting out a cooldown would announce a lie.
    CHECK(d.update(alongside(1000, 1.5f, -1), cfg) == Cue::RiderLeft);
    // In the hold band (between on and clear): side held, no chatter.
    CHECK(d.update(alongside(1500, 7.0f, -1), cfg) == Cue::None);
    // Back inside on the same side within the repeat window: still quiet.
    CHECK(d.update(alongside(2000, 3.0f, -1), cfg) == Cue::None);
    // Camped alongside past the repeat cooldown: re-announce.
    CHECK(d.update(alongside(13000, 3.0f, -1), cfg) == Cue::RiderLeft);
}

TEST_CASE("alongside: no side means no call; clear pairs via release") {
    Detector d;
    Config cfg;
    // Pairing, not episode length: these timelines are compressed to
    // fractions of a second, which the clear-min-episode gate would
    // silence. Its own case is below.
    cfg.clearMinEpisodeMs = 0;

    // Nobody on either side -- which is also how the scan reports a rival
    // directly in line, since they are behind you rather than beside you.
    CHECK(d.update(alongside(0, 1.0f, 0), cfg) == Cue::None);
    // Side resolves: announce.
    CHECK(d.update(alongside(500, 1.0f, +1), cfg) == Cue::RiderRight);
    // Rival drops straight out of the alongside window with nobody in the
    // behind bands: that is a release, and the alongside episode pairs with
    // an explicit "clear" like a behind episode does.
    CHECK(d.update(alongside(1000, -1.0f, 0), cfg) == Cue::Clear);
}

TEST_CASE("alongside: overlapping rival ahead blocks a bogus 'clear'") {
    Detector d;
    Config cfg;
    // Pairing, not episode length: these timelines are compressed to
    // fractions of a second, which the clear-min-episode gate would
    // silence. Its own case is below.
    cfg.clearMinEpisodeMs = 0;
    // Rival half a bike AHEAD (no behind distance at all, alongside held):
    // the behind machinery must not read the empty behind band as a release
    // — "Clear." with a bar boxing you in is worse than wrong.
    CHECK(d.update(alongside(0, 2.0f, +1), cfg) == Cue::RiderRight);
    CHECK(d.update(alongside(500, 2.0f, +1), cfg) == Cue::None);
    // Only once they are genuinely gone does the clear fire.
    CHECK(d.update(alongside(1000, -1.0f, 0), cfg) == Cue::Clear);
}

TEST_CASE("lapping traffic: edge + cooldown, below blue flag") {
    Detector d;
    Config cfg;
    Inputs in;
    in.nowMs = 0;
    in.lappingTraffic = true;
    CHECK(d.update(in, cfg) == Cue::LappingTraffic);
    in.nowMs = 500;
    CHECK(d.update(in, cfg) == Cue::None);        // held: quiet
    in.nowMs = 1000;
    in.lappingTraffic = false;
    CHECK(d.update(in, cfg) == Cue::None);
    // Next backmarker inside the cooldown: consumed silently.
    in.nowMs = 5000;
    in.lappingTraffic = true;
    CHECK(d.update(in, cfg) == Cue::None);
    // A fresh edge past the cooldown announces again.
    in.lappingTraffic = false;
    in.nowMs = 40000;
    CHECK(d.update(in, cfg) == Cue::None);
    in.lappingTraffic = true;
    in.nowMs = 40500;
    CHECK(d.update(in, cfg) == Cue::LappingTraffic);

    // Blue flag on the same tick outranks it; the lapping edge retries.
    Detector d2;
    Inputs both;
    both.nowMs = 0;
    both.blueFlagged = true;
    both.lappingTraffic = true;
    CHECK(d2.update(both, cfg) == Cue::BlueFlag);
    both.nowMs = 500;
    CHECK(d2.update(both, cfg) == Cue::LappingTraffic);
}

TEST_CASE("alongside: outranks blue flag, loses to hazard") {
    Detector d;
    Config cfg;

    Inputs in = alongside(0, 2.0f, -1);
    in.blueFlagged = true;
    CHECK(d.update(in, cfg) == Cue::RiderLeft);   // alongside wins the tick
    // Blue edge was not consumed: it retries and fires next tick.
    Inputs in2 = alongside(500, 2.0f, -1);
    in2.blueFlagged = true;
    CHECK(d.update(in2, cfg) == Cue::BlueFlag);

    Detector d2;
    Inputs in3 = alongside(0, 2.0f, +1);
    in3.hazardAhead = true;
    CHECK(d2.update(in3, cfg) == Cue::HazardAhead);  // hazard outranks
    // Alongside state was still tracked; next tick announces the side.
    CHECK(d2.update(alongside(500, 2.0f, +1), cfg) == Cue::RiderRight);
}

// A rider weaving alongside used to be re-announced every time they crossed
// back inside 5m, because the release reset the announced side to 0 and the
// next entry then read as first contact — skipping the 10s cooldown entirely.
// The band is 5m in, 9m out, which at MX pace is about a quarter of a second,
// so a real logged session produced "Rider right / Clear / Rider right /
// Clear" at one-second intervals for half a minute; proximity was 56% of every
// callout in it.
TEST_CASE("alongside: weaving in and out does not re-announce every pass") {
    using namespace SpotterHazard;
    Config cfg;
    Detector d;
    Inputs in;
    in.nowMs = 0;
    in.alongsideRightMeters = 3.0f;
    CHECK(d.update(in, cfg) == Cue::RiderRight);

    // Drifts past the 9m release band, then comes straight back on the SAME
    // side, well inside the cooldown. Silence — they never went anywhere.
    in.nowMs = 800;
    in.alongsideRightMeters = 12.0f;
    d.update(in, cfg);                      // release (may emit Clear)
    in.nowMs = 1600;
    in.alongsideRightMeters = 3.0f;
    CHECK(d.update(in, cfg) != Cue::RiderRight);

    // A genuine flip to the other side is the case the cue exists for, and
    // still interrupts immediately.
    in.nowMs = 2400;
    in.alongsideRightMeters = kNoRival;
    in.alongsideLeftMeters = 3.0f;
    CHECK(d.update(in, cfg) == Cue::RiderLeft);

    // And once the cooldown really has elapsed, a re-entry speaks again.
    in.nowMs = 2400 + cfg.behindRepeatMs + 1;
    CHECK(d.update(in, cfg) == Cue::RiderLeft);
}

// Being level with a rival on EACH side is the one proximity situation where
// naming a single side is worse than saying nothing: act on "rider left" and
// you move into the rider you were not told about. The old Inputs carried one
// nearest-of-both rival and one side, so this state could not be represented
// at all — whichever of the two happened to be marginally closer won, and the
// call sent you the wrong way half the time.
TEST_CASE("alongside: boxed in on both sides is its own cue") {
    Detector d;
    Config cfg;  // alongsideOn 5, alongsideClear 9, repeat 10000
    CHECK(d.update(both(0, 2.0f), cfg) == Cue::RidersBothSides);
    CHECK(d.update(both(500, 2.0f), cfg) == Cue::None);        // held: quiet
    CHECK(d.update(both(11000, 2.0f), cfg) == Cue::RidersBothSides);  // repeat
}

// WIDENING interrupts, NARROWING does not. Left-then-both is new information
// worth cutting the cooldown for; both-then-left is the situation improving,
// and announcing improvements is what produced the "Rider right / Clear /
// Rider right" chatter that the cooldown exists to stop.
TEST_CASE("alongside: widening announces immediately, narrowing stays quiet") {
    Detector d;
    Config cfg;
    CHECK(d.update(alongside(0, 2.0f, -1), cfg) == Cue::RiderLeft);
    // A second rival arrives on the right, well inside the repeat window.
    CHECK(d.update(both(500, 2.0f), cfg) == Cue::RidersBothSides);
    // The right-hand one backs out: quieter, so nothing is said.
    CHECK(d.update(alongside(1000, 2.0f, -1), cfg) == Cue::None);
    // ...and comes back. One situation flickering, not a second widening:
    // the remembered mask never narrowed, so this stays quiet too.
    CHECK(d.update(both(1500, 2.0f), cfg) == Cue::None);
    // Once the repeat window has passed it speaks again, like any held state.
    CHECK(d.update(both(12000, 2.0f), cfg) == Cue::RidersBothSides);
}

// The pre-existing left/right flip must survive the mask rework: it is the
// same test as "a side is occupied that was not occupied last time".
TEST_CASE("alongside: a flip out of both-sides still interrupts") {
    Detector d;
    Config cfg;
    CHECK(d.update(alongside(0, 2.0f, +1), cfg) == Cue::RiderRight);
    CHECK(d.update(alongside(500, 2.0f, -1), cfg) == Cue::RiderLeft);
    CHECK(d.update(both(1000, 2.0f), cfg) == Cue::RidersBothSides);
}

// Each side gets the hysteresis band independently: a rival between on and
// clear on one side is held, not dropped, so the other side arriving is still
// read as a widening rather than as first contact.
TEST_CASE("alongside: per-side hold bands do not leak into each other") {
    Detector d;
    Config cfg;
    Inputs in;
    in.nowMs = 0;
    in.alongsideLeftMeters = 2.0f;
    CHECK(d.update(in, cfg) == Cue::RiderLeft);
    // Left drifts into its hold band (7m: past on, inside clear) while a
    // rival arrives on the right. Only the right side is "on", so this is a
    // right-hand call, not a both-sides one.
    in.nowMs = 500;
    in.alongsideLeftMeters = 7.0f;
    in.alongsideRightMeters = 2.0f;
    CHECK(d.update(in, cfg) == Cue::RiderRight);
    // Left comes back inside: now genuinely both.
    in.nowMs = 1000;
    in.alongsideLeftMeters = 2.0f;
    CHECK(d.update(in, cfg) == Cue::RidersBothSides);
}

// A spotter earns their keep on what you CANNOT see. The window used to be
// |along| <= 5m, which called a rival up to five metres UP THE ROAD — a rider
// you are already looking at, and the complaint that produced this split: the
// calls fired for riders that felt "more like in front of me".
TEST_CASE("alongside: a rival ahead of you is one you can see") {
    Detector d;
    Config cfg;  // ahead 2, behind 5, clear 9

    // Four metres ahead: inside the old symmetric window, outside this one.
    CHECK(d.update(alongside(0, -4.0f, +1), cfg) == Cue::None);
    // Level-ish, their rear wheel by your front: still worth calling.
    CHECK(d.update(alongside(500, -1.0f, +1), cfg) == Cue::RiderRight);
    // The same four metres BEHIND is the blind spot, and does call.
    Detector d2;
    CHECK(d2.update(alongside(0, 4.0f, -1), cfg) == Cue::RiderLeft);
}

// Zero is a supported setting, not an accident of the clamp: it is what
// "never tell me about anyone I could see by turning my head" looks like.
TEST_CASE("alongside: front distance 0 calls only from level backwards") {
    Detector d;
    Config cfg;
    cfg.alongsideAheadMeters = 0.0f;
    CHECK(d.update(alongside(0, -0.5f, +1), cfg) == Cue::None);
    CHECK(d.update(alongside(500, 0.5f, +1), cfg) == Cue::RiderRight);
}

// The release band stays symmetric on purpose. A rival who eases half a bike
// ahead has not gone anywhere, and dropping the episode there would fire a
// "Clear." while they are still beside you — worse than wrong.
TEST_CASE("alongside: easing ahead holds the episode rather than clearing it") {
    Detector d;
    Config cfg;
    // Pairing, not episode length: these timelines are compressed to
    // fractions of a second, which the clear-min-episode gate would
    // silence. Its own case is below.
    cfg.clearMinEpisodeMs = 0;
    CHECK(d.update(alongside(0, 2.0f, +1), cfg) == Cue::RiderRight);
    // Now 4m ahead: past the announce window, still inside the release band.
    CHECK(d.update(alongside(500, -4.0f, +1), cfg) == Cue::None);
    // Genuinely gone.
    CHECK(d.update(alongside(1000, kNoRival, +1), cfg) == Cue::Clear);
}

// The announce window and the release band are configured independently, and
// the forward announce reaches 20m against a 9m default release — so they can
// be set inverted. A rival inside the window that ANNOUNCES but outside the
// one that HOLDS would be called and then cleared while still beside you.
TEST_CASE("alongside: a release band narrower than the window cannot clear early") {
    Detector d;
    Config cfg;
    // Pairing, not episode length: these timelines are compressed to
    // fractions of a second, which the clear-min-episode gate would
    // silence. Its own case is below.
    cfg.clearMinEpisodeMs = 0;
    cfg.alongsideAheadMeters = 20.0f;   // wider than the 9m release
    // 15m ahead: announced, because the window says so...
    CHECK(d.update(alongside(0, -15.0f, +1), cfg) == Cue::RiderRight);
    // ...so it must also be HELD. Before the hold took the max of the two,
    // this tick fired "Clear." with the rival still being announced.
    CHECK(d.update(alongside(500, -15.0f, +1), cfg) == Cue::None);
    CHECK(d.update(alongside(1000, -15.0f, +1), cfg) == Cue::None);
    // Genuinely gone still clears.
    CHECK(d.update(alongside(1500, kNoRival, +1), cfg) == Cue::Clear);
}

// The CALLER decides how far to look, and anything it discards the detector
// never sees — so a scan bounded by the release band alone silently caps the
// forward announce window at it. alongside_ahead_m steps to 20m in the
// settings against a 9m default release, so that cap was reachable from the
// UI with no error and nothing on screen to explain it.
TEST_CASE("alongside: the scan reaches as far as the widest window") {
    Config cfg;
    // Defaults: the release band is the widest, so it sets the reach.
    CHECK(alongsideScanReach(cfg) == cfg.alongsideClearMeters);
    // Push the forward window past it and the reach follows, rather than the
    // window being quietly truncated.
    cfg.alongsideAheadMeters = 20.0f;
    CHECK(alongsideScanReach(cfg) == 20.0f);
    // Same for the rearward one.
    cfg.alongsideAheadMeters = 2.0f;
    cfg.alongsideOnMeters = 15.0f;
    CHECK(alongsideScanReach(cfg) == 15.0f);
    // And a rival at exactly the reach is still worth carrying: the detector's
    // hold band uses the same numbers, so the two agree at the boundary.
    Detector d;
    cfg = Config();
    cfg.alongsideAheadMeters = 20.0f;
    CHECK(d.update(alongside(0, -alongsideScanReach(cfg), +1), cfg) ==
          Cue::RiderRight);
}

// The scan reaches further than the announce window — it must, for the hold
// band — so it can see a rival the detector will not call. Handing the
// detector the NEAREST of those, rather than one inside the window, hides a
// genuine "rider right" behind somebody the machine then reports as nobody.
//
// The caller does the ranking (it is the one holding two candidates), so this
// pins the predicate they share: what counts as inside the window, end for
// end, at the boundaries the ranking turns on.
TEST_CASE("alongside: the window predicate is shared with the caller's scan") {
    Config cfg;   // ahead 2, behind 5, clear/scan reach 9
    CHECK(alongsideInWindow(cfg, 0.0f));
    CHECK(alongsideInWindow(cfg, 5.0f));     // the rearward edge
    CHECK(alongsideInWindow(cfg, -2.0f));    // ...and the forward one
    CHECK_FALSE(alongsideInWindow(cfg, 5.1f));
    CHECK_FALSE(alongsideInWindow(cfg, -2.1f));
    // The case the scan has to rank: 3m AHEAD is nearer than 4m behind, and
    // only the second is a call. Nearest-wins would take the first and the
    // detector would then see an empty side.
    CHECK_FALSE(alongsideInWindow(cfg, -3.0f));
    CHECK(alongsideInWindow(cfg, 4.0f));
    // Both are inside the scan's reach, which is what makes the ranking a
    // choice rather than a filter.
    CHECK(3.0f <= alongsideScanReach(cfg));
    CHECK(4.0f <= alongsideScanReach(cfg));
}
