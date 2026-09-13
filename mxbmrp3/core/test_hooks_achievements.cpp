// ============================================================================
// core/test_hooks_achievements.cpp
// The MXBMRP3_Test_* exports for the achievements ladder and the exploration
// signals behind it: tiers and values by catalogue id, the toast ledger, the
// prestige trade, the tracked-rider and settings switches those signals watch,
// and the two seams a headless run cannot reach any other way -- a signal set
// straight to a value, and a ladder stood one row short of finished.
//
// SPLIT OUT OF core/test_hooks.cpp, which had grown past its budget one export
// at a time; this is the one family in it a reader goes looking for by name.
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "achievements.h"
#include "achievement_manager.h"
#include "exploration_stats.h"
#include "exploration_signals.h"
#include "hud_manager.h"
#include "stats_manager.h"
#include "tracked_riders_manager.h"
#include "update_checker.h"
#include "../hud/achievement_widget.h"

#include <cstring>

extern "C" {

// --- Achievements. Tier and value by catalogue ID (achievements.h), the
// toast ledger (every toast ever queued this run, and the last one's text),
// and the RELOAD_CONFIG feed -- which HudManager reaches through the input
// path a headless run cannot drive, so the same entry point is exported here.
__declspec(dllexport) int MXBMRP3_Test_AchievementTier(const char* id) {
    const Achievements::Entry* e = Achievements::findById(id);
    if (!e) return -1;
    return AchievementManager::getInstance().stateOf(static_cast<int>(e - Achievements::kCatalogue)).tier;
}
__declspec(dllexport) double MXBMRP3_Test_AchievementValue(const char* id) {
    const Achievements::Entry* e = Achievements::findById(id);
    if (!e) return -1.0;
    return AchievementManager::getInstance().valueOf(static_cast<int>(e - Achievements::kCatalogue));
}
// Earned tiers over total tiers, both over the COUNTED rows
// (Achievements::countsTowardCompletion), so the pair cannot read past 100%.
__declspec(dllexport) void MXBMRP3_Test_AchievementUnits(int* earned, int* total) {
    const AchievementManager& a = AchievementManager::getInstance();
    if (earned) *earned = a.earnedUnits();
    if (total) *total = a.totalUnits();
}
// Achievements earned at any tier, and the listed total (the tab's summary).
__declspec(dllexport) void MXBMRP3_Test_AchievementRows(int* earned, int* total) {
    const AchievementManager& a = AchievementManager::getInstance();
    if (earned) *earned = a.earnedAchievements();
    if (total) *total = a.listedAchievements();
}
// The tab's "(+n)": earned rows outside the listed total.
__declspec(dllexport) int MXBMRP3_Test_AchievementBonus() {
    return AchievementManager::getInstance().bonusAchievements();
}
__declspec(dllexport) unsigned int MXBMRP3_Test_AchievementToastsQueued() {
    return AchievementManager::getInstance().toastsQueued();
}
// "title|detail" of the most recently queued toast.
__declspec(dllexport) void MXBMRP3_Test_AchievementLastToast(char* out, int cap) {
    if (!out || cap <= 0) return;
    const AchievementManager::Toast& t = AchievementManager::getInstance().lastToast();
    snprintf(out, static_cast<size_t>(cap), "%s|%s", t.title, t.detail);
}
__declspec(dllexport) void MXBMRP3_Test_ConfigReloaded() {
    AchievementManager::getInstance().onConfigReloaded();
}
// --- Prestige. Two hooks, because the two halves are separately worth driving:
// the LEVEL alone (so a test can put the Hat on screen, or read the tab's
// summary, without earning a hundred achievements first), and the real TRADE,
// which refuses unless the Platinum Sweep is earned and is the only thing that
// clears the counters.
// The "(Seven Oaks)" a per-track / per-bike maximum row carries beside its
// title -- empty for every other row. The tab draws exactly this string, so a
// test can pin WHO is carrying Local Hero without reading pixels.
__declspec(dllexport) void MXBMRP3_Test_AchievementLeader(const char* id, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    const Achievements::Entry* e = Achievements::findById(id);
    if (!e) return;
    AchievementManager::getInstance().leaderFor(*e, out, static_cast<size_t>(cap));
}
__declspec(dllexport) int MXBMRP3_Test_Prestige() {
    return StatsManager::getInstance().getPrestige();
}
__declspec(dllexport) void MXBMRP3_Test_SetPrestige(int level) {
    StatsManager::getInstance().setPrestige(level);
}
// 1 if the trade happened; 0 if it was refused (see StatsManager::prestige).
__declspec(dllexport) int MXBMRP3_Test_TakePrestige() {
    return StatsManager::getInstance().prestige() ? 1 : 0;
}
// 1 while the toast widget has a toast on screen (after a draw()).
__declspec(dllexport) int MXBMRP3_Test_AchievementToastShowing() {
    const AchievementWidget* w = HudManager::getInstance().getAchievementWidget();
    return (w && w->isShowing()) ? 1 : 0;
}
// The hide-all-HUDs hotkey's state, set directly: the widget must not take a
// toast while it cannot be drawn.
__declspec(dllexport) void MXBMRP3_Test_SetHudsEnabled(int enabled) {
    HudManager::getInstance().setHudsEnabled(enabled != 0);
}
// The Widgets master toggle, the same way.
__declspec(dllexport) void MXBMRP3_Test_SetWidgetsEnabled(int enabled) {
    HudManager::getInstance().setWidgetsEnabled(enabled != 0);
}
// One HUD's game-surface visibility, by harness id (testHudByName). 0 = no
// such HUD. With SetEveryHudVisible(0) first, a test can put exactly the HUDs
// it drives on screen, so none sits on top of another at the default layout.
__declspec(dllexport) int MXBMRP3_Test_SetHudVisible(const char* name, int visible) {
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (hud && name && std::strcmp(hud->getHarnessId(), name) == 0) {
            hud->setVisible(visible != 0);
            // As the checkbox would: the setup is observed at the switch.
            StatsManager::getInstance().exploration().observeSettings(HudManager::getInstance());
            return 1;
        }
    }
    return 0;
}
// Every registered HUD's game-surface visibility at once (Tyre Kicker counts
// the ones ever seen on at a settings save).
__declspec(dllexport) void MXBMRP3_Test_SetEveryHudVisible(int visible) {
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (hud) hud->setVisible(visible != 0);
    }
    StatsManager::getInstance().exploration().observeSettings(HudManager::getInstance());   // as the checkboxes would
}
// Put a rider on the tracked list, as the Riders tab does (Stalker).
__declspec(dllexport) int MXBMRP3_Test_TrackRider(const char* name) {
    return TrackedRidersManager::getInstance().addTrackedRider(name ? name : "") ? 1 : 0;
}
// The local clock the date-based exploration signals read (fixed in a test
// build until set here), and the once-a-second tick DrawHandler would fire.
__declspec(dllexport) void MXBMRP3_Test_SetLocalTime(int year, int month, int day, int hour) {
    ExplorationStats::setLocalTimeOverride(year, month, day, hour);
}
__declspec(dllexport) void MXBMRP3_Test_ExplorationTick(int spectating, int rumbleLive, int onTrack,
                                                        int moving, int frames, unsigned int overlayTotal) {
    StatsManager::getInstance().exploration().tick(spectating != 0, rumbleLive != 0, onTrack != 0,
                                                   moving != 0, frames, overlayTotal);
}

__declspec(dllexport) void MXBMRP3_Test_StatsSave() {
    StatsManager::getInstance().save();
}

// Set an exploration signal directly, by the JSON key it persists under, and
// re-evaluate. The tier/toast/halfway machinery needs a FOUR-TIER row it can
// step to an exact value, and the only cheap event hook (ConfigReloaded) now
// drives a one-shot - so without this the machinery tests would have to grind a
// real counter to its Gold midpoint. Feed correctness is exploration_test's
// job; these tests are about what the manager does with a value once it moves.
__declspec(dllexport) void MXBMRP3_Test_ExplorationSet(const char* signalKey, double value) {
    if (!signalKey) return;
    for (const Exploration::SignalInfo& si : Exploration::kSignals) {
        if (std::strcmp(si.key, signalKey) != 0) continue;
        StatsManager::getInstance().exploration().restoreValue(static_cast<int>(si.signal), value);
        AchievementManager::getInstance().onStatsChanged();
        return;
    }
}

// Every counted row at a tier except one, so a test can stand a ladder one row
// short of finished without earning a hundred achievements. Writes state and
// evaluates nothing on purpose: the row left out has to arrive through the real
// evaluation, which is where the Sweeps' own ordering is decided.
__declspec(dllexport) void MXBMRP3_Test_AchievementForceTiers(int tier, const char* exceptId) {
    AchievementManager::getInstance().testForceTiers(tier, exceptId);
}

// The update channel. Early Access is marked off the LIVE setting, and the only
// thing that changes it in a running plugin is a click on the Updates tab, so a
// test needs the same mid-session switch a player makes - an INI written before
// startup would go through a different door entirely.
__declspec(dllexport) void MXBMRP3_Test_SetUpdateChannel(int prerelease) {
    UpdateChecker::getInstance().setChannel(prerelease
        ? UpdateChecker::UpdateChannel::PRERELEASE
        : UpdateChecker::UpdateChannel::STABLE);
}

} // extern "C"

#endif // MXBMRP3_TEST_BUILD
