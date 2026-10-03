// ============================================================================
// handlers/run_handler.cpp
// Processes run lifecycle data (run init/deinit/start/stop)
// ============================================================================
#include "run_handler.h"
#include <cstring>
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/input_manager.h"
#include "../core/hud_manager.h"
#include "../core/stats_manager.h"
#include "../core/settings_manager.h"
#include "../hud/fuel_widget.h"
#include "../diagnostics/logger.h"

void Handlers::handleRunInit(Unified::SessionData* psSessionData) {
    HANDLER_NULL_CHECK(psSessionData);

    // Event logging now handled by PluginManager

    // Update plugin data store
    PluginData::getInstance().setSession(psSessionData->session);
    PluginData::getInstance().setConditions(static_cast<int>(psSessionData->conditions));
    PluginData::getInstance().setAirTemperature(psSessionData->airTemperature);
    PluginData::getInstance().setTrackTemperature(psSessionData->trackTemperature);
    PluginData::getInstance().setSetupFileName(psSessionData->setupFileName);

    // Warn if using default setup (empty or "Default")
    if (psSessionData->setupFileName[0] == '\0' ||
        strcmp(psSessionData->setupFileName, "Default") == 0) {
        PluginData::getInstance().notifyDefaultSetup();
    }

    // Reset fuel tracking when entering track (rider may have refueled in pits)
    HudManager::getInstance().getFuelWidget().resetFuelTracking();

    // The player is leaving the pits, so the lap that was in progress is dead:
    // drop the live timer's anchor NOW. The classification's pit flag (1 -> 0)
    // does this too, but it arrives a frame or more after RunStart, and the
    // first track-position sample at the pit exit lands in between -- read
    // against the dead lap's paused anchor at a position most of a lap on, it
    // showed a gap to PB of -60 s for one frame (an in-game capture, 2026-09-17;
    // pinned by pb_gap_test). The position baseline goes with it (rejoinedTrack):
    // the first sample is at the pit box, and read as a delta from where the
    // rider LEFT the track a box more than half a lap behind that point is a wrap
    // that would re-anchor the lap at the box (pinned by pb_gap_test too).
    // Nothing to drop when the player is not the timed rider (spectating) or on
    // the first entry of a session, when nothing is anchored.
    PluginData::getInstance().invalidateLapTimerAnchor(PluginData::getInstance().getPlayerRaceNum(),
                                                       /*rejoinedTrack=*/true);

    // Start stats session tracking (pass session type so stats only reset on session change, not pit stops)
    StatsManager::getInstance().recordSessionStart(psSessionData->session);
}

void Handlers::handleRunStart() {
    // Event logging now handled by PluginManager

    // Set player running flag (cleared in RunStop/RunDeinit)
    PluginData::getInstance().setPlayerRunning(true);

    // Resume stats session timer (paused in RunStop)
    StatsManager::getInstance().notifyResume();

    // Reset fuel tracking for new run
    HudManager::getInstance().getFuelWidget().resetFuelTracking();

    // Refresh window information at run start to detect any resolution changes
    // that might have happened while in menus
    InputManager::getInstance().forceWindowRefresh();
}

void Handlers::handleRunStop() {
    // Event logging now handled by PluginManager

    // Clear player running flag
    PluginData::getInstance().setPlayerRunning(false);

    // Pause stats session timer (resumed in RunStart)
    StatsManager::getInstance().notifyPause();

    // The simulation stopped (RunStop is "paused" in the API: the Esc menu, or the step into
    // the pits): persist deferred settings + stats now. This is where the ~2ms settings
    // serialize lands — a frame hitch here is invisible; the disk I/O itself is on the
    // AtomicFileWriter thread. Each is a no-op if nothing changed. (RunDeinit repeats this for
    // a direct exit, and Shutdown() for quitting.)
    //
    // Nothing persisted is written while the player is riding, and the stats and the PB
    // traces are always written TOGETHER (StatsManager::save() writes both): a PB on disk
    // without its trace is the inconsistency to avoid. A game crash mid-session loses what
    // changed since the last of these, which is the accepted trade.
    SettingsManager::getInstance().flushIfDirty(HudManager::getInstance());
    StatsManager::getInstance().save();   // the PB gap traces flush with the stats
}

void Handlers::handleRunDeinit() {
    // Event logging now handled by PluginManager

    // Leaving the track: the lap the next RaceLap closes went through the pits.
    // Marked on the player's own leave-track callback because the classification's
    // pit flag may never rise for a pit taken from the menu (the sim has stopped
    // first), which is how the Timing panel's own latch missed it. Unconditional,
    // not "with a lap under way": a pit before the session's first crossing has
    // no timer running either, and the game still reports the first crossing
    // after a re-entry as a 0 ms lap (an in-game log, 2026-09-19). See
    // PluginData::markLapViaPits.
    PluginData::getInstance().markLapViaPits(PluginData::getInstance().getPlayerRaceNum());

    // Clear player running flag
    PluginData::getInstance().setPlayerRunning(false);

    // Record race finish if player actually completed the race
    StatsManager::getInstance().tryRecordRaceFinish(PluginData::getInstance(), /*final=*/true);
    // ...and if they raced but did not finish, arm Rage Quit: counted only if
    // the race is gone before they finish (a pit stop is also a RunDeinit).
    {
        StatsManager& stats = StatsManager::getInstance();
        if (PluginData::getInstance().isRaceSession() && !stats.raceFinishRecorded() &&
            stats.getSessionLaps() > 0) {
            stats.armRaceLeft();
        }
    }

    // End stats session and save
    StatsManager::getInstance().recordSessionEnd();
    StatsManager::getInstance().save();   // the PB gap traces flush with the stats

    // Exiting the run: flush any deferred settings changes (no-op if nothing changed).
    SettingsManager::getInstance().flushIfDirty(HudManager::getInstance());

    // Off the track: take the in-game overlay's HUD off screen immediately
    // instead of waiting out its staleness backstop (see notifyGameInactive) —
    // the game issues no Draw calls in the menus, so a lingering HUD sits over
    // them. No-op unless the overlay renderer is on.
}
