// ============================================================================
// hud/settings/settings_tab_notices.cpp
// Tab renderer and click handler for Notices HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../notices_hud.h"

// Static member function of SettingsHud - handles click events for Notices tab
bool SettingsHud::handleClickTabNotices(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::NOTICES_DURATION_UP:
        case ClickRegion::NOTICES_DURATION_DOWN:
            // Cycle notice duration: 1s -> 2s -> ... -> 30s -> 1s (wraps)
            if (m_notices) {
                bool forward = (region.type == ClickRegion::NOTICES_DURATION_UP);
                int& duration = m_notices->m_noticeDurationMs;

                if (forward) {
                    if (duration >= NoticesHud::MAX_NOTICE_DURATION_MS) {
                        duration = NoticesHud::MIN_NOTICE_DURATION_MS;  // Wrap to 1s
                    } else {
                        duration += NoticesHud::DURATION_STEP_MS;
                    }
                } else {
                    if (duration <= NoticesHud::MIN_NOTICE_DURATION_MS) {
                        duration = NoticesHud::MAX_NOTICE_DURATION_MS;  // Wrap to 30s
                    } else {
                        duration -= NoticesHud::DURATION_STEP_MS;
                    }
                }
                m_notices->setDataDirty();
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to NoticesHud
BaseHud* SettingsHud::renderTabNotices(SettingsLayoutContext& ctx) {
    NoticesHud* hud = ctx.parent->getNoticesHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("notices");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud, false);  // No title support (notices don't have a title bar)
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    // Duration cycle control: 1s -> 2s -> ... -> 30s (wraps)
    char durationValue[16];
    snprintf(durationValue, sizeof(durationValue), "%ds", hud->m_noticeDurationMs / 1000);
    ctx.addCycleControl("Duration", durationValue, 10,
        SettingsHud::ClickRegion::NOTICES_DURATION_DOWN,
        SettingsHud::ClickRegion::NOTICES_DURATION_UP,
        hud, true, false, "notices.duration");
    ctx.addSpacing(0.5f);

    // === CONTENT SECTION ===
    ctx.addSectionHeader("Warnings");

    bool wrongWayOn = (hud->m_enabledNotices & NoticesHud::NOTICE_WRONG_WAY) != 0;
    ctx.addToggleControl("Wrong way", wrongWayOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_WRONG_WAY, true,
        "notices.wrong_way");

    bool blueFlagOn = (hud->m_enabledNotices & NoticesHud::NOTICE_BLUE_FLAG) != 0;
    ctx.addToggleControl("Blue flag", blueFlagOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_BLUE_FLAG, true,
        "notices.blue_flag");

    bool defaultSetupOn = (hud->m_enabledNotices & NoticesHud::NOTICE_DEFAULT_SETUP) != 0;
    ctx.addToggleControl("Default setup", defaultSetupOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_DEFAULT_SETUP, true,
        "notices.default_setup");
    ctx.addSpacing(0.5f);

    ctx.addSectionHeader("Hazards");

    bool hazardStationaryOn = (hud->m_enabledNotices & NoticesHud::NOTICE_HAZARD_STATIONARY) != 0;
    ctx.addToggleControl("Stationary rider", hazardStationaryOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_HAZARD_STATIONARY, true,
        "notices.hazard_stationary");

    bool hazardWrongWayOn = (hud->m_enabledNotices & NoticesHud::NOTICE_HAZARD_WRONG_WAY) != 0;
    ctx.addToggleControl("Wrong way rider", hazardWrongWayOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_HAZARD_WRONG_WAY, true,
        "notices.hazard_wrong_way");
    ctx.addSpacing(0.5f);

    ctx.addSectionHeader("Race Events");

    bool overtimeOn = (hud->m_enabledNotices & NoticesHud::NOTICE_OVERTIME) != 0;
    ctx.addToggleControl("Overtime", overtimeOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_OVERTIME, true,
        "notices.overtime");

    bool lastLapOn = (hud->m_enabledNotices & NoticesHud::NOTICE_LAST_LAP) != 0;
    ctx.addToggleControl("Final lap", lastLapOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_LAST_LAP, true,
        "notices.last_lap");

    bool finishedOn = (hud->m_enabledNotices & NoticesHud::NOTICE_FINISHED) != 0;
    ctx.addToggleControl("Finished", finishedOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_FINISHED, true,
        "notices.finished");
    ctx.addSpacing(0.5f);

    ctx.addSectionHeader("Training");

    bool segmentOn = (hud->m_enabledNotices & NoticesHud::NOTICE_SEGMENT) != 0;
    ctx.addToggleControl("Segment", segmentOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_SEGMENT, true,
        "notices.segment");
    ctx.addSpacing(0.5f);

    ctx.addSectionHeader("Personal Bests");

    bool allTimePBOn = (hud->m_enabledNotices & NoticesHud::NOTICE_ALLTIME_PB) != 0;
    ctx.addToggleControl("Alltime PB", allTimePBOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_ALLTIME_PB, true,
        "notices.alltime_pb");

    bool fastestLapOn = (hud->m_enabledNotices & NoticesHud::NOTICE_FASTEST_LAP) != 0;
    ctx.addToggleControl("Fastest lap", fastestLapOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_FASTEST_LAP, true,
        "notices.fastest_lap");

    bool sessionPBOn = (hud->m_enabledNotices & NoticesHud::NOTICE_SESSION_PB) != 0;
    ctx.addToggleControl("Session PB", sessionPBOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledNotices, NoticesHud::NOTICE_SESSION_PB, true,
        "notices.session_pb");

    return hud;
}
