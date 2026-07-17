// ============================================================================
// core/test_hooks.cpp
// Test-only DLL exports for the headless (mingw/Wine) test harness. These let a
// test driver invoke internal actions that are normally only reachable through
// the in-game settings UI — reset-to-defaults (all / active profile / one HUD),
// copy-profile-to-all, profile switch, an explicit save, and the update-checker
// version comparison.
//
// The ENTIRE file is gated on MXBMRP3_TEST_BUILD, so it is empty in every
// shipping (MSVC) configuration and the exports never exist in a released DLL.
// It is also not referenced by mxbmrp3.vcxproj; only the cross-build's source
// glob picks it up. See tests/integration/ and DEVELOPMENT.md.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "settings_manager.h"
#include "ui_config.h"
#include "hud_manager.h"
#include "companion_window.h"
#include "../hud/settings_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/map_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/event_log_hud.h"
#include "../hud/notices_hud.h"
#include "../hud/timing_hud.h"
#include "../hud/gamepad_widget.h"
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "input_manager.h"
#include "analytics_manager.h"
#include "plugin_manager.h"
#include "plugin_thread.h"
#include "plugin_data.h"
#include "xinput_reader.h"
#include "profile_manager.h"
#include "director_manager.h"
#include "stats_manager.h"
#if GAME_HAS_FMX
#include "fmx_manager.h"
#endif
#include "update_checker.h"
#include "update_downloader.h"
#include "http_server.h"
#include "../game/game_config.h"
#if GAME_HAS_RECORDER
#include "event_recorder.h"
#endif
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include <string>
#include <vector>
#include <cmath>

extern "C" {

#if GAME_HAS_HTTP_SERVER
// Start the web overlay server directly (default off), so an integration test can
// read /api/state without seeding a settings file and restarting the plugin.
__declspec(dllexport) void MXBMRP3_Test_StartHttp() {
    HttpServer::getInstance().setEnabled(true);
    HttpServer::getInstance().start();
}


// Build the /api/state snapshot directly (game thread) and return it, WITHOUT
// starting the server or going through the socket + rebuild-gating. This is how
// plugin-logic tests observe computed state in isolation — no HTTP machinery.
// The returned pointer is valid until the next call (single-threaded test use).
__declspec(dllexport) const char* MXBMRP3_Test_Snapshot() {
    static std::string buf;
    buf = HttpServer::getInstance().testSnapshot();
    return buf.c_str();
}
#endif


// Reset EVERYTHING to factory defaults (per-profile HUDs for all profiles +
// globals). Mirrors settings_hud.cpp's "Reset All". Persists.
__declspec(dllexport) void MXBMRP3_Test_ResetAll() {
    SettingsManager::getInstance().resetAllToFactoryDefaults(HudManager::getInstance());
}

// Reset the ACTIVE profile's HUDs/widgets to defaults; globals + other profiles
// untouched. Does not persist on its own — call MXBMRP3_Test_Save().
__declspec(dllexport) void MXBMRP3_Test_ResetActiveProfile() {
    SettingsManager::getInstance().resetActiveProfileToFactoryDefaults(HudManager::getInstance());
}

// Reset one named HUD (active profile) to defaults — models a per-tab "Reset".
// keepVisibility mirrors the tab behaviour (single-HUD tabs keep visibility).
__declspec(dllexport) void MXBMRP3_Test_ResetHud(const char* hudName, int keepVisibility) {
    std::vector<std::string> names{ hudName ? std::string(hudName) : std::string() };
    SettingsManager::getInstance().resetHudsToFactoryDefaults(
        HudManager::getInstance(), names, keepVisibility != 0);
}

// Copy the active profile's settings to all other profiles ("Copy to all").
__declspec(dllexport) void MXBMRP3_Test_CopyProfileToAll() {
    SettingsManager::getInstance().applyToAllProfiles(HudManager::getInstance());
}

// Switch the active profile (0=Practice,1=Qualify,2=Race,3=Spectate). Uses the
// same public path as the in-game switch (captures current, applies new, saves).
__declspec(dllexport) void MXBMRP3_Test_SwitchProfile(int idx) {
    if (idx < 0 || idx >= static_cast<int>(ProfileType::COUNT)) return;
    SettingsManager::getInstance().switchProfile(HudManager::getInstance(), static_cast<ProfileType>(idx));
}

// Enable/disable auto-by-session profile switching (the ProfileManager flag the
// Settings UI toggles). Lets a test arm the auto-switch path before driving
// RaceSession/spectate callbacks and asserting the active profile follows.
__declspec(dllexport) void MXBMRP3_Test_SetAutoSwitch(int enabled) {
    ProfileManager::getInstance().setAutoSwitchEnabled(enabled != 0);
}

// Read the active profile index (0=Practice,1=Qualify,2=Race,3=Spectate). The
// active profile isn't in /api/state, so the auto-switch test reads it directly.
__declspec(dllexport) int MXBMRP3_Test_GetActiveProfile() {
    return static_cast<int>(ProfileManager::getInstance().getActiveProfile());
}

// Force a settings save (the reset/profile calls that don't persist rely on this).
__declspec(dllexport) void MXBMRP3_Test_Save() {
    SettingsManager::getInstance().saveSettings(
        HudManager::getInstance(), PluginManager::getInstance().getSavePath());
}

// Mark settings dirty WITHOUT writing — the deferred auto-save path (a HUD drag/toggle).
// The write happens later, on a leave-track flush. Lets a test assert the deferral: after
// this, nothing is on disk until a flush.
__declspec(dllexport) void MXBMRP3_Test_MarkDirty() {
    SettingsManager::getInstance().markDirty();
}

// Flush pending settings to disk if dirty (synchronous atomic write) — models the
// leave-track transition (RunStop/RunDeinit) firing. No-op when clean OR auto-save is off.
__declspec(dllexport) void MXBMRP3_Test_FlushIfDirty() {
    SettingsManager::getInstance().flushIfDirty(HudManager::getInstance());
}

// Unsaved-changes state — drives the settings Save button (lit when dirty, "Saved" when not).
__declspec(dllexport) int MXBMRP3_Test_IsDirty() {
    return SettingsManager::getInstance().isDirty() ? 1 : 0;
}

// Toggle Auto-Save (default on). With it off, leaving the track does NOT auto-flush; the user
// persists via the Save button (MXBMRP3_Test_Save).
__declspec(dllexport) void MXBMRP3_Test_SetAutoSave(int enabled) {
    UiConfig::getInstance().setAutoSave(enabled != 0);
}

// (Re)load settings from an arbitrary save path into live state, via the same
// applier Startup uses. Lets a single-process test perturb an INI on disk and
// pull it into live state (the "set live state" seam), so it can then reset and
// re-save without a second process.
__declspec(dllexport) void MXBMRP3_Test_LoadSettings(const char* savePath) {
    if (!savePath) return;
    SettingsManager::getInstance().loadSettings(HudManager::getInstance(), savePath);
}

// Active settings tab (by display name) - drives the persisted-tab restore test. Set
// selects a tab (no-op if the name is unknown / unavailable on this build); Get copies the
// current tab's name out. Read/written through the same accessors save/load use.
__declspec(dllexport) void MXBMRP3_Test_SetActiveTab(const char* name) {
    HudManager::getInstance().getSettingsHud().setActiveTabByName(name ? name : "");
}

// Count / click the shared stepped-control regions on the ACTIVE settings tab,
// through the real click path (hit-test -> dispatchRegion -> applySteppedControl).
// The settings-click surface is otherwise reachable only via real OS mouse input;
// this seam makes the converted steppers' step/clamp/wrap/acceleration behavior
// assertable headless. holdRepeats forces the hold-repeat counter (accel tiers:
// <6 -> x1, <16 -> x5, else x10) for the duration of the one click.
__declspec(dllexport) int MXBMRP3_Test_SettingsSteppedCount(int up) {
    return HudManager::getInstance().getSettingsHud().testSteppedRegionCount(up != 0);
}
__declspec(dllexport) int MXBMRP3_Test_SettingsClickStepped(int index, int up, int holdRepeats) {
    return HudManager::getInstance().getSettingsHud().testClickStepped(index, up != 0, holdRepeats) ? 1 : 0;
}

// Cycle-control twin of the stepped seam: count / click the shared
// CYCLE_UP/CYCLE_DOWN regions on the ACTIVE settings tab through the real click
// path (hit-test -> dispatchRegion -> applyCycleControl). No hold tier - cycles
// never accelerate.
__declspec(dllexport) int MXBMRP3_Test_SettingsCycleCount(int up) {
    return HudManager::getInstance().getSettingsHud().testCycleRegionCount(up != 0);
}
__declspec(dllexport) int MXBMRP3_Test_SettingsClickCycle(int index, int up) {
    return HudManager::getInstance().getSettingsHud().testClickCycle(index, up != 0) ? 1 : 0;
}

// The ACTIVE rumble config's Bumps light-motor strength. Reads through the same
// getRumbleConfig() resolution the Rumble tab binds to (global, or the current
// bike's profile in per-bike mode), so the stepped-control profile-binding
// guard is assertable headless: a click swallowed after a bike swap leaves BOTH
// the old and the new profile's value unchanged.
__declspec(dllexport) float MXBMRP3_Test_RumbleActiveBumpsLight() {
    return XInputReader::getInstance().getRumbleConfig().suspensionEffect.lightStrength;
}

// Open/close the settings menu (SettingsHud) — mirrors the TOGGLE_SETTINGS hotkey.
// Lets a dump/preview test capture the settings UI without simulating input.
__declspec(dllexport) void MXBMRP3_Test_ShowSettings(int visible) {
    SettingsHud& s = HudManager::getInstance().getSettingsHud();
    if (visible) s.show(); else s.hide();
}

// Open/close the standalone companion window (renders the HUD off-game). Lets a
// driver open the real in-process window and screenshot it under Wine.
__declspec(dllexport) void MXBMRP3_Test_CompanionWindow(int on) {
    CompanionWindow::getInstance().setEnabled(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_GetActiveTab(char* out, int cap) {
    if (!out || cap <= 0) return;
    const char* name = HudManager::getInstance().getSettingsHud().getActiveTabName();
    strncpy(out, name ? name : "", cap - 1);
    out[cap - 1] = '\0';
}

// --- Analytics dry-run capture seam. Analytics's only real effect is a network POST the
// harness can't observe, so these drive the payload build + the sampling gate directly:
// Prime fakes identity/session/host + turns on capture mode (real senders become no-ops,
// so a test build never phones home); AppStarted returns the always-sent app_started body;
// QueueSessionEnd/QueueCustom run the gated paths; SeedCrash runs the (never-gated) crash
// path; DrainPending returns whatever those enqueued (the events that WOULD be sent). ---
__declspec(dllexport) void MXBMRP3_Test_AnalyticsPrime() {
    AnalyticsManager::getInstance().testPrime();
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsSetFullLaunch(int full) {
    AnalyticsManager::getInstance().testSetFullLaunch(full != 0);
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsAppStarted(char* out, int cap) {
    if (!out || cap <= 0) return;
    std::string body = AnalyticsManager::getInstance().testBuildAppStarted();
    strncpy(out, body.c_str(), cap - 1);
    out[cap - 1] = '\0';
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsQueueSessionEnd() {
    AnalyticsManager::getInstance().testQueueSessionEnd();
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsQueueCustom(const char* name) {
    AnalyticsManager::getInstance().testQueueCustom(name ? name : "");
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsSeedCrash(const char* markerPath,
        const char* fault, const char* code) {
    AnalyticsManager::getInstance().testSeedAndReportCrash(
        markerPath ? markerPath : "", fault ? fault : "", code ? code : "");
}
// Join the pending event bodies with '\n' into out; returns the number drained.
__declspec(dllexport) int MXBMRP3_Test_AnalyticsDrainPending(char* out, int cap) {
    std::vector<std::string> events = AnalyticsManager::getInstance().testDrainPending();
    std::string joined;
    for (const auto& e : events) { joined += e; joined += '\n'; }
    if (out && cap > 0) {
        strncpy(out, joined.c_str(), cap - 1);
        out[cap - 1] = '\0';
    }
    return static_cast<int>(events.size());
}

// Update-checker version comparison: <0 if a<b, 0 if equal/unparseable, >0 if a>b.
__declspec(dllexport) int MXBMRP3_Test_CompareVersions(const char* a, const char* b) {
    return UpdateChecker::compareVersions(a ? a : "", b ? b : "");
}

// --- Auto-director controls (the rider lock invariants). The director only
// evaluates while spectating/replaying and its cadence is wall-clock; these hooks
// expose the enable + rider-lock toggles and the lock state so a test can assert
// the lock's release rules without depending on shot timing. ---
__declspec(dllexport) void MXBMRP3_Test_DirectorSetEnabled(int enabled) {
    DirectorManager::getInstance().setEnabled(enabled != 0);
}
__declspec(dllexport) void MXBMRP3_Test_DirectorToggleLock() {
    DirectorManager::getInstance().toggleLock();
}
__declspec(dllexport) int MXBMRP3_Test_DirectorIsLocked() {
    return DirectorManager::getInstance().isLocked() ? 1 : 0;
}
// The next camera role the rider lock rotates to from `cur` (SpectateHandler::CameraRole
// as int). Deterministic given the enabled-camera config; lets a test assert the cycle
// order + wrap without depending on the wall-clock shot cadence.
__declspec(dllexport) int MXBMRP3_Test_DirectorNextLockedCamera(int cur) {
    return DirectorManager::getInstance().nextLockedCamera(cur);
}

// Set the director's story-follow toggles from a bitmask so a broadcast harness can
// replay one tape under different story configs (all on, all off, battles-only, ...)
// and compare the resulting airtime/camera mix. Bits (LSB first):
//   1=battles 2=incidents 4=fastestLap 8=pace 16=lappers 32=drops.
__declspec(dllexport) void MXBMRP3_Test_DirectorSetStories(int mask) {
    DirectorManager& d = DirectorManager::getInstance();
    d.setFollowBattles((mask & 1) != 0);
    d.setFollowIncidents((mask & 2) != 0);
    d.setFollowFastestLap((mask & 4) != 0);
    d.setFollowPace((mask & 8) != 0);
    d.setFollowLappers((mask & 16) != 0);
    d.setFollowDrops((mask & 32) != 0);
}

// --- Broadcast-measurement hook. The director's shot pacing (min/max shot, holds,
// variety cadence) is wall-clock driven, so a naive tape replay - which fires every
// event back-to-back in milliseconds - collapses a whole race into one shot. This lets
// a test inject a simulated clock from each recorded event's timestamp so the pacing
// plays out at the real cadence; every cut the director then makes is logged by cutTo(),
// so the broadcast (cut count, per-rider screen time, camera-angle distribution) is
// reconstructed by parsing the plugin log. ---
__declspec(dllexport) void MXBMRP3_Test_DirectorSetNowMs(long long ms) {
    DirectorManager::testSetNowMs(ms);
}

// Enable/disable the "Director" event-log type. This is a DISPLAY filter only — director
// cuts/state changes are pushed to the event log unconditionally (raw-data contract), so a
// test toggles this to check that emission is independent of the display filter.
__declspec(dllexport) void MXBMRP3_Test_EventLogEnableDirector(int on) {
    HudManager::getInstance().getEventLogHud().setEventTypeEnabled(EventLogType::Director, on != 0);
}

// Make the Event Log HUD visible (default off) for a demo/screenshot.
__declspec(dllexport) void MXBMRP3_Test_EventLogSetVisible(int visible) {
    HudManager::getInstance().getEventLogHud().setVisible(visible != 0);
}

// Return the icon-color-slot override of the most-recent event-log entry whose message
// contains `messageSubstr`: a ColorSlot value (>=0) for an overridden entry (e.g. director
// state-transition tints), -1 for an entry using its per-type default color, or -2 if no
// matching entry exists. Lets a headless test assert the director state->color plumbing
// without the render color leaking into the /api/state web contract.
__declspec(dllexport) int MXBMRP3_Test_EventLogIconColorSlot(const char* messageSubstr) {
    if (!messageSubstr) return -2;
    const auto& log = PluginData::getInstance().getEventLog();
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        if (std::string(it->message).find(messageSubstr) != std::string::npos)
            return it->iconColorSlot;
    }
    return -2;
}

// Hide the Notices HUD (the "ALL-TIME PB" etc. flashes) so a demo/screenshot of another
// centered HUD isn't overlapped by a transient notice.
__declspec(dllexport) void MXBMRP3_Test_NoticesSetVisible(int visible) {
    HudManager::getInstance().getNoticesHud().setVisible(visible != 0);
}

// Configure the Timing HUD for a demo/screenshot: show the big time row and a set of
// comparison rows. The (gapEnabled, primaryGap, secondaryMask) args are the pre-redesign
// shape; here they collapse to the union of the enabled gap types as comparison rows.
// Enable a set of Timing comparison rows for tests. The redesigned Timing HUD has no
// primary/secondary distinction (just a flat set of enabled comparisons), but this hook keeps
// the old 3-arg shape so existing call sites don't churn: the three args are OR-folded into one
// enable mask (gapEnabled gates whether primaryGap is included). Effectively an "enable these
// comparison flags" call; the time row is always turned on.
__declspec(dllexport) void MXBMRP3_Test_TimingConfig(int gapEnabled, int primaryGap, int secondaryMask) {
    TimingHud& t = HudManager::getInstance().getTimingHud();
    int mask = secondaryMask | (gapEnabled ? primaryGap : 0);
    const GapTypeFlags all[] = { GAP_TO_PB, GAP_TO_ALLTIME, GAP_TO_IDEAL, GAP_TO_OVERALL, GAP_TO_LASTLAP, GAP_TO_RECORD };
    for (GapTypeFlags f : all) t.setComparisonEnabled(f, (mask & f) != 0);
    t.setTimeEnabled(true);
}

// The Timing HUD's reference (ms) for a gap type at a given split boundary: the cumulative
// target (S1 / S1+S2 / … ; -1 = full lap), or -1 if unavailable. targetSplit == -999 uses the
// LIVE sector the rider is driving toward (passiveReferenceMs, i.e. currentTargetSplit()).
// The rendered chip text isn't in /api/state, so this exposes the progressive-reference
// selection for a headless test (see timing_hud.cpp).
__declspec(dllexport) int MXBMRP3_Test_TimingReferenceMs(int gapFlag, int targetSplit) {
    TimingHud& t = HudManager::getInstance().getTimingHud();
    GapTypeFlags type = static_cast<GapTypeFlags>(gapFlag);
    return (targetSplit == -999) ? t.passiveReferenceMs(type) : t.cumulativeReferenceMs(type, targetSplit);
}
// The split boundary the Timing HUD's live reference is tracking (currentTargetSplit()):
// 0 = heading to S1, 1 = heading to S2, … ; -1 = timer idle / heading to the finish (full lap).
__declspec(dllexport) int MXBMRP3_Test_TimingTargetSplit() {
    return HudManager::getInstance().getTimingHud().currentTargetSplit();
}
// Whether the Timing HUD's time cell is currently rendering "INVALID" (frozen on a lap flagged
// invalid). Exposed because the rendered text isn't in /api/state. A lap that passed through the
// pits is not flagged invalid, so this stays 0 for a pit out-lap even though the lap is "invalid".
__declspec(dllexport) int MXBMRP3_Test_TimingInvalidShown() {
    return HudManager::getInstance().getTimingHud().showingInvalid() ? 1 : 0;
}
// Whether the Timing HUD is currently holding a frozen official split/lap time (the display
// freeze that follows a split/lap event). Exposed because the freeze state isn't in /api/state;
// it's the signal for the "a completed lap must freeze" regression, including the garage-start
// first-lap case where a stale pit-interrupted flag used to suppress the freeze.
__declspec(dllexport) int MXBMRP3_Test_TimingFrozen() {
    return HudManager::getInstance().getTimingHud().isFrozen() ? 1 : 0;
}
// The Timing panel's rendered geometry (each value ×1e6 as an int, so a headless test can do
// exact integer comparisons). Lets a test assert the panel is a whole number of grid bands
// (height == (showTime ? lineHeightLarge : 0) + rows*lineHeightNormal). Drive a Draw first so
// the bounds reflect the current config.
__declspec(dllexport) void MXBMRP3_Test_TimingGeometry(int* height, int* paddingV,
        int* fontLarge, int* fontNormal, int* lineLarge, int* lineNormal) {
    TimingHud::TestGeometry g = HudManager::getInstance().getTimingHud().testGeometry();
    auto q = [](float v) { return static_cast<int>(v * 1e6f + 0.5f); };
    if (height)     *height     = q(g.height);
    if (paddingV)   *paddingV   = q(g.paddingV);
    if (fontLarge)  *fontLarge  = q(g.fontLarge);
    if (fontNormal) *fontNormal = q(g.fontNormal);
    if (lineLarge)  *lineLarge  = q(g.lineLarge);
    if (lineNormal) *lineNormal = q(g.lineNormal);
}

// The display rider's live elapsed lap time (ms), or -1 when there is no valid anchor — the
// placeholder shown before the first S/F crossing and (after the pit-exit fix) after leaving
// the pits until the next S/F. Wall-clock based, so tests assert the PLACEHOLDER condition
// (== -1) or "running" (>= 0), never an exact value.
__declspec(dllexport) int MXBMRP3_Test_ElapsedLapTime() {
    return PluginData::getInstance().getElapsedLapTime();
}

// Whether the display rider's lap timer is in the grid-start grace window: anchored at the
// green flag and not yet reset by the first lap's completion, so an intermediate S/F crossing
// won't zero it. Exposed for the grid-start timing regression (grid->S/F run is counted).
__declspec(dllexport) int MXBMRP3_Test_LapTimerFromRaceStart() {
    return PluginData::getInstance().isLapTimerAnchoredFromRaceStart() ? 1 : 0;
}

// Whether the display rider is in the standing (grid) start grace window - from the green flag,
// through the gate hold, until it clears the first split. Both the wrong-way notice and the
// grid-hazard suppression key on this, so it's the single behavioural signal to assert.
__declspec(dllexport) int MXBMRP3_Test_InGridStartGrace() {
    return PluginData::getInstance().isInGridStartGrace() ? 1 : 0;
}

// Read a rider's computed real-time gap (ms). This is internal plugin state that
// the RaceTrackPosition pipeline computes for the in-game StandingsHud live-gap
// mode; it is NOT emitted in the /api/state JSON, so it's read directly here.
// Returns the leader-relative live gap (0 for the leader), or -1 if unknown.
__declspec(dllexport) int MXBMRP3_Test_GetRealTimeGap(int raceNum) {
    const StandingsData* s = PluginData::getInstance().getStanding(raceNum);
    return s ? s->realTimeGap : -1;
}

// Whether the rider is in the "recently seen in a RaceTrackPosition batch" set
// that feeds liveGapValid. Internal state (the JSON flag ANDs this with other
// conditions, so it can't be observed in isolation there) — used to pin that
// removeRaceEntry() evicts a departed rider so a raceNum reuse doesn't inherit
// a stale "active" bit.
__declspec(dllexport) int MXBMRP3_Test_HasActiveTrackPos(int raceNum) {
    return PluginData::getInstance().hasActiveTrackPos(raceNum) ? 1 : 0;
}

// Number of riders in the derived hazard-ahead list (the cached vector NoticesHud
// consumes). Internal state (not in /api/state) — used to pin that removeRaceEntry()
// invalidates the cache: no callbacks arrive while the player sits in menus, so a
// departed rider left in the cached list would linger there indefinitely.
__declspec(dllexport) int MXBMRP3_Test_HazardRaceNumCount() {
    return static_cast<int>(PluginData::getInstance().getHazardRaceNums().size());
}

// Run the update extract/install pipeline against destDir with an in-memory ZIP,
// bypassing the WinHTTP download. Exercises the real backup → extract → verify →
// rollback path (and the locked-file retry). Returns 1 on success, 0 on failure;
// the error message is copied into errOut. Lets a Wine test hold a handle on the
// target .dlo and assert clean abort + rollback under a lock.
__declspec(dllexport) int MXBMRP3_Test_ExtractAndInstall(const char* destDir,
        const char* zipData, int zipLen, char* errOut, int errCap) {
    std::string err;
    std::vector<char> bytes;
    if (zipData && zipLen > 0) bytes.assign(zipData, zipData + zipLen);
    const bool ok = UpdateDownloader::getInstance().testExtractAndInstall(
        destDir ? destDir : "", bytes, err);
    if (errOut && errCap > 0) {
        strncpy(errOut, err.c_str(), errCap - 1);
        errOut[errCap - 1] = '\0';
    }
    return ok ? 1 : 0;
}

// --- Per-surface HUD decoupling (companion instance) ------------------------
// The companion window carries its OWN on/off + position per HUD, independent of
// the in-game surface (see base_hud.h). That divergence lives in private BaseHud
// members that never reach /api/state, and the settings-INI base section round-
// trips verbatim (so a persistence test can't distinguish "captured from the live
// HUD" from "passed through"). These hooks drive and read the LIVE StandingsHud so
// the runtime semantics (mirror-while-unconfigured, snapshot-on-first-edit, clear-
// reverts-to-mirror) and the real save/load round-trip are asserted directly.
__declspec(dllexport) void MXBMRP3_Test_StandingsSetVisible(int visible) {
    HudManager::getInstance().getStandingsHud().setVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_StandingsSetCompanionVisible(int visible) {
    HudManager::getInstance().getStandingsHud().setCompanionVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_StandingsClearCompanion() {
    HudManager::getInstance().getStandingsHud().clearCompanionState();
}

// Session Charts HUD: drive visibility and which charts are shown for headless render
// checks / screenshots (the HUD is off by default and its charts are chosen via
// checkboxes in the settings tab). mask is a bitmask of ChartFlags
// (1=Lap 2=Trace 4=Gap 8=Pace; 15=all).
__declspec(dllexport) void MXBMRP3_Test_SessionChartsSetVisible(int visible) {
    HudManager::getInstance().getSessionChartsHud().setVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_SessionChartsSetCharts(int mask) {
    HudManager::getInstance().getSessionChartsHud().setEnabledCharts(static_cast<uint32_t>(mask));
}

// Read the StandingsHud's surface state in one call: whether the companion
// instance has diverged (configured), the companion-surface visibility (mirrors
// the game while unconfigured), and the in-game visibility. Any out-pointer may
// be null.
__declspec(dllexport) void MXBMRP3_Test_StandingsCompanionState(
        int* configured, int* companionVisible, int* gameVisible) {
    const BaseHud& hud = HudManager::getInstance().getStandingsHud();
    if (configured)       *configured       = hud.isCompanionConfigured() ? 1 : 0;
    if (companionVisible) *companionVisible = hud.getCompanionVisible() ? 1 : 0;
    if (gameVisible)      *gameVisible      = hud.isVisible() ? 1 : 0;
}

// --- Surface render routing (game vs companion frame) -----------------------
// The riskiest new code is collectSurface + draw()'s per-target routing: the
// game frame is suppressed in COMPANION mode (except with settings open), the
// companion frame filters HUDs by their companion visibility and translates each
// by its (companion - game) offset delta, and the window's X-button falls the
// target back to In-game. None of that is in /api/state, so these hooks set the
// display target and read the collected frames directly.
__declspec(dllexport) void MXBMRP3_Test_SetDisplayTarget(int target) {
    UiConfig::getInstance().setDisplayTarget(static_cast<DisplayTarget>(target));
}

__declspec(dllexport) int MXBMRP3_Test_GetDisplayTarget() {
    return static_cast<int>(UiConfig::getInstance().getDisplayTarget());
}

// Sizes of the last-collected game and companion frames, plus the sum of each
// frame's quad leading-corner X (a cheap fingerprint that shifts when the
// companion offset-delta translation moves a HUD). Any out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_SurfaceFrameStats(
        int* gameQuads, int* companionQuads, double* gameSumX, double* companionSumX) {
    const HudManager& hm = HudManager::getInstance();
    const auto& g = hm.getGameQuads();
    const auto& c = hm.getCompanionQuads();
    if (gameQuads)      *gameQuads      = static_cast<int>(g.size());
    if (companionQuads) *companionQuads = static_cast<int>(c.size());
    if (gameSumX)      { double s = 0; for (const auto& q : g) s += q.m_aafPos[0][0]; *gameSumX = s; }
    if (companionSumX) { double s = 0; for (const auto& q : c) s += q.m_aafPos[0][0]; *companionSumX = s; }
}

// Move the StandingsHud on the companion surface (configures its instance), so a
// test can exercise the offset-delta translation independent of visibility.
__declspec(dllexport) void MXBMRP3_Test_StandingsSetCompanionOffset(float x, float y) {
    HudManager::getInstance().getStandingsHud().setCompanionPosition(x, y);
}

// Simulate the user closing the companion window with its X button (sets the
// consumed userClosed flag the game thread checks in draw()).
__declspec(dllexport) void MXBMRP3_Test_CompanionSimulateUserClose() {
    CompanionWindow::getInstance().requestClose();
}

// Force a fake connected controller (with a few buttons/sticks engaged) and show the
// gamepad widget, so the companion-window preview harness can screenshot the gamepad
// layout headless (no real controller). connected=0 clears the override.
__declspec(dllexport) void MXBMRP3_Test_FakeGamepad(int connected) {
    if (connected) {
        XInputData d;
        d.isConnected = true;
        d.leftTrigger = 0.6f; d.rightTrigger = 0.35f;
        d.leftStickX = 0.5f;  d.leftStickY = -0.4f;
        d.rightStickX = -0.3f; d.rightStickY = 0.2f;
        d.buttonA = true; d.buttonY = true; d.dpadUp = true; d.leftShoulder = true;
        d.buttonStart = true;
        XInputReader::getInstance().testForceData(d);
    } else {
        XInputReader::getInstance().testClearForcedData();
    }
    HudManager::getInstance().getGamepadWidget().setVisible(connected != 0);
}

// Where the gamepad's content sits inside its controller frame, as the fraction of
// the frame (quad[0], the background box) spanned from its top to the LOWEST-RIGHT
// content pixel: out[0] = bottom-most content Y fraction, out[1] = right-most X
// fraction. The buttons are positioned off the interior line height; when that grew
// relative to the fontSize-sized frame (#256), the content slid down/right and these
// fractions jumped. So they're a stable golden signature of the layout: the fix pins
// them, a re-detaching change moves them. Requires a faked connected controller.
__declspec(dllexport) void MXBMRP3_Test_GamepadContentExtent(float* outBottom, float* outRight) {
    if (outBottom) *outBottom = -1.0f;
    if (outRight)  *outRight  = -1.0f;
    const auto& quads = HudManager::getInstance().getGamepadWidget().getQuads();
    if (quads.size() < 2) return;
    auto extent = [](const SPluginQuad_t& q, float& x0, float& y0, float& x1, float& y1) {
        x0 = y0 = 1e9f; x1 = y1 = -1e9f;
        for (int i = 0; i < 4; ++i) {
            float x = q.m_aafPos[i][0], y = q.m_aafPos[i][1];
            if (x < x0) x0 = x; if (x > x1) x1 = x;
            if (y < y0) y0 = y; if (y > y1) y1 = y;
        }
    };
    float bx0, by0, bx1, by1;
    extent(quads[0], bx0, by0, bx1, by1);
    float bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0) return;
    float maxY = -1e9f, maxX = -1e9f;
    for (size_t i = 1; i < quads.size(); ++i) {
        float x0, y0, x1, y1;
        extent(quads[i], x0, y0, x1, y1);
        if (y1 > maxY) maxY = y1;
        if (x1 > maxX) maxX = x1;
    }
    if (outBottom) *outBottom = (maxY - by0) / bh;
    if (outRight)  *outRight  = (maxX - bx0) / bw;
}

// Pin the active input surface (0=Game, 1=Companion, -1=off) so the headless preview
// can screenshot surface-scoped rendering (e.g. the settings menu, which only draws on
// the active surface — and the companion window never takes focus, so it's never the
// foreground-derived active surface under Wine).
__declspec(dllexport) void MXBMRP3_Test_ForceActiveSurface(int surface) {
    InputManager::getInstance().testForceActiveSurface(surface);
}

// Comma-joined, sorted list of the section names captureToCache() produces for the
// current live HUDs. settings_sections_test asserts every one of them is actually
// serialized to the INI — guarding against the "third hardcoded list" trap where a
// HUD is captured/applied but missing from serializeSettings' order list (the bug
// that silently dropped FriendsHud). See CLAUDE.md "Adding a New HUD" step 6.
__declspec(dllexport) void MXBMRP3_Test_CapturedSections(char* out, int cap) {
    if (!out || cap <= 0) return;
    const auto names = SettingsManager::getInstance()
        .testCapturedSectionNames(HudManager::getInstance());
    std::string joined;
    for (const auto& n : names) { if (!joined.empty()) joined += ','; joined += n; }
    strncpy(out, joined.c_str(), cap - 1);
    out[cap - 1] = '\0';
}

// --- Map HUD perf isolation --------------------------------------------------
// The map is enabled by default and rebuilds on every rider-position update, so
// it dominates Draw on a full grid. These hooks let the interleaved perf driver
// toggle the map on/off and switch its view mode (rotate-to-player / zoom, which
// defeat the ribbon-quad cache) so the map's per-frame rebuild cost — and the
// cache's effect — can be measured in isolation against a map-off baseline.
__declspec(dllexport) void MXBMRP3_Test_MapSetVisible(int visible) {
    HudManager::getInstance().getMapHud().setVisible(visible != 0);
}
__declspec(dllexport) void MXBMRP3_Test_MapSetRotate(int on) {
    HudManager::getInstance().getMapHud().setRotateToPlayer(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_MapSetZoom(int on) {
    HudManager::getInstance().getMapHud().setZoomEnabled(on != 0);
}
// Legacy preset shim, kept so older drivers/tests keep meaning the same thing:
// 0=AUTO (adaptive, 100%), 1=HIGH (fixed, 200% = 1.0m), 2=LOW (fixed, 60% ≈
// 3.3m). New code uses the percent/adaptive hooks below.
__declspec(dllexport) void MXBMRP3_Test_MapSetDetail(int detail) {
    MapHud& map = HudManager::getInstance().getMapHud();
    switch (detail) {
        case 1:  map.setAdaptiveDetail(false); map.setDetailScale(2.0f);  break;
        case 2:  map.setAdaptiveDetail(false); map.setDetailScale(0.6f);  break;
        default: map.setAdaptiveDetail(true);  map.setDetailScale(1.0f); break;  // old AUTO == 100%, not the new default
    }
}
// Detail scale as the settings row shows it: a percentage (20-200).
__declspec(dllexport) void MXBMRP3_Test_MapSetDetailPct(int pct) {
    HudManager::getInstance().getMapHud().setDetailScale(static_cast<float>(pct) / 100.0f);
}
// Outline control as the combined settings row drives it: 0 = off, else on with
// the rim width at pct/100 (100 = the classic 1.4x pass multiplier).
__declspec(dllexport) void MXBMRP3_Test_MapSetOutline(int pct) {
    MapHud& map = HudManager::getInstance().getMapHud();
    if (pct <= 0) { map.setShowOutline(false); return; }
    map.setShowOutline(true);
    map.setOutlineWidthScale(static_cast<float>(pct) / 100.0f);
}
__declspec(dllexport) void MXBMRP3_Test_MapSetAdaptive(int on) {
    HudManager::getInstance().getMapHud().setAdaptiveDetail(on != 0);
}
// Read back the detail state as percent + 1000*adaptive (e.g. 1100 = adaptive
// 100%, 60 = fixed 60%), so the legacy-INI migration can be asserted headless.
__declspec(dllexport) int MXBMRP3_Test_MapDetailState() {
    const MapHud& map = HudManager::getInstance().getMapHud();
    int pct = static_cast<int>(map.getDetailScale() * 100.0f + 0.5f);
    return pct + (map.getAdaptiveDetail() ? 1000 : 0);
}
// Map render stats: quad count + a position checksum (sum of every quad vertex
// X and Y) + a non-finite flag. The checksum is a cheap geometry fingerprint used
// to assert the world-ribbon cache is transparent (identical quads across mode
// round-trips) and that no quad carries NaN/Inf (the degenerate-track divide-by-
// zero). anyNonFinite/sums may be null.
__declspec(dllexport) int MXBMRP3_Test_MapQuadStats(double* sumX, double* sumY, int* anyNonFinite) {
    const auto& quads = HudManager::getInstance().getMapHud().getQuads();
    double sx = 0, sy = 0; int bad = 0;
    for (const auto& q : quads) {
        for (int i = 0; i < 4; ++i) {
            float x = q.m_aafPos[i][0], y = q.m_aafPos[i][1];
            if (!std::isfinite(x) || !std::isfinite(y)) bad = 1;
            sx += x; sy += y;
        }
    }
    if (sumX) *sumX = sx;
    if (sumY) *sumY = sy;
    if (anyNonFinite) *anyNonFinite = bad;
    return static_cast<int>(quads.size());
}
// Sum-of-SQUARES variant of the checksum above. The plain sum is blind to ribbon
// WIDTH changes (left/right edges move symmetrically, center ± w*perp, so the
// width term cancels); the squared sum keeps it (2c^2 + 2w^2), so tests can
// assert that a width-only change actually moved the vertices.
__declspec(dllexport) int MXBMRP3_Test_MapQuadSumSq(double* sumSqX, double* sumSqY) {
    const auto& quads = HudManager::getInstance().getMapHud().getQuads();
    double sx2 = 0, sy2 = 0;
    for (const auto& q : quads) {
        for (int i = 0; i < 4; ++i) {
            double x = q.m_aafPos[i][0], y = q.m_aafPos[i][1];
            sx2 += x * x; sy2 += y * y;
        }
    }
    if (sumSqX) *sumSqX = sx2;
    if (sumSqY) *sumSqY = sy2;
    return static_cast<int>(quads.size());
}
// Read + reset the accumulated per-phase rebuild time (microseconds), rebuild
// count (return value), and ribbon-cache hit/miss counts. Attributes the map's
// per-frame cost to bounds/layout vs ribbon vs markers vs riders.
__declspec(dllexport) long long MXBMRP3_Test_MapProfile(
        double* boundsUs, double* ribbonUs, double* markersUs, double* ridersUs,
        long long* ribbonHits, long long* ribbonMiss) {
    double b = 0, r = 0, m = 0, ri = 0; long long c = 0, hits = 0, miss = 0;
    mapHudReadProfile(b, r, m, ri, c, hits, miss);
    if (boundsUs) *boundsUs = b;
    if (ribbonUs) *ribbonUs = r;
    if (markersUs) *markersUs = m;
    if (ridersUs) *ridersUs = ri;
    if (ribbonHits) *ribbonHits = hits;
    if (ribbonMiss) *ribbonMiss = miss;
    return c;
}

// Toggle the developer BenchmarkWidget (always created; normally gated behind
// developer mode in the UI). on=1 activates the built-in profiler (per-callback +
// per-HUD rebuild timing) and resets its counters; on=0 hides it, which exports a
// timestamped report to <savePath>/mxbmrp3/benchmarks/. Lets the headless bench
// driver capture a whole-plugin timing breakdown using the plugin's own component.
__declspec(dllexport) void MXBMRP3_Test_BenchmarkWidget(int on) {
    BenchmarkWidget* bw = HudManager::getInstance().getBenchmarkWidget();
    if (bw) bw->setVisible(on != 0);
}

// Force every HUD/widget visible (or hidden) so the benchmark driver can profile
// the plugin with EVERYTHING enabled, not just the default-on HUDs.
__declspec(dllexport) void MXBMRP3_Test_ShowAllHuds(int on) {
    HudManager::getInstance().testSetAllHudsVisible(on != 0);
}

// Crank the heavy HUDs' individual settings to maximum (all columns/rows/events,
// max row counts, long names, highest map detail) for worst-case profiling.
__declspec(dllexport) void MXBMRP3_Test_MaxHudSettings() {
    SettingsManager::getInstance().testMaxAllHudSettings(HudManager::getInstance());
}

// Read + reset the accumulated per-phase StandingsHud::rebuildRenderData() time
// (microseconds): setup / format / name+anim / layout / render; return value is
// the rebuild count. Attributes the standings rebuild cost for the perf probe.
__declspec(dllexport) long long MXBMRP3_Test_StandingsProfile(
        double* setupUs, double* formatUs, double* nameAnimUs, double* layoutUs, double* renderUs) {
    double se = 0, fo = 0, na = 0, la = 0, re = 0; long long c = 0;
    standingsReadProfile(se, fo, na, la, re, c);
    if (setupUs) *setupUs = se;
    if (formatUs) *formatUs = fo;
    if (nameAnimUs) *nameAnimUs = na;
    if (layoutUs) *layoutUs = la;
    if (renderUs) *renderUs = re;
    return c;
}

// Sub-phase of render: total us spent resolving the TRACKED-column status icon
// per rider since last read (the target of option-1 status caching).
__declspec(dllexport) double MXBMRP3_Test_StandingsTrackedUs() {
    return standingsReadTrackedUs();
}

// Experimental plugin worker thread: turn it on AFTER Startup (the flag is normally
// read once at init) and start the worker, so a test can drive callbacks through the
// off-thread offload path. Flush() blocks until the worker has drained everything
// queued so far, so the test can then read a deterministic snapshot(). See
// core/plugin_thread.{h,cpp} and tests/integration/tests/plugin_thread_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_PluginThreadEnable() {
    UiConfig::getInstance().setPluginThread(true);
    PluginThread::getInstance().start();
}
__declspec(dllexport) int MXBMRP3_Test_PluginThreadEnabled() {
    return PluginThread::getInstance().enabled() ? 1 : 0;
}
// Set ONLY the [Advanced] flag (as a live INI reload would) without starting/stopping
// the worker — so a test can exercise the game-thread reconcileEnabled() switch that a
// RELOAD_CONFIG hotkey triggers, by then driving a draw().
__declspec(dllexport) void MXBMRP3_Test_SetPluginThreadFlag(int on) {
    UiConfig::getInstance().setPluginThread(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_PluginThreadFlush() {
    PluginThread::getInstance().flush();
}
// Fault injection: kill the worker with an exception that escapes threadMain(),
// so the abort self-heal (inline fallback + reconcileEnabled join/drain/latch)
// can be asserted. See plugin_thread_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_PluginThreadAbortWorker() {
    PluginThread::getInstance().testAbortWorker();
}
__declspec(dllexport) void MXBMRP3_Test_PluginThreadStop() {
    // Clear the flag too, so the game-thread reconcileEnabled() (in handleDraw) doesn't
    // immediately restart the worker on the next draw.
    UiConfig::getInstance().setPluginThread(false);
    PluginThread::getInstance().stop();
}
// Inject an artificial per-frame stall into the render build (produceFrame), to
// demonstrate the game-thread isolation: this cost is paid inside Draw in sync mode
// but on the worker in plugin-thread mode.
__declspec(dllexport) void MXBMRP3_Test_SetProduceDelayMs(int ms) {
    HudManager::testSetProduceDelayMs(ms);
}
// XInput I/O thread: stop it so a test can inspect the rumble command setVibration
// posts without the worker draining it; drive index/vibration; read the pending post.
// Proves the send policy + quantization survive moving XInputSetState off-thread.
__declspec(dllexport) void MXBMRP3_Test_XInputStopIo() {
    XInputReader::getInstance().stopIoThread();
}
__declspec(dllexport) void MXBMRP3_Test_XInputSetIndex(int idx) {
    XInputReader::getInstance().setControllerIndex(idx);
}
__declspec(dllexport) void MXBMRP3_Test_XInputVibrate(float left, float right) {
    XInputReader::getInstance().setVibration(left, right);
}
__declspec(dllexport) int MXBMRP3_Test_XInputConsumePending(int* left8, int* right8, int* idx) {
    int a = 0, b = 0, c = 0;
    bool has = XInputReader::getInstance().testConsumePendingRumble(a, b, c);
    if (left8) *left8 = a;
    if (right8) *right8 = b;
    if (idx) *idx = c;
    return has ? 1 : 0;
}
// --- Rumble effect math seam. updateRumbleFromTelemetry() runs on every real
// RunTelemetry (the handler derives the spike/slip inputs from the raw frame),
// but its outputs — the per-channel contributions and the combined motor values,
// i.e. the numbers users tune in the Rumble tab — are in-game-only (the rumble
// graph + the motor feed), never in /api/state. These read them back, and flip
// the per-bike-profile mode / reload the profile JSON without a plugin restart,
// so rumble_effect_test can pin the math and the JSON load/fallback. The send
// POLICY stays pinned separately by xinput_thread_test. ---
__declspec(dllexport) void MXBMRP3_Test_RumbleSetPerBike(int on) {
    XInputReader::getInstance().getGlobalRumbleConfig().usePerBikeEffects = on != 0;
}
// Master enable. Off (the default), updateRumbleFromTelemetry still computes
// every per-channel value (the graph stays live) but feeds the motors 0 — so a
// test asserting the COMBINED heavy/light values must switch it on.
__declspec(dllexport) void MXBMRP3_Test_RumbleSetEnabled(int on) {
    XInputReader::getInstance().getGlobalRumbleConfig().enabled = on != 0;
}
__declspec(dllexport) void MXBMRP3_Test_RumbleLoadProfiles(const char* savePath) {
    RumbleProfileManager::getInstance().load(savePath ? savePath : "");
}
__declspec(dllexport) int MXBMRP3_Test_RumbleHasProfile() {
    return RumbleProfileManager::getInstance().hasProfileForCurrentBike() ? 1 : 0;
}
__declspec(dllexport) void MXBMRP3_Test_RumbleChannels(
        float* heavy, float* light, float* susp, float* suspRear, float* spin,
        float* lock, float* lockRear, float* wheelie, float* rpm, float* slide,
        float* surface, float* steer) {
    const XInputReader& xi = XInputReader::getInstance();
    if (heavy)    *heavy    = xi.getLastHeavyMotor();
    if (light)    *light    = xi.getLastLightMotor();
    if (susp)     *susp     = xi.getLastSuspensionRumble();
    if (suspRear) *suspRear = xi.getLastSuspensionRumbleRear();
    if (spin)     *spin     = xi.getLastWheelspinRumble();
    if (lock)     *lock     = xi.getLastLockupRumble();
    if (lockRear) *lockRear = xi.getLastLockupRumbleRear();
    if (wheelie)  *wheelie  = xi.getLastWheelieRumble();
    if (rpm)      *rpm      = xi.getLastRpmRumble();
    if (slide)    *slide    = xi.getLastSlideRumble();
    if (surface)  *surface  = xi.getLastSurfaceRumble();
    if (steer)    *steer    = xi.getLastSteerRumble();
}
// Read the live PerformanceHud metrics (fps / plugin ms / plugin %), to assert they
// stay live in plugin-thread mode (the worker publishes them, not DrawHandler).
__declspec(dllexport) void MXBMRP3_Test_GetDebugMetrics(float* fps, float* pluginMs, float* pct) {
    const auto& m = PluginData::getInstance().getDebugMetrics();
    if (fps) *fps = m.currentFps;
    if (pluginMs) *pluginMs = m.pluginTimeMs;
    if (pct) *pct = m.pluginPercent;
}

#if GAME_HAS_FMX
// --- FMX trick-detection seam. FmxManager's whole state machine runs on the wall
// clock (dt integration, the 0.5s airborne/ground debounces, the 0.75s landing
// grace, the 2s chain window), so back-to-back headless callbacks give dt≈0 and
// nothing ever advances. The injectable clock (Fmx::clockNow) lets a test step
// simulated time with each telemetry frame; this hook sets it (µs; -1 restores
// the real clock). ---
__declspec(dllexport) void MXBMRP3_Test_FmxSetNowUs(long long us) {
    Fmx::testSetNowUs(us);
}

// Read the FMX score/chain/active-trick state in one call. The FMX score is
// in-game-only (never in /api/state), so detection results are read directly.
// lastTrickType is the most recent trick banked into the chain — or, once the
// chain has completed/failed (which moves the chain into the end animation),
// the final type snapshotted there. Any out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_FmxState(int* sessionScore, int* tricksCompleted,
        int* tricksFailed, int* chainCount, int* chainScore,
        int* activeState, int* activeType, int* lastTrickType) {
    const FmxManager& fmx = FmxManager::getInstance();
    const Fmx::FmxScore& score = fmx.getScore();
    if (sessionScore)    *sessionScore    = score.sessionScore;
    if (tricksCompleted) *tricksCompleted = score.tricksCompleted;
    if (tricksFailed)    *tricksFailed    = score.tricksFailed;
    if (chainCount)      *chainCount      = score.chainCount;
    if (chainScore)      *chainScore      = score.chainScore;
    if (activeState)     *activeState     = static_cast<int>(fmx.getActiveTrick().state);
    if (activeType)      *activeType      = static_cast<int>(fmx.getActiveTrick().type);
    if (lastTrickType) {
        const auto& chain = fmx.getChainTricks();
        *lastTrickType = static_cast<int>(chain.empty()
            ? fmx.getChainEndAnimation().finalType
            : chain.back().type);
    }
}
#endif

// --- Stats odometer seam. Distance integrates speed over the WALL-CLOCK gap
// between telemetry calls, so the odometer test injects the clock (µs; -1
// restores the real one) to make each tick's dt — and the expected distance —
// exact. ---
__declspec(dllexport) void MXBMRP3_Test_StatsSetNowUs(long long us) {
    StatsManager::testSetNowUs(us);
}

// Read the live odometer state: the current bike's odometer + the session trip
// (both meters), plus the ~100m dirty-coalescing internals (distance accumulated
// since the last dirty mark, and the dirty flag itself) — neither observable
// through the stats file, because a save only ever happens off-track. Any
// out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_StatsOdometerState(double* bikeOdometer,
        double* sessionTrip, double* unsavedDistance, int* dirty) {
    const StatsManager& sm = StatsManager::getInstance();
    if (bikeOdometer)    *bikeOdometer    = sm.getOdometerForCurrentBike();
    if (sessionTrip)     *sessionTrip     = sm.getSessionTripDistance();
    if (unsavedDistance) *unsavedDistance = sm.testUnsavedDistance();
    if (dirty)           *dirty           = sm.testIsDirty() ? 1 : 0;
}

// Force a stats save (the same save() the RunStop/RunDeinit leave-track flush
// calls; a no-op when clean). Lets a test establish a known-clean baseline
// before asserting the dirty-coalescing behaviour.
__declspec(dllexport) void MXBMRP3_Test_StatsSave() {
    StatsManager::getInstance().save();
}

#if GAME_HAS_RECORDS_PROVIDER
// --- Records fetch/parse seam. The records fetch is user/auto-triggered network
// I/O whose response parsing was previously only testable live in-game. These
// hooks (a) run a canned response body through the REAL parse path and read the
// parsed records back (records_parse_test), and (b) arm a stubbed fetch worker
// (sleep + canned response, no network) so a test can hold a fetch in flight
// and pin the join contract: HudManager::clear() joins the fetch thread BEFORE
// nulling the cached HUD pointers the worker touches on completion (TimingHud). ---
//
// Parse `json` as `provider` (0=CBR, 1=MXB_RANKED) through the real parse path.
// Returns the parsed record count, or -1 on a parse error.
__declspec(dllexport) int MXBMRP3_Test_RecordsParse(int provider, const char* json) {
    RecordsHud& hud = HudManager::getInstance().getRecordsHud();
    if (!hud.testParseResponse(provider, json ? json : "")) return -1;
    return hud.testRecordCount();
}
// Current parsed-record count (readable independent of the last parse result).
__declspec(dllexport) int MXBMRP3_Test_RecordsCount() {
    return HudManager::getInstance().getRecordsHud().testRecordCount();
}
// Copy one parsed record out (strings truncated to the caller's caps; any
// out-pointer may be null). Returns 1 if index is valid, 0 otherwise.
__declspec(dllexport) int MXBMRP3_Test_RecordsGet(int index,
        char* rider, int riderCap, char* bike, int bikeCap,
        int* laptime, int* s1, int* s2, int* s3, char* date, int dateCap) {
    RecordsHud::RecordEntry e;
    if (!HudManager::getInstance().getRecordsHud().testGetRecord(index, e)) return 0;
    auto copy = [](char* dst, int cap, const char* src) {
        if (!dst || cap <= 0) return;
        strncpy(dst, src, cap - 1);
        dst[cap - 1] = '\0';
    };
    copy(rider, riderCap, e.rider);
    copy(bike, bikeCap, e.bike);
    copy(date, dateCap, e.date);
    if (laptime) *laptime = e.laptime;
    if (s1) *s1 = e.sector1;
    if (s2) *s2 = e.sector2;
    if (s3) *s3 = e.sector3;
    return 1;
}
// Arm/disarm the fetch-worker stub (delayMs < 0 disarms): the worker sleeps,
// then completes with `response` through the normal parse/notify path.
__declspec(dllexport) void MXBMRP3_Test_RecordsSetFetchStub(int delayMs, const char* response) {
    RecordsHud::testSetFetchStub(delayMs, response ? response : "");
}
// Start a real fetch (same cooldown/state gate as the Compare button).
// Returns 1 if a fetch is now in flight, 0 if the gate refused it.
__declspec(dllexport) int MXBMRP3_Test_RecordsStartFetch() {
    RecordsHud& hud = HudManager::getInstance().getRecordsHud();
    hud.testStartFetch();
    return hud.testFetchState() == static_cast<int>(RecordsHud::FetchState::FETCHING) ? 1 : 0;
}
// Fetch state as int (0=IDLE, 1=FETCHING, 2=SUCCESS, 3=FETCH_ERROR).
__declspec(dllexport) int MXBMRP3_Test_RecordsFetchState() {
    return HudManager::getInstance().getRecordsHud().testFetchState();
}
#endif

#if GAME_HAS_RECORDER
// Callback-tape recorder: open a tape at an explicit path and finalize it. Lets a
// test record the live callback stream it drives, then replay the produced tape
// back (round-trip) to prove the in-plugin recorder writes a harness-readable tape.
// startRecording bypasses the [Recorder] enabled gate (beginSessionRecording is the
// gated entry point); the replayer skips the Startup/Shutdown events, so no
// recordStartup is needed here.
__declspec(dllexport) int MXBMRP3_Test_StartRecording(const char* path) {
    return EventRecorder::getInstance().startRecording(path ? path : "") ? 1 : 0;
}
__declspec(dllexport) void MXBMRP3_Test_StopRecording() {
    EventRecorder::getInstance().stopRecording();
}
#endif

} // extern "C"

#endif // MXBMRP3_TEST_BUILD
