// ============================================================================
// core/spotter_manager_compose.cpp
// SpotterManager's cue composition: the ambient variable set, the central
// emitCue() (phrase selection, subtitles, audio routing) and the settings-menu
// voice preview. Split from spotter_manager.cpp; every method body is
// unchanged.
// ============================================================================
#include "spotter_manager.h"

#include "plugin_data.h"
#include "plugin_utils.h"
#include "hud_manager.h"
#include "../hud/fuel_widget.h"
#include "fuel_estimate.h"
#include "spotter_mix.h"
#include "spotter_manager_internal.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// The always-available half of the variable set. Read from live state at cue
// time — that is the whole contract (spotter_vars.h): a template may name any
// of these in any cue, and one that does not apply right now resolves to
// nothing rather than to a literal "{gap_to_ahead}".
//
// COST: a handful of standings lookups per CUE, which is a few per lap, not
// per frame. Nothing here belongs on the Draw path.
//
// The live gaps here and the gap_* CUES are deliberately different numbers and
// both are right: a cue reports the STOPWATCH value from the last shared
// timing point (spotter_pace.h), which is what a spotter says at the line;
// {gap_to_ahead} is where the gap is NOW, which is what a template asking mid-cue
// wants. The trend pair comes from the pace tracker, so it moves at crossings.
void SpotterManager::fillAmbientVars(SpotterVars::Vars& v) const {
    // NEVER overwrite a value the emitter already set. Where both have an
    // answer the EVENT's is the better one: the gap cues carry the stopwatch
    // gap from the last shared timing point, and clobbering that with the live
    // estimate computed below would quietly replace a measurement with a
    // guess. Assign through this rather than to the field directly.
    auto fill = [](std::string& field, std::string value) {
        if (field.empty()) field = std::move(value);
    };
    const PluginData& pd = PluginData::getInstance();
    const SessionData& sd = pd.getSessionData();

    if (sd.trackName[0] != '\0') fill(v.trackName, sd.trackName);
    // The same label the Session HUD shows — "Race 1", "Warmup", "Qualify
    // Practice" — rather than a second, coarser mapping of the same enum.
    if (const char* label = PluginUtils::getSessionString(sd.eventType,
                                                         sd.session)) {
        fill(v.sessionName, label);
    }
    // "Waiting", "Sighting Lap", "Pre-Start", "In Progress", "Complete",
    // "Race Over", "Cancelled" — the same words the Session HUD shows.
    if (const char* st = PluginUtils::getSessionStateString(m_sessionState)) {
        fill(v.sessionState, st);
    }

    // Session length and what is left of it, in TWO shapes. A session is
    // measured in laps or in time depending on how it was set up, so the
    // specific variables answer only when they apply, while the generic pair
    // answers in whichever unit this session actually uses — that is what a
    // template can rely on everywhere.
    auto minuteWords = [](int ms) {
        const int minutes = ms / 60000;
        // Whole minutes is how session lengths are set and how a rider thinks
        // about them — but the truncation has a floor, and below it the answer
        // is nonsense: a short qualifying session announced itself as "Qualify
        // underway, ZERO MINUTES". Under a minute, say the seconds.
        if (minutes <= 0) return SpotterPhrase::durationWords(ms);
        return SpotterPhrase::numberWords(minutes) +
               (minutes == 1 ? " minute" : " minutes");
    };
    // A gap is in SECONDS while you are on the same lap and in LAPS once you
    // are not — which is what a spotter says, and the only reading of the
    // number that is true.
    auto gapWords = [](int laps, int ms) {
        return laps > 0 ? SpotterPhrase::lapsWords(laps)
                        : SpotterPhrase::gapWordsMs(ms);
    };
    // THREE session shapes, not two, and the third is the common one in MX
    // Bikes: a race is usually "10 minutes + 2 laps" — a clock, then that many
    // bonus laps once it expires. Reporting only the clock drops half the
    // format, so both halves are spoken, the same way the event log writes
    // "03:00 + 2L".
    //
    // In a time+laps race sessionNumLaps is the BONUS lap count, not the race
    // distance. That is what makes {laps_remaining} below a lap-race-only
    // figure: subtracting laps completed from a bonus count is arithmetic on
    // two different things.
    const bool timed = sd.sessionLength > 0;
    const bool lapped = sd.sessionNumLaps > 0;
    // Nothing is REMAINING until the session is running. On the grid the full
    // distance is still ahead, so the subtraction below returns it verbatim and
    // a template reads it as a countdown: leaving the pits before a four-lap
    // race, "Pit exit, up to speed. Race 2, FOUR LAPS LEFT" — forty-five
    // seconds before the start, and directly after session_prestart had just
    // said "Race 2 starting, four laps". {session_length} is the variable for
    // that sentence; the remaining trio answers only once there is a difference
    // between the two.
    const bool running =
        (sd.sessionState & PluginConstants::SessionState::IN_PROGRESS) != 0;
    auto withBonusLaps = [&](const std::string& timePart) {
        if (!lapped) return timePart;
        // Bonus laps are a SUFFIX on a clock, never a figure of their own. With
        // no time part they read as the whole answer, which is how a fresh
        // 8min+1lap race announced itself as "eight minutes plus one lap, ONE
        // LAP LEFT" — the clock had not arrived yet, so {session_remaining}
        // fell through to the bonus count. Empty in, empty out: the optional
        // group drops and the line simply omits what is not known yet.
        if (timePart.empty()) return std::string();
        return timePart + " plus " + SpotterPhrase::lapsWords(sd.sessionNumLaps);
    };
    if (timed) {
        fill(v.sessionLength, withBonusLaps(minuteWords(sd.sessionLength)));
        const int remainMs = pd.getSessionTime();
        if (running && remainMs > 0) fill(v.timeRemaining, minuteWords(remainMs));
        fill(v.sessionRemaining, withBonusLaps(v.timeRemaining));
    } else if (lapped) {
        fill(v.sessionLength, SpotterPhrase::lapsWords(sd.sessionNumLaps));
    }

    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return;

    // Your name as the game has it. UTF-8, so a name with accents reads
    // correctly through text-to-speech but garbles in the SUBTITLE — the
    // in-game font is a byte-indexed CP1252 table (see CLAUDE.md), which is a
    // renderer limit rather than something to fix here.
    if (const RaceEntryData* entry = pd.getRaceEntry(me)) {
        if (entry->name[0] != '\0') {
            // Rating servers prefix the display name ("B1 | Thomas"), and TTS
            // read the tag out. The name is what follows the LAST " | "; a name
            // without the separator is untouched.
            //
            // DELIBERATELY NOT matchRiderName's rule, which splits on the FIRST
            // separator and strips only when the prefix is alphanumeric. That
            // one answers "is this entry the player?", where a wrong strip
            // misidentifies a rider, so it is conservative. This one answers
            // "what should be SPOKEN", where the cost of not stripping is the
            // tag read aloud: for "Team A | B1 | Thomas" the conservative rule
            // strips nothing (the first prefix has a space, so it fails the
            // alphanumeric test) and TTS would say the pipes. Same input,
            // different question -- so the rules differ on purpose. Identity
            // matching is not done here at all; the rider is already resolved
            // by getDisplayRaceNum().
            const char* name = entry->name;
            if (const char* sep = std::strstr(name, " | ")) {
                while (const char* more = std::strstr(sep + 3, " | ")) sep = more;
                if (sep[3] != '\0') name = sep + 3;
            }
            fill(v.riderName, name);
        }
    }

    const int pos = pd.getPositionForRaceNum(me);
    // {position} withheld from the session wrap-up when the finish cue has
    // already read it out this session -- "Checkered flag, P two, ..." then
    // "That's Race 1 done, P two" was the same number twice in two breaths.
    // The optional group drops and the wrap-up closes the session, not the
    // scoreboard.
    if (pos > 0 && !m_sessionEndHidePosition) {
        fill(v.position, SpotterPhrase::numberWords(pos));
    }

    const StandingsData* mine = pd.getStanding(me);
    if (mine) {
        fill(v.lapNumber, SpotterPhrase::numberWords(mine->numLaps + 1));
        fill(v.bestLapTime, SpotterPhrase::lapTimeWordsMs(mine->bestLap));
        // The standings' penalty column, in the same words as one penalty's
        // own amount. Rounded the way the web snapshot rounds it, so the two
        // never disagree by a second on the same figure.
        if (mine->penalty > 0) {
            fill(v.penaltyTotal,
                 SpotterPhrase::secondsWords((mine->penalty + 500) / 1000));
        }
        // A LAPPED rider's gap in milliseconds says nothing (standings carry
        // the real answer in gapLaps), so a seconds figure there would be a
        // confidently wrong number rather than a missing one.
        //
        // RACE ONLY, for the same reason the ahead/behind gaps are — see the
        // block below. A logged warmup ended with "Warmup complete, you
        // finished P eleven, SIXTY SIX POINT FOUR off the lead", and the
        // fastest lap of that warmup was one oh six point four: with no valid
        // lap of your own the column had handed back a figure of the leader's
        // lap time, not a gap to anybody.
        if (pos > 1 && pd.isRaceSession()) {
            fill(v.gapToLeader, gapWords(mine->gapLaps, mine->gap));
        }
        // Lap races ONLY: there sessionNumLaps is the distance, so laps left
        // is distance minus laps done. In a time+laps race it is the bonus
        // count instead, and that subtraction would report a number that
        // means nothing (2 - 5 on lap five). Overtime supplies the real
        // figure below, from the leader.
        if (running && lapped && !timed) {
            const int left = sd.sessionNumLaps - mine->numLaps;
            if (left > 0) {
                fill(v.lapsRemaining, SpotterPhrase::numberWords(left));
                fill(v.sessionRemaining, SpotterPhrase::lapsWords(left));
            }
        }
    }
    // Overtime overrides the arithmetic above: once the clock has expired the
    // laps remaining are the leader's, not a lap count nobody is running to.
    const int toGo = pd.getLeaderLapsToGo();
    if (toGo > 0) {
        fill(v.lapsRemaining, SpotterPhrase::numberWords(toGo));
        fill(v.sessionRemaining, SpotterPhrase::lapsWords(toGo));
    }

    // The setup you are on. The game reports a stock setup as "Default" and an
    // unnamed one as empty, which mean the same thing to a rider — so empty is
    // normalised rather than left blank, otherwise the one case worth warning
    // about is the one that says nothing.
    if (sd.setupFileName[0] != '\0') {
        // The game hands this over as a raw setup FILE name, and it arrives
        // prefixed: a real session logged ":Husk f". The part before the colon
        // is empty here and is not the setup's name in any case, so only the
        // human half is spoken. Conservative on purpose — a LEADING colon is
        // the artefact, so a setup genuinely called "Husk: fast" keeps its
        // name, and nothing is stripped from a name without one.
        const char* name = sd.setupFileName;
        if (*name == ':') {
            ++name;
            while (*name == ' ') ++name;
        }
        // A name that was ONLY the prefix falls through to "Default" below,
        // same as an empty one: both mean "not a setup I chose".
        if (*name != '\0') fill(v.setupName, name);
    }
    // The game reports a stock setup as "Default" and an unnamed one as empty,
    // which mean the same thing to a rider — so empty is normalised rather
    // than left blank, otherwise the one case worth warning about is the one
    // that says nothing.
    fill(v.setupName, "Default");

    // Your total race time, present only once you have actually finished:
    // it is what the classification carries at the flag, not a running clock.
    if (mine && mine->finishTime > 0) {
        fill(v.finishTime, SpotterPhrase::durationWords(mine->finishTime));
    }

    const IdealLapData* ideal = pd.getIdealLapData();
    if (ideal && !m_lapReportHideTime) {
        fill(v.lastLapTime, SpotterPhrase::lapTimeWordsMs(ideal->lastLapTime));
    }

    // Laps left in the tank, from the same history the Fuel widget shows —
    // documented as an ambient variable ("read from the live race at the
    // moment a cue fires") but only ever set by the fuel warnings, so
    // `hotkey_triggered = ...[, {fuel_laps} in the tank]` always dropped its
    // group. One source, one wording (lapsWords carries the noun).
    {
        const float fuelLaps =
            HudManager::getInstance().getFuelWidget().getLapsRemaining();
        if (fuelLaps >= 0.0f) {
            fill(v.fuelLaps,
                 SpotterPhrase::lapsWords(static_cast<int>(fuelLaps)));
        }
    }

    // The five reference laps TimingHud compares against, as times and as
    // your last lap's gap to each. Resolved from the same sources it uses so
    // the spotter cannot disagree with what is on screen; any of them being
    // absent (a track you have never ridden, an ideal lap with a sector
    // missing, a game with no records provider) leaves both variables empty,
    // which an optional group then drops.
    const int lastLap = ideal ? ideal->lastLapTime : -1;
    auto reference = [&](int refMs, std::string& timeOut, std::string& gapOut) {
        if (refMs <= 0) return;
        timeOut = SpotterPhrase::lapTimeWordsMs(refMs);
        if (lastLap <= 0) return;
        const int d = lastLap - refMs;
        // A lap compared with ITSELF says nothing. When the lap that just
        // finished IS the new best, the reference has already absorbed it and
        // the difference is zero -- which spoke as "zero point zero slower than
        // your best" on the very lap the session-best cue was celebrating.
        // Empty instead, so the optional group drops and the good news stands
        // on its own. bestLapIsFirstLap() guards the other end of the same
        // idea; this is the tie.
        if (d == 0) return;
        // The direction word rides WITH the number: spoken aloud, a bare
        // "point three" does not say which way it went, and making every
        // template add the word would be the same clause written many times.
        gapOut = SpotterPhrase::gapWordsMs(d) +
                 (d < 0 ? " quicker" : " slower");
    };
    // The lap BEFORE the last one, which is what "gap to last lap" compares
    // your latest against. Walked back through the log rather than kept as a
    // member: invalid laps are skipped, the same way TimingHud's Last Lap row
    // skips them, so the comparison is always against a lap that counted.
    if (const std::deque<LapLogEntry>* log = pd.getLapLog()) {
        int seen = 0;
        for (auto it = log->rbegin(); it != log->rend(); ++it) {
            // Both halves of "a lap that counted": a time exists AND the lap
            // was valid. Race-session cut laps arrive with the time preserved
            // and isValid=false, and skipping only the timeless ones made a
            // cut 1:20 the reference — the next clean lap read "twenty eight
            // point four slower" as {gap_to_last_lap}.
            if (it->lapTime <= 0 || !it->isValid) continue;
            if (++seen == 2) {
                // {last_lap_time} is the LAST lap; this one is the lap
                // before it, so only the gap half is wanted here.
                std::string unused;
                reference(it->lapTime, unused, v.gapToLastLap);
                break;
            }
        }
    }
    // {best_lap_time} is a FACT and is always filled; {gap_to_best_lap} is a
    // comparison, and an opening lap is not something to compare against
    // (bestLapIsFirstLap). Routed through a scratch string rather than skipped
    // so the time still reaches templates that ask for it.
    {
        std::string unusedGap;
        std::string& gapOut = bestLapIsFirstLap() ? unusedGap : v.gapToBestLap;
        if (const LapLogEntry* pb = pd.getBestLapEntry()) {
            reference(pb->lapTime, v.bestLapTime, gapOut);
        } else if (mine) {
            reference(mine->bestLap, v.bestLapTime, gapOut);
        }
    }
    if (ideal) reference(ideal->getIdealLapTime(), v.idealLapTime, v.gapToIdeal);
    reference(pd.getOverallBestLapTime(), v.overallBestTime, v.gapToOverall);
    reference(allTimeBestLapTime(), v.alltimeBestTime, v.gapToAlltime);
    reference(trackRecordLapTime(), v.recordTime, v.gapToRecord);

    // The riders either side, by classification order.
    //
    // ONE RULER for a gap in seconds: the stopwatch — their crossing of a
    // timing point against yours (spotter_pace.h). There used to be a second,
    // the difference between two riders' official gaps to the leader, served
    // here whenever the stopwatch had nothing. It was never the same
    // measurement, and the differences are not academic:
    //
    //   - each rider's gap column is refreshed at THEIR own crossing, so
    //     subtracting two of them only means something while both are fresh
    //     and on the same lap — which nothing here can check. A logged race
    //     had you at 566 and a rider one lap up at 31378 on the same table;
    //   - outside a race it is not a rider-to-rider figure at all (a warmup
    //     showed it NEGATIVE at -61925, then frozen at 2407/2259/2757 across
    //     six crossings, with gapLaps flat 0 for a rider two laps up), which
    //     is why it needed a race gate the stopwatch never did;
    //   - and the two cannot be compared, so a trend could not span them. Of
    //     three lap reports in one logged race, two spoke the estimate and one
    //     the stopwatch, and the trend was silent all race because of it.
    //
    // So the cached MEASUREMENT is what a cue between timing points serves,
    // and only while it still describes the rider currently there — which is
    // what Gap::raceNum is for. No measurement, or a new neighbour since it,
    // means the variable stays empty and the optional group drops whole,
    // rather than quoting a softer number in the same sentence shape.
    //
    // {rider_ahead}, {position_ahead} and their behind counterparts are just
    // the classification order and stay filled either way — they are as true
    // in practice as in a race. Only the seconds come from the stopwatch.
    auto lapsDown = [](const StandingsData* s) { return s ? s->gapLaps : 0; };
    const std::vector<int>& order = pd.getClassificationOrder();
    // Being a LAP down is a different question from being N seconds back, and
    // the standings answer it directly rather than by subtraction. Race-only
    // for the reason above; a false 0 there costs a phrase, never a wrong one.
    const bool lapsAreReal = pd.isRaceSession();
    auto measured = [](const SpotterPace::Gap& g, int num) {
        return g.raceNum == num && g.gapMs > 0
                   ? SpotterPhrase::gapWordsMs(g.gapMs) : std::string();
    };
    int aheadNum = -1, behindNum = -1;
    if (pos >= 2 && static_cast<size_t>(pos - 2) < order.size()) {
        const int num = order[pos - 2];
        aheadNum = num;
        fill(v.positionAhead, SpotterPhrase::numberWords(pos - 1));
        fill(v.riderAhead, SpotterPhrase::riderRef(num, -1));
        // Their last lap, from the same rider the NAME came from -- the live
        // order here, since this path has no stopwatch reading to be loyal to.
        // Unguarded by m_lapReportFreshGapsOnly on purpose: that switch is about
        // a cached GAP being read as a fresh crossing's fact, and a completed lap
        // time is not an estimate that goes stale between timing points.
        fill(v.lastLapAhead, lastLapWordsFor(num));
        const int lapDiff = lapsDown(mine) - lapsDown(pd.getStanding(num));
        if (lapsAreReal && lapDiff > 0) {
            fill(v.gapToAhead, SpotterPhrase::lapsWords(lapDiff));
        } else if (!m_lapReportFreshGapsOnly) {
            // Cached stopwatch reading, identity-guarded by Gap::raceNum --
            // right for a cue BETWEEN timing points (a hotkey ask mid-lap),
            // and held back for the lap report, which speaks at a crossing
            // and must not read a lap-old number as that crossing's fact.
            fill(v.gapToAhead, measured(m_lastAhead, num));
        }
    }
    if (pos >= 1 && static_cast<size_t>(pos) < order.size()) {
        const int num = order[pos];
        behindNum = num;
        fill(v.positionBehind, SpotterPhrase::numberWords(pos + 1));
        fill(v.riderBehind, SpotterPhrase::riderRef(num, -1));
        fill(v.lastLapBehind, lastLapWordsFor(num));
        const int lapDiff = lapsDown(pd.getStanding(num)) - lapsDown(mine);
        if (lapsAreReal && lapDiff > 0) {
            fill(v.gapToBehind, SpotterPhrase::lapsWords(lapDiff));
        } else if (!m_lapReportFreshGapsOnly) {
            fill(v.gapToBehind, measured(m_lastBehind, num));
        }
    }

    // Trends come from the pace tracker's last resolved report, so they mean
    // "since the last shared timing point" rather than "since the last frame"
    // — a per-frame trend on a noisy estimate would flip constantly.
    //
    // GUARDED BY raceNum, exactly as the gaps above are. A cached reading
    // outlives the crossing that produced it, and the rider ahead changes; with
    // no guard the DELTA came from the last rider measured while the NAME came
    // from the live standings, so the two described different people. A logged
    // race said "two point four gaining on rider twenty nine" at five
    // consecutive splits across two laps -- one reading taken against rider
    // twelve, re-attributed four times. A gap delta cannot repeat exactly, so
    // the tell was there to hear; nothing in the code could see it, because
    // Gap::raceNum was carried for this and only the gaps consulted it.
    if (!m_lapReportFreshGapsOnly &&
        m_lastAhead.hasTrend && m_lastAhead.raceNum == aheadNum) {
        fill(v.trendAhead, SpotterPhrase::trendAheadWord(m_lastAhead.deltaMs));
        fill(v.gainedOnAhead, SpotterPhrase::gapWordsMs(m_lastAhead.deltaMs));
    }
    if (!m_lapReportFreshGapsOnly &&
        m_lastBehind.hasTrend && m_lastBehind.raceNum == behindNum) {
        fill(v.trendBehind, SpotterPhrase::trendBehindWord(m_lastBehind.deltaMs));
        fill(v.gainedOnBehind, SpotterPhrase::gapWordsMs(m_lastBehind.deltaMs));
    }

    if (!order.empty()) fill(v.leaderName, SpotterPhrase::riderRef(order[0], me));

    // Places against each of StandingsHud's three PosGain references, so a
    // template names the one it means. Each is -1 where it does not apply —
    // no grid outside a race, no crossing before your first one — and stays
    // empty rather than reporting a change against nothing.
    auto places = [&](int refPos, std::string& out) {
        if (refPos <= 0 || pos <= 0 || refPos == pos) return;
        const int d = refPos - pos;
        fill(out, SpotterPhrase::numberWords(d < 0 ? -d : d));
    };
    places(pd.getRaceStartPosition(me), v.positionsSinceStart);
    places(pd.getSfReferencePosition(me), v.positionsSinceLap);
    places(pd.getSplitReferencePosition(me), v.positionsSinceSector);
}

void SpotterManager::emitCue(const char* key, SpotterPhrase::Category cat,
                             SpotterVars::Vars vars, int sessionTimeMs,
                             int riderNum, int timeComposed, int timeTenths,
                             int penaltySecs, int lapsLeft, int posValue) {
    // THE category gate. It lived at each emitter instead, which is to say it
    // was reimplemented twenty times — and three of those drifted: fuel never
    // checked at all, the hotkey cue skipped the check its sibling gate_drop
    // applies, and spectate_target checked General while the registry, the
    // shipped pack and the generated reference all filed it under Opponents.
    // Every one of them left a switch in the settings menu that did not
    // silence what it named.
    //
    // Here it cannot drift: the category a cue is emitted AS is now the same
    // one that mutes it, by construction. Emitters may still gate early to
    // skip expensive work — that is an optimisation now, not the contract.
    if (!isCategoryEnabled(cat)) {
#if defined(MXBMRP3_TEST_BUILD)
        // Swept with the other two returns rather than left because no test reads
        // it today: a route left standing from the last audible cue is the same
        // stale-seam class either way, and "no test reads it" is a fact about
        // today's tests, not about the seam.
        m_lastAudioRoute = std::string(key) + "|muted";
#endif
        return;
    }
    // The ambient half, read from live state rather than carried by the event
    // — this is what makes every variable usable in every cue. Filled here so
    // no emitter has to remember to, and so a new variable reaches every
    // template at once (spotter_vars.h).
    if (posValue > 0 && vars.position.empty()) {
        vars.position = SpotterPhrase::numberWords(posValue);
    }
    // ONE carve-out from that: penalty_you owns {penalty_total} outright,
    // including owning it when it decides to be EMPTY (see the Penalty branch
    // in onRaceEvent — a first penalty would only repeat the amount). The
    // ambient half reads the same standings column the tally exists to
    // outrun, and "empty" is indistinguishable from "unset" to it, so it
    // would put back exactly what that branch just declined to say.
    const bool ownsPenaltyTotal = std::strcmp(key, "penalty_you") == 0;
    std::string ownPenaltyTotal;
    if (ownsPenaltyTotal) ownPenaltyTotal = vars.penaltyTotal;
    fillAmbientVars(vars);
    if (ownsPenaltyTotal) vars.penaltyTotal = std::move(ownPenaltyTotal);
    // Variant pick: the base key plus any <key>_2.. alternates the pack
    // defines, chosen per firing (xorshift — statistical variety, not
    // crypto). With no pack variants this collapses to the base key.
    const std::vector<std::string> variants =
        SpotterCuePack::variantKeys(m_pack, key);
    std::string chosen = key;
    if (variants.size() > 1) {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        size_t pick = m_rngState % variants.size();
#if defined(MXBMRP3_TEST_BUILD)
        // Rolled anyway above, so the RNG advances identically whether or not
        // a test is pinning — a pinned case cannot shift what a later
        // unpinned one sees (see testPinVariant).
        if (m_pinVariant >= 0) {
            pick = static_cast<size_t>(m_pinVariant) < variants.size()
                 ? static_cast<size_t>(m_pinVariant)
                 : variants.size() - 1;
        }
#endif
        chosen = variants[pick];
    }

    // Text resolution: the chosen variant's phrase, else the base key's (a
    // wav-only variant inherits its subtitle). An empty pack value is an
    // explicit mute. The subtitle shows this text whichever backend plays.
    std::string text;
    auto it = m_pack.phrases.find(chosen);
    if (it == m_pack.phrases.end() && chosen != key) {
        it = m_pack.phrases.find(key);
    }
    // ...then the key this one refines, so a pack that wrote `gap_ahead` with
    // {trend_ahead} in it covers the trend cases too (spotter_cue_pack.h).
    if (it == m_pack.phrases.end()) {
        if (const char* base = SpotterCuePack::fallbackCueKey(key)) {
            it = m_pack.phrases.find(base);
        }
    }
    if (it != m_pack.phrases.end()) {
        text = SpotterCuePack::expand(it->second, vars);
    }
    // No row anywhere means SILENCE. There is deliberately no built-in to fall
    // back to — see reloadCuePack for what removing that copy bought.

    // Audio resolution ladder: mix (chunk stitch, when every placeholder has
    // a value) > wav > TTS — the chosen variant's OWN audio only (inherited
    // audio would defeat the variation). Chunk EXISTENCE is checked on the
    // worker (no file I/O here); the recipe carries the text as its fallback.
    //
    // Both lookups take the SAME refinement fallback the phrase takes, and for
    // the same contract (spotter_cue_pack.h: "a pack that says only
    // `session_started` means it for all of them"). Without it a pack that
    // wrote the general key got the general WORDS with no audio and dropped to
    // TTS — silence under Wine, which is the one place the recorded clip is
    // not a luxury.
    //
    // ONLY WHEN NO VARIANT WAS PICKED, which is the whole difference between
    // the two axes. The refinement axis is practice_started → session_started;
    // the VARIANT axis is practice_started_2, and the paragraph above says a
    // variant never borrows. Applied to a chosen variant this walked PAST the
    // variant's own parent to the general key — so a pack with
    // `practice_started_wav` and a `practice_started_2` text variant played the
    // session_started clip under the practice_started_2 subtitle, which is the
    // one outcome worse than falling back to TTS. When a variant is in play the
    // pack authored variants for this key and owns their audio; no audio for
    // the one that came up means TTS, deliberately.
    const char* base = (chosen == key) ? SpotterCuePack::fallbackCueKey(key) : nullptr;
    std::vector<std::string> mixFiles;
    auto mx = m_pack.mixes.find(chosen);
    if (mx == m_pack.mixes.end() && base) mx = m_pack.mixes.find(base);
    if (mx != m_pack.mixes.end()) {
        mixFiles = SpotterMix::resolveTokens(mx->second, riderNum,
                                             timeComposed, timeTenths,
                                             penaltySecs, lapsLeft, posValue);
    }
    const std::string* wavName = nullptr;
    auto wv = m_pack.wavs.find(chosen);
    if (wv == m_pack.wavs.end() && base) wv = m_pack.wavs.find(base);
    if (wv != m_pack.wavs.end() && !wv->second.empty()) {
        wavName = &wv->second;
    }
    // A template whose every variable came back empty expands to its own
    // PUNCTUATION — "[{rider_behind} is on your tail]." leaves ".", and
    // "P {position}." leaves "P." — because expand() tidies spacing but has
    // nothing to remove the fixed characters around the hole. Silence is what
    // an all-empty line means, so anything with no letter or digit in it is
    // treated as the empty string rather than queued for a voice to read.
    if (!text.empty()) {
        bool speakable = false;
        for (unsigned char c : text) {
            if (std::isalnum(c) || c >= 0x80) { speakable = true; break; }
        }
        if (!speakable) text.clear();
    }
#if defined(MXBMRP3_TEST_BUILD)
    // RECORDED HERE, above both returns, because both are answers: the silent
    // return below is the "this cue plays nothing" case, and the spoken-audio
    // gate further down is subtitles-only mode. Written after them, "silent" was
    // unreachable and a muted-audio session reported whatever the last audible
    // cue had chosen — a seam that goes stale is worse than no seam, because a
    // test reads it as an answer.
    m_lastAudioRoute = chosen + "|";
    if (!mixFiles.empty()) {
        m_lastAudioRoute += "mix:";
        for (size_t i = 0; i < mixFiles.size(); ++i) {
            if (i) m_lastAudioRoute += "+";
            m_lastAudioRoute += mixFiles[i];
        }
    } else if (wavName) {
        m_lastAudioRoute += "wav:" + *wavName;
    } else if (!text.empty()) {
        m_lastAudioRoute += "tts";
    } else {
        m_lastAudioRoute += "silent";
    }
#endif
    if (text.empty() && mixFiles.empty() && !wavName) return;  // silent

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings): every spoken cue, interleaved with the
    // regular log so a session reads as a transcript against the events that
    // produced it. Normally #ifdef MXBMRP3_TEST_BUILD; unguarded on purpose so
    // it appears in a RELEASE build alongside the STANDINGS probe below, which
    // is the whole point of the pairing — you cannot compare what the spotter
    // said with what the table showed if only one of them logs.
    // DELIBERATELY STILL HERE, by the author's decision: the spotter's timing is
    // still being retraced against the standings table, and this pairing is how
    // that is read. Reviews have flagged it as an oversight on four consecutive
    // passes, so it is written down — a choice with a known cost, not a leftover.
    //
    // REMOVE BEFORE RELEASE. The grep is the authority; this is the list, because
    // a bare number has been wrong three times running ("both", then "four", then
    // "five") and each time LOW:
    //
    //   1. this line (SPOTTER SAY), in spotter_manager_compose.cpp
    //   2. debugLogStandingsAt's FORWARD DECLARATION, above onRaceEvent in
    //      spotter_manager_events.cpp — the one every count so far has missed,
    //      and the one that does not fail quietly: delete the definition
    //      without it and a static function is declared and never defined,
    //      which the cross build takes as an error.
    //   3. its definition (spotter_manager_events.cpp)
    //   4. its call in the event tap (the finish; spotter_manager_events.cpp)
    //   5. its call in the split tap (spotter_manager_events.cpp)
    //   6. its call in the lap report (spotter_manager_events.cpp)
    //   ...and once all six are gone, the switch itself in
    //   spotter_manager_internal.h.
    //
    // COST, since these are staying a while: it is EVENT-rate, not frame-rate —
    // roughly four splits, a lap report and a handful of cues per lap — so the
    // 480fps steady state is not the worry. Each line is a mutex-held write with
    // an explicit flush on the GAME THREAD, so each one is a frame hitch where it
    // lands, and run_perf.sh drives no cues and cannot see them.
    DEBUG_INFO_F("SPOTTER SAY [%s] %s", chosen.c_str(), text.c_str());
#endif

    if (!text.empty()) {
        {
            MutexLock lock(m_mutex);
            m_cueLog.push_back({text, cat, sessionTimeMs});
            if (m_cueLog.size() > kCueLogCapacity) m_cueLog.pop_front();
        }
        m_cueLogRevision.fetch_add(1, std::memory_order_relaxed);
    }

    // Audio only with the spoken-audio master on: the cue log above is what
    // the subtitle widget shows, so subtitles-only mode ends here.
    if (!m_enabled) return;

    // Where a rider IS decays; what a rider DID does not. The proximity calls
    // are marked so the queue drops them rather than delivering them behind a
    // backlog — see isPerishableCue and spotter_queue.h.
    const bool perish = SpotterCuePack::isPerishableCue(key);

    if (!mixFiles.empty()) {
        SpotterCue cue;
        cue.kind = SpotterCue::Kind::MixSpec;
        cue.payload = text;            // the TTS fallback, if a chunk is missing
        cue.mixDir = m_packDir;
        cue.mixChunks = std::move(mixFiles);
        cue.perishable = perish;
        cue.enqueuedMs = GetTickCount64();
        enqueue(std::move(cue));
    } else if (wavName) {
        playWav(m_packDir + "\\" + *wavName, perish);
    } else {
        say(text, perish);
    }
}

// The voice_preview's fixed sample data. Not arbitrary: the point of a voice_preview is to
// expose what actually DIFFERS between packs, which is the stitching, not the
// timbre of a single word. A three-digit rider number and a lap time both come
// out of the number chunks, and since every pack ships the num_0..99 split set,
// 965 becomes "nine"+"sixty five" and 132 becomes "one"+"thirty two" — four
// joins in one line, which is exactly where a pack's `[Mix] gap_ms` is audible.
// A round number like 100 or a time like 1:30.0 would demonstrate none of it.
constexpr int kPreviewRider = 965;      // -> rider nine sixty five
constexpr int kPreviewComposed = 132;   // -> one thirty two  (1:32)
constexpr int kPreviewTenths = 4;       // -> point four

void SpotterManager::previewVoice(bool ttsOnly) {
    // Silent modes stay silent: this fires from a settings click, and a click
    // that makes noise with spoken audio off would be a bug, not a voice_preview.
    if (!m_enabled) return;

    // A preview REPLACES the one before it. Clicking through the voice list is
    // one click per voice and the previews queued behind each other, so a walk
    // down fifteen voices left the spotter reading fifteen samples out — in
    // fifteen different voices — long after the menu was closed. Nobody wants
    // to hear the ones they clicked past; they want the one they landed on.
    //
    // The queue is dropped whole rather than filtered for previews: nothing
    // else can be pending at a settings click except cues from a race that is
    // paused behind the menu, and those are stale by the time it closes.
    //
    // Both under ONE lock, with the flag set beside the clear rather than
    // after it. Apart, there is a window: the worker pops a normal cue and
    // releases the lock, then this runs before the worker reaches its own
    // clear — which then wipes the flag and speaks that cue to the end, with
    // the preview queued behind it. Together the two orderings are mutually
    // exclusive: either the worker popped first (the flag survives and
    // correctly interrupts what it just started) or this ran first (there is
    // nothing left to pop). The flag stays ATOMIC regardless — the poll loop
    // in speak() reads it without the lock, because it cannot hold one while
    // speaking. The lock orders it against the QUEUE, not against itself.
    {
        MutexLock lock(m_mutex);
        m_queue.clear();
        // ...and the one already SPEAKING is cut off, which the queue cannot do.
        m_interruptSpeech.store(true, std::memory_order_release);
    }

    const std::string riderWords =
        SpotterPhrase::riderRef(kPreviewRider, /*focusedRaceNum=*/-1);
    const std::string timeWords =
        SpotterPhrase::numberWords(kPreviewComposed) + " point " +
        SpotterPhrase::numberWords(kPreviewTenths);

    // A pack may write its own sample line, with its own recorded segments —
    // `voice_preview` / `preview_mix`, ordinary cue rows. Absent, the assembly below
    // uses ONLY chunks the pack format guarantees (rider.wav, num_*, point.wav),
    // so a voice_preview works for a pack whose author never heard of the key.
    // The voice_preview is a cue like any other, so it sees the same variables —
    // including the live ones. Writing `voice_preview = You're {position}, {gap_to_ahead}
    // to {rider_ahead}.` and hearing it with the real numbers is how you
    // check a template without waiting for the event that fires it.
    SpotterVars::Vars vars;
    vars.eventRider = riderWords;
    vars.eventTime = timeWords;
    fillAmbientVars(vars);

    std::string text;
    const auto ph = m_pack.phrases.find("voice_preview");
    if (ph != m_pack.phrases.end()) {
        text = SpotterCuePack::expand(ph->second, vars);
    } else {
        text = riderWords + ", " + timeWords + ".";
    }
    if (text.empty()) return;   // a pack may mute its own voice_preview

    std::vector<std::string> tokens;
    const auto mx = m_pack.mixes.find("voice_preview");
    if (ttsOnly) {
        // leave empty: fall through to say() below
    } else if (mx != m_pack.mixes.end()) {
        tokens = mx->second;
    } else if (!m_pack.wavs.empty() || !m_pack.mixes.empty()) {
        // A RECORDED pack with no voice_preview row of its own: stitch one from the
        // chunks the format guarantees. Gated on the pack having audio at all,
        // so the bundled text-only pack does not send the worker looking for
        // wav files that were never meant to exist.
        tokens = { "{event_rider}", "{event_time}" };
    }
    const std::vector<std::string> mixFiles = SpotterMix::resolveTokens(
        tokens, kPreviewRider, kPreviewComposed, kPreviewTenths,
        /*penaltySecs=*/-1, /*lapsLeft=*/-1, /*posValue=*/-1);

    // No cue-log entry and no subtitle: this is a settings-menu noise, not
    // something that happened in the race, and it must not land in the feed
    // the subtitle widget replays.
    if (!mixFiles.empty()) {
        SpotterCue cue;
        cue.kind = SpotterCue::Kind::MixSpec;
        cue.payload = text;            // the TTS fallback, if a chunk is missing
        cue.mixDir = m_packDir;
        cue.mixChunks = mixFiles;
        enqueue(std::move(cue));
    } else {
        say(text);   // no pack (TTS mode), or a pack that resolved to nothing
    }
}
