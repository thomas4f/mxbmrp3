// ============================================================================
// hud/settings/settings_tab_director.cpp
// Tab renderer for the auto-director (spectate broadcast tool).
//
// The director is a global manager, not a HUD, so this tab drives
// DirectorManager::getInstance() directly and returns nullptr (no backing HUD),
// mirroring the Updates tab. Clicks are handled by the common dispatcher in
// settings_hud.cpp (DIRECTOR_ENABLE_TOGGLE / DIRECTOR_MINSHOT_* / DIRECTOR_HOLD_* /
// DIRECTOR_BATTLEGAP_*).
//
// Layout mirrors the Rumble/Helmet tabs: the on-screen status-button widget's
// Appearance (Visible/Opacity/Scale) leads, then the director's own settings.
//
// A couple of settings are INI-only tunables (no row here): the incident hold cap
// (incidentMaxSec) and the Forks variety camera (camForks, default off) - both still
// persist in the [Director] section and can be hand-edited.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/director_manager.h"
#include "../../core/hud_manager.h"
#include "../director_widget.h"

#include <cstdio>

BaseHud* SettingsHud::renderTabDirector(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("director");

    DirectorManager& director = DirectorManager::getInstance();
    const bool on = director.isEnabled();

    char buf[16];

    // --- Appearance: the on-screen status button (a separate draggable widget).
    // Leads the tab like Rumble/Helmet; independent of the director being enabled. ---
    ctx.addSectionHeader("Appearance");
    DirectorWidget* hud = HudManager::getInstance().getDirectorWidget();
    const bool shown = hud && hud->isVisible();
    ctx.addToggleControl("Visible", shown,
        SettingsHud::ClickRegion::DIRECTOR_HUD_VISIBLE, nullptr,
        nullptr, 0, true, "director.hud_visible");
    if (hud) {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(hud->getBackgroundOpacity() * 100.0f + 0.5f));
        ctx.addCycleControl("Opacity", buf, 10,
            SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN,
            SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP,
            hud, shown, false, "common.opacity");
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(hud->getScale() * 100.0f + 0.5f));
        ctx.addCycleControl("Scale", buf, 10,
            SettingsHud::ClickRegion::SCALE_DOWN,
            SettingsHud::ClickRegion::SCALE_UP,
            hud, shown, false, "common.scale");
    }

    // --- Director: master enable, shot pacing, and the field-wide position cutoff
    // (it gates battles, incidents, overtakes AND lappers, so it lives here). ---
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Director");
    ctx.addToggleControl("Enabled", on,
        SettingsHud::ClickRegion::DIRECTOR_ENABLE_TOGGLE, nullptr,
        nullptr, 0, true, "director.enabled");

    snprintf(buf, sizeof(buf), "%ds", director.getMinShotSec());
    ctx.addCycleControl("Min shot", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_MINSHOT_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_MINSHOT_UP,
        nullptr, on, false, "director.min_shot");

    snprintf(buf, sizeof(buf), "%ds", director.getMaxShotSec());
    ctx.addCycleControl("Max shot", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_MAXSHOT_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_MAXSHOT_UP,
        nullptr, on, false, "director.max_shot");

    // Story hold: how long a story shot (crash / fastest lap / hot lap) lingers - a
    // shot-duration knob, so it sits with Min/Max shot here rather than under Stories.
    snprintf(buf, sizeof(buf), "%ds", director.getHoldSec());
    ctx.addCycleControl("Story hold", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_HOLD_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_HOLD_UP,
        nullptr, on, false, "director.hold");

    const int bmp = director.getBattleMaxPos();
    if (bmp <= 0) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "P%d", bmp);
    ctx.addCycleControl("Field cutoff", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_BATTLEMAXPOS_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_BATTLEMAXPOS_UP,
        nullptr, on, bmp <= 0, "director.battle_max_pos");

    // Battle gap sits with Field cutoff (both are the shared battle *definition* that
    // also drives the overlay panel); the on/off "Follow battles" story is under Stories.
    snprintf(buf, sizeof(buf), "%.1fs", director.getBattleGapMs() / 1000.0);
    ctx.addCycleControl("Battle gap", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_BATTLEGAP_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_BATTLEGAP_UP,
        nullptr, on, false, "director.battle_gap");

    // --- Stories: high-value events that steer the subject. A battle is a story too,
    // so it gets a plain on/off (the Battle gap that *defines* a battle lives up in the
    // Director section, since it also drives the overlay panel).
    //
    // Ordered by the director's own rating/scoring (highest precedence first), so the
    // list reads as the priority the camera actually applies. Two tiers, from
    // DirectorManager::update():
    //   Priority interrupts (cut/return in this order): Incidents (cuts instantly) >
    //     Fastest lap > Fastest sectors (non-race) > Finish lock (locks the closing laps).
    //   Scored candidates (weight multiplier on posWeight): Catch overtakes (x3.0) >
    //     Follow battles (x2.0) > Follow drops (x1.6) > Follow lappers (x1.2).
    // A new story slots in by its score. ---
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Stories");

    ctx.addToggleControl("Follow incidents", director.getFollowIncidents(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_INCIDENTS, nullptr,
        nullptr, 0, on, "director.follow_incidents");
    ctx.addToggleControl("Fastest lap", director.getFollowFastestLap(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_FASTEST, nullptr,
        nullptr, 0, on, "director.follow_fastest");
    ctx.addToggleControl("Fastest sectors", director.getFollowPace(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_PACE, nullptr,
        nullptr, 0, on, "director.follow_pace");
    ctx.addToggleControl("Finish lock", director.getFinishLock(),
        SettingsHud::ClickRegion::DIRECTOR_FINISH_LOCK, nullptr,
        nullptr, 0, on, "director.finish_lock");
    ctx.addToggleControl("Catch overtakes", director.getCatchOvertakes(),
        SettingsHud::ClickRegion::DIRECTOR_CATCH_OVERTAKES, nullptr,
        nullptr, 0, on, "director.catch_overtakes");
    ctx.addToggleControl("Follow battles", director.getFollowBattles(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_BATTLES, nullptr,
        nullptr, 0, on, "director.follow_battles");
    ctx.addToggleControl("Follow drops", director.getFollowDrops(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_DROPS, nullptr,
        nullptr, 0, on, "director.follow_drops");
    ctx.addToggleControl("Follow lappers", director.getFollowLappers(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_LAPPERS, nullptr,
        nullptr, 0, on, "director.follow_lappers");

    // --- Onboard variety: how often the director dips off Trackside into an onboard,
    // and which onboards it may use. "Onboard every" is the cadence (0 = Off = always
    // Trackside); the camera toggles below it grey out when it's Off. Forward cams
    // (Front Fender / Helmet / Helmet 2) frame the rider ahead; Rear Fender frames a
    // chaser. (Forks is INI-only.) ---
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Onboard variety");

    const int ve = director.getVarietyEvery();
    const bool varietyOn = (ve > 0);
    if (!varietyOn) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "%d", ve);
    ctx.addCycleControl("Onboard every", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_VARIETY_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_VARIETY_UP,
        nullptr, on, !varietyOn, "director.variety_every");

    // The camera choices only matter while variety dips are on, so grey them out
    // (on && varietyOn) when "Onboard every" is Off.
    const bool camsOn = on && varietyOn;
    // The two fender views share one cycle: Off > Front > Rear > Both.
    const bool cf = director.getCamFront();
    const bool cr = director.getCamRear();
    const char* fenderVal = (!cf && !cr) ? "Off"
                          : ( cf && !cr) ? "Front"
                          : (!cf &&  cr) ? "Rear"
                          :                "Both";
    ctx.addCycleControl("Fender", fenderVal, 10,
        SettingsHud::ClickRegion::DIRECTOR_CAM_FENDER_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_CAM_FENDER_UP,
        nullptr, camsOn, (!cf && !cr), "director.cam_fender");
    // The two helmet views share one cycle: Off > Helmet 1 > Helmet 2 > Both.
    const bool h1 = director.getCamHelmet();
    const bool h2 = director.getCamHelmet2();
    const char* helmetVal = (!h1 && !h2) ? "Off"
                          : ( h1 && !h2) ? "Helmet 1"
                          : (!h1 &&  h2) ? "Helmet 2"
                          :                "Both";
    ctx.addCycleControl("Helmet", helmetVal, 10,
        SettingsHud::ClickRegion::DIRECTOR_CAM_HELMET_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_CAM_HELMET_UP,
        nullptr, camsOn, (!h1 && !h2), "director.cam_helmet");

    // --- Manual control: how the caster takes over and hands back (takeover first).
    // Last, below the cameras - it's the least-touched section. ---
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Manual control");

    ctx.addToggleControl("Gamepad takeover", director.getGamepadTakeover(),
        SettingsHud::ClickRegion::DIRECTOR_GAMEPAD_TAKEOVER, nullptr,
        nullptr, 0, on, "director.gamepad_takeover");

    const int resume = director.getManualResumeSec();
    if (resume <= 0) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "%ds", resume);
    ctx.addCycleControl("Resume after", buf, 10,
        SettingsHud::ClickRegion::DIRECTOR_RESUME_DOWN,
        SettingsHud::ClickRegion::DIRECTOR_RESUME_UP,
        nullptr, on, resume <= 0, "director.resume_after");

    return nullptr;  // No specific HUD for this tab
}
