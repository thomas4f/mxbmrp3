// ============================================================================
// core/running_order.cpp
// See running_order.h. Moved out of StandingsHud so the Position widget, the
// Dash and the decks read the order from PluginData rather than from a HUD.
// ============================================================================
#include "running_order.h"

#include <algorithm>

#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"

using namespace PluginConstants;

bool RunningOrder::watched() const {
    return UiConfig::getInstance().getLiveUpdates() == LiveUpdates::BOTH || m_active;
}

int RunningOrder::positionOf(int raceNum) {
    if (!watched()) return PluginData::getInstance().getDisplayPositionForRaceNum(raceNum);
    const std::vector<int>& order = get();
    const auto it = std::find(order.begin(), order.end(), raceNum);
    return it == order.end() ? -1 : static_cast<int>(it - order.begin()) + 1;
}

// Races in progress only; anywhere else the tracker forgets its passes, so one
// race's pairs never carry into the next.
const std::vector<int>& RunningOrder::get() {
    const PluginData& pluginData = PluginData::getInstance();
    const std::vector<int>& official = pluginData.getDisplayClassificationOrder();
    const SessionData& sessionData = pluginData.getSessionData();
    const bool live = UiConfig::getInstance().getLiveUpdates() == LiveUpdates::BOTH && pluginData.isRaceSession() &&
        (sessionData.sessionState & SessionState::IN_PROGRESS) != 0;
    if (!live) {
        if (m_active) {   // hand the official order back, once
            m_active = false;
            m_tracker.reset();
            ++m_revision;
        }
        return official;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto since = now - m_time;
    if (m_active && since < std::chrono::milliseconds(POLL_MS) && official == m_input) {
        return m_rows;
    }
    if (since >= std::chrono::milliseconds(IDLE_MS)) m_tracker.reset();
    m_time = now;
    m_input.assign(official.begin(), official.end());

    m_riders.clear();
    m_riders.reserve(official.size());
    for (int raceNum : official) {
        StandingsLiveOrder::Rider rider;
        rider.raceNum = raceNum;
        const StandingsData* standing = pluginData.getStanding(raceNum);
        const RiderTrackState* track = pluginData.getRiderTrackState(raceNum);
        if (standing && track) {
            rider.numLaps = standing->numLaps;
            rider.trackPos = track->trackPos;
            // Going the wrong way (a crash, a reset) is no evidence either way,
            // like a missing sample: the rider keeps any pass already shown
            // rather than jumping back to their official row and out again.
            rider.fresh = pluginData.hasActiveTrackPos(raceNum) && !track->wrongWay;
            rider.racing = standing->state == RiderState::NORMAL && standing->pit == 0 &&
                standing->gapLaps == 0 &&
                !sessionData.isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish);
        }
        m_riders.push_back(rider);
    }

    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    m_next.reserve(official.size());
    m_tracker.update(m_riders, nowMs, m_next);
    if (!m_active || m_next != m_rows) ++m_revision;
    m_rows.swap(m_next);
    m_active = true;
    return m_rows;
}

StandingsGap::Table RunningOrder::gapTable(const std::vector<int>& order, int displayRaceNum,
                                           StandingsGap::Reference reference, StandingsGap::Scope scope) {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    const bool isRace = pluginData.isRaceSession();

    // Gather the table-wide gap inputs once. Everything the gap decision needs
    // from PluginData is read HERE; StandingsGap::planGap() sees only these
    // numbers (see standings_gap_plan.h).
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    // The leader (P1) has gap == 0 legitimately — their zero is valid data, not missing.
    const bool playerIsLeader = (!order.empty() && order[0] == displayRaceNum);

    StandingsGap::Table gapTable;
    gapTable.reference = reference;
    gapTable.scope = scope;
    gapTable.isRace = isRace;
    // Live gaps in race sessions when enabled
    gapTable.liveGapsEnabled = isRace && UiConfig::getInstance().getLiveUpdates() != LiveUpdates::OFF;
    gapTable.playerIsLeader = playerIsLeader;
    gapTable.playerOfficialGap = (playerStanding && playerStanding->gap > 0) ? playerStanding->gap : 0;
    gapTable.playerGapLaps = playerStanding ? playerStanding->gapLaps : 0;
    gapTable.playerFinishTime = playerStanding ? playerStanding->finishTime : 0;
    gapTable.leaderFinishTime = sessionData.leaderFinishTime;
    // Player needs valid gap data for relative comparisons to be meaningful.
    gapTable.playerHasGapData = playerStanding &&
        (playerStanding->bestLap > 0 || playerStanding->gap > 0 ||
         playerStanding->gapLaps > 0 || playerIsLeader);
    // The player-relative live gap subtracts the player's realTimeGap as the
    // reference for every other rider, so it must be a FRESH value. If the player
    // (spectated/target rider) has dropped out of the current track-position batch
    // its realTimeGap is stale (a batch often misses one to four riders) —
    // gate on hasActiveTrackPos exactly as the per-rider live path does, so a
    // stale reference doesn't skew every rider's gap. The leader is always valid.
    const bool playerLiveGapUsable = playerStanding &&
        (pluginData.hasActiveTrackPos(displayRaceNum) || playerIsLeader);
    gapTable.playerLiveGap = (playerLiveGapUsable && playerStanding->realTimeGap > 0)
        ? playerStanding->realTimeGap : 0;

    return gapTable;
}

StandingsGap::Row RunningOrder::gapRow(int raceNum, int index, bool isPlayer, bool isLeader) {
    StandingsGap::Row row;
    row.index = index;
    row.isLeaderRow = isLeader;
    row.isPlayerRow = isPlayer;
    const PluginData& pd = PluginData::getInstance();
    const StandingsData* s = pd.getStanding(raceNum);
    if (!s) return row;
    row.stateNormal = (s->state == RiderState::NORMAL);
    row.hasStateAbbr = (PluginUtils::getRiderStateAbbreviation(s->state)[0] != '\0');
    row.officialGap = s->gap;
    row.gapLaps = s->gapLaps;
    row.realTimeGap = s->realTimeGap;
    row.bestLap = s->bestLap;
    row.isFinished = pd.getSessionData().isRiderFinished(s->numLaps, s->numLapsAtLeaderFinish);
    row.hasActiveTrackPos = pd.hasActiveTrackPos(raceNum);
    return row;
}
