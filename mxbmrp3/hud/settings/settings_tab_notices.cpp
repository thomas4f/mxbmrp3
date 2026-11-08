// ============================================================================
// hud/settings/settings_tab_notices.cpp
// Tab renderer and click handler for Notices HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../notices_hud.h"

// Note: the Notices tab has no tab-specific click handler anymore - its only
// custom control (Duration) is a data-driven STEPPED control registered below,
// and everything else (visibility/opacity/scale/checkboxes) uses the common
// handlers. Its registry row's click is null.

// Static member function of SettingsHud - inherits friend access to NoticesHud
BaseHud* SettingsHud::renderTabNotices(SettingsLayoutContext& ctx) {
    NoticesHud* hud = ctx.parent->getNoticesHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("notices");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeading(TR("Appearance"));
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading(TR("Layout"));
    ctx.beginColumns(2, 1);   // side by side (beginColumns), like every section below

    // Duration cycle control: 1s -> 2s -> ... -> 30s (wraps)
    char durationValue[16];
    snprintf(durationValue, sizeof(durationValue), "%ds", hud->m_noticeDurationMs / 1000);
    ctx.addSteppedControl(TR("Duration"), durationValue,
        SettingsHud::SteppedControl::wrapInt(&hud->m_noticeDurationMs,
            NoticesHud::DURATION_STEP_MS, NoticesHud::MIN_NOTICE_DURATION_MS,
            NoticesHud::MAX_NOTICE_DURATION_MS, hud),
        hud, true, false, "notices.duration");
    ctx.endColumns();
    // === CONTENT SECTION ===
    ctx.addSectionHeading(TR("Warnings & Hazards"));
    ctx.beginColumns(2, 6);

    bool wrongWayOn = (hud->m_enabledNotices & NoticesHud::NOTICE_WRONG_WAY) != 0;
    ctx.addToggleControl(TR("Wrong way"), wrongWayOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_WRONG_WAY, true,
        "notices.wrong_way");

    bool blueFlagOn = (hud->m_enabledNotices & NoticesHud::NOTICE_BLUE_FLAG) != 0;
    ctx.addToggleControl(TR("Blue flag"), blueFlagOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_BLUE_FLAG, true,
        "notices.blue_flag");

    bool lapperAheadOn = (hud->m_enabledNotices & NoticesHud::NOTICE_LAPPING) != 0;
    ctx.addToggleControl(TR("Lapper ahead"), lapperAheadOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_LAPPING, true,
        "notices.lapping");

    bool defaultSetupOn = (hud->m_enabledNotices & NoticesHud::NOTICE_DEFAULT_SETUP) != 0;
    ctx.addToggleControl(TR("Default setup"), defaultSetupOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_DEFAULT_SETUP, true,
        "notices.default_setup");

    bool hazardStationaryOn = (hud->m_enabledNotices & NoticesHud::NOTICE_HAZARD_STATIONARY) != 0;
    ctx.addToggleControl(TR("Stopped rider"), hazardStationaryOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_HAZARD_STATIONARY, true,
        "notices.hazard_stationary");

    bool hazardWrongWayOn = (hud->m_enabledNotices & NoticesHud::NOTICE_HAZARD_WRONG_WAY) != 0;
    ctx.addToggleControl(TR("Wrong way rider"), hazardWrongWayOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_HAZARD_WRONG_WAY, true,
        "notices.hazard_wrong_way");
    ctx.endColumns();
    ctx.addSectionHeading(TR("Race Events"));
    ctx.beginColumns(2, 3);

    bool overtimeOn = (hud->m_enabledNotices & NoticesHud::NOTICE_OVERTIME) != 0;
    ctx.addToggleControl(TR("Overtime"), overtimeOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_OVERTIME, true,
        "notices.overtime");

    bool lastLapOn = (hud->m_enabledNotices & NoticesHud::NOTICE_LAST_LAP) != 0;
    ctx.addToggleControl(TR("Final lap"), lastLapOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_LAST_LAP, true,
        "notices.last_lap");

    bool finishedOn = (hud->m_enabledNotices & NoticesHud::NOTICE_FINISHED) != 0;
    ctx.addToggleControl(TR("Finished"), finishedOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_FINISHED, true,
        "notices.finished");
    ctx.endColumns();
    ctx.addSectionHeading(TR("Personal Bests"));
    ctx.beginColumns(2, 3);

    bool allTimePBOn = (hud->m_enabledNotices & NoticesHud::NOTICE_ALLTIME_PB) != 0;
    ctx.addToggleControl(TR("All-time PB"), allTimePBOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_ALLTIME_PB, true,
        "notices.alltime_pb");

    bool fastestLapOn = (hud->m_enabledNotices & NoticesHud::NOTICE_FASTEST_LAP) != 0;
    ctx.addToggleControl(TR("Fastest lap"), fastestLapOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_FASTEST_LAP, true,
        "notices.fastest_lap");

    bool sessionPBOn = (hud->m_enabledNotices & NoticesHud::NOTICE_SESSION_PB) != 0;
    ctx.addToggleControl(TR("Session PB"), sessionPBOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_SESSION_PB, true,
        "notices.session_pb");
    ctx.endColumns();
    ctx.addSectionHeading(TR("Media"));
    ctx.beginColumns(2, 1);

    bool trackChangedOn = (hud->m_enabledNotices & NoticesHud::NOTICE_TRACK_CHANGED) != 0;
    ctx.addToggleControl(TR("Track change"), trackChangedOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_TRACK_CHANGED, true,
        "notices.track_changed");
    ctx.endColumns();

    return hud;
}
