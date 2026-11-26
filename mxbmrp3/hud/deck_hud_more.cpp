// ============================================================================
// hud/deck_hud_more.cpp
// The later decks (deck_hud.h): Gap, Rival, Spin, Inputs, Session and
// Fuel (Standings has its own file, deck_hud_standings.cpp), Speed's
// reaction (the gate drop), and the crossings Timing shares
// with Gap and Rival. Their state steps here (on telemetry and on
// events) and what they show is gathered here; the rules are deck_score.h,
// the drawing is the layout's, as for every deck.
//
// One file for all of them, so a deck that goes is its Kind, its section here,
// its rows in each deck.ini, its Decks tab row and its registry line.
// ============================================================================
#include "deck_hud.h"
#include "deck_format.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/hud_manager.h"
#include "speed_widget.h"
#include "fuel_widget.h"
#include "../core/i18n.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "hud_defaults.h"
#include "standings_gap_plan.h"
#include "../game/game_config.h"
#if GAME_HAS_FMX
#include "../core/fmx_manager.h"
#endif

using namespace PluginConstants;
using namespace DeckLayout;
using namespace DeckFormat;

bool DeckHud::speedKmh() {
    return HudManager::getInstance().getSpeedWidget().getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
}

namespace {
// The range of the Rival deck's marks, either side of the player
constexpr int RIVAL_RANGE_MS = 3000;
// The Gap deck's bar: full at a second either way
constexpr int GAP_RANGE_MS = 1000;
// Rival: the rider behind takes over from the one ahead only when closer by this
constexpr int DEFEND_HYSTERESIS_MS = 100;

// A spin share as the whole percent shown; -1 for none
int spinPercent(float share) {
    return share >= 0.0f && std::isfinite(share) ? static_cast<int>(std::lround(share * 100.0f)) : -1;
}
}  // namespace

// ---- State -------------------------------------------------------------------

void DeckHud::resetMore() {
    m_crossings.reset();
    m_freeze.reset();
    m_holdUntilUs = 0;
    m_crossMs = -1;   // mid-lap until the line: the first sector seen is not whole
    m_crossSector = -1;
    m_gap = GapState{};
    m_speed = SpeedState{};
    m_speed.wasAwaitingDrop = PluginData::getInstance().isAwaitingGateDrop();
    m_spinShown = -1.0f;
    m_rival = RivalState{};
}

// On each fresh telemetry sample of the player's own bike
void DeckHud::stepMoreTelemetry(float dt, bool crashed) {
    const BikeTelemetryData& bike = PluginData::getInstance().getBikeTelemetry();
    const float speed = std::isfinite(bike.speedometer) ? std::max(bike.speedometer, 0.0f) : 0.0f;
    if (m_kind == Kind::Spin) {
        // A crash is no reading; off the ground the rear wheel reads 0 (core/
        // wheel_spin.h). The timer runs from the hook band's floor up
        const float share = crashed ? -1.0f : DeckScore::spinShare(bike.rearWheelSpeed, speed);
        m_spinShown = DeckScore::easeSpin(m_spinShown, share, dt);
        m_spell.step(m_spinShown >= DeckScore::HOOK_FROM, dt);
    }
}

// A split or the line, for Timing, Gap and Rival: the hold after it (the
// General tab's freeze; 0 = off), the sector just run. True when one was
// crossed this frame.
bool DeckHud::stepCrossings(int64_t nowUs) {
    const int holdMs = HudDefaults::freezeMs(FreezeDuration::FOLLOW_DEFAULT);
    if (m_kind != Kind::Rival) stepFreeze();
    const SplitCrossingDetector::Result r = m_crossings.poll();
    if (r.adopted) {
        m_crossMs = -1;
        m_crossSector = -1;
        m_timing.trapRunMs = 0.0f;
        return false;
    }
    int sector = -1, at = -1;
    bool valid = true;
    if (r.line) {
        sector = SplitCrossingDetector::NUM_SPLITS;   // the lap's last
        at = r.lapTime;
        valid = r.lapValid;
    } else if (r.splitIndex >= 0) {
        sector = r.splitIndex;
        at = r.splitTime;
    } else {
        return false;
    }
    // A sector is whole only from the crossing just before it: a split the game
    // never reported would fold two sectors into one run
    const int before = sector == 0 ? SplitCrossingDetector::NUM_SPLITS : sector - 1;
    const int run = m_crossMs >= 0 && m_crossSector == before && at > m_crossMs ? at - m_crossMs : -1;
    m_crossMs = r.line ? 0 : (at > 0 ? at : -1);
    m_crossSector = sector;
    m_timing.heldTime = at;
    m_holdUntilUs = holdMs > 0 && at > 0 ? nowUs + static_cast<int64_t>(holdMs) * 1000 : 0;
    onSector(sector, run, valid);
    setDataDirty();
    return true;
}

// A sector run (-1: not whole): Timing's top speed through it, Gap's run
// against its best. An invalid lap's last sector sets no best.
void DeckHud::onSector(int sector, int runMs, bool valid) {
    if (sector < 0 || sector >= MAX_SECTORS) return;
    if (m_kind == Kind::Timing) {
        m_timing.trapMs = runMs > 0 ? m_timing.trapRunMs : 0.0f;
        m_timing.trapRunMs = 0.0f;
        m_timing.trapDiff = INT_MIN;
        if (m_timing.trapMs <= 0.0f) return;
        const bool kmh = speedKmh();
        const float best = m_timing.trapBestMs[sector];
        if (best > 0.0f)
            m_timing.trapDiff = UnitConversion::shownSpeed(m_timing.trapMs, kmh) - UnitConversion::shownSpeed(best, kmh);
        if (valid && m_timing.trapMs > best) m_timing.trapBestMs[sector] = m_timing.trapMs;
    } else if (m_kind == Kind::Gap) {
        // Against PluginData's best of that sector, as Timing, Lap Log and the
        // Pitboard read it (valid laps, the whole session, shown or not). The
        // bests move only when a lap completes, so the line's run is set
        // against them as they stood at the split before it: by now its own
        // lap may already hold a new best there
        const IdealLapData* ideal = PluginData::getInstance().getIdealLapData();
        const int bests[MAX_SECTORS] = { ideal ? ideal->bestSector1 : -1, ideal ? ideal->bestSector2 : -1,
                                         ideal ? ideal->bestSector3 : -1, ideal ? ideal->bestSector4 : -1 };
        const bool line = sector == SplitCrossingDetector::NUM_SPLITS;
        const int best = line ? m_gap.lapBests[sector] : bests[sector];
        std::memcpy(m_gap.lapBests, bests, sizeof(bests));
        if (runMs <= 0 || !valid) return;
        m_gap.lastSector = sector;
        m_gap.lastRun = runMs;
        m_gap.lastRunHadBest = best > 0;
        m_gap.lastRunDelta = best > 0 ? runMs - best : 0;
        if (best > 0 && runMs < best) raiseStamp(Stamp::BestSector, sector + 1);
    }
}

const std::vector<int>& DeckHud::runningOrder() {
    return PluginData::getInstance().getRunningOrder().get();
}

void DeckHud::refreshRank() {
    m_rank = RankCache{};
    const PluginData& pd = PluginData::getInstance();
    const int raceNum = pd.getDisplayRaceNum();
    if (raceNum <= 0 || runningOrder().size() <= 1) return;
    const int pos = PluginData::getInstance().getRunningOrder().positionOf(raceNum);
    if (pos <= 0) return;
    if (!pd.isRaceSession()) {
        // Ranked by best lap: nothing to rank before one is set
        const StandingsData* s = pd.getStanding(raceNum);
        if (s && s->bestLap > 0) m_rank.pos = pos;
        return;
    }
    m_rank.pos = pos;
    // The Standings gap rule, measured from the player, every row planned
    // (gapToPlayer): the decks' gaps are always to you
    m_rank.table = RunningOrder::gapTable(pd.getDisplayClassificationOrder(), raceNum,
        StandingsGap::Reference::PLAYER, StandingsGap::Scope::ALL);
    m_rank.table.playerRowIndex = pos - 1;
    m_rank.hasTable = true;
}

// The rider fighting the player: in a race the one ahead, or the one behind
// when closer (on the grid, the one ahead); outside races, ranked by best
// lap, the one ahead (leading, the one behind). The gap at each split is
// held, and its change since the last one is tagged.
void DeckHud::stepRival(int64_t nowUs) {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& sd = pd.getSessionData();
    const int raceNum = pd.getDisplayRaceNum();
    const StandingsData* mine = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
    const int pos = rankedPosition();
    const std::vector<int>& order = runningOrder();
    const int field = static_cast<int>(order.size());
    int rival = -1;
    bool defending = false;
    int gapAhead = INT_MAX, gapBehind = INT_MAX;
    if (mine && pos > 0 && pos <= field) {
        const int ahead = pos > 1 ? order[static_cast<size_t>(pos - 2)] : -1;
        const int behind = pos < field ? order[static_cast<size_t>(pos)] : -1;
        const auto gap = [&](int p) {
            int laps = 0;
            const int g = gapToPlayer(p, &laps);
            return g == INT_MAX ? INT_MAX : std::abs(g);
        };
        gapAhead = gap(pos - 1);
        gapBehind = gap(pos + 1);
        if (pd.isRaceSession()) {
            const bool started = !(sd.sessionState & (SessionState::PRE_START | SessionState::SIGHTING_LAP)) &&
                                 !pd.isAwaitingGateDrop();
            const int margin = m_rival.defending ? -DEFEND_HYSTERESIS_MS : DEFEND_HYSTERESIS_MS;
            defending = ahead <= 0 || (started && behind > 0 && gapBehind != INT_MAX &&
                                       (gapAhead == INT_MAX || gapBehind + margin < gapAhead));
        } else {
            defending = pos == 1;
        }
        rival = defending ? behind : ahead;
    }
    m_rival.gapAhead = gapAhead == INT_MAX ? -1 : gapAhead;
    m_rival.gapBehind = gapBehind == INT_MAX ? -1 : gapBehind;
    if (rival != m_rival.num || defending != m_rival.defending) {
        m_rival.num = rival;
        m_rival.defending = defending;
        m_rival.heldGap = -1;
        setDataDirty();
    }
    if (rival <= 0) return;

    // At each of the player's crossings in a race: the gap held, and what it
    // gained or lost since the last one against the same rider (a change
    // that rounds to 0.00 is no news). Shrinking is gained chasing, lost
    // defending.
    if (stepCrossings(nowUs) && pd.isRaceSession()) {
        const int g = defending ? gapBehind : gapAhead;
        const int before = m_rival.heldGap;
        m_rival.heldGap = g != INT_MAX ? g : -1;
        if (before >= 0 && m_rival.heldGap >= 0) {
            const int change = m_rival.heldGap - before;
            if (std::abs(change) >= RIVAL_TAG_MIN_MS)
                raiseStamp((change < 0) != defending ? Stamp::RivalGained : Stamp::RivalLost, std::abs(change));
        }
    }
}

// The gate drops when the grid hold ends with the race running. The
// classification that reports it arrives a while after it, at the server's
// rate (every 28 ms to every 240 ms in the recorded races), so timing from its
// arrival cut the reaction by up to that much (0.08 s read in game). Its
// session time counts from the drop (down from the length in a timed race):
// the drop is back-dated by it.
void DeckHud::detectDrop(int64_t nowUs) {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& sd = pd.getSessionData();
    const bool awaiting = pd.isAwaitingGateDrop();
    if (m_speed.wasAwaitingDrop && !awaiting && (sd.sessionState & SessionState::IN_PROGRESS) != 0) {
        const int t = pd.getSessionTime();
        const int since = sd.sessionLength > 0 ? std::min(t, std::abs(sd.sessionLength - t)) : t;
        const int lagMs = std::clamp(since, 0, DeckScore::MAX_DROP_LAG_MS);
        m_speed.dropUs = nowUs - static_cast<int64_t>(lagMs) * 1000;
        m_speed.reaction = -1.0f;
        setDataDirty();
    }
    m_speed.wasAwaitingDrop = awaiting;
}

// The live gap to the reference lap (General's), held at the crossings as
// the Gap Bar and the Timing HUD hold it: Timing's delta, Gap's hero, the
// Lap Log's running time
PbGapTracker::Ref DeckHud::freezeRef() { return HudDefaults::reference(true, PbGapTracker::Ref::SESSION_PB); }

void DeckHud::stepFreeze() {
    if (m_freeze.update(freezeRef(), HudDefaults::freezeMs(FreezeDuration::FOLLOW_DEFAULT))) setDataDirty();
}

// The Lap Log's running time: green ahead of the reference lap, red behind
uint8_t DeckHud::runningTone() const {
    int gap = 0;
    return m_freeze.shownGap(freezeRef(), &gap) ? deltaTone(gap) : TONE_NORMAL;
}

// Every frame the deck is shown: a handful of compares, nothing formatted
void DeckHud::stepMoreEvents(int64_t nowUs) {
    if (m_kind == Kind::Gap) stepCrossings(nowUs);
    if (m_kind == Kind::LapLog) stepFreeze();
    if (m_kind == Kind::Rival) stepRival(nowUs);
    // Inputs draws the Telemetry HUD's history (HudManager::isTelemetryHistoryNeeded):
    // fresh on its show edge, unless another user is drawing it
    if (m_kind == Kind::Inputs && !m_wasHistoryUser) {
        m_wasHistoryUser = true;
        HudManager::getInstance().startTelemetryHistory(this);
    }
}

// What each later deck shows, at the resolution it is shown at
void DeckHud::computeMoreKey(int (&key)[KEY_SIZE]) const {
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const int raceNum = pd.getDisplayRaceNum();
    const StandingsData* mine = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
    switch (m_kind) {
    case Kind::Gap: {
        const int64_t now = DigitRoll::nowUs();
        int gap = 0;
        key[0] = m_freeze.shownGap(freezeRef(), &gap) ? gap / 10 : INT_MIN;
        key[1] = m_gap.lastSector * 100000 + std::min(m_gap.lastRun, 99999);
        key[2] = m_gap.lastRunDelta;
        key[3] = static_cast<int>(freezeRef());
        const IdealLapData* ideal = pd.getIdealLapData();
        key[4] = ideal ? ideal->getIdealLapTime() : -1;
        key[5] = pd.getPbGapTracker().bestLapTimeMs(freezeRef());
        key[6] = held(now) ? 1 : 0;
        break;
    }
    case Kind::Rival: {
        const int64_t now = DigitRoll::nowUs();
        key[0] = m_rival.num * 2 + (m_rival.defending ? 1 : 0);
        key[1] = held(now) ? m_rival.heldGap / 10 : -2;
        key[2] = rivalLastLap();
        key[3] = mine ? mine->bestLap : -1;
        key[4] = rankedPosition();
        int ahead = -1, behind = -1, shown = -1;
        rivalGaps(&ahead, &behind, &shown);
        key[5] = ahead / 30;
        key[6] = behind / 30;
        key[7] = shown / 10;
        break;
    }
    case Kind::Inputs: {
        // Every sample moves the lines: the history's revision says so
        const InputTelemetryData& in = pd.getInputTelemetry();
        key[0] = static_cast<int>(pd.getHistoryBuffers().revision);
        key[1] = std::isfinite(in.throttle) ? static_cast<int>(in.throttle * 100.0f) : 0;
        key[2] = std::isfinite(in.frontBrake) ? static_cast<int>(in.frontBrake * 100.0f) : 0;
        break;
    }
    case Kind::Standings: {
        // Who is in each row, and each gap at the hundredth it is shown at.
        // Mostly belt-and-braces: a new classification and a moved live gap
        // both notify Standings, which dirties the deck anyway. Not quite all:
        // a rider leaving the track-position batch swaps their live gap for
        // the official one with no notification, and only this key sees that.
        int places[MAX_ROWS];
        int breakAt = -1;
        const int n = standingsPlaces(m_listRows, places, &breakAt);
        const std::vector<int>& order = runningOrder();
        uint32_t who = 2166136261u, gaps = 2166136261u, pens = 2166136261u;
        for (int r = 0; r < n; ++r) {
            int laps = 0;
            const int gap = gapToPlayer(places[r], &laps);
            const int num = order[static_cast<size_t>(places[r] - 1)];
            who = (who ^ static_cast<uint32_t>(num)) * 16777619u;
            const StandingsData* s = pd.getStanding(num);
            // Outside a race each row shows its best lap instead
            const int shown = !pd.isRaceSession() ? (s ? s->bestLap : -1) : (gap == INT_MAX ? laps * 7 + 3 : gap / 10);
            gaps = (gaps ^ static_cast<uint32_t>(shown)) * 16777619u;
            pens = (pens ^ static_cast<uint32_t>(s ? s->penalty : 0)) * 16777619u;
        }
        key[0] = rankedPosition();
        key[1] = static_cast<int>(who);
        key[2] = static_cast<int>(gaps);
        key[3] = n * 100 + breakAt;
        key[4] = static_cast<int>(pens);
        break;
    }
    case Kind::Fuel: {
        // At the resolution shown: a tenth of a litre and of a lap, the
        // average to the hundredth, the tank's blocks to a sixteenth of one
        const FuelWidget& fuel = HudManager::getInstance().getFuelWidget();   // its unit
        const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum();
        key[0] = (bike.isValid ? 1 : 0) + (own ? 2 : 0);
        key[1] = quantize(bike.fuel, 0.1f);
        key[2] = quantize(pd.getFuelLapsRemaining(), 0.1f);
        key[3] = quantize(pd.getFuel().averagePerLap(), 0.01f);
        key[4] = bike.maxFuel > 0.0f ? static_cast<int>(bike.fuel / bike.maxFuel * 256.0f) : -1;
        key[5] = static_cast<int>(fuel.getFuelUnit());
        break;
    }
    case Kind::LapLog: {
        // The lap running in tenths and the sectors it has crossed; the
        // newest lap logged, the best lap and the best sectors (the tones)
        const std::deque<LapLogEntry>* log = pd.getLapLog();
        const CurrentLapData* cur = pd.getCurrentLapData();
        const LapLogEntry* best = pd.getBestLapEntry();
        const IdealLapData* ideal = pd.getIdealLapData();
        key[0] = pd.isLapTimerValid() ? pd.getElapsedLapTime() / 100 : -1;
        key[1] = cur ? cur->split1 : -1;
        key[2] = cur ? cur->split2 : -1;
        key[3] = log && !log->empty() ? log->front().lapNum * 2 + (log->front().isComplete ? 1 : 0) : -1;
        key[4] = log ? static_cast<int>(log->size()) : 0;
        key[5] = best ? best->lapTime : -1;
        key[6] = ideal ? ideal->getIdealLapTime() : -1;
        key[7] = (pd.isLapViaPits(pd.getDisplayRaceNum()) ? 1 : 0) + (pd.isDisplayRiderFinished() ? 2 : 0);
        key[8] = pd.getLapTimerCurrentLap();
        key[9] = runningTone();
        break;
    }
    case Kind::Media:
        // The worker bumps its revision whenever what is shown moves (once a
        // second while playing); the pointer over a control, and a drag
        key[0] = static_cast<int>(MediaManager::getInstance().revision());
        key[1] = m_media.controls.hover();
        key[2] = m_media.controls.dragVolume();
        key[3] = static_cast<int>(m_media.controls.dragSeek() * 10000.0f);
        key[4] = MediaManager::getInstance().isEnabled() ? 1 : 0;
        break;
    case Kind::Spin:
        key[0] = spinPercent(m_spinShown);
        key[1] = static_cast<int>(m_spell.t * 10.0f);
        key[2] = static_cast<int>(m_spell.max * 10.0f);
        key[3] = bike.isValid ? 1 : 0;
        key[4] = m_spell.active ? 1 : 0;
        break;
    default:
        break;
    }
}

// ---- What they show ----------------------------------------------------------

void DeckHud::gatherInputs() {
    // The throttle and the front brake, each a column filled to now and its
    // history as a line (the layout's kinds: column, trace); no words but the
    // title, the colours match the lines. The game sends both while
    // spectating too.
    setText(TITLE, title(Kind::Inputs));
    const InputTelemetryData& in = PluginData::getInstance().getInputTelemetry();
    const float values[2] = { in.throttle, in.frontBrake };
    const DeckLayout::Meter meters[2] = { THROTTLE, FRONT_BRAKE };
    for (int i = 0; i < 2; ++i) {
        DeckMeter::Data& m = m_rows.meter[meters[i]];
        m.show = true;
        m.value = std::isfinite(values[i]) ? std::clamp(values[i], 0.0f, 1.0f) : 0.0f;
    }
}

void DeckHud::gatherSpin() {
    // A riding gauge (deck_hud_gather.cpp): the slip's timer and MAX, the
    // block meter lit to the slip (hooked green, too much amber, spinning red), the
    // percent plain
    setText(TITLE, title(Kind::Spin));
    char buf[24];

    DeckMeter::Data& m = m_rows.meter[WHEELSPIN];
    m.show = true;
    m.zoneFrom = DeckScore::HOOK_FROM / DeckScore::SPIN_SCALE;
    m.zoneTo = DeckScore::HOOK_TO / DeckScore::SPIN_SCALE;
    m.hotFrom = DeckScore::HOOK_TO / DeckScore::SPIN_SCALE;
    m.dangerFrom = DeckScore::SPIN_FROM / DeckScore::SPIN_SCALE;
    m.value = 0.0f;

    const PluginData& pd = PluginData::getInstance();
    if (m_spinShown >= 0.0f && pd.getBikeTelemetry().isValid && pd.getDisplayRaceNum() == pd.getPlayerRaceNum()) {
        snprintf(buf, sizeof(buf), "%d%%", spinPercent(m_spinShown));
        setText(HERO, buf);   // plain: the lit blocks carry the state
        m.value = clamp01(m_spinShown / DeckScore::SPIN_SCALE);
    } else {
        setText(HERO, Placeholders::GENERIC, TONE_EMPTY);
    }
    setGaugeTimes(m_spell.t, m_spell.active, m_spell.max);
}

void DeckHud::gatherGap() {
    const PluginData& pd = PluginData::getInstance();
    const PbGapTracker::Ref ref = freezeRef();
    char buf[24];
    setText(TITLE, title(Kind::Gap));

    // The gap to the reference lap, held at the splits as Timing's delta is
    switch (ref) {
    case PbGapTracker::Ref::ALLTIME_PB:  setText(CAPTION, TR("VS ALL-TIME")); break;
    case PbGapTracker::Ref::LAST_LAP:    setText(CAPTION, TR("VS LAST")); break;
    case PbGapTracker::Ref::SERVER_BEST: setText(CAPTION, TR("VS SERVER")); break;
    default:                             setText(CAPTION, TR("VS PB")); break;
    }
    DeckMeter::Data& m = m_rows.meter[GAP_BAR];
    m.show = true;
    m.centered = true;
    m.sided = true;
    int gap = 0;
    if (pd.isLapTimerValid() && m_freeze.shownGap(ref, &gap)) {
        formatDelta(buf, sizeof(buf), gap);
        setText(HERO, buf, deltaTone(gap));
        m.value = DeckMeter::gapPosition(gap, GAP_RANGE_MS);
    } else {
        setText(HERO, Placeholders::GENERIC, TONE_EMPTY);
    }
    // The reference lap's own time under it
    const int refMs = pd.getPbGapTracker().bestLapTimeMs(ref);
    if (refMs > 0) {
        PluginUtils::formatLapTime(refMs, buf, sizeof(buf));
        setText(SUB, buf);
    }

    // The last sector against the best run of it this session
    if (m_gap.lastSector >= 0) {
        snprintf(buf, sizeof(buf), TR("S%d"), m_gap.lastSector + 1);
        setText(LINE1_LABEL, buf);
    } else {
        setText(LINE1_LABEL, TR("SECTOR"));
    }
    if (m_gap.lastSector >= 0 && m_gap.lastRunHadBest) {
        PluginUtils::formatTimeDiff(buf, sizeof(buf), m_gap.lastRunDelta);   // as the Timing HUD
        setText(LINE1, buf, m_gap.lastRunDelta < 0 ? TONE_BEST : (m_gap.lastRunDelta > 0 ? TONE_BAD : TONE_NORMAL));
    } else {
        setText(LINE1, Placeholders::GENERIC, TONE_EMPTY);   // the first run of it: nothing to beat
    }
    // The ideal lap: the best of every sector, once each has one
    setText(LINE2_LABEL, TR("IDEAL"));
    const IdealLapData* ideal = pd.getIdealLapData();
    const int idealMs = ideal ? ideal->getIdealLapTime() : -1;
    if (idealMs > 0) {
        PluginUtils::formatLapTime(idealMs, buf, sizeof(buf));
        setText(LINE2, buf);
    } else {
        setText(LINE2, Placeholders::GENERIC, TONE_EMPTY);
    }
}

// The gaps to the riders either side (-1: none) and the one shown for the
// rival: in a race live (held at the player's splits), else between best laps
int DeckHud::gapToPlayer(int p, int* laps) const {
    *laps = 0;
    const PluginData& pd = PluginData::getInstance();
    const std::vector<int>& order = runningOrder();
    const int raceNum = pd.getDisplayRaceNum();
    const StandingsData* mine = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
    const int pos = rankedPosition();
    if (!mine || pos <= 0 || p < 1 || p > static_cast<int>(order.size()) || p == pos) return INT_MAX;
    const int num = order[static_cast<size_t>(p - 1)];
    const StandingsData* o = pd.getStanding(num);
    if (!o) return INT_MAX;
    if (!pd.isRaceSession()) {
        // Ranked by best lap: the lap times' difference
        return mine->bestLap > 0 && o->bestLap > 0 ? o->bestLap - mine->bestLap : INT_MAX;
    }
    // The Standings HUD's gap rule, measured from the player: live where it
    // would show live (Live updates, a fresh sample, the same lap), else the
    // splits'; a lap apart, the laps
    if (!m_rank.hasTable) return INT_MAX;
    const StandingsGap::Plan plan = StandingsGap::planGap(m_rank.table, RunningOrder::gapRow(num, p - 1, false, p == 1));
    if (plan.kind == StandingsGap::Kind::LapDiff) {
        *laps = plan.value;
        return INT_MAX;
    }
    return plan.kind == StandingsGap::Kind::TimeDiff && plan.value != 0 ? plan.value : INT_MAX;
}

// The gaps stepRival measured this frame, and the one shown: the rival's,
// or the one held at the last split while it holds
void DeckHud::rivalGaps(int* ahead, int* behind, int* shown) const {
    *ahead = m_rival.gapAhead;
    *behind = m_rival.gapBehind;
    const int live = m_rival.defending ? *behind : *ahead;
    *shown = PluginData::getInstance().isRaceSession() && held(DigitRoll::nowUs()) && m_rival.heldGap >= 0
                 ? m_rival.heldGap : live;
}

int DeckHud::rivalLastLap() const {
    const IdealLapData* their = m_rival.num > 0 ? PluginData::getInstance().getIdealLapData(m_rival.num) : nullptr;
    return their && their->lastLapTime > 0 ? their->lastLapTime : -1;
}

void DeckHud::gatherRival() {
    const PluginData& pd = PluginData::getInstance();
    const StandingsData* other = m_rival.num > 0 ? pd.getStanding(m_rival.num) : nullptr;
    const RaceEntryData* entry = other ? pd.getRaceEntry(m_rival.num) : nullptr;
    const bool race = pd.isRaceSession();
    const int raceNum = pd.getDisplayRaceNum();
    const StandingsData* mine = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
    char buf[48];

    setText(TITLE, title(Kind::Rival));
    setText(CAPTION, m_rival.defending ? TR("DEFENDING") : TR("CHASING"));
    setText(LINE1_LABEL, TR("GAP"));
    setText(LINE2_LABEL, race ? TR("LAST") : TR("BEST"));
    DeckMeter::Data& m = m_rows.meter[RIVALS];
    m.show = true;
    m.centered = true;
    if (!entry) {
        setText(HERO, Placeholders::GENERIC, TONE_EMPTY);
        setText(LINE1, Placeholders::GENERIC, TONE_EMPTY);
        setText(LINE2, Placeholders::GENERIC, TONE_EMPTY);
        return;
    }
    // Their number as the hero, the name under it
    setText(HERO, entry->formattedRaceNum);
    snprintf(buf, sizeof(buf), "%s", entry->name);
    upper(buf);
    setText(SUB, buf);

    int ahead = -1, behind = -1, shown = -1;
    rivalGaps(&ahead, &behind, &shown);
    if (shown >= 0) {
        char gap[16];
        formatHundredths(gap, sizeof(gap), shown);
        snprintf(buf, sizeof(buf), "%s s", gap);
        setText(LINE1, buf);
    } else if (race && mine && other->gapLaps != mine->gapLaps) {
        snprintf(buf, sizeof(buf), "%dL", std::abs(other->gapLaps - mine->gapLaps));
        setText(LINE1, buf);
    } else {
        setText(LINE1, Placeholders::GENERIC, TONE_EMPTY);
    }
    const int lapMs = race ? rivalLastLap() : other->bestLap;
    if (lapMs > 0) {
        PluginUtils::formatLapTime(lapMs, buf, sizeof(buf));
        setText(LINE2, buf);
    } else {
        setText(LINE2, Placeholders::GENERIC, TONE_EMPTY);
    }

    // The riders either side as marks on a bar, the player in the middle:
    // ahead to the right, behind to the left, the rival lit
    const auto mark = [&m](int gap, float side, bool lit) {
        if (gap < 0 || m.marks >= DeckMeter::Data::MAX_MARKS) return;
        m.markAt[m.marks] = side * std::min(1.0f, static_cast<float>(gap) / static_cast<float>(RIVAL_RANGE_MS));
        m.markLit[m.marks++] = lit;
    };
    mark(behind, -1.0f, m_rival.defending);
    mark(ahead, 1.0f, !m_rival.defending);
}

// The Session HUD's rows, its own helpers: the server as the caption, then the
// track and the format; shouted, like every name on a deck
void DeckHud::gatherSession() {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& sd = pd.getSessionData();
    char buf[sizeof(m_rows.text[0])];
    setText(TITLE, title(Kind::Session));
    snprintf(buf, sizeof(buf), "%s", PluginUtils::shownServerLabel(sd.serverType, sd.serverName,
                                                                   static_cast<int>(pd.getRaceEntries().size())));
    upper(buf);
    setText(CAPTION, buf);
    setText(LINE1_LABEL, TR("TRACK"));
    if (sd.trackName[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", sd.trackName);
        upper(buf);
        setText(LINE1, buf);
    } else {
        setText(LINE1, Placeholders::GENERIC, TONE_EMPTY);
    }
    // The length only ("10:00 + 2 LAPS"): the session and its state come
    // as a stamp when they change (stepEvents)
    setText(LINE2_LABEL, TR("FORMAT"));
    PluginUtils::formatSessionFormatWords(sd.sessionLength, sd.sessionNumLaps, buf, sizeof(buf), /*shown=*/true);
    upper(buf);
    if (buf[0] != '\0') setText(LINE2, buf);
    else setText(LINE2, Placeholders::GENERIC, TONE_EMPTY);
}

void DeckHud::gatherFuel() {
    // PluginData's fuel history (every fuel readout's), in the Fuel widget's
    // unit: the laps left as the hero, warm under two; the fuel in the
    // tank and the average a lap beside it; the tank as blocks. The fuel is
    // the player's own telemetry, so spectating the deck shows nothing.
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const FuelWidget& widget = HudManager::getInstance().getFuelWidget();
    const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum() && bike.isValid;
    const bool gallons = widget.getFuelUnit() == FuelWidget::FuelUnit::GALLONS;
    const float factor = gallons ? UnitConversion::LITERS_TO_GALLONS : 1.0f;
    char buf[24];
    setText(TITLE, title(Kind::Fuel));
    setText(CAPTION, TR("LAPS LEFT"));
    setText(LINE1_LABEL, TR("FUEL"));
    setText(LINE2_LABEL, TR("AVG"));

    const float laps = own ? pd.getFuelLapsRemaining() : -1.0f;
    const bool low = laps >= 0.0f && laps < FuelEstimate::kCriticalLaps;
    if (laps >= 0.0f) {
        snprintf(buf, sizeof(buf), "%.1f", std::min(laps, 99.9f));
        setText(HERO, buf, low ? TONE_HOT : TONE_NORMAL);
    } else {
        setText(HERO, Placeholders::GENERIC, TONE_EMPTY);
    }
    if (own) {
        // The unit as the value's tail ("12.4L"), drawn smaller by the layout
        snprintf(buf, sizeof(buf), "%.1f%s", std::max(bike.fuel, 0.0f) * factor, UnitConversion::fuelLabel(gallons));
        setText(LINE1, buf);
    } else {
        setText(LINE1, Placeholders::GENERIC, TONE_EMPTY);
    }
    const float avg = pd.getFuel().averagePerLap();
    if (own && FuelEstimate::hasAverage(avg)) {
        snprintf(buf, sizeof(buf), "%.2f", avg * factor);
        setText(LINE2, buf);
    } else {
        setText(LINE2, Placeholders::GENERIC, TONE_EMPTY);
    }
    // The tank: only with a capacity to measure it against
    if (own && bike.maxFuel > 0.0f) {
        DeckMeter::Data& m = m_rows.meter[TANK];
        m.show = true;
        m.value = std::clamp(bike.fuel / bike.maxFuel, 0.0f, 1.0f);
        if (low) m.litAs = DeckMeter::HOT_LIT;
    }
}
