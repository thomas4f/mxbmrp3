// ============================================================================
// hud/settings/settings_tab_event_log.cpp
// Tab renderer and click handler for Event Log HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../event_log_hud.h"

// Static member function of SettingsHud - handles click events for Event Log tab
bool SettingsHud::handleClickTabEventLog(const ClickRegion& region) {
    if (!m_eventLog) return false;

    switch (region.type) {
        // Max events and auto-hide Duration are data-driven STEPPED controls;
        // Show mode / Order / Timestamp are data-driven CYCLE controls -
        // registered in renderTabEventLog via ctx.addSteppedControl /
        // ctx.addCycleControl.

        case ClickRegion::EVENT_LOG_ICONS_TOGGLE: {
            m_eventLog->m_showIcons = !m_eventLog->m_showIcons;
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;
        }

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to EventLogHud
BaseHud* SettingsHud::renderTabEventLog(SettingsLayoutContext& ctx) {
    EventLogHud* hud = ctx.parent->getEventLogHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("event_log");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    ctx.addToggleControl("Show icons", hud->m_showIcons,
        SettingsHud::ClickRegion::EVENT_LOG_ICONS_TOGGLE, hud,
        nullptr, 0, true, "event_log.icons");

    // Display mode: Off / Always / Auto-hide (matches RadarHud's "Show mode" wording)
    const char* modeStr = "Always";
    bool isOff = false;
    switch (hud->m_displayMode) {
    case EventLogHud::DisplayMode::OFF:       modeStr = "Off"; isOff = true; break;
    case EventLogHud::DisplayMode::ON:        modeStr = "Always"; break;
    case EventLogHud::DisplayMode::AUTO_HIDE: modeStr = "Auto-hide"; break;
    }
    // tooltipOnArrows=false on all three cycles below: these arrows historically
    // had no per-type tooltip fallback (no TAB_EVENT_LOG section in
    // getTooltipIdForRegion), so keep the tooltip on the row region only.
    ctx.addCycleControl("Show mode", modeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &EventLogHud::m_displayMode, 3, hud),
        hud, true, isOff, "event_log.display_mode", /*tooltipOnArrows=*/false);

    // Auto-hide duration (only meaningful in auto-hide mode)
    bool autoHideEnabled = (hud->m_displayMode == EventLogHud::DisplayMode::AUTO_HIDE);
    char durationValue[16];
    snprintf(durationValue, sizeof(durationValue), "%ds", hud->m_autoHideDurationMs / 1000);
    // tooltipOnArrows=false: these arrows historically had no per-type tooltip
    // fallback (no TAB_EVENT_LOG section in getTooltipIdForRegion), so keep the
    // tooltip on the row region only.
    ctx.addSteppedControl("Duration", durationValue, 10,
        SettingsHud::SteppedControl::wrapInt(&hud->m_autoHideDurationMs,
            EventLogHud::AUTO_HIDE_STEP_MS, EventLogHud::MIN_AUTO_HIDE_MS,
            EventLogHud::MAX_AUTO_HIDE_MS, hud),
        hud, autoHideEnabled, false, "event_log.duration", /*tooltipOnArrows=*/false);

    // Display order: Newest / Oldest
    const char* orderStr = (hud->m_displayOrder == EventLogHud::DisplayOrder::NEWEST_FIRST)
        ? "Newest" : "Oldest";
    ctx.addCycleControl("Order", orderStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &EventLogHud::m_displayOrder, 2, hud),
        hud, true, false, "event_log.order", /*tooltipOnArrows=*/false);

    // Max events to show
    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_maxDisplayEvents);
    ctx.addSteppedControl("Max events", rowCountValue, 10,
        SettingsHud::SteppedControl::clampInt(&hud->m_maxDisplayEvents, 1,
            EventLogHud::MIN_DISPLAY_EVENTS, EventLogHud::MAX_DISPLAY_EVENTS, hud),
        hud, true, false, "event_log.max_events", /*tooltipOnArrows=*/false);

    // Timestamp mode: Off / Session / Clock
    const char* timestampStr = "Off";
    bool tsOff = false;
    switch (hud->m_timestampMode) {
    case EventLogHud::TimestampMode::OFF:     timestampStr = "Off"; tsOff = true; break;
    case EventLogHud::TimestampMode::SESSION: timestampStr = "Session"; break;
    case EventLogHud::TimestampMode::CLOCK:   timestampStr = "Clock"; break;
    }
    ctx.addCycleControl("Timestamp", timestampStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &EventLogHud::m_timestampMode, 3, hud),
        hud, true, tsOff, "event_log.timestamp", /*tooltipOnArrows=*/false);
    ctx.addSpacing(0.5f);

    // === EVENTS SECTION ===
    // Group related events into single toggles to keep the UI concise.
    // Each toggle controls multiple bitfield flags via the CHECKBOX handler's
    // multi-bit support (set all / clear all).
    ctx.addSectionHeader("Events");

    // Session: started + state changes
    constexpr uint32_t SESSION_GROUP = EVENT_SESSION_STARTED | EVENT_SESSION_STATE;
    bool sessionOn = (hud->m_enabledEvents & SESSION_GROUP) != 0;
    ctx.addToggleControl("Session", sessionOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, SESSION_GROUP, true,
        "event_log.session");

    // Fastest lap
    bool fastestLapOn = (hud->m_enabledEvents & EVENT_FASTEST_LAP) != 0;
    ctx.addToggleControl("Fastest lap", fastestLapOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, EVENT_FASTEST_LAP, true,
        "event_log.fastest_lap");

    // Penalties: penalty + penalty cleared
    constexpr uint32_t PENALTY_GROUP = EVENT_PENALTY | EVENT_PENALTY_CLEAR;
    bool penaltyOn = (hud->m_enabledEvents & PENALTY_GROUP) != 0;
    ctx.addToggleControl("Penalties", penaltyOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, PENALTY_GROUP, true,
        "event_log.penalties");

    // Rider out: retired + DSQ + DNS
    constexpr uint32_t RIDER_OUT_GROUP = EVENT_RIDER_RETIRED | EVENT_RIDER_DSQ | EVENT_RIDER_DNS;
    bool riderOutOn = (hud->m_enabledEvents & RIDER_OUT_GROUP) != 0;
    ctx.addToggleControl("RET/DSQ/DNS", riderOutOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, RIDER_OUT_GROUP, true,
        "event_log.rider_out");

    // Overtime
    bool overtimeOn = (hud->m_enabledEvents & EVENT_OVERTIME) != 0;
    ctx.addToggleControl("Time expired", overtimeOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, EVENT_OVERTIME, true,
        "event_log.overtime");

    // Final lap
    bool finalLapOn = (hud->m_enabledEvents & EVENT_FINAL_LAP) != 0;
    ctx.addToggleControl("Final lap", finalLapOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, EVENT_FINAL_LAP, true,
        "event_log.final_lap");

    // Finished
    bool finishedOn = (hud->m_enabledEvents & EVENT_RIDER_FINISHED) != 0;
    ctx.addToggleControl("Finished", finishedOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, EVENT_RIDER_FINISHED, true,
        "event_log.finished");

    // Leader change (race only)
    bool leaderChangeOn = (hud->m_enabledEvents & EVENT_LEADER_CHANGE) != 0;
    ctx.addToggleControl("Leader change", leaderChangeOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, EVENT_LEADER_CHANGE, true,
        "event_log.leader_change");

    // Pit activity: entry + exit
    constexpr uint32_t PIT_GROUP = EVENT_PIT_ENTRY | EVENT_PIT_EXIT;
    bool pitOn = (hud->m_enabledEvents & PIT_GROUP) != 0;
    ctx.addToggleControl("Pit activity", pitOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, PIT_GROUP, true,
        "event_log.pit");

    // Director cuts: the auto-director's shot decisions (broadcast transparency). Opt-in —
    // frequent, so off unless a broadcaster wants to see the director's logic in the feed.
    bool directorOn = (hud->m_enabledEvents & EVENT_DIRECTOR) != 0;
    ctx.addToggleControl("Director", directorOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledEvents, EVENT_DIRECTOR, true,
        "event_log.director");

    return hud;
}
