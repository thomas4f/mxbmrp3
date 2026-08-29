// ============================================================================
// core/spotter_manager_proximity.cpp
// SpotterManager's per-frame proximity engine: onTrackPositions() feeds the
// pace tracker and the hazard/stretch detectors and emits the proximity,
// hazard and lapping cues. Split from spotter_manager.cpp; the method body is
// unchanged.
// ============================================================================
#include "spotter_manager.h"

#include "plugin_data.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cmath>
void SpotterManager::onTrackPositions(
    int numVehicles, const Unified::TrackPositionData* positions) {
    if ((!m_enabled && !m_subtitles) || !positions || numVehicles <= 0) {
        return;
    }
    const bool wantProx =
        isCategoryEnabled(SpotterPhrase::Category::Proximity);
    const bool wantHaz = isCategoryEnabled(SpotterPhrase::Category::Hazard);

    const PluginData& pd = PluginData::getInstance();

    // The timed-session milestones ride this callback for its CLOCK, not for
    // its positions, and they are TIMING cues. They sat below the early return
    // that guards the proximity and hazard work, so muting the two chattier
    // categories silently took "ten minutes to go" and "halfway there" with
    // them — a Timing switch overruled by an Opponents one.
    // (A lap race's halfway_point rides the leader's crossings in
    // onRaceLapCompleted instead.)
    //
    // NOT gated on isRaceSession(). Qualifying is timed and "five minutes
    // remaining" is the moment it is FOR; practice is timed too. This branch
    // un-gated the sector cue and the lap report for exactly that reason (see
    // onRaceSplit and onRaceLapCompleted) and missed this one, while
    // allCueKeys() has always described it as "ten minutes of the session
    // left" with no race qualifier. The lap-race halfway_point stays race-only
    // in onRaceLapCompleted, correctly: it needs a leader and a distance.
    // ...and only while the session is RUNNING. Position batches keep
    // arriving while riders roll back after an early Race Over, and a clock
    // still counting there would cross "five minutes left" about a session
    // that has already ended.
    const bool sessionRunning =
        (pd.getSessionData().sessionState &
         PluginConstants::SessionState::IN_PROGRESS) != 0;
    if (sessionRunning && isCategoryEnabled(SpotterPhrase::Category::Timing)) {
        const int nowMs = pd.getSessionElapsedTime();
        if (const char* cue = m_milestones.updateTime(
                nowMs, pd.getSessionData().sessionLength)) {
            emitCue(cue, SpotterPhrase::Category::Timing, {}, nowMs);
        }
    }

    if (!wantProx && !wantHaz) return;
    const int focused = pd.getDisplayRaceNum();
    if (focused <= 0) return;

    SpotterHazard::Inputs in;
    // Elapsed, never the raw clock: the detectors' cooldown arithmetic needs
    // a monotonic ascending clock, and the raw clock counts DOWN in timed
    // sessions — with it, every cooldown "never expires" and blue-flag /
    // hazard cues would fire at most once per timed race.
    in.nowMs = pd.getSessionElapsedTime();

    // Proximity ("rider behind"/"clear"): nearest non-excluded rider behind
    // the focused rider along the racing line, from THIS batch — the game
    // sends the ~10 closest riders, which is exactly the set that matters.
    // Suppressed on the pre-start grid and during the launch (a packed grid
    // is not a rival closing on you), same grace the hazard detector uses.
    // Also quiet once YOUR race is over — the same reasoning at the other end.
    // Riders still racing sweep past you on the cool-down lap and the detector
    // read every one of them as a rival on your tail: the demo weekend called
    // "Rider behind", "Rider right" and "Clear" after the checkered flag. The
    // hazard half below is deliberately NOT gated: a rider down ahead is still
    // something to avoid on the lap back to the pits.
    const float trackLen = pd.getSessionData().trackLength;
    const bool raceOver = subjectRaceOver();
    const bool gridQuiet =
        (pd.isRaceSession() &&
         !(pd.getSessionData().sessionState &
           PluginConstants::SessionState::IN_PROGRESS)) ||
        pd.isInGridStartGrace() || raceOver;
    // "The scan ALLOWED", which is not the same as "the scan produced
    // inputs" — see proxScanned below.
    const bool proxAllowed = wantProx && trackLen > 0.0f && !gridQuiet;
    bool proxScanned = false;
    if (proxAllowed) {
        const Unified::TrackPositionData* focusedPos = nullptr;
        for (int i = 0; i < numVehicles; ++i) {
            if (positions[i].raceNum == focused) {
                focusedPos = &positions[i];
                break;
            }
        }
        if (focusedPos) {
            // The rival's offset ACROSS the track. Headings are compass-style
            // (degrees from north, clockwise — the track builder's convention:
            // forward = (sin, cos) in the X/Z plane, +90deg = the rider's
            // right), so the lateral offset is rel . (cos, -sin): positive =
            // your RIGHT.
            const float yawRad =
                focusedPos->yaw * 3.14159265358979323846f / 180.0f;
            const float cosYaw = std::cos(yawRad);
            const float sinYaw = std::sin(yawRad);
            auto lateralOf = [&](const Unified::TrackPositionData& p) {
                return (p.posX - focusedPos->posX) * cosYaw -
                       (p.posZ - focusedPos->posZ) * sinYaw;
            };

            float nearestBehind = -1.0f;
            float nearestLeft = SpotterHazard::kNoRival;
            float nearestRight = SpotterHazard::kNoRival;
            // Whether a lap difference means anything here. Outside a race
            // everyone is on their own schedule -- an out lap beside a hot lap
            // is not "a lap down" in any sense worth acting on -- so the
            // filter below applies to races only, the same basis
            // fillAmbientVars uses for the lap-count gaps.
            const bool lapsAreReal = pd.isRaceSession();
            const StandingsData* mine = pd.getStanding(focused);
            const int myLaps = mine ? mine->gapLaps : 0;
            for (int i = 0; i < numVehicles; ++i) {
                const Unified::TrackPositionData& p = positions[i];
                if (p.raceNum == focused || p.crashed) continue;
                const StandingsData* st = pd.getStanding(p.raceNum);
                if (st && pd.isRiderExcludedFromDetection(*st)) continue;
                // Is this somebody you are actually racing? A rider a lap up
                // or down shares the track with you and nothing else, and the
                // two cases that DO matter already have their own cues:
                // blue_flag when they are lapping you, lapping_traffic when
                // you are lapping them. "Rider behind" for a backmarker you
                // are about to pass is noise on top of a call you already got.
                //
                // Deliberately NOT applied to the alongside cues below. Those
                // are contact-imminent: a rider a lap down who is level with
                // your front wheel can still take you out, and which lap they
                // are on has nothing to do with it.
                const bool sameLap =
                    !lapsAreReal || !st || st->gapLaps == myLaps;
                // Along the racing line is only HALF of proximity, and on its
                // own it is the half that lies: a rider ten metres back down
                // the centreline can be right across a wide straight from you.
                // Both cues take the same lateral gate — see Config.
                const float lat = lateralOf(p);
                const float latAbs = lat < 0.0f ? -lat : lat;
                if (latAbs > m_hazardCfg.lateralMeters) continue;
                // Positive = p is behind us.
                const float alongM =
                    alongTrackDelta(focusedPos->trackPos, p.trackPos) * trackLen;
                if (sameLap && alongM > 0.0f &&
                    (nearestBehind < 0.0f || alongM < nearestBehind)) {
                    nearestBehind = alongM;
                }
                // The nearest overlapping rival ON EACH SIDE, so being boxed
                // in is expressible. The dead band |lat| < 1m is "directly in
                // line, no side" — a rider you are nose-to-tail with is
                // behind, not beside — and they simply land on neither side
                // rather than, as before, occupying the one nearest slot and
                // silencing a rider genuinely alongside.
                //
                // Kept SIGNED (positive = behind), because the announce
                // window is asymmetric: the detector must be able to tell a
                // rival level with your shoulder from one three metres up the
                // road that you can see perfectly well.
                const float alongAbs = alongM < 0.0f ? -alongM : alongM;
                // As far as the WIDEST window the detector may apply — see
                // alongsideScanReach. Bounding this by the release band alone
                // capped the forward announce window at it.
                if (alongAbs > SpotterHazard::alongsideScanReach(m_hazardCfg)) {
                    continue;
                }
                if (latAbs < 1.0f) continue;
                // NEAREST is not the right test on its own. The scan reaches
                // further than the announce window (it has to, for the hold
                // band), so a rival just outside the window can be closer than
                // one squarely inside it — and taking the closer of the two
                // hands the detector a "nobody here" and masks a real call. A
                // rival IN the window always wins the slot; among equals, the
                // nearest.
                float& side = lat > 0.0f ? nearestRight : nearestLeft;
                const bool candIn =
                    SpotterHazard::alongsideInWindow(m_hazardCfg, alongM);
                const bool heldIn =
                    SpotterHazard::alongsideInWindow(m_hazardCfg, side);
                const float sideAbs = side < 0.0f ? -side : side;
                if (candIn != heldIn ? candIn : alongAbs < sideAbs) {
                    side = alongM;
                }
            }
            in.nearestBehindMeters = nearestBehind;
            in.alongsideLeftMeters = nearestLeft;
            in.alongsideRightMeters = nearestRight;
            // Only NOW is the scan a fact. Permission was not enough: with the
            // focused rider absent from the batch the loop above never ran, the
            // inputs kept their "nobody anywhere" defaults, the detector read
            // that as a release and "Clear." escaped the guard — the same stray
            // clear as before, one level further in. RadarHud guards the same
            // state for the same reason.
            proxScanned = true;
        }
    }

    // Lapping traffic (you closing on a backmarker) is Opponents-facing;
    // same cached detection NoticesHud polls for its LAPPING notice.
    if (wantProx && !raceOver) {
        in.lappingTraffic = pd.isRiderLapping(focused);
    }

    // Hazard/blue-flag edges over PluginData's existing (lazily cached)
    // detection — the same queries NoticesHud polls.
    if (wantHaz) {
        // Blue flags go with the proximity calls: nobody is being held up by a
        // rider who has already finished, so the flag is a leftover.
        in.blueFlagged = !raceOver && pd.isRiderBlueFlagged(focused);
        in.hazardAhead = pd.isHazardAhead();
        if (in.hazardAhead) {
            for (int rn : pd.getHazardRaceNums()) {
                if (pd.getRiderHazardType(rn) == HazardType::WrongWay) {
                    in.hazardIsWrongWay = true;
                    break;
                }
            }
        }
    }

    using Cue = SpotterHazard::Cue;
    const int t = in.nowMs;
    switch (m_detector.update(in, m_hazardCfg)) {
        // The proximity pair re-checks that the scan actually RAN, not just
        // that its category is on. A tick with no scan leaves nearest = -1,
        // which the detector reads as a release — and that synthetic "Clear."
        // must not escape. Two ways in, and only the first was guarded:
        // turning Opponents off while a rival is tracked, and the scan being
        // suppressed on the grid or once your race is over. The second is how
        // a "Clear." still followed the checkered flag after the alongside
        // cooldown fixed the OTHER stray one — same symptom, different path.
        //
        // The detector's own state still advances; it is only the CUE that is
        // withheld, so nothing is left latched for the next session.
        //
        // Hazard cues need no re-check — their inputs are zeroed above when
        // the category is off, so no edge can rise.
        case Cue::RiderLeft:
            if (proxScanned) {
                emitCue("rider_left", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::RiderRight:
            if (proxScanned) {
                emitCue("rider_right", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::RidersBothSides:
            if (proxScanned) {
                emitCue("riders_both_sides", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::RiderBehind:
            if (proxScanned) {
                emitCue("rider_behind", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::Clear:
            if (proxScanned) {
                emitCue("rider_behind_clear", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        // The two lapping cues NAME their subject, which the rest of the
        // detector cues cannot: both sides of that pairing are already in the
        // blue-flag cache, so the number costs a lookup rather than new
        // detection. It goes in {event_rider} (the cue IS about them) and is
        // passed as riderNum too, so a recorded pack's _mix can stitch it from
        // its number clips instead of dropping to speech.
        //
        // -1 is the normal answer, not an error — the flag can be up with the
        // pairing already recomputed away — so riderRef() yields "" and the
        // pack's optional group drops. Deliberately NOT the same as naming the
        // hazard cues' subject: getHazardRaceNums() is not distance-ordered,
        // so with three riders down it would announce an arbitrary one.
        case Cue::BlueFlag: {
            const int lapper = pd.getRiderLappedBy(focused);
            SpotterVars::Vars v;
            v.eventRider = SpotterPhrase::riderRef(lapper, focused);
            emitCue("blue_flag", SpotterPhrase::Category::Hazard, v, t, lapper);
            break;
        }
        case Cue::LappingTraffic:
            if (wantProx) {
                const int backmarker = pd.getRiderLappingTarget(focused);
                SpotterVars::Vars v;
                v.eventRider = SpotterPhrase::riderRef(backmarker, focused);
                emitCue("lapping_traffic", SpotterPhrase::Category::Proximity, v,
                        t, backmarker);
            }
            break;
        case Cue::HazardAhead:
            emitCue("hazard_ahead", SpotterPhrase::Category::Hazard, {}, t);
            break;
        case Cue::WrongWayAhead:
            emitCue("wrong_way_ahead", SpotterPhrase::Category::Hazard, {}, t);
            break;
        case Cue::None:
            break;
    }
}
