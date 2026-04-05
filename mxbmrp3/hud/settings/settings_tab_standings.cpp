// ============================================================================
// hud/settings/settings_tab_standings.cpp
// Tab renderer for Standings HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../standings_hud.h"
#include "../../core/settings_manager.h"
#include "../../core/plugin_data.h"

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
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_COLUMN_TOGGLE:
            // Cycle backward: OFF <- PLAYER <- ADJACENT <- ALL <- OFF
            if (standingsHud) {
                switch (standingsHud->m_gapMode) {
                    case StandingsHud::GapMode::OFF:      standingsHud->m_gapMode = StandingsHud::GapMode::ALL; break;
                    case StandingsHud::GapMode::PLAYER:   standingsHud->m_gapMode = StandingsHud::GapMode::OFF; break;
                    case StandingsHud::GapMode::ADJACENT: standingsHud->m_gapMode = StandingsHud::GapMode::PLAYER; break;
                    case StandingsHud::GapMode::ALL:      standingsHud->m_gapMode = StandingsHud::GapMode::ADJACENT; break;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;
        case ClickRegion::GAP_SCOPE_TOGGLE:
            // Cycle forward: OFF -> PLAYER -> ADJACENT -> ALL -> OFF
            if (standingsHud) {
                switch (standingsHud->m_gapMode) {
                    case StandingsHud::GapMode::OFF:      standingsHud->m_gapMode = StandingsHud::GapMode::PLAYER; break;
                    case StandingsHud::GapMode::PLAYER:   standingsHud->m_gapMode = StandingsHud::GapMode::ADJACENT; break;
                    case StandingsHud::GapMode::ADJACENT: standingsHud->m_gapMode = StandingsHud::GapMode::ALL; break;
                    case StandingsHud::GapMode::ALL:      standingsHud->m_gapMode = StandingsHud::GapMode::OFF; break;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

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
            {
                PluginData& pd = PluginData::getInstance();
                pd.setLiveGapsEnabled(!pd.isLiveGapsEnabled());
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

        case ClickRegion::ANIMATE_POSITIONS_TOGGLE:
            {
                auto* boolPtr = std::get_if<bool*>(&region.targetPointer);
                if (!boolPtr || !*boolPtr || !region.targetHud) return false;
                **boolPtr = !**boolPtr;
                region.targetHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::NAME_MODE_UP:
            if (standingsHud) {
                using NM = StandingsHud::NameMode;
                switch (standingsHud->m_nameMode) {
                    case NM::OFF:   standingsHud->m_nameMode = NM::SHORT; break;
                    case NM::SHORT: standingsHud->m_nameMode = NM::LONG;  break;
                    case NM::LONG:  standingsHud->m_nameMode = NM::OFF;   break;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::NAME_MODE_DOWN:
            if (standingsHud) {
                using NM = StandingsHud::NameMode;
                switch (standingsHud->m_nameMode) {
                    case NM::OFF:   standingsHud->m_nameMode = NM::LONG;  break;
                    case NM::SHORT: standingsHud->m_nameMode = NM::OFF;   break;
                    case NM::LONG:  standingsHud->m_nameMode = NM::SHORT; break;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

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

    // Live gaps toggle (global setting stored in PluginData, not a HUD member)
    // nullptr for boolPtr is intentional: LIVE_GAPS_TOGGLE click handler manages
    // state directly via PluginData::setLiveGapsEnabled(), not through the pointer.
    ctx.addToggleControl("Live gaps",
        PluginData::getInstance().isLiveGapsEnabled(),
        SettingsHud::ClickRegion::LIVE_GAPS_TOGGLE, hud,
        static_cast<bool*>(nullptr), true,
        "standings.live_gaps", nullptr);

    // Animate position changes
    ctx.addToggleControl("Animate positions",
        hud->m_bAnimatePositions,
        SettingsHud::ClickRegion::ANIMATE_POSITIONS_TOGGLE, hud,
        &hud->m_bAnimatePositions, true,
        "standings.animate_positions", nullptr);

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

    // Column toggles - using addToggleControl with tooltips
    ctx.addToggleControl("Rider status icon", (hud->m_enabledColumns & StandingsHud::COL_TRACKED) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_TRACKED, true,
        "standings.col_tracked");
    ctx.addToggleControl("Position number", (hud->m_enabledColumns & StandingsHud::COL_POS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_POS, true,
        "standings.col_pos");
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
            SettingsHud::ClickRegion::NAME_MODE_DOWN,
            SettingsHud::ClickRegion::NAME_MODE_UP,
            hud, true, hud->m_nameMode == StandingsHud::NameMode::OFF,
            "standings.col_name");
    }
    ctx.addToggleControl("Bike model", (hud->m_enabledColumns & StandingsHud::COL_BIKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BIKE, true,
        "standings.col_bike");
    ctx.addToggleControl("Best lap time", (hud->m_enabledColumns & StandingsHud::COL_BEST_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BEST_LAP, true,
        "standings.col_bestlap");

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
            SettingsHud::ClickRegion::GAP_COLUMN_TOGGLE,
            SettingsHud::ClickRegion::GAP_SCOPE_TOGGLE,
            hud, true, isOff, "standings.gap_mode");
    }

    ctx.addToggleControl("Penalty indicator", (hud->m_enabledColumns & StandingsHud::COL_PENALTY) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_PENALTY, true,
        "standings.col_penalty");

    return hud;
}
