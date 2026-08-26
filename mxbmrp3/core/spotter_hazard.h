// ============================================================================
// core/spotter_hazard.h
// The spotter's proximity/hazard cue state machine — the "spotting" part of
// the spotter. Pure (all inputs injected, no clocks, no PluginData) so the
// unit suite drives every edge (test_spotter_hazard.cpp); SpotterManager
// gathers the inputs each track-position tick and speaks the result.
//
// DETECTION ITSELF IS NOT HERE. Blue flags and stationary/wrong-way hazard
// riders are already computed by PluginData for NoticesHud; this machine's
// job is the part a screen notice doesn't need but a voice does: EDGES and
// RESTRAINT. A notice can stay on screen for the whole time a state holds;
// a spotter who says "blue flag" every tick for ten seconds gets muted by
// the rider. So: announce on the rising edge, re-announce only after a
// cooldown, and pair "rider behind" with an explicit "clear" on a WIDER
// release threshold (hysteresis — a rival oscillating around one threshold
// must not produce behind/clear chatter).
//
// One cue per tick, highest urgency first: an oncoming rider outranks a
// stationary one, outranks a blue flag, outranks proximity. A lower cue
// suppressed this tick fires naturally on a later tick if its state holds.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>

namespace SpotterHazard {

// "Nobody on this side." Far enough out that every window below rejects it,
// so the absent case needs no separate flag — and, unlike a negative
// sentinel, it cannot be mistaken for a real signed offset.
inline constexpr float kNoRival = 1.0e6f;

struct Config {
    // "Rider behind" when the nearest rider behind is within this; "clear"
    // only once they drop past clearMeters (or leave entirely). The gap
    // between the two is the hysteresis band.
    float behindOnMeters = 12.0f;
    float clearMeters = 30.0f;
    // "Rider left/right" when a rival OVERLAPS along-track with a clear
    // lateral side; the announced side is held until they pass
    // alongsideClear (its own hysteresis band). Side flips re-announce
    // immediately — that is the cue's whole value, and so is being boxed in
    // on BOTH sides at once (RidersBothSides).
    //
    // THE WINDOW IS ASYMMETRIC, because your eyes are. It used to be
    // |along| <= alongsideOn, which called a rider up to five metres AHEAD of
    // you — somebody plainly in your field of view, whom you are already
    // looking at. A spotter earns their keep on what you CANNOT see, so the
    // window reaches much further back (the blind spot from your shoulder
    // rearwards) than forward.
    //
    // The forward default is about a bike length: a rival whose rear wheel is
    // roughly level with your front is a genuine door-to-door you may not
    // have picked up, and anything further ahead than that you can see. Set
    // it to 0 for strictly-not-visible calling.
    float alongsideOnMeters = 5.0f;      // how far BEHIND still counts
    float alongsideAheadMeters = 2.0f;   // ...and how far AHEAD
    // Release band, applied both ways. Deliberately symmetric and generous
    // where the announce window is not: its job is to stop chatter and to
    // block a bogus "Clear." while a rival is still level with you, and both
    // of those want to hold the episode open longer than it started.
    float alongsideClearMeters = 9.0f;
    // How far ACROSS the track a rival can be and still be worth calling.
    //
    // Both cues above measure along the racing line, which on its own is not
    // proximity: on a wide straight or a split line a rider ten metres back
    // along the centreline can be twenty metres across it, and the Radar HUD
    // — whose primary filter is straight-line distance — correctly shows them
    // as nowhere near. "Rider behind" fired anyway, which is what made the
    // calls feel unrelated to what is on screen.
    //
    // Applied to BOTH cues, where it used to be a hardcoded 12 in the
    // alongside branch only. The default is that same 12: about two bike
    // widths plus a berm, wide enough to keep a genuine side-by-side on a
    // rutted line and narrow enough to drop the far side of a start straight.
    float lateralMeters = 12.0f;
    int behindRepeatMs = 10000;    // re-announce a camped rival at most this often
    // A "Clear." is voiced only when the episode LASTED at least this; a rider
    // blipping through the band (or transiting it while passing wide of the
    // alongside window) ends the episode SILENTLY -- state still resets, so
    // the next contact announces normally. A real session log produced
    // "Rider behind." / "All clear behind." 215ms apart from exactly that
    // transit, and a clear that fast is chatter about nothing. 0 = voice
    // every clear (the old behaviour).
    int clearMinEpisodeMs = 3000;
    int blueFlagCooldownMs = 30000;
    int hazardCooldownMs = 20000;  // shared by stationary and wrong-way
    int lappingCooldownMs = 30000; // "backmarker ahead" while working traffic
};

enum class Cue : uint8_t {
    None,
    WrongWayAhead,   // oncoming rider — most urgent
    HazardAhead,     // stationary rider/bike on track
    RiderLeft,       // rival alongside on your left — contact-imminent info
    RiderRight,
    RidersBothSides, // boxed in — a rival on EACH side at once
    BlueFlag,        // faster rider (a lap up) closing on you
    LappingTraffic,  // YOU are the lapper: backmarker just ahead
    RiderBehind,
    Clear,
};

struct Inputs {
    int nowMs = 0;                    // session ELAPSED time (monotonic)
    // Only riders BEHIND can be here, so this one is a plain distance and
    // -1 means none. The two below are signed and use kNoRival instead.
    float nearestBehindMeters = -1.0f;  // nearest non-excluded rider behind; < 0 = none
    // The nearest OVERLAPPING rival on each side, as a SIGNED along-track
    // offset in meters: POSITIVE = behind you, NEGATIVE = ahead of you,
    // kNoRival = nobody on that side within the alongside scan.
    //
    // Signed because the announce window is asymmetric (see Config): "beside
    // and slightly behind" is the call worth making and "three metres up the
    // road" is one you can see for yourself, and an absolute value cannot
    // tell those apart.
    //
    // Per side rather than one nearest-of-both, because "both sides" is not
    // expressible as a single side and it is the case where "rider left" on
    // its own is actively harmful — act on it and you move into the rider you
    // were not told about. Splitting it also drops a quirk of the old shape:
    // when the nearest overlapping rival happened to be directly in line, the
    // cue fell silent even with somebody plainly alongside.
    //
    // A rival directly in line (inside the caller's lateral dead band) is on
    // NEITHER side: they are behind you, not beside you, and
    // nearestBehindMeters already carries them.
    float alongsideLeftMeters = kNoRival;
    float alongsideRightMeters = kNoRival;
    bool blueFlagged = false;         // focused rider is being shown the blue flag
    bool lappingTraffic = false;      // focused rider is closing on a backmarker
    bool hazardAhead = false;
    bool hazardIsWrongWay = false;    // refines hazardAhead
};

// Is this signed along-track offset inside the window that ANNOUNCES? Used by
// the detector below and by the caller's scan, which has to rank candidates by
// it — see alongsideScanReach for why the scan sees more than this.
inline bool alongsideInWindow(const Config& cfg, float along) {
    return along >= -cfg.alongsideAheadMeters &&
           along <= cfg.alongsideOnMeters;
}

// How far along-track the CALLER must look to feed this machine honestly.
//
// The scan discards rivals outside it, so anything narrower than the widest
// window silently caps that window: alongside_ahead_m steps to 20m in the
// settings, and a scan bounded by the 9m release band meant a rival 15m ahead
// never reached the detector at all. The stepper appeared to work and did
// nothing past 9 — a cap with no error and no way to see it from the UI.
inline float alongsideScanReach(const Config& cfg) {
    return std::max(cfg.alongsideClearMeters,
                    std::max(cfg.alongsideOnMeters, cfg.alongsideAheadMeters));
}

class Detector {
public:
    Cue update(const Inputs& in, const Config& cfg) {
        // A session restart rewinds the clock; stale timestamps would then sit
        // an entire cooldown in the future and mute every cue. Any meaningful
        // backward jump resets the machine.
        if (in.nowMs + 5000 < m_lastNowMs) reset();
        m_lastNowMs = in.nowMs;

        Cue cue = Cue::None;

        // Hazard edge (wrong-way and stationary share one cooldown — they are
        // often the same incident evolving, and two callouts for it is spam).
        if (in.hazardAhead && !m_hazardPrev &&
            in.nowMs - m_lastHazardMs >= cfg.hazardCooldownMs) {
            cue = in.hazardIsWrongWay ? Cue::WrongWayAhead : Cue::HazardAhead;
            m_lastHazardMs = in.nowMs;
        }
        m_hazardPrev = in.hazardAhead;

        // Alongside: announce on entering with a side, re-announce immediately
        // when the THREAT WIDENS (that is the point of the cue), repeat a
        // camped alongside rival on the shared repeat cooldown. Outranks the
        // blue flag — a bar alongside is contact-imminent, a lapper is context.
        // Positive is behind you, negative is ahead, so the two limits are
        // not interchangeable — see Config for why the forward one is short.
        auto within = [](float along, float ahead, float behind) {
            return along >= -ahead && along <= behind;
        };
        // Which sides are occupied right now: bit 0 = left, bit 1 = right.
        const int sideMask =
            (alongsideInWindow(cfg, in.alongsideLeftMeters) ? 1 : 0) |
            (alongsideInWindow(cfg, in.alongsideRightMeters) ? 2 : 0);
        // The release band must COVER the announce window, end for end. A
        // rival inside the window that ANNOUNCES but outside the one that
        // HOLDS gets "Rider right." and then "Clear." while still sitting
        // beside you — the exact bogus clear the hold exists to prevent.
        // Nothing else guarantees it: alongside_ahead_m reaches 20m and
        // alongside_clear_m defaults to 9, so a hand-edited INI (or a stepper
        // taken to its ceiling) can invert them. Taking the max here makes
        // that unrepresentable rather than merely discouraged.
        const float holdAhead =
            std::max(cfg.alongsideClearMeters, cfg.alongsideAheadMeters);
        const float holdBehind =
            std::max(cfg.alongsideClearMeters, cfg.alongsideOnMeters);
        const bool alongsideHeld =
            within(in.alongsideLeftMeters, holdAhead, holdBehind) ||
            within(in.alongsideRightMeters, holdAhead, holdBehind);
        if (sideMask != 0) {
            // WIDENING bypasses the cooldown: a side is occupied that was not
            // occupied at the last announcement. One expression covers both
            // urgent cases — left->right is the flip this has always
            // interrupted for, and left->both is the same news with more of
            // it. NARROWING (both->left) deliberately says nothing: the
            // situation improved, and announcing an improvement is how you get
            // the "Rider right / Clear / Rider right" chatter the cooldown
            // below was added to stop.
            //
            // Because the remembered mask is only written when something IS
            // announced, it never narrows on its own — so both->left->both is
            // one situation flickering rather than two widenings, and stays
            // quiet. It goes stale after behindRepeatMs like everything else,
            // which is what stops one sandwich muting every later flip.
            const bool widened = (sideMask & ~m_alongsideMask) != 0;
            // The cooldown applies to a RE-ENTRY on the same side, not just to
            // a rival who never left. The remembered side used to reset on
            // release, so every re-entry read as first contact and skipped the
            // cooldown entirely — and the release band is 5m in, 9m out, which
            // at MX pace is a quarter of a second. A real session produced
            // "Rider right / Clear / Rider right / Clear" at one-second
            // intervals for half a minute, and proximity was 56% of every
            // callout in it. Surviving the release is what fixed that.
            const bool announce =
                m_alongsideMask == 0 || widened ||
                in.nowMs - m_lastAlongsideMs >= cfg.behindRepeatMs;
            if (announce && cue == Cue::None) {
                cue = sideMask == 3   ? Cue::RidersBothSides
                      : sideMask == 1 ? Cue::RiderLeft
                                      : Cue::RiderRight;
                m_lastAlongsideMs = in.nowMs;
                m_alongsideMask = sideMask;
            }
            // A due announce that lost this tick's slot to a higher cue is
            // NOT consumed (state unchanged, same as the blue-flag retry):
            // it fires on the next tick the side still holds.
            // An alongside rival IS a tracked rival: pair the eventual
            // "clear" without requiring them to pass through the behind band.
            if (!m_behindActive) {
                m_behindActive = true;
                m_lastBehindMs = in.nowMs;
                m_behindStartMs = in.nowMs;
            }
        } else if (!alongsideHeld) {
            // Released — but the mask is REMEMBERED (see the announce gate),
            // so coming back on a side already announced stays under the
            // cooldown. m_lastAlongsideMs is what expires, not the mask.
        }

        // Blue flag edge. A rising edge suppressed by a hazard cue THIS tick
        // is not consumed (m_bluePrev stays false), so it retries next tick;
        // a rising edge inside the cooldown is consumed silently.
        const bool blueEdge = in.blueFlagged && !m_bluePrev;
        const bool blueReady = in.nowMs - m_lastBlueMs >= cfg.blueFlagCooldownMs;
        if (blueEdge && blueReady && cue == Cue::None) {
            cue = Cue::BlueFlag;
            m_lastBlueMs = in.nowMs;
            m_bluePrev = true;
        } else if (!(blueEdge && blueReady)) {
            m_bluePrev = in.blueFlagged;
        }

        // Lapping traffic: same edge-plus-cooldown shape as the blue flag,
        // and the same retry rule when a higher cue takes the tick.
        const bool lapEdge = in.lappingTraffic && !m_lappingPrev;
        const bool lapReady =
            in.nowMs - m_lastLappingMs >= cfg.lappingCooldownMs;
        if (lapEdge && lapReady && cue == Cue::None) {
            cue = Cue::LappingTraffic;
            m_lastLappingMs = in.nowMs;
            m_lappingPrev = true;
        } else if (!(lapEdge && lapReady)) {
            m_lappingPrev = in.lappingTraffic;
        }

        // Proximity: behind with hysteresis + repeat, clear on release. A
        // held alongside rival blocks the release — they show up in neither
        // behind band while overlapping you (slightly ahead has no "behind"
        // distance at all), and "Clear." with a bar boxing you in is worse
        // than wrong.
        const bool inRange = in.nearestBehindMeters >= 0.0f &&
                             in.nearestBehindMeters <= cfg.behindOnMeters;
        const bool released = (in.nearestBehindMeters < 0.0f ||
                               in.nearestBehindMeters > cfg.clearMeters) &&
                              !alongsideHeld;
        if (inRange) {
            if (!m_behindActive) m_behindStartMs = in.nowMs;
            if ((!m_behindActive ||
                 in.nowMs - m_lastBehindMs >= cfg.behindRepeatMs) &&
                cue == Cue::None) {
                cue = Cue::RiderBehind;
                m_lastBehindMs = in.nowMs;
                m_behindActive = true;
            } else if (!m_behindActive) {
                // Announced by a later tick (suppressed now): still mark
                // active so "clear" pairs correctly even if the announce
                // never got airtime.
                m_behindActive = true;
                m_lastBehindMs = in.nowMs;
            }
        } else if (released && m_behindActive) {
            m_behindActive = false;
            // Episode length gates the VOICE only (cfg.clearMinEpisodeMs):
            // the state above resets either way.
            if (cue == Cue::None &&
                in.nowMs - m_behindStartMs >= cfg.clearMinEpisodeMs) {
                cue = Cue::Clear;
            }
            // A clear suppressed by a higher cue is dropped, not deferred:
            // by the next tick the state is already inactive and stale
            // "clear" with no rival around reads as a glitch.
        }
        // Between behindOn and clearMeters: hold current state (hysteresis).

        return cue;
    }

    void reset() {
        m_behindActive = false;
        m_alongsideMask = 0;
        m_bluePrev = false;
        m_lappingPrev = false;
        m_hazardPrev = false;
        m_lastBehindMs = kLongAgo;
        m_behindStartMs = 0;
        m_lastAlongsideMs = kLongAgo;
        m_lastBlueMs = kLongAgo;
        m_lastLappingMs = kLongAgo;
        m_lastHazardMs = kLongAgo;
        m_lastNowMs = 0;
    }

private:
    // "Longer ago than any cooldown" without underflowing int arithmetic.
    static constexpr int kLongAgo = -1000000;

    bool m_behindActive = false;
    int m_behindStartMs = 0;       // when the current behind episode began
    // The sides LAST ANNOUNCED (bit 0 = left, bit 1 = right), kept across a
    // release so a rider weaving in and out is not re-announced every time
    // they cross 5m. 0 = never announced this session (or reset()).
    int m_alongsideMask = 0;
    bool m_bluePrev = false;
    bool m_lappingPrev = false;
    bool m_hazardPrev = false;
    int m_lastBehindMs = kLongAgo;
    int m_lastAlongsideMs = kLongAgo;
    int m_lastBlueMs = kLongAgo;
    int m_lastLappingMs = kLongAgo;
    int m_lastHazardMs = kLongAgo;
    int m_lastNowMs = 0;
};

}  // namespace SpotterHazard
