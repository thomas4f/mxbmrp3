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
            {
                auto* boolPtr = std::get_if<bool*>(&region.targetPointer);
                if (!boolPtr || !*boolPtr || !region.targetHud) return false;
                **boolPtr = !**boolPtr;
                region.targetHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_SCOPE_TOGGLE:
            if (standingsHud) {
                if (standingsHud->m_gapScope == StandingsHud::GapScope::PLAYER) {
                    standingsHud->m_gapScope = StandingsHud::GapScope::ALL;
                } else {
                    standingsHud->m_gapScope = StandingsHud::GapScope::PLAYER;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_REFERENCE_TOGGLE:
            if (standingsHud) {
                if (standingsHud->m_gapReferenceMode == StandingsHud::GapReferenceMode::LEADER) {
                    standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::PLAYER;
                } else {
                    standingsHud->m_gapReferenceMode = StandingsHud::GapReferenceMode::LEADER;
                }
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::LIVE_STANDINGS_TOGGLE:
            {
                PluginData& pd = PluginData::getInstance();
                pd.setLiveStandingsEnabled(!pd.isLiveStandingsEnabled());
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

        case ClickRegion::NAME_MODE_TOGGLE:
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

    // Live standings toggle (global setting stored in PluginData, not a HUD member)
    // nullptr for boolPtr is intentional: LIVE_STANDINGS_TOGGLE click handler manages
    // state directly via PluginData::setLiveStandingsEnabled(), not through the pointer.
    ctx.addToggleControl("Live position updates",
        PluginData::getInstance().isLiveStandingsEnabled(),
        SettingsHud::ClickRegion::LIVE_STANDINGS_TOGGLE, hud,
        static_cast<bool*>(nullptr), true,
        "standings.live_positions", nullptr);

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
    ctx.addToggleControl("Tracked rider marker", (hud->m_enabledColumns & StandingsHud::COL_TRACKED) != 0,
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
        ctx.addToggleControl("Rider name",
            hud->m_nameMode != StandingsHud::NameMode::OFF,
            SettingsHud::ClickRegion::NAME_MODE_TOGGLE, hud,
            static_cast<bool*>(nullptr), true,
            "standings.col_name", nameModeValue);
    }
    ctx.addToggleControl("Bike model", (hud->m_enabledColumns & StandingsHud::COL_BIKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BIKE, true,
        "standings.col_bike");
    ctx.addToggleControl("Rider status", (hud->m_enabledColumns & StandingsHud::COL_STATUS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_STATUS, true,
        "standings.col_status");
    ctx.addToggleControl("Penalty indicator", (hud->m_enabledColumns & StandingsHud::COL_PENALTY) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_PENALTY, true,
        "standings.col_penalty");
    ctx.addToggleControl("Best lap time", (hud->m_enabledColumns & StandingsHud::COL_BEST_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BEST_LAP, true,
        "standings.col_bestlap");

    // Debug column - only visible in developer mode
    if (SettingsManager::getInstance().isDeveloperMode()) {
        ctx.addToggleControl("Debug data", (hud->m_enabledColumns & StandingsHud::COL_DEBUG) != 0,
            SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_DEBUG, true,
            "standings.col_debug");
    }

    ctx.addSpacing(0.5f);

    // === GAPS SECTION ===
    ctx.addSectionHeader("Gaps");

    const bool gapEnabled = hud->m_showGapColumn;

    // Gap column toggle (on/off - data source auto-selected based on live standings toggle)
    ctx.addToggleControl("Gap column",
        hud->m_showGapColumn,
        SettingsHud::ClickRegion::GAP_COLUMN_TOGGLE, hud,
        &hud->m_showGapColumn, true,
        "standings.gap_column", nullptr);

    // Gap scope toggle (Player/All) - muted when gap column is off
    // nullptr for boolPtr: GAP_SCOPE_TOGGLE handler accesses standingsHud->m_gapScope directly
    const char* gapScopeValue = (hud->m_gapScope == StandingsHud::GapScope::PLAYER)
        ? "Player" : "All";
    // isOn=true because both Player/All are valid active states (neither is "off")
    ctx.addToggleControl("Show mode",
        true,
        SettingsHud::ClickRegion::GAP_SCOPE_TOGGLE, hud,
        static_cast<bool*>(nullptr), gapEnabled,
        "standings.gap_scope", gapScopeValue);

    // Gap reference toggle (Leader/Player) - muted when gap column is off
    // nullptr for boolPtr: GAP_REFERENCE_TOGGLE handler accesses standingsHud->m_gapReferenceMode directly
    // isOn=true because both Leader/Player are valid active states
    const char* gapRefValue = (hud->m_gapReferenceMode == StandingsHud::GapReferenceMode::LEADER)
        ? "Leader" : "Player";
    ctx.addToggleControl("Reference point",
        true,
        SettingsHud::ClickRegion::GAP_REFERENCE_TOGGLE, hud,
        static_cast<bool*>(nullptr), gapEnabled,
        "standings.gap_reference", gapRefValue);

    return hud;
}
