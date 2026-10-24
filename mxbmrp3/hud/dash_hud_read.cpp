// ============================================================================
// hud/dash_hud_read.cpp
// Dash - the readers: what each page shows, as integers from the
// central data, for update()'s compare and rebuildRenderData()'s text. See
// dash_hud.h.
// ============================================================================
#include "dash_hud.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <deque>
#include <vector>

#include "delta_trace_hud.h"
#include "event_log_hud.h"
#include "fuel_widget.h"
#include "gap_bar_hud.h"
#include "lap_log_hud.h"
#include "session_charts_hud.h"
#include "session_hud.h"
#include "standings_gap_plan.h"
#include "standings_hud.h"
#include "timing_hud.h"
#include "../core/hud_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/ui_config.h"

using namespace PluginConstants;

void DashHud::findNeighbours(Neighbour (&rows)[STANDINGS_ROWS]) {
    for (Neighbour& n : rows) n = Neighbour{};
    const PluginData& pd = PluginData::getInstance();
    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return;
    const std::vector<int>& order = pd.getDisplayClassificationOrder();
    const int count = static_cast<int>(order.size());
    int index = -1;
    for (int i = 0; i < count; ++i) {
        if (order[i] == me) { index = i; break; }
    }
    const StandingsData* mine = index >= 0 ? pd.getStanding(me) : nullptr;
    if (!mine) return;

    // The Standings HUD's own gap table: its Gap reference, Gap mode and Live
    // gaps (races only), so the rows read as that HUD's do; planGap() holds
    // the guards a bare subtraction misses -- a rider not yet through a split,
    // a lap down, out of the race. The player's row is this order's.
    const SessionData& session = pd.getSessionData();
    const StandingsHud& standings = HudManager::getInstance().getStandingsHud();
    StandingsGap::Table table = standings.gapTableFor(order, me);
    const bool status = standings.showsStatusColumn();
    table.playerRowIndex = index;
    const bool gapsOff = table.scope == StandingsGap::Scope::OFF;   // no gap column at all
    const bool sideTint = table.reference == StandingsGap::Reference::PLAYER && table.playerHasGapData;

    auto fill = [&](Neighbour& out, int at, bool isPlayer) {
        if (at < 0 || at >= count) return;
        const StandingsData* other = pd.getStanding(order[at]);
        if (!other) return;
        StandingsGap::Row row;
        row.index = at;
        row.stateNormal = (other->state == PluginConstants::RiderState::NORMAL);
        row.hasStateAbbr = (PluginUtils::getRiderStateAbbreviation(other->state)[0] != '\0');
        row.isLeaderRow = (at == 0);
        row.officialGap = other->gap;
        row.gapLaps = other->gapLaps;
        row.realTimeGap = other->realTimeGap;
        row.bestLap = other->bestLap;
        row.isFinished = session.isRiderFinished(other->numLaps, other->numLapsAtLeaderFinish);
        row.hasActiveTrackPos = pd.hasActiveTrackPos(order[at]);
        row.isPlayerRow = isPlayer;
        StandingsGap::Plan plan;
        if (gapsOff) plan.kind = StandingsGap::Kind::Empty;
        else plan = StandingsGap::planGap(table, row);
        out.raceNum = order[at];
        out.position = at + 1;
        out.gapKind = static_cast<uint8_t>(plan.kind);
        out.gapTint = static_cast<uint8_t>(plan.tint);
        out.gapStyle = static_cast<uint8_t>(plan.style);
        out.gapValue = plan.value;
        out.state = other->state;
        out.finished = row.stateNormal && (row.isFinished || other->sessionFinished);
        out.self = isPlayer;
        out.penaltyMs = other->penalty;
        // Ahead red, behind green, as the Standings HUD's Adjacent gaps: the
        // page only ever shows the riders either side, so it tints whatever the
        // Standings gap scope, but only for gaps to the rider (Gap reference
        // Player): a gap to the leader has no side.
        if (sideTint && !isPlayer && plan.tint == StandingsGap::Tint::None &&
            (plan.kind == StandingsGap::Kind::TimeDiff || plan.kind == StandingsGap::Kind::LapDiff)) {
            out.gapTint = static_cast<uint8_t>(at < index ? StandingsGap::Tint::Ahead : StandingsGap::Tint::Behind);
        }
        // The Standings HUD's status marker (lock, hazard, blue flag, pits,
        // finished, last lap): the page shows it while that HUD's column is on.
        if (status) {
            const StandingsHud::Status mark = StandingsHud::statusFor(out.raceNum, out.state, out.finished);
            out.statusMark = static_cast<uint8_t>(mark.mark);
            out.statusFlag = static_cast<uint8_t>(mark.flag);
        }
        // Standings labels the reference row ("Player"); the Dash shows the
        // rider's best lap there instead.
        if (isPlayer && plan.kind == StandingsGap::Kind::Label) {
            out.gapKind = static_cast<uint8_t>(mine->bestLap > 0 ? StandingsGap::Kind::LapTime
                                                                 : StandingsGap::Kind::Placeholder);
            out.gapStyle = static_cast<uint8_t>(StandingsGap::Style::OFFICIAL);
            out.gapValue = mine->bestLap;
        }
    };
    // The rider in the middle; leading, the top row and two behind; last, the
    // bottom row and two ahead. Fewer riders than rows leave the rest empty.
    const int first = std::max(0, std::min(index - 1, count - STANDINGS_ROWS));
    for (int i = 0; i < STANDINGS_ROWS; ++i) fill(rows[i], first + i, first + i == index);
}

PbGapTracker::Ref DashHud::timingReference() {
    return HudManager::getInstance().getTimingHud().getLiveGapReference();
}

int DashHud::timingFreezeMs() {
    return HudManager::getInstance().getTimingHud().getLiveGapFreezeMs();
}

PbGapTracker::Ref DashHud::barReference() {
    return HudManager::getInstance().getGapBarHud().getReference();
}

const char* DashHud::pageReferenceNote(Page page) {
    switch (page) {
        case Page::TIMING:      return TimingHud::segmentModeActive() ? "Best" : PbGapTracker::refName(timingReference());
        case Page::GAP_BAR:     return PbGapTracker::refName(barReference());
        case Page::DELTA_TRACE: return PbGapTracker::refName(HudManager::getInstance().getDeltaTraceHud().getReference());
        default:                return nullptr;
    }
}

int DashHud::barFreezeMs() {
    return HudManager::getInstance().getGapBarHud().getFreezeMs();
}

DashHud::LapClock DashHud::readLapClock() const {
    // The central lap timer, and the gap as the Timing HUD and Gap Bar show it
    // (the official one held through a split's freeze, else the live one), to
    // the millisecond the Timing HUD prints them in.
    const PluginData& pd = PluginData::getInstance();
    LapClock clock;
    if (TimingHud::segmentModeActive()) {
        const SegmentReadout line = TimingHud::segmentLine(pd.getSegmentTimer(), m_segHold.active());
        clock.timeMs = line.timeMs;
        clock.hasGap = line.hasDelta;
        clock.gapMs = line.deltaMs;
        clock.targetMs = line.hasDelta ? -1 : line.bestMs;
        return clock;
    }
    const StandingsData* mine = pd.isDisplayRiderFinished() ? pd.getStanding(pd.getDisplayRaceNum()) : nullptr;
    // The Timing HUD's order: INVALID, the held official time, the finish
    // time, the running lap.
    if (m_timingHold.active()) {
        clock.invalid = m_heldInvalid;
        clock.timeMs = m_heldInvalid || m_heldTimeMs <= 0 ? -1 : m_heldTimeMs;
    } else if (mine && mine->finishTime > 0) {
        clock.timeMs = mine->finishTime;
        clock.finished = true;
    } else {
        const int elapsed = pd.getElapsedLapTime();
        clock.timeMs = elapsed >= 0 ? elapsed : -1;
    }
    int gap = 0;
    if (m_gapFreeze.shownGap(timingReference(), &gap)) {
        clock.hasGap = true;
        clock.gapMs = gap;
    }
    return clock;
}

void DashHud::followSegmentHold() {
    // As TimingHud::update: a completion starts the hold, and with no segments
    // left there is nothing to hold.
    const PluginData::SegmentTimerData& seg = PluginData::getInstance().getSegmentTimer();
    const int freeze = timingFreezeMs();
    if (seg.completionCounter != m_segCompletion) {
        m_segCompletion = seg.completionCounter;
        if (freeze > 0 && seg.lastSeg >= 0) m_segHold.start();
    }
    if (seg.segmentCount() < 1) m_segHold.stop();
    if (m_segHold.expire(freeze) && m_page == Page::TIMING) setDataDirty();
}

DashHud::BarView DashHud::readBar() {
    const PluginData& pd = PluginData::getInstance();
    const PbGapTracker::Ref reference = barReference();
    BarView view;
    // The fill: the live gap against the Gap Bar HUD's Range, as that bar draws it.
    const int live = pd.hasValidLiveGap(reference) ? pd.getLiveGap(reference) : 0;
    const int range = HudManager::getInstance().getGapBarHud().rangeMs(m_barRange, live);
    const float ratio = std::clamp(static_cast<float>(live) / static_cast<float>(range), -1.0f, 1.0f);
    view.fill = static_cast<int>(std::lround(ratio * static_cast<float>(BAR_STEPS)));
    // The text: as the Gap Bar's, the official gap held through its freeze.
    int gap = 0;
    if (m_barFreeze.shownGap(reference, &gap)) {
        view.hasGap = true;
        view.gapMs = gap;
    } else {
        // As the Gap Bar says it: an all-time PB whose trace was never kept.
        view.noTrace = reference == PbGapTracker::Ref::ALLTIME_PB && pd.isAllTimeTraceMissing();
    }
    const IdealView laps = readIdeal();
    view.lastMs = laps.lastMs;
    view.bestMs = laps.bestMs;
    return view;
}

DashHud::SessionClock DashHud::readSessionClock() {
    const PluginData& pd = PluginData::getInstance();
    SessionClock clock;
    clock.lapsToGo = pd.getLeaderLapsToGo();
    // In overtime the label ("2 TO GO") shows no seconds, so they don't count.
    clock.seconds = clock.lapsToGo >= 0 ? 0 : pd.getSessionTime() / PluginConstants::TimeConversion::MS_PER_SECOND;
    return clock;
}

int DashHud::rpmSegments() {
    // The RPM widget's light count (INI-only), so the two strips read alike.
    const RpmWidget& rpm = HudManager::getInstance().getRpmWidget();
    return std::clamp(rpm.m_segments, RpmWidget::MIN_SEGMENTS, RpmWidget::MAX_SEGMENTS);
}

int DashHud::readRpm() {
    const BikeTelemetryData& bike = PluginData::getInstance().getBikeTelemetry();
    if (!bike.isValid) return -1;
    return (std::max(0, bike.rpm) + RPM_STEP / 2) / RPM_STEP * RPM_STEP;
}

DashHud::PageStyle DashHud::readPageStyle() {
    const HudManager& huds = HudManager::getInstance();
    const FuelWidget& fuel = huds.getFuelWidget();
    const RpmWidget& rpm = huds.getRpmWidget();
    PageStyle s;
    s.fuelFont = fuel.getFont(FontCategory::DIGITS);
    const ColorSlot fuelSlots[6] = { ColorSlot::SECONDARY, ColorSlot::MUTED, ColorSlot::TERTIARY,
                                     ColorSlot::PRIMARY, ColorSlot::WARNING, ColorSlot::NEGATIVE };
    for (int i = 0; i < 6; ++i) s.fuelInk[i] = fuel.getColor(fuelSlots[i]);
    s.rpmInk[0] = rpm.getColor(ColorSlot::POSITIVE);
    s.rpmInk[1] = rpm.getColor(ColorSlot::WARNING);
    s.rpmInk[2] = rpm.getColor(ColorSlot::NEGATIVE);
    return s;
}

DashHud::FuelView DashHud::readFuel() {
    // As the Fuel widget: its rows and unit, and its figures (the tank, what this
    // run used, the average per lap and the laps it leaves), rounded as it prints.
    const PluginData& pd = PluginData::getInstance();
    const FuelWidget& widget = HudManager::getInstance().getFuelWidget();
    FuelView v;
    v.rows = widget.m_enabledRows;
    v.unit = static_cast<int>(widget.getFuelUnit());
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    if (pd.getDrawState() != ViewState::ON_TRACK) {
        for (int i = 0; i < FUEL_ROWS; ++i) v.state[i] = FUEL_NA;
        return v;
    }
    if (!bike.isValid) {
        for (int i = 0; i < FUEL_ROWS; ++i) v.state[i] = FUEL_NONE;
        return v;
    }
    const float factor = widget.getFuelUnit() == FuelWidget::FuelUnit::GALLONS ? UnitConversion::LITERS_TO_GALLONS
                                                                               : 1.0f;
    // The tank bar: the share left of the capacity, when the game sends one.
    // A bar is lit while any of its share is left, the top one faded by how
    // much, in whole steps so the compare moves a few times a bar.
    if (bike.maxFuel > 0.0f) {
        const float bars = std::clamp(bike.fuel / bike.maxFuel * static_cast<float>(FUEL_BAR_SEGMENTS),
                                      0.0f, static_cast<float>(FUEL_BAR_SEGMENTS));
        const int whole = static_cast<int>(bars);
        const float part = bars - static_cast<float>(whole);
        v.level = static_cast<uint8_t>(whole);
        if (part > 0.0f) {
            v.level = static_cast<uint8_t>(whole + 1);
            v.step = static_cast<uint8_t>(std::clamp(static_cast<int>(part * RpmWidget::LEAD_STEPS),
                                                     0, RpmWidget::LEAD_STEPS - 1));
        }
    }
    // Signed: Used goes below 0 after a refuel, and the widget prints it so.
    auto put = [&](int row, float shown) {
        if (shown >= 10.0f) {
            v.value[row] = static_cast<int>(std::lround(shown)) * 10;
            v.whole[row] = true;
        } else {
            v.value[row] = static_cast<int>(std::lround(shown * 10.0f));
        }
    };
    put(0, bike.fuel * factor);
    if (widget.hasFuelUsed()) put(1, widget.getFuelUsed() * factor);
    else v.state[1] = FUEL_NONE;
    const float avg = widget.getAveragePerLap();
    if (avg > 0.001f) {
        put(2, avg * factor);
        const float laps = FuelEstimate::lapsRemaining(bike.fuel, avg);
        put(3, laps);
        v.lapsTint = laps < FuelEstimate::kCriticalLaps ? 2 : laps < FuelEstimate::kWarnLaps ? 1 : 0;
    } else {
        v.state[2] = v.state[3] = FUEL_NONE;
    }
    return v;
}

DashHud::IdealView DashHud::readIdeal() {
    // As IdealLapHud's lap rows: Last from IdealLapData, Best the session's best
    // lap entry, Ideal the best sectors summed, and the ideal the previous PB's
    // sectors made for the "beat the ideal" comparison.
    const PluginData& pd = PluginData::getInstance();
    IdealView view;
    const IdealLapData* ideal = pd.getIdealLapData();
    if (ideal) {
        if (ideal->lastLapTime > 0) view.lastMs = ideal->lastLapTime;
        view.idealMs = ideal->getIdealLapTime();
        if (ideal->previousBestSector1 > 0 && ideal->previousBestSector2 > 0 && ideal->previousBestSector3 > 0) {
            view.prevIdealMs = ideal->previousBestSector1 + ideal->previousBestSector2 + ideal->previousBestSector3;
#if GAME_SECTOR_COUNT >= 4
            if (ideal->previousBestSector4 > 0) view.prevIdealMs += ideal->previousBestSector4;
#endif
        }
    }
    if (const LapLogEntry* best = pd.getBestLapEntry()) {
        if (best->lapTime > 0) view.bestMs = best->lapTime;
    }
    return view;
}

DashHud::LapsView DashHud::readLaps() {
    // The Lap Log's data (PluginData's lap history, newest first), drawn in its
    // Order. Its live row is left off: the card's three rows hold finished laps,
    // and the Timing page has the running time.
    const PluginData& pd = PluginData::getInstance();
    LapsView view;
    view.oldestFirst = HudManager::getInstance().getLapLogHud().getDisplayOrder() ==
                       LapLogHud::DisplayOrder::OLDEST_FIRST;
    if (const LapLogEntry* best = pd.getBestLapEntry()) {
        if (best->isComplete && best->lapTime > 0) {
            view.bestMs = best->lapTime;
            view.bestLapNum = best->lapNum;
        }
    }
    int row = 0;
    const std::deque<LapLogEntry>* log = pd.getLapLog();
    if (!log) return view;
    for (const LapLogEntry& e : *log) {
        if (row >= LAPS_ROWS) break;
        LapRow& r = view.rows[row++];
        r.lapNum = e.lapNum;
        r.hasTime = e.isComplete && e.lapTime > 0;
        r.lapTime = r.hasTime ? e.lapTime : -1;
        r.valid = e.isValid;
        r.viaPits = e.viaPits;
    }
    return view;
}

DashHud::GapChartView DashHud::readGapChart() {
    // Who the lines belong to, by the classification now: a pass swaps them.
    const PluginData& pd = PluginData::getInstance();
    GapChartView view;
    view.elements = HudManager::getInstance().getSessionChartsHud().getEnabledElements();
    view.sessionGeneration = pd.getSessionData().sessionGeneration;
    view.mine = pd.getDisplayRaceNum();
    const std::vector<int>& order = pd.getClassificationOrder();
    const size_t n = order.size();
    for (size_t i = 0; i < n; ++i) {
        if (order[i] != view.mine) continue;
        if (i > 0) view.ahead = order[i - 1];
        if (i + 1 < n) view.behind = order[i + 1];
        break;
    }
    return view;
}

DashHud::EventsView DashHud::readEvents() {
    const EventLogHud& log = HudManager::getInstance().getEventLogHud();
    const std::deque<EventLogEntry>& entries = PluginData::getInstance().getEventLog();
    EventsView v;
    v.count = entries.size();
    if (!entries.empty())
        v.newestTick = static_cast<long long>(entries.back().steadyTime.time_since_epoch().count());
    v.filter = log.getEnabledEvents();
    v.order = static_cast<uint8_t>(log.getDisplayOrder());
    v.icons = log.showsIcons();
    return v;
}

bool DashHud::sessionInfoChanged() const {
    // Integers and the two names compared where they live: nothing copied or
    // formatted while the page is up.
    const PluginData& pd = PluginData::getInstance();
    const SessionData& s = pd.getSessionData();
    const SessionHud& hud = HudManager::getInstance().getSessionHud();
    const SessionView& v = m_shownSessionInfo;
    return v.state != s.sessionState || v.length != s.sessionLength || v.numLaps != s.sessionNumLaps ||
           v.session != s.session || v.eventType != s.eventType || v.serverType != s.serverType ||
           v.conditions != s.conditions || v.air != s.airTemperature || v.track != s.trackTemperature ||
           v.multiRider != (pd.getRaceEntries().size() > 1) || v.icons != hud.m_bShowIcons ||
           v.rows != hud.m_enabledRows ||
           v.units != static_cast<int>(UiConfig::getInstance().getTemperatureUnit()) ||
           strcmp(v.server, s.serverName) != 0 || strcmp(v.trackName, s.trackName) != 0;
}

void DashHud::readSessionInfo(SessionView& out) {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& s = pd.getSessionData();
    const SessionHud& hud = HudManager::getInstance().getSessionHud();
    out.state = s.sessionState;
    out.length = s.sessionLength;
    out.numLaps = s.sessionNumLaps;
    out.session = s.session;
    out.eventType = s.eventType;
    out.serverType = s.serverType;
    out.conditions = s.conditions;
    out.air = s.airTemperature;
    out.track = s.trackTemperature;
    // The server label reads "Online" from a second rider on (SessionHud::update).
    out.multiRider = pd.getRaceEntries().size() > 1;
    out.icons = hud.m_bShowIcons;
    out.rows = hud.m_enabledRows;
    out.units = static_cast<int>(UiConfig::getInstance().getTemperatureUnit());
    snprintf(out.server, sizeof(out.server), "%s", s.serverName);
    snprintf(out.trackName, sizeof(out.trackName), "%s", s.trackName);
}
