// ============================================================================
// handlers/opening_line.cpp
// See the header. The one place a split crossing turns into the Holeshot
// claim, Charger's start and the cue, so the three cannot drift apart in what
// they award.
// ============================================================================
#include "opening_line.h"
#include "../core/plugin_data.h"
#include "../core/spotter_manager.h"
#include "../core/stats_manager.h"
#include "../diagnostics/logger.h"

void Handlers::claimOpeningLine(int raceNum, int splitIndex) {
    PluginData& data = PluginData::getInstance();
    ExplorationStats& exploration = StatsManager::getInstance().exploration();
    const bool isPlayer = (raceNum == data.getPlayerRaceNum());
    const bool wasArmed = exploration.holeshotArmed();

    // Once per race, when it settles the holeshot: which split, and whose.
    if (wasArmed) {
        DEBUG_INFO_F("Opening line: split %d by raceNum=%d%s",
                     splitIndex + 1, raceNum, isPlayer ? " (player)" : "");
    }

    exploration.onFirstSplit(isPlayer);   // the Holeshot row; its own arm
    // Riding, not watching: a replay delivers these crossings too (see the
    // classification handler's finish gate).
    if (isPlayer && data.isPlayerRunning()) {
        // Charger's starting position: where the player sat at their own first
        // line. Its own arm, so a rival taking the holeshot does not cost the
        // player their start.
        exploration.onPlayerOpeningSplit(data.getPositionForRaceNum(raceNum));
        // The cue fires ONLY on the crossing that claimed it - THIS call took
        // the arm down and it was ours. tookHoleshot() alone stays true for the
        // whole race, and reading it on every later crossing announced the
        // holeshot again at every split.
        const bool claimedNow = wasArmed && !exploration.holeshotArmed();
        if (claimedNow && exploration.tookHoleshot()) {
            SpotterManager::getInstance().onHoleshot();
        }
    }
}
