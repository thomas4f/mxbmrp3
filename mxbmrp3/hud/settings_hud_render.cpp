// ============================================================================
// hud/settings_hud_render.cpp
// SettingsHud::rebuildRenderData() — the settings menu's full render build: it
// lays out every tab's controls, labels, click regions and tooltips into the
// HUD's quad/string vectors. Extracted verbatim from settings_hud.cpp (it was
// ~890 lines, the bulk of that file) when the file grew past ~1.8k lines; the
// SettingsHud class, members, and public API are unchanged — only where this
// one method body lives moves. Companion to settings_hud_input.cpp.
//
// The tab bar and its two icon helpers are member functions below, not lambdas.
// They were lambdas until the "8+ parameters" claim behind that choice was
// actually measured (it is 2, and 5 for the bar); the per-tab CONTROL lambdas
// this file used to hold are gone entirely, replaced by SettingsLayoutContext.
// ============================================================================
#include <cmath>

#include "settings_hud.h"
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
#include "../core/profile_manager.h"
#include "../core/update_checker.h"
#include "../core/update_downloader.h"
#include "../core/director_manager.h"
#include "../core/spotter_manager.h"
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
    { TAB_SPOTTER,      "Spotter",    "spotter",       nullptr,                                                              false, &SettingsHud::renderTabSpotter,         &SettingsHud::handleClickTabSpotter,      nullptr,            &SettingsHud::resetTabSpotter,          nullptr, "Beta" },
    { TAB_UPDATES,      "Updates",    "updates",       nullptr,                                                              false, &SettingsHud::renderTabUpdates,         &SettingsHud::handleClickTabUpdates,      nullptr,            &SettingsHud::resetTabUpdates,          nullptr, nullptr },
    // HIDDEN (the trailing true): reached from the footer's About button, never
    // drawn in the sidebar. Its POSITION still matters even so -- it is in the GLOBAL
    // group because tools/check_docs.py splits this registry at TAB_SECTION_PROFILE
    // to decide which README table a tab belongs in, and About is a global page, not
    // a HUD. Placed last in the group for the same reason it would be if it were
    // listed.
    { TAB_ABOUT,        "About",      "about",         nullptr,                                                              false, &SettingsHud::renderTabAbout,           nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr, true },
    { TAB_SECTION_PROFILE,  nullptr,  nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr, nullptr },
    { TAB_STANDINGS,    "Standings",  "standings",     [](const SettingsHud& s) -> BaseHud* { return s.m_standings; },       false, &SettingsHud::renderTabStandings,       &SettingsHud::handleClickTabStandings,    "StandingsHud",     &SettingsHud::resetTabStandingsExtra,   nullptr, nullptr },
    { TAB_MAP,          "Map",        "map",           [](const SettingsHud& s) -> BaseHud* { return s.m_mapHud; },          false, &SettingsHud::renderTabMap,             &SettingsHud::handleClickTabMap,          "MapHud",           nullptr,                                nullptr, nullptr },
    { TAB_RADAR,        "Radar",      "radar",         [](const SettingsHud& s) -> BaseHud* { return s.m_radarHud; },        false, &SettingsHud::renderTabRadar,           &SettingsHud::handleClickTabRadar,        "RadarHud",         nullptr,                                nullptr, nullptr },
    { TAB_LAP_LOG,      "Lap Log",    "lap_log",       [](const SettingsHud& s) -> BaseHud* { return s.m_lapLog; },          false, &SettingsHud::renderTabLapLog,          &SettingsHud::handleClickTabLapLog,       "LapLogHud",        nullptr,                                nullptr, nullptr },
    { TAB_IDEAL_LAP,    "Ideal Lap",  "ideal_lap",     [](const SettingsHud& s) -> BaseHud* { return s.m_idealLap; },        false, &SettingsHud::renderTabIdealLap,        nullptr,                                  "IdealLapHud",      nullptr,                                nullptr, nullptr },
    { TAB_SESSION_CHARTS, "Charts",   "session_charts",[](const SettingsHud& s) -> BaseHud* { return s.m_sessionCharts; },   false, &SettingsHud::renderTabSessionCharts,   nullptr,                                  "SessionChartsHud", nullptr,                                nullptr, nullptr },
    { TAB_TELEMETRY,    "Telemetry",  "telemetry",     [](const SettingsHud& s) -> BaseHud* { return s.m_telemetry; },       false, &SettingsHud::renderTabTelemetry,       nullptr,                                  "TelemetryHud",     nullptr,                                nullptr, nullptr },
    { TAB_RECORDS,      "Records",    "records",       [](const SettingsHud& s) -> BaseHud* { return s.m_records; },         true,  &SettingsHud::renderTabRecords,         &SettingsHud::handleClickTabRecords,      "RecordsHud",       &SettingsHud::resetTabRecordsExtra,     nullptr, nullptr },
    { TAB_PITBOARD,     "Pitboard",   "pitboard",      [](const SettingsHud& s) -> BaseHud* { return s.m_pitboard; },        false, &SettingsHud::renderTabPitboard,        nullptr,                                  "PitboardHud",      nullptr,                                nullptr, nullptr },
    { TAB_SESSION,      "Session",    "session",       [](const SettingsHud& s) -> BaseHud* { return s.m_session; },         false, &SettingsHud::renderTabSession,         &SettingsHud::handleClickTabSession,      "SessionHud",       nullptr,                                nullptr, nullptr },
    { TAB_TIMING,       "Timing",     "timing",        [](const SettingsHud& s) -> BaseHud* { return s.m_timing; },          false, &SettingsHud::renderTabTiming,          &SettingsHud::handleClickTabTiming,       "TimingHud",        nullptr,                                nullptr, nullptr },
    { TAB_GAP_BAR,      "Gap Bar",    "gap_bar",       [](const SettingsHud& s) -> BaseHud* { return s.m_gapBar; },          false, &SettingsHud::renderTabGapBar,          &SettingsHud::handleClickTabGapBar,       "GapBarHud",        nullptr,                                nullptr, nullptr },
    { TAB_NOTICES,      "Notices",    "notices",       [](const SettingsHud& s) -> BaseHud* { return s.m_notices; },         false, &SettingsHud::renderTabNotices,         nullptr,                                  "NoticesHud",       nullptr,                                nullptr, nullptr },
    { TAB_EVENT_LOG,    "Event Log",  "event_log",     [](const SettingsHud& s) -> BaseHud* { return s.m_eventLog; },        false, &SettingsHud::renderTabEventLog,        &SettingsHud::handleClickTabEventLog,     "EventLogHud",      nullptr,                                nullptr, nullptr },
    { TAB_FRIENDS,      "Friends",    "friends",       [](const SettingsHud& s) -> BaseHud* { return s.m_friends; },         true,  &SettingsHud::renderTabFriends,         &SettingsHud::handleClickTabFriends,      "FriendsHud",       nullptr,                                nullptr, nullptr },
    { TAB_FMX,          "FMX",        "fmx",           [](const SettingsHud& s) -> BaseHud* { return s.m_fmxHud; },          true,  &SettingsHud::renderTabFmx,             &SettingsHud::handleClickTabFmx,          "FmxHud",           nullptr,                                nullptr, nullptr },
    { TAB_STATS,        "Stats",      "stats",         [](const SettingsHud& s) -> BaseHud* { return s.m_statsHud; },        false, &SettingsHud::renderTabStats,           &SettingsHud::handleClickTabStats,        "StatsHud",         nullptr,                                nullptr, nullptr },
    { TAB_PERFORMANCE,  "Performance","performance",   [](const SettingsHud& s) -> BaseHud* { return s.m_performance; },     false, &SettingsHud::renderTabPerformance,     &SettingsHud::handleClickTabPerformance,  "PerformanceHud",   nullptr,                                nullptr, nullptr },
    { TAB_WIDGETS,      "Widgets",    "widgets",       nullptr,                                                              false, &SettingsHud::renderTabWidgets,         nullptr,                                  nullptr,            &SettingsHud::resetTabWidgets,          nullptr, nullptr },
};

const SettingsHud::TabDescriptor* SettingsHud::findTabDescriptor(int tabId) {
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId == tabId) return &row;
    }
    return nullptr;
}

// ==========================================================================
// Tab-bar drawing helpers, split out of rebuildRenderData().
//
// These were lambdas, held that way by a rule (CLAUDE.md "Design Decisions", plus
// a NOTE in this file) claiming a conversion would need "8+ parameters". Measured
// rather than assumed, each needs TWO beyond its original arguments: the scaled
// dimensions and the checkbox cell width. Everything else they touch -- addIcon,
// addString, m_clickRegions, m_hoveredRegionIndex -- is a member, which a member
// function gets for free and a lambda had to capture by reference.
// ==========================================================================

// Shared dim level for "inactive" tab icons (disabled toggles + non-toggle section
// tabs) so they read as equally subdued; enabled toggles stay at full opacity.
constexpr float INACTIVE_ICON_OPACITY = 0.5f;


// Draws an identity icon in a tab's checkbox cell at the given colour. Returns false
// if no icon is assigned/available (caller can fall back to text). Icons render a bit
// smaller than the row font (they fill their glyph box more than text fills the em) and
// nudged up ~2px (at 1080p, scaled) so they sit optically centred on the row.
bool SettingsHud::drawTabIcon(float x, float y, const char* iconName, unsigned long color,
                              const ScaledDimensions& dim, float checkboxWidth) {

    // Same global switch that drives the title-bar icons gates the tab icons.
    int spriteIndex = (UiConfig::getInstance().getTitleIcons() && iconName && iconName[0])
        ? AssetManager::getInstance().getIconSpriteIndex(iconName) : 0;
    if (spriteIndex <= 0) return false;
    constexpr float TAB_ICON_SCALE = 0.63f;
    float cellW = checkboxWidth * 0.25f;
    float iconCenterY = y + dim.lineHeightNormal * 0.5f - (2.0f / 1080.0f) * dim.scale;
    addIcon(x + cellW * 1.5f, iconCenterY, spriteIndex, color, dim.fontSize * TAB_ICON_SCALE);
    return true;
}

// Draws a tab's enable/disable toggle in semantic colours: POSITIVE when enabled,
// NEGATIVE when disabled (a disabled icon lightens 10% on hover as an affordance).
// Falls back to the legacy "[x]"/"[ ]" text when no icon is available.
// Call right after pushing the tab's toggle ClickRegion so the hover check targets it.
void SettingsHud::drawTabToggle(float x, float y, const char* iconName, bool enabled,
                                bool onBand, const ScaledDimensions& dim, float checkboxWidth) {

    ColorConfig& cc = ColorConfig::getInstance();
    // Full-opacity semantic base: POSITIVE (enabled) / NEGATIVE (disabled).
    unsigned long base = enabled ? cc.getPositive() : cc.getNegative();
    bool hovered = (m_hoveredRegionIndex >= 0 &&
                    m_hoveredRegionIndex == static_cast<int>(m_clickRegions.size()) - 1);
    unsigned long iconColor;
    if (hovered) {
        // Clear affordance in BOTH states: full opacity + a strong lighten, so a
        // disabled icon jumps from dimmed to bright and an enabled one brightens.
        // (lightenColor keeps alpha, so build from the full-opacity base.)
        iconColor = PluginUtils::lightenColor(base, 0.25f);
    } else if (onBand) {
        // THE SELECTED ROW. Its background is the accent band, and the two things
        // that make an icon readable on the panel work against it there: a disabled
        // icon is dimmed to half, and the whole palette is warm, so a dimmed red on
        // amber disappeared -- reported as the selected tab having no icon at all.
        //
        // Full opacity plus a lift, never the dimmed variant. The HUE still carries
        // the state (green on, red off), which is why this is not simply switched to
        // PRIMARY the way the label beside it is: the label has no state to lose.
        iconColor = PluginUtils::lightenColor(base, 0.35f);
    } else {
        // Enabled pops at full; disabled is dimmed to the muted section level so it
        // doesn't scream.
        iconColor = enabled ? base : PluginUtils::applyOpacity(base, INACTIVE_ICON_OPACITY);
    }
    if (!drawTabIcon(x, y, iconName, iconColor, dim, checkboxWidth)) {
        // Text fallback (no icon assigned, or asset missing on this build)
        addString(enabled ? "[x]" : "[ ]", x, y, Justify::LEFT,
            Fonts::getNormal(), iconColor, dim.fontSize);
    }

}

// ==========================================================================
// The vertical tab bar. Split out for the same reason as the helpers above, and
// measured the same way: the loop reads exactly FIVE values from the enclosing
// scope -- the two tab-bar origins, the tab and checkbox widths, and the scaled
// dimensions. tabStartY is advanced locally and deliberately NOT returned: the
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
    // two sibling cards, and it is the engine's to spend. The cards themselves were
    // drawn by addPlanBackground before any of this ran, so the open/close pair
    // this function used to carry is gone with them.
    size_t group = 0;
    float tabStartY = plan.colContentY(col, 0);
    // Visual tab order comes straight from the descriptor registry (rows are in
    // display order; negative TAB_SECTION_* rows are the section headers).
    for (const TabDescriptor& tabRow : s_tabRegistry) {
        const int i = tabRow.tabId;

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
            // "Global" is captioned again, and the row it costs was paid for by
            // MERGING "Elements" into "Profile" -- exactly one caption row each way,
            // so the sidebar is no taller than when this group was anonymous. (It had
            // lost its label when the Spotter tab landed, because the sidebar is the
            // panel's binding height under theme_geometry_test's fits-the-screen
            // contract and this was the cheapest row to give up.)
            //
            // The merge is also the truer grouping: the element tabs ARE per-profile,
            // so listing them under the profile cycler says what they are. Two groups
            // now, not three -- which costs one card and one seam less as well.
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

            // Right arrow " >" with click region (cycles to next profile)
            addString(" >", currentX, tabStartY, Justify::LEFT,
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

        // Determine if this tab's HUD/widgets are enabled. Per-HUD checkboxes show
        // the focused surface's on/off (companion vs game); the manager/global toggles
        // (widgets/rumble/updates/director) are shared, not decoupled.
        //
        // The helmet reads its GAME flag even on the companion, and that is correct
        // rather than an oversight: it never renders on the companion at all
        // (BaseHud::rendersOnCompanion), so the game flag is its only visibility.
        bool isHudEnabled;
        if (tabHud) {
            isHudEnabled = tabHud->isVisibleOnActiveSurface();
        } else if (i == TAB_WIDGETS) {
            isHudEnabled = HudManager::getInstance().areWidgetsEnabled();
        } else if (i == TAB_RUMBLE) {
            isHudEnabled = XInputReader::getInstance().getGlobalRumbleConfig().enabled;
        } else if (i == TAB_HELMET) {
            isHudEnabled = m_helmetOverlay && m_helmetOverlay->isVisible();
        } else if (i == TAB_UPDATES) {
            isHudEnabled = UpdateChecker::getInstance().isEnabled();
        } else if (i == TAB_DIRECTOR) {
            isHudEnabled = DirectorManager::getInstance().isEnabled();
        } else if (i == TAB_SPOTTER) {
            // The SPOKEN-AUDIO master, matching the tab's own first toggle.
            // Subtitles are deliberately not part of this reading: they are a
            // standalone mode (silent, captioned), so a lit checkbox here
            // means "you will hear it".
            isHudEnabled = SpotterManager::getInstance().isEnabled();
        } else {
            isHudEnabled = true;  // General is always "enabled"
        }

        // Tab color: PRIMARY if active, ACCENT if inactive
        unsigned long tabColor = isActive ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getAccent();

        float currentTabX = tabStartX;

        // THE ROW'S BAND, EMITTED FIRST. Quads draw in push order, and this used to be
        // pushed after the row's identity icon -- so the selected tab's band painted
        // over its own icon and the row looked like it had none. (The label survived
        // only because it is pushed later still.) Reported from the game as the
        // selected tab having no icon; it reads as a colour clash, but nothing about
        // the colours was wrong.
        //
        // The hover test needs the index the TAB region will get, which is one past
        // the checkbox region the branches below may push -- hence the prediction
        // rather than a captured index. Click order is unchanged: regions are still
        // pushed checkbox-then-tab, which is what makes a click on the box toggle
        // rather than select (pinned by settings_layout_test's region golden).
        const bool rowHasCheckbox = (tabHud != nullptr) || i == TAB_WIDGETS || i == TAB_RUMBLE
                                 || i == TAB_HELMET || i == TAB_UPDATES || i == TAB_DIRECTOR
                                 || i == TAB_SPOTTER;
        const size_t tabRegionIndex = m_clickRegions.size() + (rowHasCheckbox ? 1u : 0u);
        {
            // The highlight spans the tab COLUMN, not the label: full row width,
            // identity icon included, the same for every tab whatever its label length.
            //
            // Two bugs in one line here before. It used to start a char right of the
            // icon (leaving the icon outside the highlight meant to select its row),
            // and it used to END at the label, so its inset from the group card's right
            // edge varied with the label while the left inset stayed fixed -- the uneven
            // left/right margins in the tab menu. Both go away by using the column.
            if (isActive) {
                // Themed button shape when the theme provides one; the plain solid
                // quad otherwise. The COLOUR is passed through either way -- it
                // carries state (unsaved / disabled / hovered), not decoration.
                // ButtonFill::State: this is the SELECTED ROW of the tab list, not a
                // control, so it keeps the accent fill rather than taking the neutral
                // surface an unthemed button now gets. A selection has to read at a
                // glance; a button has a label to carry its meaning.
                addButtonQuad(tabStartX, tabStartY, tabWidth, dim.lineHeightNormal,
                              PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(),
                                                        128.0f / 255.0f),
                              /*opaque=*/true, ButtonFill::State);
            } else if (m_hoveredRegionIndex >= 0 &&
                       static_cast<size_t>(m_hoveredRegionIndex) == tabRegionIndex) {
                // The same band every row highlight spans (plan.rowBandX/W), taken
                // from THIS column's box -- the sidebar and the content column
                // opposite it now answer the same way.
                addRowHighlight(plan.rowBandX(col), tabStartY, plan.rowBandW(col),
                                dim.lineHeightNormal,
                                PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(),
                                                          ROW_HOVER_ALPHA));
            }
        }

        // Add checkbox for tabs with toggleable HUDs or widgets
        if (tabHud) {
            // Checkbox click region for individual HUD
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HUD_TOGGLE, tabHud
            ));

            // Identity icon (or text fallback) for the individual HUD
            drawTabToggle(currentTabX, tabStartY, tabHud->getIconName(), isHudEnabled, isActive, dim, checkboxWidth);

            currentTabX += checkboxWidth;
        } else if (i == TAB_WIDGETS) {
            // Checkbox click region for widgets master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::WIDGETS_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-widgets", isHudEnabled, isActive, dim, checkboxWidth);

            currentTabX += checkboxWidth;
        } else if (i == TAB_RUMBLE) {
            // Checkbox click region for rumble master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::RUMBLE_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-rumble", isHudEnabled, isActive, dim, checkboxWidth);

            currentTabX += checkboxWidth;
        } else if (i == TAB_HELMET) {
            // Checkbox click region for helmet overlay master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HELMET_OVERLAY_TOGGLE, m_helmetOverlay
            ));

            // Helmet icon is game-specific (the helmet shape differs per game).
#if defined(GAME_MXBIKES)
            drawTabToggle(currentTabX, tabStartY, "hud-helmet-mx", isHudEnabled, isActive, dim, checkboxWidth);
#else
            drawTabToggle(currentTabX, tabStartY, "hud-helmet", isHudEnabled, isActive, dim, checkboxWidth);
#endif

            currentTabX += checkboxWidth;
        } else if (i == TAB_UPDATES) {
            // Checkbox click region for update checking toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::UPDATE_CHECK_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-updates", isHudEnabled, isActive, dim, checkboxWidth);

            currentTabX += checkboxWidth;
        } else if (i == TAB_DIRECTOR) {
            // Checkbox click region for the auto-director master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::DIRECTOR_ENABLE_TOGGLE, nullptr
            ));

            // hud-video, not the outlined marker "video": every other row in this list
            // is a flat hud-* glyph, and the marker set carries a baked 2px outline for
            // contrast over the track (see assets/icons/README.md), which read as one
            // heavier icon among twenty. Both files exist; this is the identity one.
            drawTabToggle(currentTabX, tabStartY, "hud-video", isHudEnabled, isActive, dim, checkboxWidth);

            currentTabX += checkboxWidth;
        } else if (i == TAB_SPOTTER) {
            // Checkbox click region for the spotter's spoken-audio master.
            // Same region TYPE as the toggle inside the tab body — one
            // handler, in the common switch (a tab-list checkbox is clicked
            // from whatever tab is open, so a tab-scoped handler would leave
            // it dead everywhere but its own tab).
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::SPOTTER_ENABLED_TOGGLE, nullptr
            ));

            // Identity icon, from assets/icons/hud-spotter.svg like every
            // other tab's — a headset, for the voice in your ear.
            drawTabToggle(currentTabX, tabStartY, "hud-spotter", isHudEnabled, isActive, dim, checkboxWidth);

            currentTabX += checkboxWidth;
        } else {
            // Non-toggleable section tabs: the identity icon takes the SAME colour rule
            // as the label beside it -- PRIMARY on the active row, ACCENT elsewhere.
            //
            // It was pinned to ACCENT, and the active row's band is accent: the selected
            // tab's icon was accent on accent and simply disappeared, while its label
            // (which already switched) stayed readable. One rule for the pair, so the
            // band's colour cannot swallow half of it again.
            drawTabIcon(currentTabX, tabStartY, tabRow.sectionIcon ? tabRow.sectionIcon : "",
                tabColor, dim, checkboxWidth);
            currentTabX += checkboxWidth;
        }

        // Tab click region (for selecting the tab)
        float tabLabelWidth = tabWidth - checkboxWidth;
        // (tabRegionIndex was predicted above, before the band was emitted.)
        assert(tabRegionIndex == m_clickRegions.size());

        // Tab ID for description lookup (lowercase). (Table-driven; the old
        // hand-maintained chain had drifted - it was missing Notices, so that
        // tab's hover showed the General description.)
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
        // an invitation to go and look -- POSITIVE. They were both WARNING, which made
        // a finished feature announce itself in the colour this plugin uses everywhere
        // else for "careful", and left the actual caveat with nothing to distinguish
        // it. The row bands below take the same POSITIVE for the same reason.
        //
        // Not folded into the label string, for two reasons. getTabName() is the
        // PERSISTED name ([Profiles] activeTab, what setActiveTabByName matches, and
        // what the "Reset <tab>" button reads), so badging it would strand anyone who
        // had the tab open. And the sidebar has no room: 16 characters less the
        // 4-character icon column leaves 12 for the label, which "Spotter (Beta)" (14)
        // overran into the content column -- seen in a screenshot as the badge sitting
        // on the first word of the description beside it. At the small size the tag
        // costs about three characters instead of seven, and fits.
        //
        // RIGHT-ALIGNED ON THE SIDEBAR'S CONTENT EDGE, not measured off the label.
        // Measured placement put each tag at a different x, so a column of tabs showed
        // its tags on a ragged diagonal following the name lengths -- correct per row
        // and wrong as a group, which is how it was reported. One edge for all of them
        // reads as a column, and it is the same edge the content column starts after.
        //
        // "Update" is the longest badge (6 chars = 4.5 cells at fontSizeS) but sits on
        // "Updates" (7), for 11.5 of the 13 -- roomier than the pair below.
        //
        // The tightest pair is Appearance + "New": the label area is
        // settingsSidebarWidth minus the 4-character icon column, so 13 cells at the
        // shipped 17; "Appearance" is 10 and a Small "New" costs 3 x fontSizeS =
        // 2.25, leaving 0.75 of a cell between them -- exactly one small-font space.
        // The 17 EXISTS FOR THIS PAIR: at 16 there were 12 cells against 12.25 asked,
        // the tag started where the label's last glyph ended, and the row read
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
// Those were two unrelated expressions that agreed only by 3.7px of accidental
// slack, which is why the gaps around the band were un-auditable -- nothing in the
// height said what the layout would do with the space it reserved. Moving the band
// by 8px turned that slack into an overflow, so they are now the same arithmetic.
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
    // UNGATED. This was hasThemedContentCard(), then hasThemedCard() — a gate
    // that always failed, then one that fails whenever no theme is selected,
    // which is the default. Both make [content] padding dead here while margin
    // and border work, and the second is the same mistake PanelBox::layoutPanel
    // made with the whole box: only a BORDER needs art to draw it with. Padding
    // is spacing and owes a theme nothing.
    const float pad = static_cast<float>(th->boxContentPadding.set
                                             ? th->boxContentPadding.v.t
                                             : L.boxContentPadding.t);
    // THE BOX TERM ALONE. There used to be a `settingsSectionPadding` base of
    // one cell under it, on this panel's own cellH lattice, with the box term
    // composed on top -- one distance in two spellings again, and the legacy
    // half was unreachable from any ini. A card's interior pad is
    // [content] padding, here as everywhere else.
    //
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
// the same story: a private half-character lead-in used to be added to it, so the
// clearance a themed card kept from its rows was a constant and [content] padding
// could only ever widen it.
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
    // The tab LIST's own order and its own availability rule -- the registry walked
    // exactly as buildTabBar walks it, so a game-gated tab is absent here for the
    // same reason it is absent on screen.
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
    ctx.finishSections();
    out.endY = ctx.currentY;
    out.sections = ctx.measuredSections;
    m_activeTab = savedTab;

    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    m_steppedControls.clear();
    m_cycleControls.clear();
    return out;
}

// THE SIDEBAR AS SECTIONS -- one per tab-list group, content height each, in the
// order the registry lists them. The engine lays the column out from these exactly
// as it lays the content column out from a tab's; the air BETWEEN groups is the
// same seam it puts between any two sibling cards, which is why none of it appears
// here (this used to add `contentGapY + cardPadTopY + cardPadBotY` per boundary,
// in two places that had to agree -- the height loop and the draw).
//
// The registry is walked the same way buildTabBar walks it, so a new group or a
// game-gated tab moves both without either being told: a marker closes the running
// group and opens the next, a tab row is one line, and the two markers that carry a
// caption pay for it (Global draws none -- see buildTabBar).
std::vector<float> SettingsHud::measureTabGroups(const ScaledDimensions& dim) const {
    std::vector<float> out;
    float rows = 0.0f;
    bool started = false;
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId >= 0) {
            // Hidden tabs cost the sidebar nothing -- that is the point of them.
            if (!row.hidden && isTabAvailable(row.tabId)) rows += 1.0f;
            continue;
        }
        if (started) out.push_back(rows * dim.lineHeightNormal);
        rows = 0.0f;
        started = true;
        rows += 1.0f;                                         // the group's caption
        if (row.tabId == TAB_SECTION_PROFILE) rows += 1.0f;   // ...and its control row
    }
    if (started) out.push_back(rows * dim.lineHeightNormal);
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
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId < 0 || !row.render) continue;      // a section header, not a tab
        // NO `hidden` skip here, and the asymmetry is the load-bearing part: a hidden
        // tab draws no sidebar row but its CONTENT still has to fit the panel, which
        // does not resize when you open it. Skipping it here would let About overflow
        // the one panel a player cannot scroll.
        if (!isTabAvailable(row.tabId)) continue;
        const TabMeasure m = measureTab(row.tabId, dim, labelX, controlX, rightColumnX,
                                        contentAreaStartX, contentAreaWidth,
                                        panelContentRightX);

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

        tallest = std::max(tallest, planBodyHeight(dim, w));
    }

    m_tallestContentRows = tallest;
    m_tallestKey = key;
    return tallest;
}

void SettingsHud::rebuildRenderData() {
    if (!m_bVisible) return;  // vis-gate: menu is active-surface-only (see show())

    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    m_steppedControls.clear();  // rebuilt in lockstep with the click regions
    m_cycleControls.clear();    // rebuilt in lockstep with the click regions

    // Update cached window size (use actual pixel dimensions)
    const InputManager& input = InputManager::getInstance();
    m_cachedWindowWidth = input.getWindowWidth();
    m_cachedWindowHeight = input.getWindowHeight();

    auto dim = getScaledDimensions();

    // The panel's columns, COMPOSED: sidebar ask + trough (the seam read,
    // sectionGap + gap — see troughCells) + content ask, in characters —
    // fractional once the terms enter, so the width is built from one
    // character's width rather than the int-only helper.
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    constexpr float sectionSpacing = 0.0150f;

    // ======================================================================
    // DECLARE, THEN DRAW -- the two steps every HUD and widget takes, and the
    // reason this function is now a tenth of its old length.
    //
    // What used to be here was this panel's own X and Y chains: an overhang
    // stack, a caption block, per-card pads, a tallest-tab row count and a
    // hand-built button row, each of them a second spelling of something
    // PanelBox already computes for every other panel in the tree. Every
    // geometry fault reported against this menu was a divergence between one of
    // those spellings and the engine's -- [content] margin acting on one axis,
    // [content] border acting on none, a caption block twice the height of a
    // HUD's, a band that reserved a border it did not draw. None of them is
    // expressible now: there is one owner, and this panel asks it.
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
    const float sidebarAsk = std::ceil(
        charW * static_cast<float>(layout().settingsSidebarWidth) / dim.cellW - 1e-4f)
        * dim.cellW;
    const float contentAsk = charW * static_cast<float>(layout().settingsContentAreaChars());

    // MEASURED AT THE ORIGIN, with the same RELATIVE columns the draw will use.
    // A section's height depends on its rows and on how far prose wraps, and
    // wrapping is a character count -- never on where the column sits. Measuring
    // at 0 says so, and removes the provisional-origin pass this used to need.
    const float labelToControl = PluginUtils::calculateMonospaceTextWidth(24, dim.fontSize);
    const float labelToRight = PluginUtils::calculateMonospaceTextWidth(
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
    // flow instead let a tab with more sections outgrow the floor, which is why the
    // panel used to change height between General and Appearance.
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
    const PanelBox::ColumnGeom& sideCol = plan.col(0, 0);
    const PanelBox::ColumnGeom& mainCol = plan.col(0, 1);

    // CENTRED ON THE CONTENT, which is this panel's one layout privilege: it
    // cannot be dragged, so it places itself -- and what it centres is the
    // character lattice (sidebar + content asks), NOT the panel box. Centring
    // the box splits every theme-dependent term in half and pushes that half
    // into the content, which is what walked the row controls sideways as
    // themes were cycled (theme_geometry_test contract 1; the pre-port chain
    // said the same at its contentAnchorX). The theme's air and borders hang
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

    // The frame, the caption's band and one card per section of BOTH columns.
    addPlanBackground(plan, startX, startY);
    setBounds(startX, startY, startX + panelWidth, startY + backgroundHeight);
    addPlanTitle(plan, "MXBMRP3 SETTINGS", Fonts::getTitle(),
                 ColorConfig::getInstance().getPrimary());

    // ---- the columns, in the engine's coordinates ------------------------
    const float tabStartX = plan.colContentX(sideCol);
    const float tabWidth = sidebarAsk;
    const float contentAreaStartX = plan.colContentX(mainCol);
    const float leftColumnX = contentAreaStartX;
    const float rightColumnX = contentAreaStartX + labelToRight;
    const float controlX = leftColumnX + labelToControl;
    const float contentAreaWidth = plan.colContentW(mainCol);
    // A row ends where its own column ends. It used to stop short of the panel's
    // surface line by a hand-kept correction, because the CARD was inset from the
    // column by the label indent and the row had to give that back; the card is
    // the column's own box now, so there is nothing to give back.
    const float panelContentRightX = contentAreaStartX + contentAreaWidth;
    float currentY = plan.colContentY(mainCol, 0);
    float checkboxWidth = PluginUtils::calculateMonospaceTextWidth(4, dim.fontSize);  // "[X] " or "    "

    // The sidebar draws into its OWN column's sections -- one per tab-list group,
    // at the origins the engine placed them. It used to carry a cursor and add the
    // seam between groups itself, in an expression the height loop had to match.
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
#endif

    if (const TabDescriptor* tabDesc = findTabDescriptor(m_activeTab); tabDesc && tabDesc->render) {
        // Route to the extracted per-tab renderer (settings_tab_*.cpp) via the registry.
        layoutCtx.currentY = currentY;   // Sync context cursor
        tabDesc->render(layoutCtx);
        layoutCtx.finishSections();
        currentY = layoutCtx.currentY;   // Sync local cursor back

        // THE WHAT'S-NEW ROW BANDS, in one pass over what the tab just registered.
        //
        // Here rather than inside every row helper because a row's identity is its
        // row-wide tooltip region, and by now they all exist -- one loop marks any
        // row on any tab, and no helper needs to know this feature exists.
        //
        // Drawn AFTER the rows and still behind them: the plugin API takes quads and
        // strings as two arrays, so every quad draws before every string whatever
        // order they were pushed in (see HudManager::draw). The band cannot cover the
        // label it is pointing at.
        //
        // The POSITIVE colour, matching the "New" tag on the tab that led the player
        // here -- one colour for the whole trail, tag to row. It was WARNING, which
        // this plugin spends everywhere else on "careful": a band in it read as a
        // problem with the row rather than as the thing worth looking at, and it was
        // indistinguishable from the Beta caveat two tabs down.
        //
        // At the same alpha the hover band uses, so it reads as "look here" rather
        // than as a selection -- and so it disappears under the hover band the moment
        // the pointer arrives, which is also when it is dismissed.
        // SPANNED FROM THE PLAN (rowBandX/W), not from the region's own rect. A
        // highlight is a property of the COLUMN, not of the control in it -- the
        // hover band below carries the full story of why, and this pass repeated
        // exactly the mistake it records as fixed: a row that builds its tooltip
        // region by hand (the Web Server row, and five tab files with their own copy
        // of the width expression) gets a different rect from one that went through
        // the layout helpers, so the green band's width changed by tab and did not
        // line up with the accent band that replaces it on hover.
        for (const ClickRegion& r : m_clickRegions) {
            if (r.tooltipId.empty()) continue;
            if (!WhatsNew::liveForRow(m_activeTab, r.tooltipId.c_str())) continue;
            addRowHighlight(plan.rowBandX(mainCol), r.y, plan.rowBandW(mainCol), r.height,
                            PluginUtils::applyOpacity(
                                ColorConfig::getInstance().getPositive(), ROW_HOVER_ALPHA));
        }

        // HOW FAR THE TAB OVERRAN THE SPACE RESERVED FOR IT, in rows -- negative is
        // slack, and it should ALWAYS be negative: the height is measured from the
        // tallest tab, so this one had room by construction.
        //
        // Plus the last card's bottom pad, which finishSections() drew BELOW
        // currentY: the cursor stops on the last row, the card does not, and it is
        // the CARD the footer buttons collide with.
        //
        // It is kept because "by construction" has one failure mode left: a renderer
        // that lays out differently between the measure pass and this one -- reading
        // the panel's own height, say. The warning is for a player's log, the number
        // for CI (settings_fit_test reads it for every tab).
        // WHERE THE COLUMN'S LAST SECTION ENDS, straight off the engine -- what the
        // tab was given. It used to be the panel's bottom minus a hand-composed
        // footer block, three terms that had to match what the button row actually
        // spent.
        const float contentLimit = mainCol.sections.empty()
            ? plan.Y(plan.g.btnTop)
            : plan.Y(mainCol.sections.back().bot);
        const float overflow = (currentY + cardPadBotY() - contentLimit)
                             / dim.lineHeightNormal;
#if defined(MXBMRP3_TEST_BUILD)
        m_testOverflowRows = overflow;
#endif
        if (overflow > 0.0f) {
            DEBUG_WARN_F("Settings tab %d overflows the panel by %.1f rows -- it "
                         "measured shorter than it drew, so a tab renderer is not "
                         "reproducible", m_activeTab, overflow);
        }
    } else {
        DEBUG_WARN_F("Invalid tab index: %d, defaulting to TAB_STANDINGS", m_activeTab);
    }

    currentY += sectionSpacing;

    // Draw hover highlight for TOOLTIP_ROW regions
    if (m_hoveredRegionIndex >= 0 && m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size())) {
        const ClickRegion& hoveredRegion = m_clickRegions[m_hoveredRegionIndex];
        if (hoveredRegion.type == ClickRegion::TOOLTIP_ROW) {
            // THE CONTENT COLUMN, from the plan (rowBandX/W) -- the same band the
            // sidebar, StandingsHud and RecordsHud span. This column was the odd one
            // out: it took the card's INTERIOR, so its band absorbed [content]
            // padding while the other three were inset by it.
            //
            // It used to take the region's own x and width, plus a one-character
            // fudge on the left "because the right edge already reaches the content
            // edge". That tied the decoration to a hit-test rectangle: every row that
            // built its region by hand -- the Web Server row, and five whole tab files
            // carrying their own copy of the width expression -- highlighted to a
            // different width from the rows that went through the layout helpers.
            // A highlight is a property of the column, not of the control in it. Then
            // it became the column plus a lead-in, with a SECOND expression for the
            // themed case (planCardRightX - cardBorderX()) because the card had gained
            // a clamp against the frame that the first knew nothing about. The plan
            // answers both, and the clamp with them.
            addRowHighlight(plan.rowBandX(mainCol), hoveredRegion.y,
                            plan.rowBandW(mainCol),
                            hoveredRegion.height,
                            PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(),
                                                      ROW_HOVER_ALPHA));
        }
    }

    // Render description or tooltip at the reserved position (replaces each other).
    // The box spans from the label column to the content edge — a whole number of
    // character cells, so take it from SettingsMetrics rather than dividing the
    // emitted float span by one character's width. That round-trip returned one
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
            // Show control tooltip
            const char* tooltipText = TooltipManager::getInstance().getControlTooltip(m_hoveredTooltipId.c_str());
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

    // Bottom button row - always [Save/Saved] [Close]. The Save button reflects unsaved changes:
    // lit + clickable ("Save") when there are pending changes, grayed-out ("Saved") when
    // everything is persisted. It lets the player save manually without leaving the track,
    // regardless of the Auto-Save setting (which only controls the automatic leave-track flush).
    // THE ENGINE'S BUTTON ROW. Its y, its height and each button's box come from
    // the plan, which placed them under the body with the same margins and gap any
    // other child gets. This used to walk back from the panel's bottom edge through
    // a footer pad, a margin and a box height composed here -- the reserve-and-spend
    // pair that has to agree, and the one this panel got wrong twice.
    const PlanButtonTerms bt = planButtonTerms(dim);
    const float buttonBoxH = plan.H(plan.g.btnH);
    const float buttonRowY = plan.Y(plan.g.btnTop);
    const float buttonAreaCenterX = startX + panelWidth / 2.0f;
    bool settingsDirty = SettingsManager::getInstance().isDirty();

    // Size both buttons for the widest label they can show (Saved / Close =
    // 5 chars), plus the [button] border+padding each side — the box-model
    // terms, resolved with the same fallbacks the plan applies. The gap
    // between the two is the SUM of the facing [button] margins. At the
    // shipped defaults the gap reproduces the old 1-char seam; the WIDTHS
    // follow the terms and deliberately do not reproduce the old fixed 7
    // chars — 6 unthemed (padding 0.5/side), 8 themed (border 1 + padding
    // 0.5/side) — and [Advanced] buttonPadding retunes them.
    float saveButtonWidth = PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize)
        + bt.insetL + bt.insetR;
    float closeButtonWidth = saveButtonWidth;
    float buttonGap = bt.gap;
    float totalWidth = saveButtonWidth + buttonGap + closeButtonWidth;
    float startButtonX = buttonAreaCenterX - totalWidth / 2.0f;

    // [Save] / [Saved] button
    float saveButtonX = startButtonX;
    if (settingsDirty) {
        // Unsaved changes: lit and clickable.
        size_t saveRegionIndex = m_clickRegions.size();
        m_clickRegions.push_back(ClickRegion(
            saveButtonX, buttonRowY, saveButtonWidth, buttonBoxH,
            ClickRegion::SAVE_BUTTON, nullptr, 0, false, 0
        ));
        addStateButton(saveButtonX, buttonRowY, saveButtonWidth, buttonBoxH,
            "Save", buttonRowY + bt.insetT, dim.fontSize,
            ColorConfig::getInstance().getPositive(),
            (m_hoveredRegionIndex == static_cast<int>(saveRegionIndex))
                ? ButtonState::Hovered : ButtonState::Idle);
    } else {
        // Nothing to save: grayed out, not clickable (no click region -> no hover/click).
        addStateButton(saveButtonX, buttonRowY, saveButtonWidth, buttonBoxH,
            "Saved", buttonRowY + bt.insetT, dim.fontSize,
            ColorConfig::getInstance().getPositive(), ButtonState::Disabled);
    }

    // [Close] button
    float closeButtonX = saveButtonX + saveButtonWidth + buttonGap;
    size_t closeRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        closeButtonX, buttonRowY, closeButtonWidth, buttonBoxH,
        ClickRegion::CLOSE_BUTTON, nullptr, 0, false, 0
    ));
    addStateButton(closeButtonX, buttonRowY, closeButtonWidth, buttonBoxH,
        "Close", buttonRowY + bt.insetT, dim.fontSize,
        ColorConfig::getInstance().getAccent(),
        (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
            ? ButtonState::Hovered : ButtonState::Idle);

    // [Reset <TabName>] button - bottom left corner.
    //
    // ONLY WHERE THERE IS SOMETHING TO RESET. A tab's reset is its registry row's
    // resetHud / resetExtra, and a row with neither has nothing the button could do
    // -- About is prose and links, so "Reset About" was a live-looking control that
    // did nothing at all when clicked. Read off the registry rather than a list of
    // exceptions, so a future page of pure text gets the same treatment for free.
    const TabDescriptor* activeDesc = findTabDescriptor(m_activeTab);
    const bool tabHasReset = activeDesc && (activeDesc->resetHud || activeDesc->resetExtra);
    if (tabHasReset) {
    float resetTabButtonY = buttonRowY;
    char resetTabButtonText[32];
    snprintf(resetTabButtonText, sizeof(resetTabButtonText), "Reset %s", getTabName(m_activeTab));
    int resetTabButtonChars = static_cast<int>(strlen(resetTabButtonText));
    // The [button] insets pad the label — the old "+2 chars" hand padding.
    float resetTabButtonWidth = PluginUtils::calculateMonospaceTextWidth(resetTabButtonChars, dim.fontSize)
        + bt.insetL + bt.insetR;
    // LEFT-ALIGNED ON THE SIDEBAR'S CARD, which is the panel's leftmost surface --
    // the same line every other left edge in this panel now comes from.
    const float resetTabButtonX = plan.X(sideCol.cardLeft);

    // Add click region first for hover check
    size_t resetTabRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        resetTabButtonX, resetTabButtonY, resetTabButtonWidth, buttonBoxH,
        ClickRegion::RESET_TAB_BUTTON, nullptr
    ));

    // NEGATIVE, like the Reset button in General's Reset section: both destroy
    // settings, and a destructive control that reads as an ordinary accent action
    // is the one place in this panel where colour should carry the warning.
    addStateButton(resetTabButtonX, resetTabButtonY, resetTabButtonWidth, buttonBoxH,
        resetTabButtonText, resetTabButtonY + bt.insetT, dim.fontSize,
        ColorConfig::getInstance().getNegative(),
        (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
            ? ButtonState::Hovered : ButtonState::Idle);
    }   // tabHasReset

    // [About] button - bottom right corner.
    //
    // THIS USED TO BE THE VERSION STRING, and carried the update notice with it: it
    // grew into a green "vX.Y.Z available!" chip whose click jumped to the Updates
    // tab, with an easter-egg counter underneath for the ordinary case. Two
    // unrelated jobs on one label, and the version -- the part that never changes --
    // was the only thing a player could actually read at a glance.
    //
    // It is an About button now, and the two jobs went to the places that own them:
    // the update notice is a tag on the Updates row in the sidebar (see
    // updateTagLive, which is dismissible and re-arms for a newer version), and the
    // version is the first line of the About page this opens.
    //
    // A REAL BUTTON rather than the muted text it replaced, because it is now the
    // ONLY way to reach About -- the page is not in the tab list (TabDescriptor::
    // hidden), so an affordance that does not look clickable would make it
    // unreachable in practice. Secondary rather than Close's accent: it is a quieter
    // action than the one that shuts the panel.
    //
    // The five-click easter egg still works from here, and still works after the
    // first click has navigated: the footer is drawn on every tab, so clicks two
    // through five land while About is already open.
    {
        // The content column's right edge -- the same line a row ends on, so the
        // button sits flush with the settings above it.
        const float rightEdgeX = plan.X(mainCol.cardLeft + mainCol.cardW);
        const char* aboutLabel = "About";
        const float aboutWidth =
            PluginUtils::calculateMonospaceTextWidth(
                static_cast<int>(strlen(aboutLabel)), dim.fontSize) + bt.insetL + bt.insetR;
        const float aboutX = rightEdgeX - aboutWidth;

        const size_t aboutRegionIndex = m_clickRegions.size();
        ClickRegion aboutRegion;
        aboutRegion.type = ClickRegion::VERSION_CLICK;
        aboutRegion.x = aboutX;
        aboutRegion.y = buttonRowY;
        aboutRegion.width = aboutWidth;
        aboutRegion.height = buttonBoxH;
        m_clickRegions.push_back(aboutRegion);

        addStateButton(aboutX, buttonRowY, aboutWidth, buttonBoxH,
            aboutLabel, buttonRowY + bt.insetT, dim.fontSize,
            ColorConfig::getInstance().getSecondary(),
            (m_hoveredRegionIndex == static_cast<int>(aboutRegionIndex))
                ? ButtonState::Hovered : ButtonState::Idle);
    }

    // This panel rebuilds DIRECTLY from its ~30 interaction sites rather than
    // through processDirtyFlags, which is where every other HUD's fill gets cut
    // -- so without this the sweep never ran here at all, the centre slice kept
    // covering the whole interior, and every card (and the title band) sat on it
    // at double opacity: the settings-cards-read-darker bug, surviving the sweep
    // fix that cured the other panels. Consumes m_fillFirst, so the dirty-flag
    // path finalizing again is a no-op, not a double cut.
    finalizeThemedFill();
}

