// ============================================================================
// hud/settings_hud_render.cpp
// SettingsHud::rebuildRenderData() — the settings menu's full render build: it
// lays out every tab's controls, labels, click regions and tooltips into the
// HUD's quad/string vectors (the footer button row is settings_hud_footer.cpp).
// Companion to settings_hud_input.cpp.
//
// The tab bar and its two icon helpers are member functions below, not lambdas
// (each needs 2 parameters beyond its original arguments, 5 for the bar); the
// per-tab CONTROL code lives in SettingsLayoutContext.
// ============================================================================
// file-budget: 1350 the tab registry and shared render scaffolding; per-tab code is already split
#include <cmath>

#include "settings_hud.h"
#include "prestige_widget.h"
#include "ideal_lap_hud.h"
#include "lap_log_hud.h"
#include "friends_hud.h"
#include "session_charts_hud.h"
#include "standings_hud.h"
#include "performance_hud.h"
#include "pitboard_hud.h"
#include "session_hud.h"
#include "timing_hud.h"
#include "gap_bar_hud.h"
#include "notices_hud.h"
#include "records_hud.h"
#include "event_log_hud.h"
#include "map_hud.h"
#include "radar_hud.h"
#include "achievement_widget.h"
#include "spotter_widget.h"
#include "../core/stats_manager.h"
#include "settings/settings_layout.h"
#include "settings/text_wrap.h"
#include "telemetry_hud.h"
#include "rumble_hud.h"
#include "helmet_overlay_hud.h"
#include "fmx_hud.h"
#include "stats_hud.h"
#include "settings_button_widget.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "delta_trace_hud.h"
#include "stream_chat_hud.h"
#include "../core/profile_manager.h"
#include "../core/update_checker.h"
#include "../core/update_downloader.h"
#include "../core/director_manager.h"
#include "../core/spotter_manager.h"
#include "../core/achievement_manager.h"
#include "director_widget.h"
#include "../core/hotkey_manager.h"
#if GAME_HAS_DISCORD
#include "../core/discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "../core/steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "../core/http_server.h"
#endif
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include "../core/ui_config.h"
#include "../core/plugin_data.h"
#include "../core/tooltip_manager.h"
#include "../handlers/draw_handler.h"
#include "settings/whats_new.h"
#include <cstring>
#include <iterator>
#include <algorithm>

using namespace PluginConstants;


// ============================================================================
// Per-tab descriptor registry - the ONE place that routes everything per-tab
// (see TabDescriptor in settings_hud.h). Rows are in VISUAL ORDER: the tab-list
// render loop iterates this table directly, so a row's position here is its
// position in the tab column. Adding a tab = one Tab enum value + one row.
//
// Notes:
// - hud: backing HUD for the tab-list checkbox (HUD_TOGGLE) and, for gameGated
//   rows, the availability probe. Master-toggle tabs (Widgets/Rumble/Helmet/
//   Updates/Director) keep hud=null - their checkbox is special-cased in the
//   tab-list loop because they toggle managers, not a BaseHud.
// - click=null: the tab has no tab-specific handler; its regions are handled
//   by the common switch in dispatchRegion().
// - resetHud/resetExtra: see resetCurrentTab(). resetHud runs first (standard
//   keep-visibility HUD reset), then resetExtra for anything outside the
//   per-HUD snapshot.
// - The global section lists every tab. The profile section lists its tabs
//   most-used first, then a TAB_GROUP row ("More") owning the rest up to the
//   next marker. A group never draws its tabs in the
//   sidebar: its one row opens the More page (TAB_MORE, settings_tab_more.cpp),
//   which lists them -- so the sidebar's height does not depend on the tab. The profile
//   tabs follow the usage survey's HUD adoption (MX Bikes, Oct 2026), Widgets
//   first because widgets out-adopt every HUD; Delta Trace is too new to rank and
//   sits beside the Gap Bar. A tab carrying what's-new markers sits ABOVE its
//   section's More: the More header has no "New" tag (its tag slot shows the
//   on-count), so news inside a closed More would draw nowhere. Pinned by
//   whats_new_test.
// - Game-gated tabs (Records/FMX/Friends) are gated at RUNTIME via
//   gameGated + a null HUD pointer (the 'Disabling a Feature Per-Game'
//   pattern), so rows need no #if guards.
// ============================================================================
const SettingsHud::TabDescriptor SettingsHud::s_tabRegistry[] = {
    // tabId            name          tooltipId        hud (backing HUD getter)                                              gameGated  render                                 click                                     resetHud            resetExtra                              sectionIcon
    { TAB_SECTION_GLOBAL,   nullptr,  nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr },
    { TAB_GENERAL,      "General",    "general",       nullptr,                                                              false, &SettingsHud::renderTabGeneral,         &SettingsHud::handleClickTabGeneral,      nullptr,            &SettingsHud::resetTabGeneral,          "hud-general" , nullptr },
    { TAB_APPEARANCE,   "Appearance", "appearance",    nullptr,                                                              false, &SettingsHud::renderTabAppearance,      &SettingsHud::handleClickTabAppearance,   nullptr,            &SettingsHud::resetTabAppearance,       "hud-appearance" , nullptr },
    { TAB_HOTKEYS,      "Hotkeys",    "hotkeys",       nullptr,                                                              false, &SettingsHud::renderTabHotkeys,         &SettingsHud::handleClickTabHotkeys,      nullptr,            &SettingsHud::resetTabHotkeys,          "hud-hotkeys" , nullptr },
    { TAB_RIDERS,       "Riders",     "riders",        nullptr,                                                              false, &SettingsHud::renderTabRiders,          &SettingsHud::handleClickTabRiders,       nullptr,            &SettingsHud::resetTabRiders,           "hud-riders" , nullptr },
    { TAB_RUMBLE,       "Rumble",     "rumble",        nullptr,                                                              false, &SettingsHud::renderTabRumble,          &SettingsHud::handleClickTabRumble,       nullptr,            &SettingsHud::resetTabRumble,           nullptr, nullptr },
    { TAB_HELMET,       "Helmet",     "helmet",        nullptr,                                                              false, &SettingsHud::renderTabHelmet,          &SettingsHud::handleClickTabHelmet,       nullptr,            &SettingsHud::resetTabHelmet,           nullptr, nullptr },
    { TAB_DIRECTOR,     "Director",   "director",      nullptr,                                                              false, &SettingsHud::renderTabDirector,        nullptr,                                  nullptr,            &SettingsHud::resetTabDirector,         nullptr, nullptr },
    // A GLOBAL tab with a backing HUD: the sidebar checkbox is the chat HUD's own
    // HUD_TOGGLE (visibility only -- each platform's connection has its own
    // switch on the tab), and its settings persist in [Twitch]/[YouTube], not the
    // per-profile cache, so there is no resetHud: resetTabStreamChat replays the
    // sections instead. NO badge: "Stream Chat" is 11 of the sidebar's 13 label
    // cells, so a "New" tag (2.25 cells) would overflow them (see the badge
    // comment in the tab-list loop); its rows still band, and so does its sidebar
    // row instead of the tag (WhatsNew::tabCanTag / tabHighlightsRow).
    { TAB_STREAM_CHAT,       "Stream Chat", "stream_chat",   [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getStreamChatHud(); },
                                                                                                                                      false, &SettingsHud::renderTabStreamChat,          &SettingsHud::handleClickTabStreamChat,       nullptr,            &SettingsHud::resetTabStreamChat,           nullptr, nullptr },
    // The trailing pair: not hidden, and the subtitle widget is what this tab
    // positions (see TabDescriptor::previewHud).
    { TAB_SPOTTER,      "Spotter",    "spotter",       nullptr,                                                              false, &SettingsHud::renderTabSpotter,         &SettingsHud::handleClickTabSpotter,      nullptr,            &SettingsHud::resetTabSpotter,          nullptr, "Beta", false,
      [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getSpotterWidget(); } },
    // NO badge, ever: "Achievements" is 12 of the sidebar's 13 label cells, so a
    // Small "New" (2.25 cells) would collide with it -- see settingsSidebarWidth.
    // Its news bands the row instead (WhatsNew::tabHighlightsRow).
    { TAB_ACHIEVEMENTS, "Achievements", "achievements", nullptr,                                                            true,  &SettingsHud::renderTabAchievements,    &SettingsHud::handleClickTabAchievements, nullptr,            &SettingsHud::resetTabAchievements,     nullptr, nullptr, false,
      // The card exists on every game (it carries the system toasts), so the
      // gate is the feature flag, not the widget being registered.
      [](const SettingsHud&) -> BaseHud* { return GAME_HAS_ACHIEVEMENTS ? HudManager::getInstance().getAchievementWidget() : nullptr; } },
    { TAB_UPDATES,      "Updates",    "updates",       nullptr,                                                              false, &SettingsHud::renderTabUpdates,         &SettingsHud::handleClickTabUpdates,      nullptr,            &SettingsHud::resetTabUpdates,          nullptr, nullptr },
    // HIDDEN (the trailing true): reached from the footer's About button, never
    // drawn in the sidebar. Its POSITION still matters even so -- it is in the GLOBAL
    // section because tools/check_docs.py splits this registry at the TAB_SECTION_*
    // markers to decide which README table a tab belongs in, and About is a global
    // page, not a HUD. Placed last for the same reason it would be if it were
    // listed.
    { TAB_ABOUT,        "About",      "about",         nullptr,                                                              false, &SettingsHud::renderTabAbout,           nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr, true },
    // HIDDEN too: a section's More row opens it (m_moreGroupRow says which section).
    { TAB_MORE,         "More",       "more",          nullptr,                                                              false, &SettingsHud::renderTabMore,            nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr, true },
    { TAB_SECTION_PROFILE,  nullptr,  nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr },
    { TAB_WIDGETS,      "Widgets",    "widgets",       nullptr,                                                              false, &SettingsHud::renderTabWidgets,         nullptr,                                  nullptr,            &SettingsHud::resetTabWidgets,          nullptr, nullptr },
    { TAB_MAP,          "Map",        "map",           [](const SettingsHud& s) -> BaseHud* { return s.m_mapHud; },          false, &SettingsHud::renderTabMap,             &SettingsHud::handleClickTabMap,          "MapHud",           nullptr,                                nullptr, nullptr },
    { TAB_NOTICES,      "Notices",    "notices",       [](const SettingsHud& s) -> BaseHud* { return s.m_notices; },         false, &SettingsHud::renderTabNotices,         nullptr,                                  "NoticesHud",       nullptr,                                nullptr, nullptr },
    { TAB_STANDINGS,    "Standings",  "standings",     [](const SettingsHud& s) -> BaseHud* { return s.m_standings; },       false, &SettingsHud::renderTabStandings,       &SettingsHud::handleClickTabStandings,    "StandingsHud",     &SettingsHud::resetTabStandingsExtra,   nullptr, nullptr },
    { TAB_FRIENDS,      "Friends",    "friends",       [](const SettingsHud& s) -> BaseHud* { return s.m_friends; },         true,  &SettingsHud::renderTabFriends,         &SettingsHud::handleClickTabFriends,      "FriendsHud",       nullptr,                                nullptr, nullptr },
    { TAB_TIMING,       "Timing",     "timing",        [](const SettingsHud& s) -> BaseHud* { return s.m_timing; },          false, &SettingsHud::renderTabTiming,          &SettingsHud::handleClickTabTiming,       "TimingHud",        nullptr,                                nullptr, nullptr },
    { TAB_LAP_LOG,      "Lap Log",    "lap_log",       [](const SettingsHud& s) -> BaseHud* { return s.m_lapLog; },          false, &SettingsHud::renderTabLapLog,          &SettingsHud::handleClickTabLapLog,       "LapLogHud",        nullptr,                                nullptr, nullptr },
    { TAB_GAP_BAR,      "Gap Bar",    "gap_bar",       [](const SettingsHud& s) -> BaseHud* { return s.m_gapBar; },          false, &SettingsHud::renderTabGapBar,          &SettingsHud::handleClickTabGapBar,       "GapBarHud",        nullptr,                                nullptr, nullptr },
    // NO badge: "Delta Trace" is 11 of the sidebar's 13 label cells, like Stream
    // Chat above; its news bands the sidebar row instead (WhatsNew::tabCanTag).
    { TAB_DELTA_TRACE,  "Delta Trace","delta_trace",   [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getDeltaTraceHud(); },
                                                                                                                                      false, &SettingsHud::renderTabDeltaTrace,      nullptr,                                  "DeltaTraceHud",    nullptr,                                nullptr, nullptr },
    { TAB_PITBOARD,     "Pitboard",   "pitboard",      [](const SettingsHud& s) -> BaseHud* { return s.m_pitboard; },        false, &SettingsHud::renderTabPitboard,        nullptr,                                  "PitboardHud",      nullptr,                                nullptr, nullptr },
    { TAB_GROUP,        "More",       nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr },
    { TAB_RADAR,        "Radar",      "radar",         [](const SettingsHud& s) -> BaseHud* { return s.m_radarHud; },        false, &SettingsHud::renderTabRadar,           nullptr,                                  "RadarHud",         nullptr,                                nullptr, nullptr },
    { TAB_IDEAL_LAP,    "Ideal Lap",  "ideal_lap",     [](const SettingsHud& s) -> BaseHud* { return s.m_idealLap; },        false, &SettingsHud::renderTabIdealLap,        nullptr,                                  "IdealLapHud",      nullptr,                                nullptr, nullptr },
    { TAB_SESSION,      "Session",    "session",       [](const SettingsHud& s) -> BaseHud* { return s.m_session; },         false, &SettingsHud::renderTabSession,         &SettingsHud::handleClickTabSession,      "SessionHud",       nullptr,                                nullptr, nullptr },
    { TAB_PERFORMANCE,  "Performance","performance",   [](const SettingsHud& s) -> BaseHud* { return s.m_performance; },     false, &SettingsHud::renderTabPerformance,     &SettingsHud::handleClickTabPerformance,  "PerformanceHud",   nullptr,                                nullptr, nullptr },
    { TAB_STATS,        "Stats",      "stats",         [](const SettingsHud& s) -> BaseHud* { return s.m_statsHud; },        false, &SettingsHud::renderTabStats,           &SettingsHud::handleClickTabStats,        "StatsHud",         nullptr,                                nullptr, nullptr },
    { TAB_TELEMETRY,    "Telemetry",  "telemetry",     [](const SettingsHud& s) -> BaseHud* { return s.m_telemetry; },       false, &SettingsHud::renderTabTelemetry,       nullptr,                                  "TelemetryHud",     nullptr,                                nullptr, nullptr },
    { TAB_RECORDS,      "Records",    "records",       [](const SettingsHud& s) -> BaseHud* { return s.m_records; },         true,  &SettingsHud::renderTabRecords,         &SettingsHud::handleClickTabRecords,      "RecordsHud",       &SettingsHud::resetTabRecordsExtra,     nullptr, nullptr },
    { TAB_FMX,          "FMX",        "fmx",           [](const SettingsHud& s) -> BaseHud* { return s.m_fmxHud; },          true,  &SettingsHud::renderTabFmx,             &SettingsHud::handleClickTabFmx,          "FmxHud",           nullptr,                                nullptr, nullptr },
    { TAB_EVENT_LOG,    "Event Log",  "event_log",     [](const SettingsHud& s) -> BaseHud* { return s.m_eventLog; },        false, &SettingsHud::renderTabEventLog,        &SettingsHud::handleClickTabEventLog,     "EventLogHud",      nullptr,                                nullptr, nullptr },
    { TAB_SESSION_CHARTS, "Charts",   "session_charts",[](const SettingsHud& s) -> BaseHud* { return s.m_sessionCharts; },   false, &SettingsHud::renderTabSessionCharts,   nullptr,                                  "SessionChartsHud", nullptr,                                nullptr, nullptr },
};

const SettingsHud::TabDescriptor* SettingsHud::findTabDescriptor(int tabId) {
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId == tabId) return &row;
    }
    return nullptr;
}

int SettingsHud::groupRowOf(int tabId) {
    if (tabId < 0) return -1;
    int group = -1;
    const int registrySize = static_cast<int>(std::size(s_tabRegistry));
    for (int r = 0; r < registrySize; ++r) {
        const int id = s_tabRegistry[r].tabId;
        if (id == TAB_GROUP) group = r;
        else if (id < 0) group = -1;                 // a section header closes the group
        else if (id == tabId) return s_tabRegistry[r].hidden ? -1 : group;   // About: no group
    }
    return -1;
}

// [Back] under the content of a tab opened from a More page: the way back to the
// page, where the Updates tab puts Check Now. The same click as that group's More
// row in the sidebar (a TAB region flagged GROUP_HEADER, see handleTabClick).
// Its section gets no card (rebuildRenderData): the button sits on the panel.
// Called after the tab's own rows in BOTH the measure and the drawing pass, so
// its height is part of the tab's and the tallest-tab measure counts it.
void SettingsHud::addBackButton(SettingsLayoutContext& ctx) {
    if (groupRowOf(m_activeTab) < 0) return;
    ctx.beginUntitledSection();
    const size_t first = m_clickRegions.size();
    ctx.addActionButton("Back", 6, ClickRegion::TAB,
                        SettingsLayoutContext::ButtonRole::Accent, true, "more.back");
    if (first < m_clickRegions.size()) {
        ClickRegion& back = m_clickRegions[first];
        back.flagBit = GROUP_HEADER;
        back.tabIndex = m_activeTab;
    }
}

// ==========================================================================
// One More group's row in the tab bar: an icon, "More", and on the right edge how
// many of its toggles are on (when it has two or more). It reads as a tab, and it
// is selected while its More page -- or any tab it lists -- is open, so the
// sidebar always shows where you are.
//
// A click opens the More page for this group (handleTabClick, keyed by the
// region's GROUP_HEADER flag on an ordinary ClickRegion::TAB whose tabIndex names
// the group's first tab). The region carries the "more" tooltip id.
//
// A group with no available tab (every one game-gated off) draws nothing.
// ==========================================================================
void SettingsHud::buildMoreRow(const ScaledDimensions& dim, const PanelPlan& plan,
                               const PanelBox::ColumnGeom& col, int groupRow,
                               float tabStartX, float& tabStartY,
                               float tabWidth, float checkboxWidth) {
    ColorConfig& cc = ColorConfig::getInstance();
    const int registrySize = static_cast<int>(std::size(s_tabRegistry));

    int firstTab = -1;
    int toggles = 0;
    int togglesOn = 0;
    for (int r = groupRow + 1; r < registrySize && s_tabRegistry[r].tabId >= 0; ++r) {
        const TabDescriptor& row = s_tabRegistry[r];
        if (row.hidden || !isTabAvailable(row.tabId)) continue;
        if (firstTab < 0) firstTab = row.tabId;
        BaseHud* hud = row.hud ? row.hud(*this) : nullptr;
        bool on = false;
        if (tabToggleState(row.tabId, hud, &on)) {
            ++toggles;
            if (on) ++togglesOn;
        }
    }
    if (firstTab < 0) return;

    int pageGroup = -1;
    moreGroupTabs(nullptr, 0, &pageGroup);
    const bool selected = (m_activeTab == TAB_MORE && pageGroup == groupRow)
                       || groupRowOf(m_activeTab) == groupRow;

    // Band first, like a tab row's (quads draw in push order): the selected fill,
    // else the hover band when the region this row is about to push is hovered.
    if (selected) {
        addButtonQuad(tabStartX, tabStartY, tabWidth, dim.lineHeightNormal,
                      PluginUtils::applyOpacity(cc.getAccent(), 128.0f / 255.0f),
                      /*opaque=*/true, ButtonFill::State);
    } else if (m_hoveredRegionIndex >= 0 &&
               static_cast<size_t>(m_hoveredRegionIndex) == m_clickRegions.size()) {
        addRowHighlight(plan.rowBandX(col), tabStartY, plan.rowBandW(col), dim.lineHeightNormal,
                        PluginUtils::applyOpacity(cc.getAccent(), ROW_HOVER_ALPHA));
    }

    const unsigned long color = selected ? cc.getPrimary() : cc.getAccent();
    drawTabIcon(tabStartX, tabStartY, "layer-group", color, dim, checkboxWidth);

    const float labelX = tabStartX + checkboxWidth;
    const float labelW = tabWidth - checkboxWidth;

    ClickRegion region;
    region.x = tabStartX;
    region.y = tabStartY;
    region.width = tabWidth;
    region.height = dim.lineHeightNormal;
    region.type = ClickRegion::TAB;
    region.targetPointer = std::monostate{};
    region.flagBit = GROUP_HEADER;
    region.isRequired = false;
    region.targetHud = nullptr;
    region.tabIndex = firstTab;
    region.tooltipId = "more";
    m_clickRegions.push_back(region);

    addString(s_tabRegistry[groupRow].name, labelX, tabStartY, Justify::LEFT,
              Fonts::getNormal(), color, dim.fontSize);

    // Right edge, Small and MUTED, in the slot and centring of a tab row's badge:
    // how many of the group's toggles are on. The slot never carries "New" -- a
    // tab with news sits above More instead (see the registry).
    if (toggles > 1) {   // "0/1" over one toggle says less than the icon would
        char count[8];
        snprintf(count, sizeof(count), "%d/%d", togglesOn, toggles);
        addString(count, labelX + labelW, tabStartY + labelRowYOffset(dim), Justify::RIGHT,
                  Fonts::getStrong(), selected ? cc.getPrimary() : cc.getMuted(), dim.fontSizeSmall);
    }

    tabStartY += dim.lineHeightNormal;
}

// What the More page lists: the group its row opened, else the last group (a
// restored "More" tab has no row to say which). Writes up to `cap` of the group's
// available tab ids to `tabs` (may be null) and the group's row to *groupRow;
// returns how many there are.
int SettingsHud::moreGroupTabs(int* tabs, int cap, int* groupRow) const {
    const int registrySize = static_cast<int>(std::size(s_tabRegistry));
    int group = m_moreGroupRow;
    if (group < 0) {
        for (int r = 0; r < registrySize; ++r) {
            if (s_tabRegistry[r].tabId == TAB_GROUP) group = r;
        }
    }
    if (groupRow) *groupRow = group;
    int count = 0;
    for (int r = group + 1; group >= 0 && r < registrySize && s_tabRegistry[r].tabId >= 0; ++r) {
        const TabDescriptor& row = s_tabRegistry[r];
        if (row.hidden || !isTabAvailable(row.tabId)) continue;
        if (tabs && count < cap) tabs[count] = row.tabId;
        ++count;
    }
    return count;
}

// ==========================================================================
// The vertical tab bar. Split out for the same reason as the helpers above: the
// loop reads exactly FIVE values from the enclosing scope -- the two tab-bar
// origins, the tab and checkbox widths, and the scaled dimensions. tabStartY is
// advanced locally and deliberately NOT returned: the
// content column starts at the panel cursor, not below the tabs, so the final
// value has no reader.
//
// Region emission ORDER is behaviour here -- clicks are hit-tested in order -- and
// is pinned by tests/integration/tests/settings_layout_test.cpp's golden.
void SettingsHud::buildTabBar(const ScaledDimensions& dim, const PanelPlan& plan,
                              const PanelBox::ColumnGeom& col, float tabStartX,
                              float tabWidth, float checkboxWidth) {
    // ONE SECTION PER GROUP, at the engine's own origin for it. Every group marker
    // below jumps the cursor to the next section's top instead of advancing by a
    // seam it computes -- the seam between two groups is the same one between any
    // two sibling cards, and it is the engine's to spend. The cards themselves are
    // drawn by addPlanBackground before any of this runs, so nothing here opens or
    // closes one.
    size_t group = 0;
    float tabStartY = plan.colContentY(col, 0);
    // Visual tab order comes straight from the descriptor registry (rows are in
    // display order; negative TAB_SECTION_* rows are the section headers).
    int curGroupRow = -1;   // the TAB_GROUP row owning the rows being walked; -1 = none
    const int registrySize = static_cast<int>(std::size(s_tabRegistry));
    for (int rowIdx = 0; rowIdx < registrySize; ++rowIdx) {
        const TabDescriptor& tabRow = s_tabRegistry[rowIdx];
        const int i = tabRow.tabId;

        if (i == TAB_GROUP) {
            curGroupRow = rowIdx;
            buildMoreRow(dim, plan, col, rowIdx, tabStartX, tabStartY, tabWidth, checkboxWidth);
            continue;
        }
        if (i == TAB_SECTION_GLOBAL || i == TAB_SECTION_PROFILE) curGroupRow = -1;
        // A group's tabs draw nothing here; its More page lists them.
        if (i >= 0 && curGroupRow >= 0) continue;

        // Skip game-gated tabs whose backing HUD isn't registered on this build (Records on
        // GP Bikes, FMX on karts, Friends on non-Steam). Section headers are negative ids and
        // fall through to their own handling below. Single source of truth: isTabAvailable().
        // A HIDDEN tab draws no row: About is reached from the footer, not the list.
        // Checked before availability because the two say different things -- hidden
        // is "not in this column", available is "selectable at all", and About is
        // both hidden and available.
        if (tabRow.hidden) continue;
        if (i >= 0 && !isTabAvailable(i)) {
            continue;
        }

        // Section headers (bold, primary color, not clickable). Each opens a themed
        // card over its whole group, closed by the next marker (or by the loop end),
        // matching the content column's section cards exactly.

        if (i == TAB_SECTION_GLOBAL) {
            // TWO GROUPS, Global and Profile: the element tabs ARE per-profile, so
            // listing them under the profile cycler says what they are, and a third
            // group would cost a card and a seam. Every caption row counts: the
            // sidebar is the panel's binding height under theme_geometry_test's
            // fits-the-screen contract.
            tabStartY = plan.colContentY(col, group++);
            addString("Global", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }
        if (i == TAB_SECTION_PROFILE) {
            tabStartY = plan.colContentY(col, group++);
            addString("Profile", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;

            // Profile cycle control: < Practice >
            float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
            const char* profileName = ProfileManager::getProfileName(activeProfile);

            float currentX = tabStartX;

            // Left arrow "<" with click region (cycles to previous profile)
            addString("<", currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, tabStartY, charWidth * 2, dim.lineHeightNormal,
                ClickRegion::PROFILE_CYCLE_DOWN, nullptr
            ));
            currentX += charWidth * 2;

            // Profile name (not clickable)
            char profileLabel[12];
            snprintf(profileLabel, sizeof(profileLabel), "%-8s", profileName);
            addString(profileLabel, currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentX += charWidth * 8;

            // Right arrow with click region (cycles to next profile)
            addString(">", currentX + charWidth, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, tabStartY, charWidth * 2, dim.lineHeightNormal,
                ClickRegion::PROFILE_CYCLE_UP, nullptr
            ));

            tabStartY += dim.lineHeightNormal;
            continue;
        }

        bool isActive = (i == m_activeTab);

        // Get the HUD for this tab (nullptr for master-toggle/section tabs)
        BaseHud* tabHud = tabRow.hud ? tabRow.hud(*this) : nullptr;

        bool isHudEnabled = true;  // tabs without a toggle (General...) read as enabled
        const bool rowHasCheckbox = tabToggleState(i, tabHud, &isHudEnabled);

        // Tab color: PRIMARY if active, ACCENT if inactive
        unsigned long tabColor = isActive ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getAccent();

        float currentTabX = tabStartX;

        // THE ROW'S BAND, EMITTED FIRST. Quads draw in push order, so a band pushed
        // after the row's identity icon paints over it and the selected tab looks
        // like it has none (the label survives only because it is pushed later
        // still). That reads as a colour clash, but nothing about the colours is
        // wrong.
        //
        // The hover test needs the index the TAB region will get, which is one past
        // the checkbox region the branches below may push -- hence the prediction
        // rather than a captured index. Click order: regions are pushed
        // checkbox-then-tab, which is what makes a click on the box toggle rather
        // than select (pinned by settings_layout_test's region golden).
        const size_t tabRegionIndex = m_clickRegions.size() + (rowHasCheckbox ? 1u : 0u);
        {
            // The highlight spans the tab COLUMN, not the label: full row width,
            // identity icon included, the same for every tab whatever its label length.
            // Starting right of the icon would leave the icon outside the highlight
            // meant to select its row; ending at the label would make the inset from
            // the group card's right edge vary with the label while the left inset
            // stays fixed -- uneven left/right margins in the tab menu.
            if (isActive) {
                // Themed button shape when the theme provides one; the plain solid
                // quad otherwise. The COLOUR is passed through either way -- it
                // carries state (unsaved / disabled / hovered), not decoration.
                // ButtonFill::State: this is the SELECTED ROW of the tab list, not a
                // control, so it keeps the accent fill rather than taking the neutral
                // surface an unthemed button gets. A selection has to read at a
                // glance; a button has a label to carry its meaning.
                addButtonQuad(tabStartX, tabStartY, tabWidth, dim.lineHeightNormal,
                              PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(),
                                                        128.0f / 255.0f),
                              /*opaque=*/true, ButtonFill::State);
            } else if (m_hoveredRegionIndex >= 0 &&
                       static_cast<size_t>(m_hoveredRegionIndex) == tabRegionIndex) {
                // The same band every row highlight spans (plan.rowBandX/W), taken
                // from THIS column's box -- the sidebar and the content column
                // opposite it answer the same way.
                addRowHighlight(plan.rowBandX(col), tabStartY, plan.rowBandW(col),
                                dim.lineHeightNormal,
                                PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(),
                                                          ROW_HOVER_ALPHA));
            } else if (WhatsNew::tabHighlightsRow(i)) {
                // A tab with news but no room for the "New" tag gets the what's-new
                // ROW band instead (addWhatsNewRowBands): same colour, same alpha,
                // same span as the hover band that replaces it -- and hovering is
                // what dismisses it (dismissMarkedTab).
                addRowHighlight(plan.rowBandX(col), tabStartY, plan.rowBandW(col),
                                dim.lineHeightNormal,
                                PluginUtils::applyOpacity(ColorConfig::getInstance().getPositive(),
                                                          ROW_HOVER_ALPHA));
            }
        }

        // The row's toggle, when it has one: its icon IS the checkbox (tabToggleType
        // names the region); otherwise the identity icon in the label's colour.
        const char* iconName = tabIconName(i, tabHud);
        if (rowHasCheckbox) {
            BaseHud* toggleTarget = nullptr;
            const ClickRegion::Type toggleType = tabToggleType(i, tabHud, &toggleTarget);
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                toggleType, toggleTarget
            ));
            drawTabToggle(currentTabX, tabStartY, iconName, isHudEnabled, isActive, dim, checkboxWidth);
        } else {
            // Non-toggleable section tabs: the identity icon takes the SAME colour rule
            // as the label beside it -- PRIMARY on the active row, ACCENT elsewhere.
            //
            // The active row's band is accent, so an icon pinned to ACCENT is accent on
            // accent on the selected row and simply disappears while its label stays
            // readable. One rule for the pair, so the band's colour cannot swallow
            // half of it.
            drawTabIcon(currentTabX, tabStartY, iconName ? iconName : "", tabColor, dim, checkboxWidth);
        }
        currentTabX += checkboxWidth;

        // Tab click region (for selecting the tab)
        float tabLabelWidth = tabWidth - checkboxWidth;
        // (tabRegionIndex was predicted above, before the band was emitted.)
        assert(tabRegionIndex == m_clickRegions.size());

        // Tab ID for description lookup (lowercase). Table-driven, so a tab cannot
        // be missing from a hand-maintained chain and show another's description.
        const char* tabId = tabRow.tooltipId ? tabRow.tooltipId : "general";

        ClickRegion tabRegion;
        tabRegion.x = currentTabX;
        tabRegion.y = tabStartY;
        tabRegion.width = tabLabelWidth;
        tabRegion.height = dim.lineHeightNormal;
        tabRegion.type = ClickRegion::TAB;
        tabRegion.targetPointer = std::monostate{};
        tabRegion.flagBit = 0;
        tabRegion.isRequired = false;
        tabRegion.targetHud = nullptr;
        tabRegion.tabIndex = i;
        tabRegion.tooltipId = tabId;  // Show tab description on hover
        m_clickRegions.push_back(tabRegion);

        // Label stays at its original position (the highlight leads it, above).
        const char* label = getTabName(i);
        addString(label, currentTabX, tabStartY, Justify::LEFT, Fonts::getNormal(), tabColor, dim.fontSize);

        // A STATUS TAG ON THE ROW'S RIGHT EDGE -- "Beta", or "New" for a tab carrying an
        // undismissed what's-new marker (see settings/whats_new.h). A tab's own
        // badge wins: the Spotter is both new and beta, and "Spotter Beta New"
        // says less than either alone.
        // THREE SOURCES, in precedence order, and each says a different thing:
        //   the tab's OWN badge ("Beta") -- a standing caveat, so it outranks news;
        //   "Update"  -- a release is waiting on this tab (UpdateChecker);
        //   "New"     -- an undismissed what's-new marker (settings/whats_new.h).
        // One slot, so a tab that qualifies for two shows the more important: the
        // Spotter is both new and beta, and "Spotter Beta New" says less than either.
        const char* badge = tabRow.badge;
        bool badgeIsNews = false;
        if (!badge && i == TAB_UPDATES &&
            UpdateChecker::getInstance().shouldShowUpdateTag()) {
            badge = "Update";
            badgeIsNews = true;
        }
        if (!badge && WhatsNew::tabHasLive(i)) { badge = "New"; badgeIsNews = true; }

        // The tag draws in the SMALL size, so it reads as a note about the tab rather
        // than part of its name.
        //
        // TWO COLOURS, because the two tags say opposite things. "Beta" is a caveat --
        // WARNING, the slot the tab's own body text uses for the same caveat. "New" is
        // an invitation to go and look -- POSITIVE. One colour for both would make a
        // finished feature announce itself in the colour this plugin uses everywhere
        // else for "careful", and leave the actual caveat with nothing to distinguish
        // it. The row bands below take the same POSITIVE for the same reason.
        //
        // Not folded into the label string, for two reasons. getTabName() is the
        // PERSISTED name ([Profiles] activeTab, what setActiveTabByName matches, and
        // what the "Reset <tab>" button reads), so badging it would strand anyone who
        // had the tab open. And the sidebar has no room: 16 characters less the
        // 4-character icon column leaves 12 for the label, which "Spotter (Beta)" (14)
        // would overrun into the content column. At the small size the tag costs
        // about three characters instead of seven, and fits.
        //
        // RIGHT-ALIGNED ON THE SIDEBAR'S CONTENT EDGE, not measured off the label.
        // Measured placement puts each tag at a different x, so a column of tabs shows
        // its tags on a ragged diagonal following the name lengths -- correct per row
        // and wrong as a group. One edge for all of them reads as a column, and it is
        // the same edge the content column starts after.
        //
        // "Update" is the longest badge (6 chars = 4.5 cells at fontSizeS) but sits on
        // "Updates" (7), for 11.5 of the 13 -- roomier than the pair below.
        //
        // The tightest pair is Appearance + "New": the label area is
        // settingsSidebarWidth minus the 4-character icon column, so 13 cells at the
        // shipped 17; "Appearance" is 10 and a Small "New" costs 3 x fontSizeS =
        // 2.25, leaving 0.75 of a cell between them -- exactly one small-font space.
        // The 17 EXISTS FOR THIS PAIR: at 16 it is 12 cells against 12.25 asked, the
        // tag starts where the label's last glyph ends, and the row reads
        // "AppearanceNew". See the comment on settingsSidebarWidth, which owns the
        // sum. Adding a badge to a tab with a longer name means redoing it, which
        // nothing automates -- a collision here is a rendering artefact, not an
        // overflow any layout test can measure.
        //
        // It is also why the tag is Small rather than Normal: at full size "New"
        // would cost 3 whole cells and collide even at 17.
        //
        // Vertically it takes labelRowYOffset, the same centring every Small-size label
        // in the plugin uses to sit in a normal-height row.
        if (badge) {
            addString(badge, currentTabX + tabLabelWidth,
                      tabStartY + labelRowYOffset(dim), Justify::RIGHT,
                      Fonts::getStrong(),
                      badgeIsNews ? ColorConfig::getInstance().getPositive()
                                  : ColorConfig::getInstance().getWarning(),
                      dim.fontSizeSmall);
        }

        tabStartY += dim.lineHeightNormal;
    }
}

// Panel top edge -> where tab content begins: the title band and the air around it.
//
// ONE function because the panel HEIGHT reserves this and the LAYOUT spends it.
// Two separate expressions agree only by accidental slack, which makes the gaps
// around the band un-auditable -- nothing in the height says what the layout will
// do with the space it reserved -- and a moved band turns that slack into an
// overflow. So they are the same arithmetic.
//
// The themed case is written as the stack it is, top to bottom, so it can be read
// against a screenshot: frame clearance, band, seam, card pad. The seam is the
// composed contentGapY read; the trailing term is the first card's own pad
// (cardPadTopY), since a card's top edge sits that far above the content
// cursor this returns.
// See the declaration. Split per end because [content] padding is per-side; the
// theme→built-in fallback is resolvePanelSpec's. Pixels, and the two halves
// convert on their own lattices: the settings base is a historic cellH
// constant, while the [content] padding is a BOX TERM and box terms are
// square on screen (cellW * aspect — the engine's unit), so composing both
// under cellH would render the term ~20% taller here than a plan panel
// renders the very same key.
// The box terms this panel spends on its own chrome. One resolver shape for all
// four: the theme's key if it set one, else the [Advanced] built-in, converted
// square on screen (cellW * aspect) and scaled — exactly what the engine's
// `unit` does, so a cell here is the same distance as a cell in a plan panel.
namespace {
inline double resolveSide(const ThemeAsset::BoxTerm& themed, const PanelBox::Sides& builtIn,
                          bool bottom) {
    const PanelBox::Sides& s = themed.set ? themed.v : builtIn;
    return bottom ? s.b : s.t;
}
}  // namespace





float SettingsHud::cardPadTopY() const {
    const ThemeAsset* th = activeTheme();
    const LayoutMetrics& L = layout();
    // UNGATED. A gate on hasThemedCard() fails whenever no theme is selected,
    // which is the default, and makes [content] padding dead here while margin
    // and border work. Only a BORDER needs art to draw it with; padding is
    // spacing and owes a theme nothing.
    const float pad = static_cast<float>(th->boxContentPadding.set
                                             ? th->boxContentPadding.v.t
                                             : L.boxContentPadding.t);
    // THE BOX TERM ALONE: a card's interior pad is [content] padding, here as
    // everywhere else. No private base composed under it -- that is one distance
    // in two spellings, with the private half unreachable from any ini.
    return cardBorderY() + pad * L.cellW * PluginConstants::UI_ASPECT_RATIO * m_fScale;
}

float SettingsHud::cardPadBotY() const {
    const ThemeAsset* th = activeTheme();
    const LayoutMetrics& L = layout();
    const float pad = static_cast<float>(th->boxContentPadding.set   // see cardPadTopY
                                             ? th->boxContentPadding.v.b
                                             : L.boxContentPadding.b);
    return cardBorderY() + pad * L.cellW * PluginConstants::UI_ASPECT_RATIO * m_fScale;
}


// THE BOX TERM ALONE, both sides — the horizontal twin of cardPadTopY/BotY, and
// the same rule: no private lead-in composed under it, or the clearance a themed
// card keeps from its rows becomes a constant that [content] padding can only
// ever widen.
float SettingsHud::cardPadLeftCells() const {
    const ThemeAsset* th = activeTheme();
    return static_cast<float>(th->boxContentPadding.set ? th->boxContentPadding.v.l
                                                        : layout().boxContentPadding.l);
}






#if defined(MXBMRP3_TEST_BUILD)
// DEFINED HERE, not beside the other test seams in settings_hud_input.cpp: the
// registry is declared in the header with an unspecified size and defined in THIS
// file, so it is the only translation unit that can walk it.
// Every SELECTABLE tab, hidden ones included. Defined here rather than inline in
// the header because s_tabRegistry is only complete in this TU.
const char* SettingsHud::testAnyTabNameAt(int i) const {
    if (i < 0) return nullptr;
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId < 0) continue;                 // a section header, not a tab
        if (!isTabAvailable(row.tabId)) continue;
        if (i-- == 0) return getTabName(row.tabId);
    }
    return nullptr;
}

const char* SettingsHud::testTabNameAt(int i) const {
    // The tab LIST's own order and its own availability rule, so a game-gated tab
    // is absent here for the same reason it is absent on screen. Tabs in a closed
    // More group are listed too: they are in the list, one header click away.
    if (i < 0) return nullptr;
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId < 0) continue;                 // a section header, not a tab
        if (row.hidden) continue;                    // not in the list (About)
        if (!isTabAvailable(row.tabId)) continue;
        if (i-- == 0) return getTabName(row.tabId);
    }
    return nullptr;
}
#endif

// THE TALLEST TAB, IN ROWS -- laid out, measured, and cached until something that
// could change it changes.
//
// WHY IT IS MEASURED AT ALL: a tab renderer DRAWS AS IT MEASURES. Each control emits
// its quads and strings and advances a cursor, so there is no cheap "how tall would
// you be" to ask one. A HUD like Records sidesteps this by DECLARING its content size
// to planPanel before drawing; a settings tab cannot, because its height would then
// be a second expression per tab file, sitting beside the drawing code and required
// to agree with it. Twenty-eight of those is twenty-eight chances to disagree.
//
// SO IT DRAWS AND THROWS THE DRAWING AWAY, rather than running the renderers in a
// "measure only" mode. A mode is a flag every add* helper in the layout context has
// to honour, and the first one that forgets reports a short tab and clips it -- which
// is the exact failure this exists to remove. Discarding output needs nothing to be
// honoured by anybody.
//
// AND IT RUNS ONCE PER LAYOUT, not once per rebuild. The panel rebuilds on hover, so
// twenty-eight lay-outs per rebuild would be absurd; the answer only moves when the
// theme, the scale, the fonts or the set of available tabs moves, and all of those
// arrive as a LAYOUT dirty. invalidateTallestTab() is called from rebuildLayout() and
// from show(), so the cost is paid on opening the menu and on changing a theme.
//
// THE STALE WINDOW is live data: a tab whose rows follow the session (Riders lists
// entries) can grow while the menu is open without dirtying the layout, and the
// height then lags until the menu is reopened. The overflow warning in
// rebuildRenderData still fires if that ever bites, which is the honest trade for not
// re-measuring twenty-eight tabs every time a rider joins.
// LAY ONE TAB OUT WITH NO INTENTION OF DRAWING IT, and hand back what the pass
// learned. Two callers want different halves of the same walk -- the tallest-tab
// sweep wants where the cursor ended, the panel's own declaration wants the
// section list -- and running the renderer is the only way to learn either, so
// they share the walk rather than each having one.
//
// Everything the pass emits is scaffolding, cleared before returning: the click
// regions especially, which are hit-tested against the cursor and would otherwise
// carry a provisional tab's controls at a provisional origin.
SettingsHud::TabMeasure SettingsHud::measureTab(int tabId, const ScaledDimensions& dim,
                                                float labelX, float controlX,
                                                float rightColumnX,
                                                float contentAreaStartX,
                                                float contentAreaWidth,
                                                float panelContentRightX) {
    TabMeasure out;
    const TabDescriptor* desc = findTabDescriptor(tabId);
    if (!desc || !desc->render) return out;

    const int savedTab = m_activeTab;
    // Set DIRECTLY, never through setActiveTabByName: that one rebuilds the panel
    // (it is the same event as a tab click), and rebuilding from inside the
    // measurement is unbounded recursion.
    m_activeTab = tabId;
    SettingsLayoutContext ctx(this, dim, labelX, controlX, rightColumnX,
                              contentAreaStartX, contentAreaWidth,
                              panelContentRightX, /*currentY=*/0.0f);
    desc->render(ctx);
    addBackButton(ctx);
    ctx.finishSections();
    out.endY = ctx.currentY;
    out.sections = ctx.measuredSections;
    m_activeTab = savedTab;

    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    m_steppedControls.clear();
    m_cycleControls.clear();
    m_sliders.clear();
    m_dropdown.firstRegion = -1;
    return out;
}

// THE SIDEBAR AS SECTIONS -- one per tab-list section (card), content height each,
// in the order the registry lists them. The engine lays the column out from these
// exactly as it lays the content column out from a tab's; the air BETWEEN sections
// is the same seam it puts between any two sibling cards, which is why none of it
// appears here.
//
// The registry is walked the same way buildTabBar walks it, so a new group or a
// game-gated tab moves both without either being told: a section marker closes the
// running card and opens the next, a visible tab or a non-empty group header is one
// line, and each section's caption pays for itself (see buildTabBar).
//
// ONE ROW PER GROUP, whatever tab is open: a More group never draws its tabs here
// (its page lists them), so the sidebar's height does not change with the tab and
// needs no slack for a tallest state.
std::vector<float> SettingsHud::measureTabGroups(const ScaledDimensions& dim) const {
    std::vector<float> rows;          // per section
    bool groupHasTab = false;         // the group being walked has a visible tab
    int groupRow = -1;                // its TAB_GROUP row; -1 = not in a group
    auto closeGroup = [&]() {
        if (groupRow >= 0 && groupHasTab) rows.back() += 1.0f;   // its More row
        groupRow = -1;
        groupHasTab = false;
    };
    const int registrySize = static_cast<int>(std::size(s_tabRegistry));
    for (int r = 0; r < registrySize; ++r) {
        const TabDescriptor& row = s_tabRegistry[r];
        if (row.tabId >= 0) {
            // Hidden tabs cost the sidebar nothing -- that is the point of them.
            if (row.hidden || !isTabAvailable(row.tabId) || rows.empty()) continue;
            if (groupRow >= 0) groupHasTab = true;
            else rows.back() += 1.0f;
            continue;
        }
        closeGroup();
        if (row.tabId == TAB_GROUP) {
            groupRow = r;
            continue;
        }
        rows.push_back(1.0f);                                    // the section's caption
        if (row.tabId == TAB_SECTION_PROFILE) rows.back() += 1.0f;  // ...and its control row
    }
    closeGroup();

    std::vector<float> out;
    out.reserve(rows.size());
    for (float r : rows) out.push_back(r * dim.lineHeightNormal);
    return out;
}

float SettingsHud::measureTallestBodyH(const ScaledDimensions& dim,
                                       float labelX, float controlX,
                                       float rightColumnX,
                                       float contentAreaStartX,
                                       float contentAreaWidth,
                                       float panelContentRightX,
                                       float sidebarAsk, float contentAsk,
                                       const std::vector<float>& tabGroups) {
    // KEYED, not merely cached: see TallestKey. A drag re-enters here every frame
    // and must find a hit, or the panel re-lays every tab to answer a question
    // whose inputs have not moved.
    const TallestKey key{ dim.lineHeightNormal, dim.fontSize, dim.cellW, mxbThemeGeneration() };
    if (m_tallestContentRows >= 0.0f && key == m_tallestKey) return m_tallestContentRows;

    float tallest = 0.0f;
    int tallestTab = -1;
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId < 0 || !row.render) continue;      // a section header, not a tab
        // NO `hidden` skip here, and the asymmetry is the load-bearing part: a hidden
        // tab draws no sidebar row but its CONTENT still has to fit the panel, which
        // does not resize when you open it. Skipping it here would let About overflow
        // the one panel a player cannot scroll.
        if (!isTabAvailable(row.tabId)) continue;
        m_measuringTallest = true;
        const TabMeasure m = measureTab(row.tabId, dim, labelX, controlX, rightColumnX,
                                        contentAreaStartX, contentAreaWidth,
                                        panelContentRightX);
        m_measuringTallest = false;

        // The SAME want the panel below declares, with this tab's sections and no
        // floor -- so what comes back is the height this tab would really occupy,
        // card chrome and seams included, in the engine's own arithmetic.
        PanelWant w;
        w.tier = TitleTier::Large;
        w.captionW = planTitleWidth(dim, "MXBMRP3 SETTINGS", TitleTier::Large);
        PanelWant::BandWant band;
        band.columns.push_back({ sidebarAsk, tabGroups });
        band.columns.push_back({ contentAsk, m.sections });
        w.bands.push_back(std::move(band));
        w.buttons = 3;
        w.buttonW = PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize);
        w.buttonH = dim.lineHeightNormal;

        const float bodyH = planBodyHeight(dim, w);
        if (bodyH > tallest) {
            tallest = bodyH;
            tallestTab = row.tabId;
        }
    }

    // One line per measure (a measure is per key, not per frame): the number a tab
    // must stay under is whichever tab binds, and nothing else reports it --
    // theme_geometry_test says only that the panel overflows.
    DEBUG_INFO_F("Settings panel measures %.2f body rows (tallest: tab %d)",
                 tallest / dim.lineHeightNormal, tallestTab);

    m_tallestContentRows = tallest;
    m_tallestKey = key;
    return tallest;
}

bool SettingsHud::isTabAvailable(int tabId) const {
    if (tabId < 0 || tabId >= TAB_COUNT) return false;
    const TabDescriptor* tabDesc = findTabDescriptor(tabId);
    if (!tabDesc) return false;
    // Game-gated tabs (Records/FMX/Friends): selectable only when their backing HUD is
    // registered on this build. The tab-list render loop and the persisted-tab restore
    // both route through here, so they can't drift.
    // `hud` names the tab's HUD where the sidebar draws a checkbox for it,
    // `previewHud` where it does not (Achievements owns the toast widget).
    auto* backing = tabDesc->hud ? tabDesc->hud : tabDesc->previewHud;
    if (tabDesc->gameGated && !(backing && backing(*this))) return false;
    return true;
}

void SettingsHud::recordTabOpened(int tabId) const {
    if (!isTabListed(tabId)) return;   // About is available but not in the list
    const char* listed[TAB_COUNT];
    int count = 0;
    for (int t = 0; t < TAB_COUNT; ++t) {
        if (isTabListed(t)) listed[count++] = getTabName(t);
    }
    StatsManager::getInstance().exploration().onTabOpened(getTabName(tabId), listed, count);
}

BaseHud* SettingsHud::activeTabHud() const {
    const TabDescriptor* tabDesc = findTabDescriptor(m_activeTab);
    if (!tabDesc) return nullptr;
    // previewHud first: it exists for the two tabs whose HUD is not the one the
    // sidebar checkbox names. See TabDescriptor.
    if (tabDesc->previewHud) return tabDesc->previewHud(*this);
    return tabDesc->hud ? tabDesc->hud(*this) : nullptr;
}

void SettingsHud::rebuildRenderData() {
    if (!m_bVisible) return;  // vis-gate: menu is active-surface-only (see show())

    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    m_steppedControls.clear();  // rebuilt in lockstep with the click regions
    m_cycleControls.clear();    // rebuilt in lockstep with the click regions
    m_sliders.clear();          // ...and so are these
    m_dropdown.anchored = false;   // set again by the open dropdown's box, if drawn
    m_dropdown.firstRegion = -1;   // ...and its entries, by buildDropdownPopup

    // The Gate::Unlocked markers' state, refreshed HERE because this is the one
    // place that runs before both readers -- the sidebar's "New" tag and the
    // tab body's row band -- and because whats_new.cpp deliberately knows about
    // no manager it could ask itself.
    // ...and never on a game without achievements: the widget is not created
    // there, so developer mode - the second key on that lock - would otherwise
    // tag the Widgets tab for a row it has no way to show.
    WhatsNew::setUnlocked(GAME_HAS_ACHIEVEMENTS && PrestigeWidget::isUnlocked());

    // Update cached window size (use actual pixel dimensions)
    const InputManager& input = InputManager::getInstance();
    m_cachedWindowWidth = input.getWindowWidth();
    m_cachedWindowHeight = input.getWindowHeight();

    auto dim = getScaledDimensions();

    constexpr float sectionSpacing = 0.0150f;

    // DECLARE (the asks and the plan -- see planSettingsPanel), THEN DRAW.
    float sidebarAsk = 0.0f, contentAsk = 0.0f, labelToControl = 0.0f, labelToRight = 0.0f;
    PanelPlan& plan = planSettingsPanel(dim, sidebarAsk, contentAsk, labelToControl, labelToRight);
    const PanelBox::ColumnGeom& sideCol = plan.col(0, 0);
    const PanelBox::ColumnGeom& mainCol = plan.col(0, 1);

    // CENTRED ON THE CONTENT, which is this panel's one layout privilege: it
    // cannot be dragged, so it places itself -- and what it centres is the
    // character lattice (sidebar + content asks), NOT the panel box. Centring
    // the box splits every theme-dependent term in half and pushes that half
    // into the content, which walks the row controls sideways as themes are
    // cycled (theme_geometry_test contract 1). The theme's air and borders hang
    // off the anchored content, so only the panel's outer edges may move.
    // startX is then derived: where the panel's left edge must be for the
    // content column's rows to land on the anchor. It stays on the lattice
    // because every term in between is whole cells (see the sidebar ask).
    const float panelWidth = plan.width();
    const float backgroundHeight = plan.height();
    const float contentAnchorX =
        snapEdgeX(0.5f + (sidebarAsk - contentAsk) / 2.0f);
    const float startX = contentAnchorX - plan.W(mainCol.rowsLeft);
    const float startY = snapEdgeY((1.0f - backgroundHeight) / 2.0f);

    // The frame, the caption's band and one card per section of BOTH columns --
    // except the Back row's (addBackButton): its section keeps its box, so the
    // button sits where it always did, but on the panel's own background.
    const PanelBox::SectionGeom* backSection =
        (groupRowOf(m_activeTab) >= 0 && !mainCol.sections.empty())
            ? &mainCol.sections.back() : nullptr;
    addPlanBackground(plan, startX, startY, backSection);
    // UNTHEMED SECTIONS. A theme paints a card per section; without one the
    // sections were only air, and with rows now side by side the panel read as
    // one block of text. A faint tint of the primary colour per section box (both
    // columns), reaching past the rows and a little into each seam, groups them
    // with no art -- one solid quad per section, behind every row. Sideways it
    // takes at most a quarter of the gutter from each side, so the sidebar and
    // the content keep a gap between them, as a theme's cards do.
    if (!plan.g.hasCard) {
        const unsigned long tint = PluginUtils::applyOpacity(
            ColorConfig::getInstance().getPrimary(), 0.06f);
        const float gutter = plan.X(mainCol.cardLeft) - plan.X(sideCol.cardLeft + sideCol.cardW);
        const float padX = std::max(0.0f, std::min(
            PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize) * 0.5f, gutter * 0.25f));
        const float padY = dim.lineHeightNormal * 0.15f;
        for (const PanelBox::BandGeom& band : plan.g.bands) {
            for (const PanelBox::ColumnGeom& col : band.columns) {
                for (const PanelBox::SectionGeom& sec : col.sections) {
                    if (&sec == backSection) continue;
                    float x = plan.X(col.cardLeft) - padX;
                    float y = plan.Y(sec.top) - padY;
                    const float w = plan.W(col.cardW) + padX * 2.0f;
                    const float h = plan.H(sec.bot - sec.top) + padY * 2.0f;
                    if (w <= 0.0f || h <= 0.0f) continue;
                    SPluginQuad_t q;
                    applyOffset(x, y);
                    setQuadPositions(q, x, y, w, h);
                    q.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;  // solid-quad-exempt: unthemed section tint, drawn only when no theme card exists
                    q.m_ulColor = tint;
                    m_quads.push_back(q);
                }
            }
        }
    }
    setBounds(startX, startY, startX + panelWidth, startY + backgroundHeight);
    addPlanTitle(plan, "MXBMRP3 SETTINGS",
                 ColorConfig::getInstance().getPrimary());

    // ---- the columns, in the engine's coordinates ------------------------
    const float tabStartX = plan.colContentX(sideCol);
    const float tabWidth = sidebarAsk;
    const float contentAreaStartX = plan.colContentX(mainCol);
    const float leftColumnX = contentAreaStartX;
    const float rightColumnX = contentAreaStartX + labelToRight;
    const float controlX = leftColumnX + labelToControl;
    const float contentAreaWidth = plan.colContentW(mainCol);
    // A row ends where its own column ends: the card is the column's own box, so
    // there is no inset for the row to give back.
    const float panelContentRightX = contentAreaStartX + contentAreaWidth;
    float currentY = plan.colContentY(mainCol, 0);
    float checkboxWidth = PluginUtils::calculateMonospaceTextWidth(4, dim.fontSize);  // "[X] " or "    "

    // The sidebar draws into its OWN column's sections -- one per tab-list group,
    // at the origins the engine placed them.
    buildTabBar(dim, plan, sideCol, tabStartX, tabWidth, checkboxWidth);

    SettingsLayoutContext layoutCtx(this, dim, leftColumnX, controlX, rightColumnX,
                                     contentAreaStartX, contentAreaWidth,
                                     panelContentRightX, currentY);
    // WHERE THE ENGINE PUT THIS TAB'S SECTIONS. Handing them over is what turns the
    // draw from "lay them out again and hope it matches the measure" into "put them
    // where they were planned" -- the two passes cannot disagree about a seam
    // neither of them spends.
    for (const PanelBox::SectionGeom& sec : mainCol.sections)
        layoutCtx.planSectionY.push_back(plan.Y(sec.rowsTop));
    layoutCtx.planCardLeftX = plan.X(mainCol.cardLeft);

#if defined(MXBMRP3_TEST_BUILD)
    recordTestAnchors(plan, sideCol, mainCol, leftColumnX, controlX, layoutCtx);
#endif

    // The tab body, marked for Motion (core/motion.h): on a tab switch it fades in
    // over the panel, sidebar and footer, which stay put.
    m_motionPartQuadFirst = static_cast<int>(m_quads.size());
    m_motionPartStringFirst = static_cast<int>(m_strings.size());
    setMotionPartKey(m_activeTab);

    currentY = renderActiveTab(layoutCtx, plan, mainCol, dim, currentY);

    currentY += sectionSpacing;

    // Draw hover highlight for TOOLTIP_ROW regions
    addHoveredRowHighlight(plan, mainCol);

    // Render description or tooltip at the reserved position (replaces each other).
    renderTooltipText(layoutCtx, dim);
    m_motionPartQuadEnd = static_cast<int>(m_quads.size());
    m_motionPartStringEnd = static_cast<int>(m_strings.size());

    // Bottom button row: [Reset <Tab>] ... [Save/Saved] [Close] ... [About]
    // (settings_hud_footer.cpp).
    buildFooterButtons(dim, plan, sideCol, mainCol, startX, panelWidth);

    // This panel rebuilds DIRECTLY from its ~30 interaction sites rather than
    // through processDirtyFlags, which is where every other HUD's fill gets cut
    // -- so without this the sweep never runs here, the centre slice keeps covering
    // the whole interior, and every card (and the title band) sits on it at double
    // opacity, reading darker than the panel. Consumes m_fillFirst, so the
    // dirty-flag path finalizing again is a no-op, not a double cut.
    finalizeThemedFill();

    // The open dropdown list, over everything (settings_controls.cpp).
    buildDropdownPopup(startX, startY, startX + panelWidth, startY + backgroundHeight);
}

// The panel's plan: both columns' asks, the active tab's measured sections and
// the tallest tab's body as the floor, plus the footer's three buttons.
PanelPlan& SettingsHud::planSettingsPanel(const ScaledDimensions& dim, float& sidebarAsk, float& contentAsk,
                                          float& labelToControl, float& labelToRight) {
    // The panel's columns, COMPOSED: sidebar ask + trough (the seam read,
    // sectionGap + gap — see troughCells) + content ask, in characters —
    // fractional once the terms enter, so the width is built from one
    // character's width rather than the int-only helper.
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    // ======================================================================
    // DECLARE, THEN DRAW -- the two steps every HUD and widget takes.
    //
    // This panel keeps no X and Y chains of its own -- no overhang stack, caption
    // block, per-card pads, tallest-tab row count or hand-built button row. Each
    // would be a second spelling of something PanelBox already computes for every
    // other panel in the tree, and every divergence between such a spelling and
    // the engine's is a geometry fault: [content] margin acting on one axis,
    // [content] border acting on none, a caption block twice the height of a
    // HUD's, a band that reserves a border it does not draw. None of them is
    // expressible here: there is one owner, and this panel asks it.
    //
    // THE ONLY THINGS STATED HERE ARE ASKS -- how wide each column's content is
    // in characters, how tall each section is in rows, how many buttons. Air,
    // borders, seams, the ceil and the panel's own size are the engine's.
    // ======================================================================

    // The two columns' content widths. Character counts, so neither depends on
    // the theme -- which is what lets the measure below run before the plan and
    // keeps switching themes from moving any text.
    //
    // The sidebar's ask is rounded UP TO A WHOLE CELL: every other term between
    // the panel's left edge and the content column's rows (frame, paddings,
    // borders, the gap) is whole cells, so with this one quantized too the
    // content-anchored startX below still lands the panel's edge on the
    // lattice. The rounding is at most one cell of air on the sidebar's right.
    sidebarAsk = std::ceil(
        charW * static_cast<float>(layout().settingsSidebarWidth) / dim.cellW - 1e-4f)
        * dim.cellW;
    contentAsk = charW * static_cast<float>(layout().settingsContentAreaChars());

    // MEASURED AT THE ORIGIN, with the same RELATIVE columns the draw will use.
    // A section's height depends on its rows and on how far prose wraps, and
    // wrapping is a character count -- never on where the column sits. Measuring
    // at 0 says so, and needs no provisional-origin pass.
    labelToControl = PluginUtils::calculateMonospaceTextWidth(SETTINGS_CONTROL_COLUMN, dim.fontSize);
    labelToRight = PluginUtils::calculateMonospaceTextWidth(
        layout().settingsControlColumn - layout().settingsLabelColumn, dim.fontSize);

    const TabMeasure active = measureTab(m_activeTab, dim, /*labelX=*/0.0f,
                                         /*controlX=*/labelToControl,
                                         /*rightColumnX=*/labelToRight,
                                         /*contentAreaStartX=*/0.0f,
                                         /*contentAreaWidth=*/contentAsk,
                                         /*panelContentRightX=*/contentAsk);

    PanelWant want;
    want.tier = TitleTier::Large;
    want.captionW = planTitleWidth(dim, "MXBMRP3 SETTINGS", TitleTier::Large);
    PanelWant::BandWant band;
    band.columns.push_back({ sidebarAsk, measureTabGroups(dim) });
    band.columns.push_back({ contentAsk, active.sections });
    want.bands.push_back(std::move(band));
    // THE PANEL'S HEIGHT IS THE TALLEST TAB'S, not this one's -- so Save and
    // Close hold still while you flick tabs. Stated as a floor under the body
    // (Spec::minBodyH) rather than by padding the last section, which would size
    // the panel right and draw one visibly stretched card.
    //
    // A HEIGHT, in the engine's own arithmetic, not a row count: the body a column
    // produces is sum(sections) plus per-section card chrome plus a seam between each
    // pair, and only the first of those three scales with a cursor. Measuring the
    // flow instead lets a tab with more sections outgrow the floor, and the panel
    // then changes height between tabs.
    want.minBodyH = measureTallestBodyH(
        dim, /*labelX=*/0.0f, labelToControl, labelToRight,
        /*contentAreaStartX=*/0.0f, contentAsk, contentAsk,
        sidebarAsk, contentAsk, measureTabGroups(dim));
    // Save, Close and the per-tab Reset. Five characters is the widest label
    // ("Saved" / "Close"); the row's height is one ordinary row.
    want.buttons = 3;
    want.buttonW = PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize);
    want.buttonH = dim.lineHeightNormal;

    PanelPlan& plan = planPanel(dim, want);
    return plan;
}

void SettingsHud::renderTooltipText(const SettingsLayoutContext& layoutCtx, const ScaledDimensions& dim) {
    // The description/tooltip box spans from the label column to the content edge — a whole number of
    // character cells, so take it from SettingsMetrics rather than dividing the
    // emitted float span by one character's width. That round-trip returns one
    // char FEWER at HUD scale 0.70 (float rounding), silently narrowing the box at
    // one scale only; the integer form is exact at every scale, and is the same
    // value tests/unit/test_tooltip_length.cpp measures shipped tooltips against.
    const int maxCharsPerLine = layout().settingsTooltipCharsPerLine(hasThemedCard());

    // Helper lambda to render up to 2 lines of word-wrapped text. The wrapping
    // itself is TextWrap::wrap (settings/text_wrap.h) — pure and unit-tested, and
    // the same function tests/unit/test_tooltip_length.cpp runs every shipped
    // tooltip through to prove none of them render cut off.
    auto renderWrappedText = [&](const std::string& text, unsigned long color) {
        float lineY = layoutCtx.tooltipY;
        for (const std::string& line : TextWrap::wrap(text, maxCharsPerLine,
                                                      TextWrap::TOOLTIP_LINES).lines) {
            addString(line.c_str(), layoutCtx.labelX, lineY, Justify::LEFT,
                Fonts::getNormal(), color, dim.fontSize);
            lineY += dim.lineHeightNormal;
        }
    };

    if (!m_hoveredTooltipId.empty()) {
        // Check if hovering a TAB region - show tab description instead of control tooltip
        bool isTabHover = (m_hoveredRegionIndex >= 0 &&
                          m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size()) &&
                          m_clickRegions[m_hoveredRegionIndex].type == ClickRegion::TAB);

        if (isTabHover) {
            // Show tab tooltip for hovered tab
            const char* tabTooltip = TooltipManager::getInstance().getTabTooltip(m_hoveredTooltipId.c_str());
            if (tabTooltip && tabTooltip[0] != '\0') {
                renderWrappedText(std::string(tabTooltip), ColorConfig::getInstance().getMuted());
            }
        } else {
            // Show control tooltip; a row named after a TAB (the More page's) shows
            // that tab's description.
            const char* tooltipText = TooltipManager::getInstance().getControlTooltip(m_hoveredTooltipId.c_str());
            if (!tooltipText || tooltipText[0] == '\0')
                tooltipText = TooltipManager::getInstance().getTabTooltip(m_hoveredTooltipId.c_str());
            if (tooltipText && tooltipText[0] != '\0') {
                renderWrappedText(std::string(tooltipText), ColorConfig::getInstance().getMuted());
            }
        }
    } else if (!layoutCtx.currentTabId.empty()) {
        // Show tab tooltip (when not hovering)
        const char* tabTooltip = TooltipManager::getInstance().getTabTooltip(layoutCtx.currentTabId.c_str());
        if (tabTooltip && tabTooltip[0] != '\0') {
            renderWrappedText(std::string(tabTooltip), ColorConfig::getInstance().getMuted());
        }
    }
}

// The active tab's own rows (via the registry), then the what's-new bands and the
// overflow check over what it drew. Returns the cursor after the tab.
float SettingsHud::renderActiveTab(SettingsLayoutContext& layoutCtx, const PanelPlan& plan,
                                   const PanelBox::ColumnGeom& mainCol, const ScaledDimensions& dim,
                                   float currentY) {
    if (const TabDescriptor* tabDesc = findTabDescriptor(m_activeTab); tabDesc && tabDesc->render) {
#if defined(MXBMRP3_TEST_BUILD)
        // Where the TAB'S OWN controls start. Everything emitted before this point is
        // the sidebar - the tab list carries a checkbox per row, so the master toggle
        // of every other tab is a click region on this one, and a sweep that treated
        // them as this tab's controls would demand its Reset restore them.
        // See SettingsHud::testPerturbActiveTab.
        m_testContentRegionBegin = static_cast<int>(m_clickRegions.size());
#endif
        // Route to the extracted per-tab renderer (settings_tab_*.cpp) via the registry.
        layoutCtx.currentY = currentY;   // Sync context cursor
        tabDesc->render(layoutCtx);
        addBackButton(layoutCtx);
        layoutCtx.finishSections();
        currentY = layoutCtx.currentY;   // Sync local cursor back

        addWhatsNewRowBands(plan, mainCol);
        checkTabOverflow(plan, mainCol, dim, currentY);
    } else {
        DEBUG_WARN_F("Invalid tab index: %d, defaulting to TAB_STANDINGS", m_activeTab);
    }
    return currentY;
}

#if defined(MXBMRP3_TEST_BUILD)
void SettingsHud::recordTestAnchors(const PanelPlan& plan, const PanelBox::ColumnGeom& sideCol,
                                    const PanelBox::ColumnGeom& mainCol, float leftColumnX, float controlX,
                                    const SettingsLayoutContext& layoutCtx) {
    // The two column edges the symmetry test reads; see SettingsHud::testColumnEdgesX.
    // Themed these are the two columns' CARD edges (a card overhangs its column by one
    // inner border at each end); unthemed the border is 0 and they are the tab
    // highlight's left and the row highlight's right.
    // The CARD edges, straight off the engine -- the outer edge of the sidebar's
    // card and of the content column's, which is what the symmetry test compares.
    m_testColumnLeftX  = plan.X(sideCol.cardLeft);
    m_testColumnRightX = plan.X(mainCol.cardLeft + mainCol.cardW);

    // The three anchors the theme-invariance test reads; see testContentColumnX().
    // The row's RIGHT EDGE, not the panel's inner edge: right-aligned glyphs are
    // placed against the row, and it is the row that has to stand still.
    m_testLabelX    = leftColumnX;
    m_testControlX  = controlX;
    m_testRowRightX = leftColumnX + layoutCtx.rowSpanWidth();

    // The two card edges bounding the GUTTER, straight off the engine's boxes --
    // what testCardEdgesX() reports for the gutter==seam contract. The content
    // side is re-stamped per section by closeSectionCard from the same plan.
    m_testSidebarCardRightX = plan.X(sideCol.cardLeft + sideCol.cardW);
}
#endif
