// ============================================================================
// hud/settings/settings_tab_standings.cpp
// Tab renderer for Standings HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../standings_hud.h"
#include "../../core/settings_manager.h"
#include "../../core/plugin_data.h"
#include "../../core/color_config.h"
#include "../../core/plugin_constants.h"

// Static member function of SettingsHud - handles click events for Standings tab
bool SettingsHud::handleClickTabStandings(const ClickRegion& region) {
    StandingsHud* standingsHud = dynamic_cast<StandingsHud*>(region.targetHud);
    if (!standingsHud) standingsHud = m_standings;

    switch (region.type) {
        case ClickRegion::ROW_COUNT_UP:
            if (standingsHud) {
                int newRowCount = standingsHud->m_displayRowCount + 2;
                if (newRowCount > StandingsHud::MAX_ROW_COUNT) newRowCount = StandingsHud::MAX_ROW_COUNT;
                standingsHud->m_displayRowCount = newRowCount;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::ROW_COUNT_DOWN:
            if (standingsHud) {
                int newRowCount = standingsHud->m_displayRowCount - 2;
                if (newRowCount < StandingsHud::MIN_ROW_COUNT) newRowCount = StandingsHud::MIN_ROW_COUNT;
                standingsHud->m_displayRowCount = newRowCount;
                // Keep the pinned top-N no larger than the total rows shown.
                if (standingsHud->m_topPositionsCount > newRowCount)
                    standingsHud->m_topPositionsCount = newRowCount;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::STANDINGS_TOP_COUNT_UP:
            if (standingsHud) {
                int maxTop = StandingsHud::MAX_TOP_POSITIONS;
                if (standingsHud->m_displayRowCount < maxTop) maxTop = standingsHud->m_displayRowCount;
                int newTop = standingsHud->m_topPositionsCount + 1;
                if (newTop > maxTop) newTop = maxTop;
                standingsHud->m_topPositionsCount = newTop;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::STANDINGS_TOP_COUNT_DOWN:
            if (standingsHud) {
                int newTop = standingsHud->m_topPositionsCount - 1;
                if (newTop < 0) newTop = 0;
                standingsHud->m_topPositionsCount = newTop;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        // Gap column (Off/Player/Adjacent/All) is a data-driven CYCLE control
        // now - registered in renderTabStandings via ctx.addCycleControl.

        case ClickRegion::GAP_REFERENCE_TOGGLE:
            // Cycle forward: Leader → Player → Auto → Leader
            if (standingsHud) {
                switch (standingsHud->m_gapReferenceMode) {
                    case StandingsHud::GapReferenceMode::LEADER:
                        standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::PLAYER;
                        break;
                    case StandingsHud::GapReferenceMode::PLAYER:
                        standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::ALTERNATING;
                        standingsHud->m_lastGapRefToggle = std::chrono::steady_clock::now();
                        standingsHud->m_alternatingCurrent = StandingsHud::GapReferenceMode::LEADER;
                        break;
                    default:
                        standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::LEADER;
                        break;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_REFERENCE_BACK:
            // Cycle backward: Leader → Auto → Player → Leader
            if (standingsHud) {
                switch (standingsHud->m_gapReferenceMode) {
                    case StandingsHud::GapReferenceMode::LEADER:
                        standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::ALTERNATING;
                        standingsHud->m_lastGapRefToggle = std::chrono::steady_clock::now();
                        standingsHud->m_alternatingCurrent = StandingsHud::GapReferenceMode::LEADER;
                        break;
                    case StandingsHud::GapReferenceMode::ALTERNATING:
                        standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::PLAYER;
                        break;
                    default:
                        standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::LEADER;
                        break;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::LIVE_GAPS_TOGGLE:
            if (standingsHud) {
                standingsHud->m_bLiveGaps = !standingsHud->m_bLiveGaps;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::FILTER_DNS_TOGGLE:
            {
                PluginData& pd = PluginData::getInstance();
                pd.setFilterDnsRiders(!pd.isFilterDnsRiders());
                rebuildRenderData();
            }
            return true;

        case ClickRegion::HEADERS_TOGGLE:
            if (standingsHud) {
                standingsHud->m_bShowHeaders = !standingsHud->m_bShowHeaders;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::SESSION_INFO_TOGGLE:
            if (standingsHud) {
                standingsHud->m_bShowSessionInfo = !standingsHud->m_bShowSessionInfo;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        // Animate positions / Rider name / Positions gained-lost are data-driven
        // CYCLE controls now - registered in renderTabStandings via
        // ctx.addCycleControl (stopping in-flight animations on OFF is the
        // animation descriptor's postStep).

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to StandingsHud
BaseHud* SettingsHud::renderTabStandings(SettingsLayoutContext& ctx) {
    StandingsHud* hud = ctx.parent->getStandingsHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("standings");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    // Row count
    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_displayRowCount);
    ctx.addCycleControl("Rows to show", rowCountValue, 10,
        SettingsHud::ClickRegion::ROW_COUNT_DOWN,
        SettingsHud::ClickRegion::ROW_COUNT_UP,
        hud, true, false, "standings.rows");

    // Top positions always pinned (0..min(10, rows)) — the leaders stay on screen
    // even when the window is centered on the player. (Same feature as the Charts HUD.)
    char topPosValue[8];
    snprintf(topPosValue, sizeof(topPosValue), "%d", hud->m_topPositionsCount);
    ctx.addCycleControl("Top positions", topPosValue, 10,
        SettingsHud::ClickRegion::STANDINGS_TOP_COUNT_DOWN,
        SettingsHud::ClickRegion::STANDINGS_TOP_COUNT_UP,
        hud, true, false, "standings.top_positions");

    // Gap reference toggle (Leader/Player/Auto) - muted when gap column is off
    {
        const bool refRelevant = (hud->m_gapMode != StandingsHud::GapMode::OFF);
        const char* gapRefValue;
        switch (hud->m_gapReferenceMode) {
            case StandingsHud::GapReferenceMode::LEADER:      gapRefValue = "Leader"; break;
            case StandingsHud::GapReferenceMode::PLAYER:      gapRefValue = "Player"; break;
            case StandingsHud::GapReferenceMode::ALTERNATING: gapRefValue = "Auto";   break;
            default:                                          gapRefValue = "Leader"; break;
        }
        ctx.addCycleControl("Gap reference", gapRefValue, 10,
            SettingsHud::ClickRegion::GAP_REFERENCE_BACK,
            SettingsHud::ClickRegion::GAP_REFERENCE_TOGGLE,
            hud, refRelevant, false, "standings.gap_reference");
    }

    // Live gaps toggle (per-profile StandingsHud member). nullptr boolPtr matches
    // the Column-headers toggle: the LIVE_GAPS_TOGGLE click handler flips the member.
    ctx.addToggleControl("Live gaps",
        hud->m_bLiveGaps,
        SettingsHud::ClickRegion::LIVE_GAPS_TOGGLE, hud,
        static_cast<bool*>(nullptr), true,
        "standings.live_gaps", nullptr);

    // Animate position changes (Off / Basic / Colored)
    {
        const char* animModeValue;
        switch (hud->m_animationMode) {
            case StandingsHud::AnimationMode::OFF:     animModeValue = "Off";     break;
            case StandingsHud::AnimationMode::BASIC:   animModeValue = "Basic";   break;
            case StandingsHud::AnimationMode::COLORED: animModeValue = "Colored"; break;
            default: animModeValue = "Basic"; break;
        }
        SettingsHud::CycleControl animCycle = SettingsHud::CycleControl::enumMember(
            hud, &StandingsHud::m_animationMode, 3, hud);
        // Stop any in-flight animations immediately when transitioning to OFF;
        // otherwise rows would keep sliding until the cleanup timer drains.
        animCycle.postStep = [hud]() {
            if (hud->m_animationMode == StandingsHud::AnimationMode::OFF) {
                hud->m_activeAnimations.clear();
            }
        };
        ctx.addCycleControl("Animate positions", animModeValue, 10, animCycle,
            hud, true, hud->m_animationMode == StandingsHud::AnimationMode::OFF,
            "standings.animate_positions");
    }

    // DNS filter toggle (global setting stored in PluginData)
    // nullptr for boolPtr: FILTER_DNS_TOGGLE click handler manages state directly.
    ctx.addToggleControl("Hide DNS riders",
        PluginData::getInstance().isFilterDnsRiders(),
        SettingsHud::ClickRegion::FILTER_DNS_TOGGLE, hud,
        static_cast<bool*>(nullptr), true,
        "standings.filter_dns", nullptr);
    ctx.addSpacing(0.5f);

    // === CONTENT SECTION ===
    ctx.addSectionHeader("Content");

    // Session-info row (live clock / leader laps / overtime label) below the title.
    // nullptr boolPtr: SESSION_INFO_TOGGLE handler flips hud->m_bShowSessionInfo directly.
    ctx.addToggleControl("Session info",
        hud->m_bShowSessionInfo,
        SettingsHud::ClickRegion::SESSION_INFO_TOGGLE, hud,
        static_cast<bool*>(nullptr), true,
        "standings.session_info", nullptr);

    // Column-header row labeling each enabled column.
    // nullptr boolPtr: HEADERS_TOGGLE handler flips hud->m_bShowHeaders directly.
    ctx.addToggleControl("Column headers",
        hud->m_bShowHeaders,
        SettingsHud::ClickRegion::HEADERS_TOGGLE, hud,
        static_cast<bool*>(nullptr), true,
        "standings.headers", nullptr);

    // Column toggles - using addToggleControl with tooltips
    ctx.addToggleControl("Rider status icon", (hud->m_enabledColumns & StandingsHud::COL_TRACKED) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_TRACKED, true,
        "standings.col_tracked");
    ctx.addToggleControl("Position number", (hud->m_enabledColumns & StandingsHud::COL_POS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_POS, true,
        "standings.col_pos");
    // Positions gained/lost mode cycle (Off < > Sector < > Lap < > Race). Labels name the
    // scope the delta covers: RACE_START = the whole race, LAST_SF = the current lap,
    // LAST_SPLIT = the current sector. Bare nouns keep them parallel and within
    // STANDARD_VALUE_WIDTH (10 chars), so formatValue() never ellipsizes them.
    {
        const char* posGainValue;
        switch (hud->m_posGainMode) {
            case StandingsHud::PosGainMode::RACE_START: posGainValue = "Race";   break;
            case StandingsHud::PosGainMode::LAST_SF:    posGainValue = "Lap";    break;
            case StandingsHud::PosGainMode::LAST_SPLIT: posGainValue = "Sector"; break;
            case StandingsHud::PosGainMode::OFF:
            default:                                    posGainValue = "Off";    break;
        }
        // The VISUAL cycle order (Off -> Sector -> Lap -> Race) is the reverse of
        // the enum's numeric order (OFF, RACE_START, LAST_SF, LAST_SPLIT), so the
        // get/set lambdas map through visual index v = (4 - enum) % 4 - a
        // self-inverse mapping, hence identical in both directions.
        SettingsHud::CycleControl posGainCycle;
        posGainCycle.get = [hud]() {
            return (4 - static_cast<int>(hud->m_posGainMode)) % 4;
        };
        posGainCycle.set = [hud](int v) {
            hud->m_posGainMode = static_cast<StandingsHud::PosGainMode>((4 - v) % 4);
        };
        posGainCycle.count = 4;
        posGainCycle.dirtyHud = hud;
        ctx.addCycleControl("Positions gained/lost", posGainValue, 10, posGainCycle,
            hud, true, hud->m_posGainMode == StandingsHud::PosGainMode::OFF,
            "standings.col_posgain", /*tooltipOnArrows=*/false);
    }
    ctx.addToggleControl("Race number", (hud->m_enabledColumns & StandingsHud::COL_RACENUM) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_RACENUM, true,
        "standings.col_racenum");
    // Rider name mode (Off/Short/Long) - uses toggle with display value like gap scope
    {
        const char* nameModeValue;
        switch (hud->m_nameMode) {
            case StandingsHud::NameMode::OFF:   nameModeValue = "Off"; break;
            case StandingsHud::NameMode::SHORT: nameModeValue = "Short"; break;
            case StandingsHud::NameMode::LONG:  nameModeValue = "Long"; break;
            default: nameModeValue = "Short"; break;
        }
        ctx.addCycleControl("Rider name", nameModeValue, 10,
            SettingsHud::CycleControl::enumMember(hud, &StandingsHud::m_nameMode, 3, hud),
            hud, true, hud->m_nameMode == StandingsHud::NameMode::OFF,
            "standings.col_name");
    }
    ctx.addToggleControl("Bike model", (hud->m_enabledColumns & StandingsHud::COL_BIKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BIKE, true,
        "standings.col_bike");
    ctx.addToggleControl("Best lap time", (hud->m_enabledColumns & StandingsHud::COL_BEST_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BEST_LAP, true,
        "standings.col_bestlap");
    ctx.addToggleControl("Last lap time", (hud->m_enabledColumns & StandingsHud::COL_LAST_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_LAST_LAP, true,
        "standings.col_lastlap");

    // Gap mode cycle (Off < > Player < > Adjacent < > All)
    {
        const char* gapModeValue = nullptr;
        bool isOff = (hud->m_gapMode == StandingsHud::GapMode::OFF);
        switch (hud->m_gapMode) {
            case StandingsHud::GapMode::OFF:      gapModeValue = "Off"; break;
            case StandingsHud::GapMode::PLAYER:   gapModeValue = "Player"; break;
            case StandingsHud::GapMode::ADJACENT: gapModeValue = "Adjacent"; break;
            case StandingsHud::GapMode::ALL:      gapModeValue = "All"; break;
        }
        ctx.addCycleControl("Gap column", gapModeValue, 10,
            SettingsHud::CycleControl::enumMember(hud, &StandingsHud::m_gapMode, 4, hud),
            hud, true, isOff, "standings.gap_mode");
    }

    ctx.addToggleControl("Penalty indicator", (hud->m_enabledColumns & StandingsHud::COL_PENALTY) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_PENALTY, true,
        "standings.col_penalty");

    // Info text - same style as the rumble tab's "Select your controller in the General tab" hint
    ColorConfig& colors = ColorConfig::getInstance();
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Toggle compact times in the Appearance tab.", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        colors.getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
