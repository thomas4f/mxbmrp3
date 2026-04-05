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
        case ClickRegion::EVENT_LOG_MODE_UP:
        case ClickRegion::EVENT_LOG_MODE_DOWN: {
            int mode = static_cast<int>(m_eventLog->m_displayMode);
            if (region.type == ClickRegion::EVENT_LOG_MODE_UP) {
                mode = (mode + 1) % 3;
            } else {
                mode = (mode + 2) % 3;
            }
            m_eventLog->m_displayMode = static_cast<EventLogHud::DisplayMode>(mode);
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::EVENT_LOG_ORDER_UP:
        case ClickRegion::EVENT_LOG_ORDER_DOWN: {
            m_eventLog->m_displayOrder = (m_eventLog->m_displayOrder == EventLogHud::DisplayOrder::NEWEST_FIRST)
                ? EventLogHud::DisplayOrder::OLDEST_FIRST
                : EventLogHud::DisplayOrder::NEWEST_FIRST;
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::EVENT_LOG_ROW_COUNT_UP:
            m_eventLog->m_maxDisplayEvents = std::min(
                m_eventLog->m_maxDisplayEvents + 1,
                EventLogHud::MAX_DISPLAY_EVENTS);
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;

        case ClickRegion::EVENT_LOG_ROW_COUNT_DOWN:
            m_eventLog->m_maxDisplayEvents = std::max(
                m_eventLog->m_maxDisplayEvents - 1,
                EventLogHud::MIN_DISPLAY_EVENTS);
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;

        case ClickRegion::EVENT_LOG_DURATION_UP:
        case ClickRegion::EVENT_LOG_DURATION_DOWN: {
            bool forward = (region.type == ClickRegion::EVENT_LOG_DURATION_UP);
            int& duration = m_eventLog->m_autoHideDurationMs;
            if (forward) {
                if (duration >= EventLogHud::MAX_AUTO_HIDE_MS) {
                    duration = EventLogHud::MIN_AUTO_HIDE_MS;
                } else {
                    duration += EventLogHud::AUTO_HIDE_STEP_MS;
                }
            } else {
                if (duration <= EventLogHud::MIN_AUTO_HIDE_MS) {
                    duration = EventLogHud::MAX_AUTO_HIDE_MS;
                } else {
                    duration -= EventLogHud::AUTO_HIDE_STEP_MS;
                }
            }
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::EVENT_LOG_TIMESTAMP_UP:
        case ClickRegion::EVENT_LOG_TIMESTAMP_DOWN: {
            int mode = static_cast<int>(m_eventLog->m_timestampMode);
            if (region.type == ClickRegion::EVENT_LOG_TIMESTAMP_UP) {
                mode = (mode + 1) % 3;
            } else {
                mode = (mode + 2) % 3;
            }
            m_eventLog->m_timestampMode = static_cast<EventLogHud::TimestampMode>(mode);
            m_eventLog->setDataDirty();
            setDataDirty();
            return true;
        }

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

    // Display mode: Off / On / Auto-hide
    const char* modeStr = "On";
    bool isOff = false;
    switch (hud->m_displayMode) {
    case EventLogHud::DisplayMode::OFF:       modeStr = "Off"; isOff = true; break;
    case EventLogHud::DisplayMode::ON:        modeStr = "On"; break;
    case EventLogHud::DisplayMode::AUTO_HIDE: modeStr = "Auto-hide"; break;
    }
    ctx.addCycleControl("Show mode", modeStr, 10,
        SettingsHud::ClickRegion::EVENT_LOG_MODE_DOWN,
        SettingsHud::ClickRegion::EVENT_LOG_MODE_UP,
        hud, true, isOff, "event_log.display_mode");

    // Auto-hide duration (only meaningful in auto-hide mode)
    bool autoHideEnabled = (hud->m_displayMode == EventLogHud::DisplayMode::AUTO_HIDE);
    char durationValue[16];
    snprintf(durationValue, sizeof(durationValue), "%ds", hud->m_autoHideDurationMs / 1000);
    ctx.addCycleControl("Duration", durationValue, 10,
        SettingsHud::ClickRegion::EVENT_LOG_DURATION_DOWN,
        SettingsHud::ClickRegion::EVENT_LOG_DURATION_UP,
        hud, autoHideEnabled, false, "event_log.duration");

    // Display order: Newest / Oldest
    const char* orderStr = (hud->m_displayOrder == EventLogHud::DisplayOrder::NEWEST_FIRST)
        ? "Newest" : "Oldest";
    ctx.addCycleControl("Order", orderStr, 10,
        SettingsHud::ClickRegion::EVENT_LOG_ORDER_DOWN,
        SettingsHud::ClickRegion::EVENT_LOG_ORDER_UP,
        hud, true, false, "event_log.order");

    // Max events to show
    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_maxDisplayEvents);
    ctx.addCycleControl("Max events", rowCountValue, 10,
        SettingsHud::ClickRegion::EVENT_LOG_ROW_COUNT_DOWN,
        SettingsHud::ClickRegion::EVENT_LOG_ROW_COUNT_UP,
        hud, true, false, "event_log.max_events");

    // Timestamp mode: Off / Session / Clock
    const char* timestampStr = "Off";
    bool tsOff = false;
    switch (hud->m_timestampMode) {
    case EventLogHud::TimestampMode::OFF:     timestampStr = "Off"; tsOff = true; break;
    case EventLogHud::TimestampMode::SESSION: timestampStr = "Session"; break;
    case EventLogHud::TimestampMode::CLOCK:   timestampStr = "Clock"; break;
    }
    ctx.addCycleControl("Timestamp", timestampStr, 10,
        SettingsHud::ClickRegion::EVENT_LOG_TIMESTAMP_DOWN,
        SettingsHud::ClickRegion::EVENT_LOG_TIMESTAMP_UP,
        hud, true, tsOff, "event_log.timestamp");
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

    return hud;
}
