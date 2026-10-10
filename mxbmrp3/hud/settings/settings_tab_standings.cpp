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

#include <algorithm>
#include <chrono>
#include <string>

// Static member function of SettingsHud - handles click events for Standings tab
bool SettingsHud::handleClickTabStandings(const ClickRegion& region) {
    StandingsHud* standingsHud = dynamic_cast<StandingsHud*>(region.targetHud);
    if (!standingsHud) standingsHud = m_standings;

    switch (region.type) {
        // Gap column (Off/Player/Adjacent/All) is a data-driven CYCLE control
        // now - registered in renderTabStandings via ctx.addCycleControl.

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
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");
    ctx.beginColumns(2, 6);   // side by side (beginColumns)

    // Row count: a plain +-2 clamped stepper with no hold acceleration (fixedInt),
    // the same control the Charts tab builds. postStep keeps the pinned top-N no
    // larger than the total rows shown. The arrows keep the "standings.rows"
    // tooltip their old dedicated region type resolved to.
    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_displayRowCount);
    SettingsHud::SteppedControl rowsControl = SettingsHud::SteppedControl::fixedInt(
        &hud->m_displayRowCount, 2,
        StandingsHud::MIN_ROW_COUNT, StandingsHud::MAX_ROW_COUNT, hud);
    rowsControl.postStep = [hud]() {
        hud->m_topPositionsCount = std::min(hud->m_topPositionsCount, hud->m_displayRowCount);
    };
    ctx.addSteppedControl("Rows to show", rowCountValue, rowsControl,
        hud, true, false, "standings.rows");

    // Top positions always pinned (0..min(10, rows)) — the leaders stay on screen
    // even when the window is centered on the player. (Same feature as the Charts HUD.)
    // The upper bound tracks the rows drawn; it's re-resolved on every rebuild.
    char topPosValue[8];
    snprintf(topPosValue, sizeof(topPosValue), "%d", hud->m_topPositionsCount);
    ctx.addSteppedControl("Top positions", topPosValue,
        SettingsHud::SteppedControl::fixedInt(&hud->m_topPositionsCount, 1, 0,
            std::min(StandingsHud::MAX_TOP_POSITIONS, hud->m_displayRowCount), hud),
        hud, true, false, "standings.top_positions", /*tooltipOnArrows=*/false);

    // Gap reference toggle (Leader/Player/Auto) - muted when gap column is off
    {
        const bool refRelevant = (hud->m_gapMode != StandingsHud::GapMode::OFF);
        // Initialized at the declaration, like the sibling GapMode switch below:
        // without a `default:` the compiler cannot prove the variable is written
        // (the enum could hold an out-of-range value), and -Werror=maybe-
        // uninitialized fails the cross-build. Initializing is what buys the
        // exhaustive switch — a new reference mode now trips -Wswitch here
        // instead of rendering silently as "Leader".
        const char* gapRefValue = "Leader";
        switch (hud->m_gapReferenceMode) {
            case StandingsHud::GapReferenceMode::LEADER:      gapRefValue = "Leader"; break;
            case StandingsHud::GapReferenceMode::PLAYER:      gapRefValue = "Player"; break;
            case StandingsHud::GapReferenceMode::ALTERNATING: gapRefValue = "Auto";   break;
            // Not a real mode — listed so the switch stays EXHAUSTIVE.
            case StandingsHud::GapReferenceMode::COUNT:                               break;
        }
        // Auto starts on Leader with a fresh timer, whichever way it is reached.
        static const char* const kGapRefs[] = { "Leader", "Player", "Auto" };
        static_assert(sizeof(kGapRefs) / sizeof(kGapRefs[0]) ==
                      static_cast<size_t>(StandingsHud::GapReferenceMode::COUNT), "one name per reference");
        SettingsHud::CycleControl gapRefCycle = SettingsHud::CycleControl::enumMember(
            hud, &StandingsHud::m_gapReferenceMode,
            static_cast<int>(StandingsHud::GapReferenceMode::COUNT), hud, kGapRefs);
        gapRefCycle.postStep = [hud]() {
            if (hud->m_gapReferenceMode == StandingsHud::GapReferenceMode::ALTERNATING) {
                hud->m_lastGapRefToggle = std::chrono::steady_clock::now();
                hud->m_alternatingCurrent = StandingsHud::GapReferenceMode::LEADER;
            }
        };
        ctx.addCycleControl("Gap reference", gapRefValue, gapRefCycle,
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
        static const char* const kAnimModes[] = { "Off", "Basic", "Colored" };
        const char* animModeValue = cycleName(kAnimModes, static_cast<int>(hud->m_animationMode));
        SettingsHud::CycleControl animCycle = SettingsHud::CycleControl::enumMember(
            hud, &StandingsHud::m_animationMode, 3, hud, kAnimModes);
        // Stop any in-flight animations immediately when transitioning to OFF;
        // otherwise rows would keep sliding until the cleanup timer drains.
        animCycle.postStep = [hud]() {
            if (hud->m_animationMode == StandingsHud::AnimationMode::OFF) {
                hud->m_activeAnimations.clear();
            }
        };
        ctx.addCycleControl("Animation", animModeValue, animCycle,
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
    ctx.endColumns();
    // === CONTENT SECTION ===
    ctx.addSectionHeading("Content");
    // Two columns, like Layout: the labels are kept to the cell's 15 characters.
    ctx.beginColumns(2, 13);

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
    ctx.addToggleControl("Status icon", (hud->m_enabledColumns & StandingsHud::COL_TRACKED) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_TRACKED, true,
        "standings.col_tracked");
    ctx.addToggleControl("Position", (hud->m_enabledColumns & StandingsHud::COL_POS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_POS, true,
        "standings.col_pos");
    // Positions gained/lost mode cycle (Off < > Sector < > Lap < > Race). Labels name the
    // scope the delta covers: RACE_START = the whole race, LAST_SF = the current lap,
    // LAST_SPLIT = the current sector. Bare nouns keep them parallel.
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
        posGainCycle.nameOf = [](int v) {
            static const char* const kNames[] = { "Off", "Sector", "Lap", "Race" };
            return std::string(kNames[v]);
        };
        posGainCycle.dirtyHud = hud;
        ctx.addCycleControl("Gained/lost", posGainValue, posGainCycle,
            hud, true, hud->m_posGainMode == StandingsHud::PosGainMode::OFF,
            "standings.col_posgain", /*tooltipOnArrows=*/false);
    }
    ctx.addToggleControl("Race number", (hud->m_enabledColumns & StandingsHud::COL_RACENUM) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_RACENUM, true,
        "standings.col_racenum");
    // Rider name mode (Off/Short/Long) - uses toggle with display value like gap scope
    {
        static const char* const kNameModes[] = { "Off", "Short", "Long" };
        ctx.addCycleControl("Rider name", cycleName(kNameModes, static_cast<int>(hud->m_nameMode)),
            SettingsHud::CycleControl::enumMember(hud, &StandingsHud::m_nameMode, 3, hud, kNameModes),
            hud, true, hud->m_nameMode == StandingsHud::NameMode::OFF,
            "standings.col_name");
    }
    ctx.addToggleControl("Class", (hud->m_enabledColumns & StandingsHud::COL_CATEGORY) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_CATEGORY, true,
        "standings.col_category");
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
        static const char* const kGapModes[] = { "Off", "Player", "Adjacent", "All" };
        // One name per mode, so a new mode trips here instead of drawing blank.
        static_assert(sizeof(kGapModes) / sizeof(kGapModes[0]) ==
                      static_cast<size_t>(StandingsHud::GapMode::COUNT), "one name per gap mode");
        const bool isOff = (hud->m_gapMode == StandingsHud::GapMode::OFF);
        ctx.addCycleControl("Gap column", cycleName(kGapModes, static_cast<int>(hud->m_gapMode)),
            // Modulus from the enum, not a literal, so a new mode cannot leave a
            // stale count behind with nothing to catch it.
            SettingsHud::CycleControl::enumMember(hud, &StandingsHud::m_gapMode,
                static_cast<int>(StandingsHud::GapMode::COUNT), hud, kGapModes),
            hud, true, isOff, "standings.gap_mode");
    }

    ctx.addToggleControl("Penalty", (hud->m_enabledColumns & StandingsHud::COL_PENALTY) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_PENALTY, true,
        "standings.col_penalty");
    ctx.endColumns();

    return hud;
}
