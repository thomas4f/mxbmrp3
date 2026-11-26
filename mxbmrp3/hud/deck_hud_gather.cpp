// ============================================================================
// hud/deck_hud_gather.cpp
// What each deck shows (deck_hud.h): its fields' text and tone and
// its meters, gathered once per rebuild into DeckHud::Rows. How the layout
// draws them is deck_hud_draw.cpp.
//
// Every value has an EMPTY form (the placeholder, toned TONE_EMPTY) so a deck
// keeps its shape before the data arrives; a field that only shows sometimes
// (a gap, a delta, a cue, a stamp) is left empty instead, and a layout box
// drawn `when` it shows disappears with it.
// ============================================================================
#include "deck_hud.h"
#include "deck_format.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../core/hud_manager.h"
#include "gap_bar_hud.h"
#include "gear_widget.h"
#include "../core/i18n.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/rider_stance.h"
#include "hud_defaults.h"
#include "../game/game_config.h"
#if GAME_HAS_FMX
#include "../core/fmx_levels.h"
#include "../core/fmx_manager.h"
#endif

using namespace PluginConstants;
using namespace DeckLayout;
using namespace DeckFormat;

namespace {
// The Wheelie deck's arc (0-90 across the half circle) and the Slide deck's
// meter, in degrees at full scale.
constexpr float TILT_SCALE = 90.0f;
constexpr float SLIP_SCALE = 60.0f;
}  // namespace

// ---- Speed -------------------------------------------------------------------

void DeckHud::gatherSpeed() {
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const SessionData& sd = pd.getSessionData();
    const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum();
    char buf[24];

    setText(TITLE, title(Kind::Speed));

    // Gear, read and rolled as the Gear widget's. Its colour comes from the
    // layout's text, set where the gear is drawn.
    GearWidget::formatGear(bike, buf, sizeof(buf));
    DigitRoll::stepGear(m_gearRoll, m_rollRaceNum, buf, pd.getDisplayRaceNum(), DigitRoll::nowUs(), 0);

    // Revs: hot from the shift point, as the Gear widget's. Telemetry is the
    // player's own; spectating, the revs are not the shown rider's, so the
    // meter stays out. The game's range or none, as the RPM and Bars widgets:
    // no invented one.
    const int top = sd.rpmRangeTop();
    const int shift = sd.shiftRPM;
    const int rpm = (bike.isValid && own) ? std::max(0, bike.rpm) : 0;
    const bool hot = GearWidget::atShiftPoint(bike, sd, own);
    setText(GEAR, buf, !bike.isValid ? TONE_EMPTY : (hot ? TONE_HOT : TONE_NORMAL));
    if (bike.isValid && own && top > 0) {
        DeckMeter::Data& m = m_rows.meter[RPM];
        m.show = true;
        m.value = clamp01(static_cast<float>(rpm) / static_cast<float>(top));
        m.hotFrom = shift > 0 ? clamp01(static_cast<float>(shift) / static_cast<float>(top)) : 1.0f;
    }

    // Speed in the Speed widget's unit (its label in capitals), and this lap's
    // top speed
    const bool kmh = speedKmh();
    snprintf(buf, sizeof(buf), "%s", UnitConversion::speedLabel(kmh));
    upper(buf);
    setText(UNIT, buf);
    if (bike.isValid) {
        snprintf(buf, sizeof(buf), "%d", std::clamp(UnitConversion::shownSpeed(bike.speedometer, kmh), 0, 999));
        setText(SPEED, buf);
    } else {
        setText(SPEED, Placeholders::GENERIC, TONE_EMPTY);
    }

    // The stance, a triangle each (the layout draws them over a space): standing
    // lights the up one, seated the down one. Only once the game sends it, and
    // only the player's own (it rides in the telemetry).
    if (RiderStance::gameSendsIt() && bike.isValid && own) {
        setText(STANCE_UP, " ", bike.riderStance == 1 ? TONE_NORMAL : TONE_EMPTY);
        setText(STANCE_DOWN, " ", bike.riderStance == 0 ? TONE_NORMAL : TONE_EMPTY);
    }
}

// ---- Race --------------------------------------------------------------------


void DeckHud::gatherRace() {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& sd = pd.getSessionData();
    const int raceNum = pd.getDisplayRaceNum();
    const StandingsData* mine = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
    const RiderTrackState* t = pd.getPlayerTrackPosition();
    char buf[PluginUtils::SESSION_CLOCK_CHARS];

    setText(TITLE, title(Kind::Race));

    // Position of the field, once there is something to rank; the session
    // under it
    const int pos = rankedPosition();
    setText(POS_LABEL, TR("POS"));
    if (pos > 0) {
        snprintf(buf, sizeof(buf), "%d/%d", pos, static_cast<int>(runningOrder().size()));
        setText(POS, buf);
    } else {
        setText(POS, Placeholders::GENERIC, TONE_EMPTY);
    }
    // The session, short enough for its column (the one session table)
    setText(SUB, I18n::trCached(PluginUtils::getSessionString(sd.eventType, sd.session, /*brief=*/true)));

    // The lap: of the race's laps in a lap race, and in a timed race once its
    // clock runs out (the laps left are known then); else just the lap.
    const bool timed = sd.sessionLength > 0;
    const bool lapRace = !timed && sd.sessionNumLaps > 0;
    const bool overtime = timed && m_race.clockOutLaps >= 0;
    const int lap = mine ? mine->numLaps + 1 : (t ? t->numLaps + 1 : 0);   // numLaps counts COMPLETED laps
    const int lastLap = lapRace ? sd.sessionNumLaps : (overtime ? m_race.clockOutLaps + 1 + sd.lapsAfterClock() : 0);
    setText(LAP_LABEL, TR("LAP"));
    if (lap <= 0) {
        setText(LAP, Placeholders::GENERIC, TONE_EMPTY);
    } else {
        if (lastLap > 0) snprintf(buf, sizeof(buf), "%d/%d", std::min(lap, lastLap), lastLap);
        else snprintf(buf, sizeof(buf), "%d", lap);
        setText(LAP, buf);
    }

    // Under it the session clock: the countdown in a timed session (its
    // extra laps as "+ 2L", then the clock's own overtime words); the time
    // run otherwise, held at the flag.
    if (timed) {
        PluginUtils::formatSessionClock(pd.getLeaderLapsToGo(), pd.getSessionClockTime(), sd.lapsAfterClock(),
                                        buf, sizeof(buf), /*shown=*/true, /*compact=*/true);
    } else {
        PluginUtils::formatTimeMinutesSeconds(pd.getSessionElapsedTime(), buf, sizeof(buf));
    }
    setText(LINE1, buf);

    // Progress: one part per lap in a lap race; the clock draining in a timed
    // one, then one part per lap left; the way round this lap otherwise.
    DeckMeter::Data& m = m_rows.meter[PROGRESS];
    m.show = true;
    const bool started = !(sd.sessionState & (SessionState::PRE_START | SessionState::SIGHTING_LAP));
    if (lapRace || overtime) {
        // Nothing is run before the green flag (the grid can sit just short of
        // the line, which would read as a lap nearly done). The position wraps
        // at the line before the lap count catches up, which would read a lap
        // back for a moment: the progress is held at its highest instead.
        const int parts = lapRace ? sd.sessionNumLaps : lastLap - m_race.clockOutLaps;
        if (t && started) {
            m_race.high = std::max(m_race.high, lapRace ? DeckMeter::raceProgress(t->numLaps, t->trackPos, parts)
                                                      : DeckMeter::raceProgress(t->numLaps - m_race.clockOutLaps, t->trackPos, parts));
        }
        m.value = m_race.high;
        DeckMeter::evenParts(m, parts);
    } else if (timed) {
        m.value = clamp01(static_cast<float>(pd.getSessionClockTime()) / static_cast<float>(sd.sessionLength));
        m.hotFrom = 0.0f;   // the layout's hot colour: time running out
    } else {
        m.value = t ? clamp01(t->trackPos) : 0.0f;
    }
}

// ---- Timing ------------------------------------------------------------------

void DeckHud::gatherTiming() {
    const PluginData& pd = PluginData::getInstance();
    const int64_t now = DigitRoll::nowUs();
    char buf[24];

    setText(TITLE, title(Kind::Timing));
    setText(CAPTION, TR("CURRENT"));

    // The lap ticking in tenths; for a while after each split and the line
    // the official time to it, in thousandths
    const int elapsed = pd.isLapTimerValid() ? pd.getElapsedLapTime() : -1;
    const bool hold = held(now) && m_timing.heldTime > 0;
    if (hold) {
        PluginUtils::formatLapTime(m_timing.heldTime, buf, sizeof(buf));
        setText(CURRENT, buf);
    } else if (elapsed >= 0) {
        PluginUtils::formatLapTimeTenths(elapsed, buf, sizeof(buf));
        setText(CURRENT, buf);
    } else {
        setText(CURRENT, Placeholders::GENERIC, TONE_EMPTY);
    }

    // The delta to the reference lap (General's), held at the crossings as
    // the Gap Bar and the Timing HUD hold it: green ahead, red behind
    int gap = 0;
    if ((elapsed >= 0 || hold) && m_freeze.shownGap(freezeRef(), &gap)) {
        formatDelta(buf, sizeof(buf), gap);
        setText(DELTA, buf, deltaTone(gap));
    }

    // While held, the sector's top speed and its difference from the best
    // through that sector this session
    if (hold && m_timing.trapMs > 0.0f) {
        const bool kmh = speedKmh();
        snprintf(buf, sizeof(buf), "%d %s", std::clamp(UnitConversion::shownSpeed(m_timing.trapMs, kmh), 0, 999),
                 UnitConversion::speedLabel(kmh));
        upper(buf);
        setText(TRAP, buf);
        if (m_timing.trapDiff != INT_MIN) {
            if (m_timing.trapDiff == 0) snprintf(buf, sizeof(buf), "\xB1" "0");   // CP1252 plus-minus
            else snprintf(buf, sizeof(buf), "%+d", std::clamp(m_timing.trapDiff, -999, 999));
            setText(TRAP_DIFF, buf, m_timing.trapDiff > 0 ? TONE_GOOD : (m_timing.trapDiff < 0 ? TONE_BAD : TONE_NORMAL));
        }
    }

    const IdealLapData* ideal = pd.getIdealLapData();
    const LapLogEntry* best = pd.getBestLapEntry();
    const int lastMs = ideal && ideal->lastLapTime > 0 ? ideal->lastLapTime : -1;
    const int bestMs = best && best->lapTime > 0 ? best->lapTime : -1;
    setText(LAST_LABEL, TR("LAST"));
    setText(BEST_LABEL, TR("BEST"));
    if (lastMs > 0) {
        PluginUtils::formatLapTime(lastMs, buf, sizeof(buf));
        setText(LAST, buf, lastMs == bestMs ? TONE_BEST : TONE_NORMAL);
    } else {
        setText(LAST, Placeholders::GENERIC, TONE_EMPTY);
    }
    if (bestMs > 0) {
        PluginUtils::formatLapTime(bestMs, buf, sizeof(buf));
        setText(BEST, buf, TONE_BEST);
    } else {
        setText(BEST, Placeholders::GENERIC, TONE_EMPTY);
    }

    fillSectorMeter(m_rows.meter[SECTORS]);
}

// Sectors: from the line, each split in lap order, to the line again; a
// sector run is green when it beat the best sector, red when it did not.
// The splits are the Gap Bar's: the game's markers (MX Bikes, Kart Racing
// Pro), else where the rider was seen crossing them (GP Bikes, whose markers
// are switched off in gpb_api.cpp).
void DeckHud::fillSectorMeter(DeckMeter::Data& m) const {
    const PluginData& pd = PluginData::getInstance();
    const int elapsed = pd.isLapTimerValid() ? pd.getElapsedLapTime() : -1;
    const IdealLapData* ideal = pd.getIdealLapData();
    m.show = true;
    float splits[SplitLearner::SLOTS];
    const int n = pd.collectSplitPositions(splits);
    float prev = 0.0f;
    for (int i = 0; i < n && m.parts < DeckMeter::MAX_PARTS - 1; ++i) {
        const float end = splits[i];
        if (end <= prev || end >= 1.0f) continue;   // two at one place: one cut
        m.partEnd[m.parts++] = prev = end;
    }
    m.partEnd[m.parts++] = 1.0f;
    if (m.parts == 1) m.parts = 0;   // no splits: one run
    const RiderTrackState* t = pd.getPlayerTrackPosition();
    m.value = elapsed >= 0 && t ? clamp01(t->trackPos) : 0.0f;
    const CurrentLapData* cur = pd.getCurrentLapData();
    if (cur && ideal && elapsed >= 0) {
        const int done[3] = { cur->split1, cur->split2, cur->split3 };
        const int bestSector[3] = { ideal->bestSector1, ideal->bestSector2, ideal->bestSector3 };
        int from = 0;
        for (int k = 0; k < 3 && k < m.parts - 1; ++k) {
            if (done[k] < 0) break;
            const int run = done[k] - from;
            from = done[k];
            if (bestSector[k] > 0) m.partTone[k] = run <= bestSector[k] ? TONE_GOOD : TONE_BAD;
        }
    }
}

// ---- Riding gauges -----------------------------------------------------------
//
// Wheelie, Slide and Spin read alike: the timer of what is going on now (the
// last one, dimmed, between), the session's longest as MAX, the gauge, and its
// value under it. The tone carries the cue, so no word is spelled out.

void DeckHud::setGaugeTimes(float t, bool active, float max) {
    char buf[24];
    formatSeconds(buf, sizeof(buf), t);
    setText(HOLD, buf, active ? static_cast<uint8_t>(TONE_NORMAL) : static_cast<uint8_t>(TONE_EMPTY));   // the last one greyed
    setText(BEST_LABEL, TR("MAX"));
    if (max > 0.0f) {
        formatSeconds(buf, sizeof(buf), max);
        setText(BEST, buf);
    } else {
        setText(BEST, Placeholders::GENERIC, TONE_EMPTY);
    }
}

void DeckHud::gatherWheelie() {
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum();
    char buf[24];

    setText(TITLE, title(Kind::Wheelie));
    const float angle = (bike.isValid && own && std::isfinite(bike.wheelieAngle)) ? std::max(bike.wheelieAngle, 0.0f) : 0.0f;
    // Plain values: the lit band and the needle carry the state
    snprintf(buf, sizeof(buf), "%d\xB0", static_cast<int>(angle + 0.5f));   // CP1252 degree sign
    setText(ANGLE, buf, m_wheelie.active ? static_cast<uint8_t>(TONE_NORMAL) : static_cast<uint8_t>(TONE_EMPTY));
    setGaugeTimes(m_wheelie.hold, m_wheelie.active, std::max(m_wheelie.best, m_wheelie.hold));

    // The arc, by the riding gauges' one rule: plain under the balance zone,
    // the zone, hot past it, danger past LOOP_FROM; the band holding the
    // needle lit
    DeckMeter::Data& tilt = m_rows.meter[TILT];
    tilt.show = true;
    tilt.band = true;
    tilt.idle = !m_wheelie.active;
    tilt.value = clamp01(angle / TILT_SCALE);
    tilt.zoneFrom = m_wheelie.zoneLo() / TILT_SCALE;
    tilt.zoneTo = m_wheelie.zoneHi() / TILT_SCALE;
    tilt.hotFrom = tilt.zoneTo;
    tilt.dangerFrom = DeckCoach::Wheelie::LOOP_FROM / TILT_SCALE;
    DeckMeter::bandParts(tilt);
}

void DeckHud::gatherSlide() {
    using DeckCoach::SlideBand;
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum();
    char buf[24];

    setText(TITLE, title(Kind::Slide));
    const float slip = (bike.isValid && own && std::isfinite(bike.slideAngle)) ? bike.slideAngle : 0.0f;
    // Plain values: the lit band and the needle carry the state
    snprintf(buf, sizeof(buf), "%d\xB0", static_cast<int>(std::fabs(slip) + 0.5f));   // CP1252 degree sign
    setText(ANGLE, buf, bike.isValid && own ? static_cast<uint8_t>(TONE_NORMAL) : static_cast<uint8_t>(TONE_EMPTY));
    setGaugeTimes(m_spell.t, m_spell.active, m_spell.max);

    // The pendulum, from the middle out: grip in the middle, then sliding
    // (hot) and save (danger) on either side. Positive is the rear out to the right, a
    // left-hand slide (core/slip_angle.h): the needle swings left for it.
    DeckMeter::Data& m = m_rows.meter[SLIP];
    m.show = true;
    m.centered = true;
    m.band = true;
    m.idle = !(bike.isValid && own);
    m.value = std::clamp(-slip / SLIP_SCALE, -1.0f, 1.0f);
    m.zoneFrom = 0.0f;
    m.zoneTo = DeckCoach::GRIP_BELOW / SLIP_SCALE;
    m.hotFrom = DeckCoach::GRIP_BELOW / SLIP_SCALE;
    m.dangerFrom = DeckCoach::SAVE_FROM / SLIP_SCALE;
    DeckMeter::bandParts(m);
}

// ---- Combo -------------------------------------------------------------------

void DeckHud::gatherCombo() {
    setText(TITLE, title(Kind::Combo));
    setText(CAPTION, TR("POINTS"));
    setText(LINE1_LABEL, TR("SCORE"));
    setText(LINE2_LABEL, TR("BEST"));
#if GAME_HAS_FMX
    const FmxManager& fmx = FmxManager::getInstance();
    const Fmx::TrickInstance& trick = fmx.getActiveTrick();
    const Fmx::FmxScore& sc = fmx.getScore();
    const Fmx::ChainHold& end = m_combo.hold;
    const std::vector<Fmx::TrickInstance>& chain = fmx.getChainTricks();
    char buf[48];

    // The trick turning now, once past its threshold; until the next starts,
    // an ended chain lingers (landed or lost), held while the rider is down
    const bool committed = Fmx::isTurning(trick);
    const bool ended = end.held && !committed && chain.empty();

    // The tricks, oldest first, a chip each: the chain's, then the one turning
    // (hot); an ended chain's all good (landed) or bad (lost). The newest
    // MAX_PILLS are kept, each with how far it has risen into the stack
    // (stepEvents counts them in).
    int total = 0;
    const auto add = [&](Fmx::TrickType type, uint8_t tone) {
        const int k = std::min(total, MAX_PILLS - 1);
        if (total >= MAX_PILLS) {
            for (int j = 1; j < MAX_PILLS; ++j) {
                std::memcpy(m_rows.pillWord[j - 1], m_rows.pillWord[j], sizeof(m_rows.pillWord[0]));
                m_rows.pillTone[j - 1] = m_rows.pillTone[j];
            }
        }
        // "WHIP", never "WHIP L": the side is the trick's business, not the chip's
        snprintf(m_rows.pillWord[k], sizeof(m_rows.pillWord[k]), "%s", Fmx::getTrickBaseName(type));
        upper(m_rows.pillWord[k]);
        m_rows.pillTone[k] = tone;
        ++total;
    };
    static_assert(Fmx::ChainHold::MAX_TRICKS >= MAX_PILLS, "a held chain keeps every chip shown");
    if (ended) {
        for (int i = 0; i < end.keptTricks(); ++i) add(end.tricks[i], end.success ? TONE_GOOD : TONE_BAD);
        total = end.trickCount;   // the chain's own count, past the ones kept
    } else {
        for (const Fmx::TrickInstance& t : chain) add(t.type, TONE_NORMAL);
        if (committed) add(trick.type, TONE_HOT);
    }
    const int n = std::min(total, MAX_PILLS);
    m_rows.pillCount = n;
    m_rows.pillFirst = total - n;   // each pill's rise: stepAnimations
    // The field holds the newest, so a layout can place by it
    if (n > 0) setText(CHIPS, m_rows.pillWord[n - 1]);

    // The trick's measurements: how long it lasted (TIME: a wheelie's is no
    // air time), how far it went, and the height and the turn of one that
    // flew or turned (a wheelie has neither)
    setText(TIME_LABEL, TR("TIME"));
    setText(DIST_LABEL, TR("DIST"));
    setText(HIGH_LABEL, TR("HIGH"));
    setText(ROT_LABEL, TR("ROT"));
    const Fmx::TrickStats& st = m_combo.trickStats;
    if (st.hasData) {
        snprintf(buf, sizeof(buf), "%.1fs", st.duration);
        setText(TIME, buf);
        snprintf(buf, sizeof(buf), "%.0fm", st.distance);
        setText(DIST, buf);
    } else {
        setText(TIME, Placeholders::GENERIC, TONE_EMPTY);
        setText(DIST, Placeholders::GENERIC, TONE_EMPTY);
    }
    if (st.hasData && st.hasHeight()) {
        snprintf(buf, sizeof(buf), "%.0fm", st.height);
        setText(HIGH, buf);
    } else {
        setText(HIGH, Placeholders::GENERIC, TONE_EMPTY);
    }
    if (st.hasData && st.hasRotation()) {
        snprintf(buf, sizeof(buf), "%d\xB0", static_cast<int>(st.rotation + 0.5f));   // CP1252 degree sign
        setText(ROT, buf);
    } else {
        setText(ROT, Placeholders::GENERIC, TONE_EMPTY);
    }

    // The points: the chain's so far with its multiplier while it runs (the
    // trick turning counts until it joins the chain: currentTrickScore stays
    // set after); what it banked once landed, nothing once lost
    const int count = sc.chainCount + (committed ? 1 : 0);
    if (ended) {
        PluginUtils::formatScore(end.bankedScore, buf, sizeof(buf));
        setText(HERO, buf, end.success ? TONE_GOOD : TONE_BAD);
    } else if (count > 0) {
        PluginUtils::formatScore(sc.chainScore + (committed ? sc.currentTrickScore : 0), buf, sizeof(buf));
        setText(HERO, buf);
        const float mult = fmx.calculateChainMultiplier(chain, committed ? trick.type : Fmx::TrickType::NONE);
        snprintf(buf, sizeof(buf), "x%.1f", std::isfinite(mult) ? std::clamp(mult, 0.0f, 99.9f) : 1.0f);
        setText(MULT, buf);
    } else {
        setText(HERO, "0", TONE_EMPTY);
    }

    // The session score, rolling up to the real one (update() steps it), and
    // the best combo
    PluginUtils::formatScore(m_combo.shownScore, buf, sizeof(buf));
    setText(LINE1, buf, m_combo.shownScore > 0 ? TONE_NORMAL : TONE_EMPTY);
    if (sc.bestComboScore > 0) {
        PluginUtils::formatScore(sc.bestComboScore, buf, sizeof(buf));
        setText(LINE2, buf, TONE_BEST);
    } else {
        setText(LINE2, Placeholders::GENERIC, TONE_EMPTY);
    }

    // The chain's time, shrinking to the middle: full (lit) while a trick
    // turns, then the time left (hot), the last of it blinking (danger)
    DeckMeter::Data& left = m_rows.meter[CHAIN_TIME];
    left.show = true;
    left.centered = left.mirrored = true;
    const float period = fmx.getConfig().chainPeriod;
    if (committed) {
        left.value = 1.0f;
    } else if (sc.chainCount > 0 && period > 0.0f) {
        left.value = clamp01(1.0f - sc.chainElapsed / period);
        left.litAs = left.value >= CHAIN_LOW ? DeckMeter::HOT_LIT
                   : chainBlinkOn(DigitRoll::nowUs()) ? DeckMeter::DANGER_LIT : DeckMeter::DANGER_DIM;
    }
#else
    setText(HERO, "0", TONE_EMPTY);
#endif
}

// ---- Stamp -------------------------------------------------------------------

void DeckHud::gatherStamp() {
    char buf[24];
    switch (m_stamp) {
    case Stamp::None:       return;
    case Stamp::NewBest:    setText(STAMP, TR("NEW BEST"), TONE_BEST); return;
    case Stamp::FinalLap:   setText(STAMP, TR("FINAL LAP"), TONE_HOT); return;
    case Stamp::PlacesUp:
        snprintf(buf, sizeof(buf), "+%d", m_stampValue);
        setText(STAMP, buf, TONE_GOOD);
        return;
    case Stamp::PlacesDown:
        snprintf(buf, sizeof(buf), "-%d", m_stampValue);
        setText(STAMP, buf, TONE_BAD);
        return;
    case Stamp::Landed: {
        char score[16];
        PluginUtils::formatScore(m_stampValue, score, sizeof(score));
        snprintf(buf, sizeof(buf), "%s +%s", TR("LANDED"), score);
        setText(STAMP, buf, TONE_GOOD);
        return;
    }
    case Stamp::Bailed:     setText(STAMP, TR("BAILED"), TONE_BAD); return;
    case Stamp::AllTimePb:  setText(STAMP, TR("ALL-TIME PB"), TONE_NORMAL); return;
    case Stamp::FastestLap: setText(STAMP, TR("FASTEST LAP"), TONE_BEST); return;
    case Stamp::SessionPb:  setText(STAMP, TR("SESSION PB"), TONE_GOOD); return;
    case Stamp::BestSector:
        snprintf(buf, sizeof(buf), TR("BEST S%d"), m_stampValue);
        setText(STAMP, buf, TONE_BEST);
        return;
    case Stamp::RivalGained:
    case Stamp::RivalLost: {
        char gap[16];
        formatHundredths(gap, sizeof(gap), m_stampValue);
        const bool gained = m_stamp == Stamp::RivalGained;
        snprintf(buf, sizeof(buf), gained ? TR("GAINED %s") : TR("LOST %s"), gap);
        setText(STAMP, buf, gained ? TONE_GOOD : TONE_BAD);
        return;
    }
    case Stamp::Reaction:
        snprintf(buf, sizeof(buf), TR("REACT %.2f"), static_cast<float>(std::min(m_stampValue, 9990)) / 1000.0f);
        setText(STAMP, buf, m_stampTone);
        return;
    case Stamp::SessionType:
    case Stamp::SessionState: {
        // The new one, as the Session HUD names it, shouted
        const SessionData& sd = PluginData::getInstance().getSessionData();
        const char* name = m_stamp == Stamp::SessionType
            ? PluginUtils::getSessionString(sd.eventType, sd.session)
            : PluginUtils::getSessionStateString(sd.sessionState);
        if (!name) return;
        char shout[sizeof(m_rows.text[0])];
        snprintf(shout, sizeof(shout), "%s", I18n::trCached(name));
        upper(shout);
        setText(STAMP, shout, TONE_NORMAL);
        return;
    }
    }
}
