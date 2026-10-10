// ============================================================================
// hud/settings/settings_tab_director.cpp
// Tab renderer for the auto-director (spectate broadcast tool).
//
// The director is a global manager, not a HUD, so this tab drives
// DirectorManager::getInstance() directly and returns nullptr (no backing HUD),
// mirroring the Updates tab. Clicks are handled by the common dispatcher in
// settings_hud_input.cpp (the toggles); the numbers and camera lists are shared
// stepped / cycle descriptors over DirectorManager's own getters and setters.
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

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

BaseHud* SettingsHud::renderTabDirector(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("director");

    DirectorManager& director = DirectorManager::getInstance();
    const bool on = director.isEnabled();

    char buf[16];

    // --- Appearance: the on-screen status button (a separate draggable widget).
    // Leads the tab like Rumble/Helmet; independent of the director being enabled. ---
    ctx.addSectionHeading("Appearance");
    // Side by side (beginColumns) here and in every section below: short labels
    // and short values, so two columns take half the rows.
    ctx.beginColumns(2, HudManager::getInstance().getDirectorWidget() ? 3 : 1);
    DirectorWidget* hud = HudManager::getInstance().getDirectorWidget();
    // Active surface: DIRECTOR_HUD_VISIBLE now toggles whichever surface the menu is
    // on, so the label has to report the same one it changes.
    const bool shown = hud && hud->isVisibleOnActiveSurface();
    ctx.addToggleControl("Visible", shown,
        SettingsHud::ClickRegion::DIRECTOR_HUD_VISIBLE, nullptr,
        nullptr, 0, true, "director.hud_visible");
    if (hud) {
        ctx.addOpacityControl(hud, shown);
        ctx.addScaleControl(hud, shown);
    }

    ctx.endColumns();

    // --- Director: master enable, shot pacing, and the field-wide position cutoff
    // (it gates battles, incidents, overtakes AND lappers, so it lives here). ---
    ctx.addSectionHeading("Director");
    ctx.beginColumns(2, 6);
    ctx.addToggleControl("Enabled", on,
        SettingsHud::ClickRegion::DIRECTOR_ENABLE_TOGGLE, nullptr,
        nullptr, 0, true, "director.enabled");

    // The numbers are sliders over the manager's own setters (which clamp). A
    // range whose Off sits one step below its low end (Max shot, Onboard every)
    // puts Off at the slider's left end: offBelow maps that step to the stored 0.
    using Stepped = SettingsHud::SteppedControl;
    auto offBelow = [](int lo, int (DirectorManager::*get)() const, void (DirectorManager::*set)(int)) {
        return std::make_pair(
            std::function<float()>([lo, get]() {
                const int v = (DirectorManager::getInstance().*get)();
                return static_cast<float>(v <= 0 ? lo - 1 : v);
            }),
            std::function<void(float)>([lo, set](float x) {
                const int v = static_cast<int>(std::lround(x));
                (DirectorManager::getInstance().*set)(v < lo ? 0 : v);
            }));
    };
    auto plain = [](int (DirectorManager::*get)() const, void (DirectorManager::*set)(int)) {
        return std::make_pair(
            std::function<float()>([get]() { return static_cast<float>((DirectorManager::getInstance().*get)()); }),
            std::function<void(float)>([set](float x) {
                (DirectorManager::getInstance().*set)(static_cast<int>(std::lround(x)));
            }));
    };

    snprintf(buf, sizeof(buf), "%ds", director.getMinShotSec());
    {
        auto gs = plain(&DirectorManager::getMinShotSec, &DirectorManager::setMinShotSec);
        ctx.addSteppedControl("Min shot", buf, Stepped::accessor(gs.first, gs.second, 1.0f,
            DirectorManager::MIN_SHOT_LO, DirectorManager::MIN_SHOT_HI, nullptr),
            nullptr, on, false, "director.min_shot");
    }

    // Max shot doubles as the forced-rotation switch: Off (one step below the low end)
    // stops the director cutting on a timer entirely, so it only cuts for a story and
    // returns to the rider the broadcaster picked. Min shot still paces those story cuts
    // and the Stories below still fire, so neither greys out - but the Onboard variety
    // section does, since a shot the caster chose stays on the plain TV camera.
    const int maxShot = director.getMaxShotSec();
    if (maxShot <= 0) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "%ds", maxShot);
    {
        auto gs = offBelow(DirectorManager::MAX_SHOT_LO, &DirectorManager::getMaxShotSec, &DirectorManager::setMaxShotSec);
        ctx.addSteppedControl("Max shot", buf, Stepped::accessor(gs.first, gs.second, 1.0f,
            DirectorManager::MAX_SHOT_LO - 1, DirectorManager::MAX_SHOT_HI, nullptr),
            nullptr, on, maxShot <= 0, "director.max_shot");
    }

    // Story hold: how long a story shot (crash / fastest lap / hot lap) lingers - a
    // shot-duration knob, so it sits with Min/Max shot here rather than under Stories.
    snprintf(buf, sizeof(buf), "%ds", director.getHoldSec());
    {
        auto gs = plain(&DirectorManager::getHoldSec, &DirectorManager::setHoldSec);
        ctx.addSteppedControl("Story hold", buf, Stepped::accessor(gs.first, gs.second, 1.0f,
            DirectorManager::HOLD_LO, DirectorManager::HOLD_HI, nullptr),
            nullptr, on, false, "director.hold");
    }

    const int bmp = director.getBattleMaxPos();
    if (bmp <= 0) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "P%d", bmp);
    {
        auto gs = plain(&DirectorManager::getBattleMaxPos, &DirectorManager::setBattleMaxPos);
        ctx.addSteppedControl("Field cutoff", buf, Stepped::accessor(gs.first, gs.second, 1.0f,
            0.0f, DirectorManager::BATTLE_MAXPOS_HI, nullptr),
            nullptr, on, bmp <= 0, "director.battle_max_pos");
    }

    // Battle gap sits with Field cutoff (both are the shared battle *definition* that
    // also drives the overlay panel); the on/off "Follow battles" story is under Stories.
    snprintf(buf, sizeof(buf), "%.1fs", director.getBattleGapMs() / 1000.0);
    {
        auto gs = plain(&DirectorManager::getBattleGapMs, &DirectorManager::setBattleGapMs);
        ctx.addSteppedControl("Battle gap", buf, Stepped::accessor(gs.first, gs.second,
            DirectorManager::BATTLE_GAP_STEP, DirectorManager::BATTLE_GAP_LO, DirectorManager::BATTLE_GAP_HI, nullptr),
            nullptr, on, false, "director.battle_gap");
    }

    ctx.endColumns();

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
    ctx.addSectionHeading("Stories");
    ctx.beginColumns(2, 7);

    ctx.addToggleControl("Incidents", director.getFollowIncidents(),
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
    ctx.addToggleControl("Overtakes", director.getCatchOvertakes(),
        SettingsHud::ClickRegion::DIRECTOR_CATCH_OVERTAKES, nullptr,
        nullptr, 0, on, "director.catch_overtakes");
    ctx.addToggleControl("Battles", director.getFollowBattles(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_BATTLES, nullptr,
        nullptr, 0, on, "director.follow_battles");
    ctx.addToggleControl("Drops", director.getFollowDrops(),
        SettingsHud::ClickRegion::DIRECTOR_FOLLOW_DROPS, nullptr,
        nullptr, 0, on, "director.follow_drops");
    ctx.endColumns();
    // Follow lappers is INI-ONLY (director_follow_lappers=), like the tab's other
    // omissions: this tab was overflowing the panel and the lapper story is the one
    // the fewest broadcasts touch. The setting, its handler and its default are
    // untouched -- only the row is gone.

    // --- Onboard variety: how often the director dips off Trackside into an onboard,
    // and which onboards it may use. "Onboard every" is the cadence (0 = Off = always
    // Trackside); the camera toggles below it grey out when it's Off. Forward cams
    // (Front Fender / Helmet / Helmet 2) frame the rider ahead; Rear Fender frames a
    // chaser. (Forks is INI-only.)
    //
    // The whole section also greys out while Max shot is Off: the director is then only
    // covering stories on a shot the caster chose, and pickShot pins those to Auto /
    // Trackside. The stored values are left alone rather than shown as Off, so turning
    // the max shot back on restores the cadence the user had set. ---
    // Static title. A header that rewrites itself as you step a control above it reads as
    // the panel twitching, so the "why is this greyed?" answer lives in the "Onboard every"
    // tooltip instead ("Needs Max shot on") - the same place every other explanation in
    // this tab lives.
    const bool rotationOn = director.forcedRotation();
    ctx.addSectionHeading("Onboard Variety");
    ctx.beginColumns(2, 3);

    const int ve = director.getVarietyEvery();
    const bool varietyOn = (ve > 0);
    if (!varietyOn) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "%d", ve);
    {
        auto gs = offBelow(DirectorManager::VARIETY_LO, &DirectorManager::getVarietyEvery, &DirectorManager::setVarietyEvery);
        ctx.addSteppedControl("Onboard every", buf, Stepped::accessor(gs.first, gs.second, 1.0f,
            DirectorManager::VARIETY_LO - 1, DirectorManager::VARIETY_HI, nullptr),
            nullptr, on && rotationOn, !varietyOn, "director.variety_every");
    }

    // The camera choices only matter while variety dips are actually running.
    const bool camsOn = on && rotationOn && varietyOn;
    // The two fender views share one cycle: Off > Front > Rear > Both.
    const bool cf = director.getCamFront();
    const bool cr = director.getCamRear();
    // A pair of camera bools as one four-state list: bit 0 the first, bit 1 the second.
    auto camPair = [](const char* const* names, bool (DirectorManager::*getA)() const,
                      bool (DirectorManager::*getB)() const, void (DirectorManager::*setA)(bool),
                      void (DirectorManager::*setB)(bool)) {
        SettingsHud::CycleControl c;
        c.count = 4;
        c.get = [getA, getB]() {
            DirectorManager& d = DirectorManager::getInstance();
            return ((d.*getA)() ? 1 : 0) + ((d.*getB)() ? 2 : 0);
        };
        c.set = [setA, setB](int order) {
            DirectorManager& d = DirectorManager::getInstance();
            (d.*setA)((order & 1) != 0);
            (d.*setB)((order & 2) != 0);
        };
        c.nameOf = [names](int i) { return std::string(names[i]); };
        return c;
    };
    static const char* const kFender[] = { "Off", "Front", "Rear", "Both" };
    ctx.addCycleControl("Fender", kFender[(cf ? 1 : 0) + (cr ? 2 : 0)],
        camPair(kFender, &DirectorManager::getCamFront, &DirectorManager::getCamRear,
                &DirectorManager::setCamFront, &DirectorManager::setCamRear),
        nullptr, camsOn, (!cf && !cr), "director.cam_fender");
    // The two helmet views share one cycle: Off > Helmet 1 > Helmet 2 > Both.
    const bool h1 = director.getCamHelmet();
    const bool h2 = director.getCamHelmet2();
    static const char* const kHelmet[] = { "Off", "Helmet 1", "Helmet 2", "Both" };
    ctx.addCycleControl("Helmet", kHelmet[(h1 ? 1 : 0) + (h2 ? 2 : 0)],
        camPair(kHelmet, &DirectorManager::getCamHelmet, &DirectorManager::getCamHelmet2,
                &DirectorManager::setCamHelmet, &DirectorManager::setCamHelmet2),
        nullptr, camsOn, (!h1 && !h2), "director.cam_helmet");

    ctx.endColumns();

    // --- Manual control: how the caster takes over and hands back (takeover first).
    // Last, below the cameras - it's the least-touched section. ---
    ctx.addSectionHeading("Manual Control");
    ctx.beginColumns(2, 2);

    ctx.addToggleControl("Pad takeover", director.getGamepadTakeover(),
        SettingsHud::ClickRegion::DIRECTOR_GAMEPAD_TAKEOVER, nullptr,
        nullptr, 0, on, "director.gamepad_takeover");

    const int resume = director.getManualResumeSec();
    if (resume <= 0) snprintf(buf, sizeof(buf), "Off");
    else snprintf(buf, sizeof(buf), "%ds", resume);
    {
        auto gs = plain(&DirectorManager::getManualResumeSec, &DirectorManager::setManualResumeSec);
        ctx.addSteppedControl("Resume after", buf, Stepped::accessor(gs.first, gs.second,
            DirectorManager::RESUME_STEP, 0.0f, DirectorManager::RESUME_HI, nullptr),
            nullptr, on, resume <= 0, "director.resume_after");
    }

    ctx.endColumns();

    return nullptr;  // No specific HUD for this tab
}
