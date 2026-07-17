// ============================================================================
// hud/settings_hud_render.cpp
// SettingsHud::rebuildRenderData() — the settings menu's full render build: it
// lays out every tab's controls, labels, click regions and tooltips into the
// HUD's quad/string vectors. Extracted verbatim from settings_hud.cpp (it was
// ~890 lines, the bulk of that file) when the file grew past ~1.8k lines; the
// SettingsHud class, members, and public API are unchanged — only where this
// one method body lives moves. The layout-local lambdas it uses stay intact
// (an intentional design — see CLAUDE.md). Companion to settings_hud_input.cpp.
// ============================================================================
#include "settings_hud.h"
#include "settings/settings_layout.h"
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
    { TAB_SECTION_GLOBAL,   nullptr,  nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr },
    { TAB_GENERAL,      "General",    "general",       nullptr,                                                              false, &SettingsHud::renderTabGeneral,         &SettingsHud::handleClickTabGeneral,      nullptr,            &SettingsHud::resetTabGeneral,          "hud-general" },
    { TAB_APPEARANCE,   "Appearance", "appearance",    nullptr,                                                              false, &SettingsHud::renderTabAppearance,      &SettingsHud::handleClickTabAppearance,   nullptr,            &SettingsHud::resetTabAppearance,       "hud-appearance" },
    { TAB_HOTKEYS,      "Hotkeys",    "hotkeys",       nullptr,                                                              false, &SettingsHud::renderTabHotkeys,         &SettingsHud::handleClickTabHotkeys,      nullptr,            &SettingsHud::resetTabHotkeys,          "hud-hotkeys" },
    { TAB_RIDERS,       "Riders",     "riders",        nullptr,                                                              false, &SettingsHud::renderTabRiders,          &SettingsHud::handleClickTabRiders,       nullptr,            &SettingsHud::resetTabRiders,           "hud-riders" },
    { TAB_RUMBLE,       "Rumble",     "rumble",        nullptr,                                                              false, &SettingsHud::renderTabRumble,          &SettingsHud::handleClickTabRumble,       nullptr,            &SettingsHud::resetTabRumble,           nullptr },
    { TAB_HELMET,       "Helmet",     "helmet",        nullptr,                                                              false, &SettingsHud::renderTabHelmet,          &SettingsHud::handleClickTabHelmet,       nullptr,            &SettingsHud::resetTabHelmet,           nullptr },
    { TAB_DIRECTOR,     "Director",   "director",      nullptr,                                                              false, &SettingsHud::renderTabDirector,        nullptr,                                  nullptr,            &SettingsHud::resetTabDirector,         nullptr },
    { TAB_UPDATES,      "Updates",    "updates",       nullptr,                                                              false, &SettingsHud::renderTabUpdates,         &SettingsHud::handleClickTabUpdates,      nullptr,            &SettingsHud::resetTabUpdates,          nullptr },
    { TAB_SECTION_PROFILE,  nullptr,  nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr },
    { TAB_SECTION_ELEMENTS, nullptr,  nullptr,         nullptr,                                                              false, nullptr,                                nullptr,                                  nullptr,            nullptr,                                nullptr },
    { TAB_STANDINGS,    "Standings",  "standings",     [](const SettingsHud& s) -> BaseHud* { return s.m_standings; },       false, &SettingsHud::renderTabStandings,       &SettingsHud::handleClickTabStandings,    "StandingsHud",     &SettingsHud::resetTabStandingsExtra,   nullptr },
    { TAB_MAP,          "Map",        "map",           [](const SettingsHud& s) -> BaseHud* { return s.m_mapHud; },          false, &SettingsHud::renderTabMap,             &SettingsHud::handleClickTabMap,          "MapHud",           nullptr,                                nullptr },
    { TAB_RADAR,        "Radar",      "radar",         [](const SettingsHud& s) -> BaseHud* { return s.m_radarHud; },        false, &SettingsHud::renderTabRadar,           &SettingsHud::handleClickTabRadar,        "RadarHud",         nullptr,                                nullptr },
    { TAB_LAP_LOG,      "Lap Log",    "lap_log",       [](const SettingsHud& s) -> BaseHud* { return s.m_lapLog; },          false, &SettingsHud::renderTabLapLog,          &SettingsHud::handleClickTabLapLog,       "LapLogHud",        nullptr,                                nullptr },
    { TAB_IDEAL_LAP,    "Ideal Lap",  "ideal_lap",     [](const SettingsHud& s) -> BaseHud* { return s.m_idealLap; },        false, &SettingsHud::renderTabIdealLap,        nullptr,                                  "IdealLapHud",      nullptr,                                nullptr },
    { TAB_SESSION_CHARTS, "Charts",   "session_charts",[](const SettingsHud& s) -> BaseHud* { return s.m_sessionCharts; },   false, &SettingsHud::renderTabSessionCharts,   nullptr,                                  "SessionChartsHud", nullptr,                                nullptr },
    { TAB_TELEMETRY,    "Telemetry",  "telemetry",     [](const SettingsHud& s) -> BaseHud* { return s.m_telemetry; },       false, &SettingsHud::renderTabTelemetry,       nullptr,                                  "TelemetryHud",     nullptr,                                nullptr },
    { TAB_RECORDS,      "Records",    "records",       [](const SettingsHud& s) -> BaseHud* { return s.m_records; },         true,  &SettingsHud::renderTabRecords,         &SettingsHud::handleClickTabRecords,      "RecordsHud",       &SettingsHud::resetTabRecordsExtra,     nullptr },
    { TAB_PITBOARD,     "Pitboard",   "pitboard",      [](const SettingsHud& s) -> BaseHud* { return s.m_pitboard; },        false, &SettingsHud::renderTabPitboard,        nullptr,                                  "PitboardHud",      nullptr,                                nullptr },
    { TAB_SESSION,      "Session",    "session",       [](const SettingsHud& s) -> BaseHud* { return s.m_session; },         false, &SettingsHud::renderTabSession,         &SettingsHud::handleClickTabSession,      "SessionHud",       nullptr,                                nullptr },
    { TAB_TIMING,       "Timing",     "timing",        [](const SettingsHud& s) -> BaseHud* { return s.m_timing; },          false, &SettingsHud::renderTabTiming,          &SettingsHud::handleClickTabTiming,       "TimingHud",        nullptr,                                nullptr },
    { TAB_GAP_BAR,      "Gap Bar",    "gap_bar",       [](const SettingsHud& s) -> BaseHud* { return s.m_gapBar; },          false, &SettingsHud::renderTabGapBar,          &SettingsHud::handleClickTabGapBar,       "GapBarHud",        nullptr,                                nullptr },
    { TAB_NOTICES,      "Notices",    "notices",       [](const SettingsHud& s) -> BaseHud* { return s.m_notices; },         false, &SettingsHud::renderTabNotices,         nullptr,                                  "NoticesHud",       nullptr,                                nullptr },
    { TAB_EVENT_LOG,    "Event Log",  "event_log",     [](const SettingsHud& s) -> BaseHud* { return s.m_eventLog; },        false, &SettingsHud::renderTabEventLog,        &SettingsHud::handleClickTabEventLog,     "EventLogHud",      nullptr,                                nullptr },
    { TAB_FRIENDS,      "Friends",    "friends",       [](const SettingsHud& s) -> BaseHud* { return s.m_friends; },         true,  &SettingsHud::renderTabFriends,         &SettingsHud::handleClickTabFriends,      "FriendsHud",       nullptr,                                nullptr },
    { TAB_FMX,          "FMX",        "fmx",           [](const SettingsHud& s) -> BaseHud* { return s.m_fmxHud; },          true,  &SettingsHud::renderTabFmx,             &SettingsHud::handleClickTabFmx,          "FmxHud",           nullptr,                                nullptr },
    { TAB_STATS,        "Stats",      "stats",         [](const SettingsHud& s) -> BaseHud* { return s.m_statsHud; },        false, &SettingsHud::renderTabStats,           &SettingsHud::handleClickTabStats,        "StatsHud",         nullptr,                                nullptr },
    { TAB_PERFORMANCE,  "Performance","performance",   [](const SettingsHud& s) -> BaseHud* { return s.m_performance; },     false, &SettingsHud::renderTabPerformance,     nullptr,                                  "PerformanceHud",   nullptr,                                nullptr },
    { TAB_WIDGETS,      "Widgets",    "widgets",       nullptr,                                                              false, &SettingsHud::renderTabWidgets,         nullptr,                                  nullptr,            &SettingsHud::resetTabWidgets,          nullptr },
};

const SettingsHud::TabDescriptor* SettingsHud::findTabDescriptor(int tabId) {
    for (const TabDescriptor& row : s_tabRegistry) {
        if (row.tabId == tabId) return &row;
    }
    return nullptr;
}

void SettingsHud::rebuildRenderData() {
    if (!m_bVisible) return;

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

    // Layout constants - compact panel for single HUD
    constexpr int panelWidthChars = SettingsHud::SETTINGS_PANEL_WIDTH;
    constexpr float sectionSpacing = 0.0150f;
    constexpr float tabSpacing = 0.0050f;

    float panelWidth = PluginUtils::calculateMonospaceTextWidth(panelWidthChars, dim.fontSize) + dim.paddingH + dim.paddingH;

    // Estimate height - sized to fit all tabs + content (Friends tab added one more row;
    // the Rumble tab's expanded front/rear splits and Rev/Pit Limiter rows are the tallest case)
    int estimatedRows = 33;
    float backgroundHeight = dim.paddingV + dim.lineHeightLarge + dim.lineHeightNormal + (estimatedRows * dim.lineHeightNormal) + dim.paddingV;

    // Center the panel horizontally and vertically
    float startX = (1.0f - panelWidth) / 2.0f;
    float startY = (1.0f - backgroundHeight) / 2.0f;

    setBounds(startX, startY, startX + panelWidth, startY + backgroundHeight);
    addBackgroundQuad(startX, startY, panelWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float currentY = startY + dim.paddingV;

    // Main title
    float titleX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
    addString("MXBMRP3 SETTINGS", titleX, currentY, Justify::CENTER,
        Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);

    currentY += dim.lineHeightLarge + tabSpacing;

    // Vertical tab bar on left side
    float tabStartX = contentStartX;
    float tabStartY = currentY;
    float tabWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_TAB_WIDTH, dim.fontSize);

    float checkboxWidth = PluginUtils::calculateMonospaceTextWidth(4, dim.fontSize);  // "[X] " or "    "

    // Shared dim level for "inactive" tab icons (disabled toggles + non-toggle section
    // tabs) so they read as equally subdued; enabled toggles stay at full opacity.
    constexpr float INACTIVE_ICON_OPACITY = 0.5f;

    // Draws an identity icon in a tab's checkbox cell at the given colour. Returns false
    // if no icon is assigned/available (caller can fall back to text). Icons render a bit
    // smaller than the row font (they fill their glyph box more than text fills the em) and
    // nudged up ~2px (at 1080p, scaled) so they sit optically centred on the row.
    auto drawTabIcon = [&](float x, float y, const char* iconName, unsigned long color) -> bool {
        // Same global switch that drives the title-bar icons gates the tab icons.
        int spriteIndex = (UiConfig::getInstance().getTitleIcons() && iconName && iconName[0])
            ? AssetManager::getInstance().getIconSpriteIndex(iconName) : 0;
        if (spriteIndex <= 0) return false;
        constexpr float TAB_ICON_SCALE = 0.63f;
        float cellW = checkboxWidth * 0.25f;
        float iconCenterY = y + dim.lineHeightNormal * 0.5f - (2.0f / 1080.0f) * dim.scale;
        addIcon(x + cellW * 1.5f, iconCenterY, spriteIndex, color, dim.fontSize * TAB_ICON_SCALE);
        return true;
    };

    // Draws a tab's enable/disable toggle in semantic colours: POSITIVE when enabled,
    // NEGATIVE when disabled (a disabled icon lightens 10% on hover as an affordance).
    // Falls back to the legacy "[x]"/"[ ]" text when no icon is available.
    // Call right after pushing the tab's toggle ClickRegion so the hover check targets it.
    auto drawTabToggle = [&](float x, float y, const char* iconName, bool enabled) {
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
        } else {
            // Enabled pops at full; disabled is dimmed to the muted section level so it
            // doesn't scream.
            iconColor = enabled ? base : PluginUtils::applyOpacity(base, INACTIVE_ICON_OPACITY);
        }
        if (!drawTabIcon(x, y, iconName, iconColor)) {
            // Text fallback (no icon assigned, or asset missing on this build)
            addString(enabled ? "[x]" : "[ ]", x, y, Justify::LEFT,
                Fonts::getNormal(), iconColor, dim.fontSize);
        }
    };

    // Visual tab order comes straight from the descriptor registry (rows are in
    // display order; negative TAB_SECTION_* rows are the section headers).
    for (const TabDescriptor& tabRow : s_tabRegistry) {
        const int i = tabRow.tabId;

        // Skip game-gated tabs whose backing HUD isn't registered on this build (Records on
        // GP Bikes, FMX on karts, Friends on non-Steam). Section headers are negative ids and
        // fall through to their own handling below. Single source of truth: isTabAvailable().
        if (i >= 0 && !isTabAvailable(i)) {
            continue;
        }

        // Section headers (bold, primary color, not clickable)
        if (i == TAB_SECTION_GLOBAL) {
            addString("Global", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }
        if (i == TAB_SECTION_PROFILE) {
            tabStartY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
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
        if (i == TAB_SECTION_ELEMENTS) {
            tabStartY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
            addString("Elements", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }

        bool isActive = (i == m_activeTab);

        // Get the HUD for this tab (nullptr for master-toggle/section tabs)
        BaseHud* tabHud = tabRow.hud ? tabRow.hud(*this) : nullptr;

        // Determine if this tab's HUD/widgets are enabled. Per-HUD checkboxes show
        // the focused surface's on/off (companion vs game); the manager/global
        // toggles (widgets/rumble/helmet/updates/director) are shared, not decoupled.
        bool companionSurface = InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
        bool isHudEnabled;
        if (tabHud) {
            isHudEnabled = companionSurface ? tabHud->getCompanionVisible() : tabHud->isVisible();
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
        } else {
            isHudEnabled = true;  // General is always "enabled"
        }

        // Tab color: PRIMARY if active, ACCENT if inactive
        unsigned long tabColor = isActive ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getAccent();

        float currentTabX = tabStartX;

        // Add checkbox for tabs with toggleable HUDs or widgets
        if (tabHud) {
            // Checkbox click region for individual HUD
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HUD_TOGGLE, tabHud
            ));

            // Identity icon (or text fallback) for the individual HUD
            drawTabToggle(currentTabX, tabStartY, tabHud->getIconName(), isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_WIDGETS) {
            // Checkbox click region for widgets master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::WIDGETS_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-widgets", isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_RUMBLE) {
            // Checkbox click region for rumble master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::RUMBLE_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-rumble", isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_HELMET) {
            // Checkbox click region for helmet overlay master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HELMET_OVERLAY_TOGGLE, m_helmetOverlay
            ));

            // Helmet icon is game-specific (the helmet shape differs per game).
#if defined(GAME_MXBIKES)
            drawTabToggle(currentTabX, tabStartY, "hud-helmet-mx", isHudEnabled);
#else
            drawTabToggle(currentTabX, tabStartY, "hud-helmet", isHudEnabled);
#endif

            currentTabX += checkboxWidth;
        } else if (i == TAB_UPDATES) {
            // Checkbox click region for update checking toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::UPDATE_CHECK_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-updates", isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_DIRECTOR) {
            // Checkbox click region for the auto-director master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::DIRECTOR_ENABLE_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "video", isHudEnabled);

            currentTabX += checkboxWidth;
        } else {
            // Non-toggleable section tabs: an ACCENT identity icon (no on/off state, so
            // it's distinct from the positive/negative toggle icons and matches the tab
            // label's colour family rather than looking disabled).
            drawTabIcon(currentTabX, tabStartY, tabRow.sectionIcon ? tabRow.sectionIcon : "",
                ColorConfig::getInstance().getAccent());
            currentTabX += checkboxWidth;
        }

        // Tab click region (for selecting the tab)
        float tabLabelWidth = tabWidth - checkboxWidth;
        size_t tabRegionIndex = m_clickRegions.size();  // Track index for hover check

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

        // The hover/active highlight leads the label by one char — matching the
        // row-highlight and button convention (which pad the highlight ~1 char around
        // the label) instead of sitting flush against the text — WITHOUT moving the
        // text: the label stays at currentTabX and the highlight starts one char to its
        // left, keeping its right edge. The 1-char lead lands in the gap between the
        // tab's identity icon and its label, so it doesn't overlap the icon.
        float labelPad = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float highlightX = currentTabX - labelPad;
        float highlightWidth = tabLabelWidth + labelPad;

        // Active tab background
        if (isActive) {
            SPluginQuad_t bgQuad;
            float bgX = highlightX, bgY = tabStartY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, highlightWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        // Hover background for inactive tabs
        else if (m_hoveredRegionIndex >= 0 && static_cast<size_t>(m_hoveredRegionIndex) == tabRegionIndex) {
            SPluginQuad_t hoverQuad;
            float hoverX = highlightX, hoverY = tabStartY;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverQuad, hoverX, hoverY, highlightWidth, dim.lineHeightNormal);
            hoverQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            hoverQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverQuad);
        }

        // Label stays at its original position (the highlight leads it, above).
        addString(getTabName(i), currentTabX, tabStartY, Justify::LEFT, Fonts::getNormal(), tabColor, dim.fontSize);

        tabStartY += dim.lineHeightNormal;
    }

    // Content area starts to the right of the tabs
    float contentAreaStartX = contentStartX + tabWidth + PluginUtils::calculateMonospaceTextWidth(2, dim.fontSize);  // 2-char gap after tabs
    // Content starts at the same Y as the tabs — currentY is already there (the tab
    // loop advances a separate cursor), so nothing to set here.

    // Helper lambdas for controls
    // NOTE: These lambdas are intentionally NOT extracted to member functions.
    // They capture local layout state (dim, currentY, contentAreaStartX, etc.) which
    // would require passing 8+ parameters if converted to methods. Lambdas improve
    // readability and maintainability here. See CLAUDE.md "Design Decisions".
    float leftColumnX = contentAreaStartX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_LEFT_COLUMN, dim.fontSize);
    float rightColumnX = contentAreaStartX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_RIGHT_COLUMN, dim.fontSize);

    // Helper lambda to add cycle control with < value > pattern - shared across all controls
    // If enabled is false, no click regions are added and muted color is used
    // If isOff is true, the value is muted (for "Off" state visual consistency)
    auto addCycleControl = [&](float baseX, float y, const char* value, int maxValueWidth,
                               ClickRegion::Type downType, ClickRegion::Type upType, BaseHud* targetHud,
                               bool enabled = true, bool isOff = false) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long valueColor = (enabled && !isOff) ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();

        // Left arrow "<" - only show when enabled
        if (enabled) {
            addString("<", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                downType, targetHud, 0, false, 0
            ));
        }
        currentX += charWidth * 2;  // "< " (spacing preserved even if arrow hidden)

        // Value with fixed width (left-aligned, padded)
        char paddedValue[32];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", maxValueWidth, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(maxValueWidth, dim.fontSize);

        // Right arrow " >" - only show when enabled
        if (enabled) {
            addString(" >", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                upType, targetHud, 0, false, 0
            ));
        }
    };

    // Helper lambda to add toggle control with < On/Off > pattern - for boolean settings
    // Both arrows trigger the same toggle action. If enabled is false, muted colors are used.
    // "Off" values are also muted for visual consistency.
    auto addToggleControl = [&](float baseX, float y, bool isOn,
                                ClickRegion::Type toggleType, BaseHud* targetHud,
                                uint32_t* bitfield = nullptr, uint32_t flag = 0,
                                bool enabled = true) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long valueColor = (enabled && isOn) ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();
        const char* value = isOn ? "On" : "Off";
        constexpr int VALUE_WIDTH = 3;  // "Off" is longest

        // Left arrow "<" - only show when enabled
        if (enabled) {
            addString("<", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            if (bitfield != nullptr) {
                // CHECKBOX type with bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, bitfield, flag, false, targetHud
                ));
            } else {
                // Simple toggle without bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, targetHud
                ));
            }
        }
        currentX += charWidth * 2;  // "< " (spacing preserved even if arrow hidden)

        // Value with fixed width
        char paddedValue[8];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", VALUE_WIDTH, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, dim.fontSize);

        // Right arrow " >" - only show when enabled
        if (enabled) {
            addString(" >", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            if (bitfield != nullptr) {
                // CHECKBOX type with bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, bitfield, flag, false, targetHud
                ));
            } else {
                // Simple toggle without bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, targetHud
                ));
            }
        }
    };

    auto addHudControls = [&](BaseHud* hud, bool enableTitle = true) -> float {
        // Save starting Y for right column (data toggles)
        float sectionStartY = currentY;

        // LEFT COLUMN: Basic controls
        float controlX = leftColumnX;
        float toggleX = controlX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);  // Align all toggles

        // Visibility toggle
        bool isVisible = hud->isVisible();
        addString("Visible", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        addToggleControl(toggleX, currentY, isVisible, ClickRegion::HUD_TOGGLE, hud);
        currentY += dim.lineHeightNormal;

        // Title toggle (can be disabled/grayed out)
        bool showTitle = enableTitle ? hud->getShowTitle() : false;
        addString("Title", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), enableTitle ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        addToggleControl(toggleX, currentY, showTitle, ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, enableTitle);
        currentY += dim.lineHeightNormal;

        // Background texture variant cycle (Off, 1, 2, ...)
        // Only enable if textures are available for this HUD
        bool hasTextures = !hud->getAvailableTextureVariants().empty();
        addString("Texture", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), hasTextures ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        char textureValue[16];
        int variant = hud->getTextureVariant();
        if (!hasTextures || variant == 0) {
            snprintf(textureValue, sizeof(textureValue), "Off");
        } else {
            snprintf(textureValue, sizeof(textureValue), "%d", variant);
        }
        addCycleControl(toggleX, currentY, textureValue, 4,
            ClickRegion::TEXTURE_VARIANT_DOWN, ClickRegion::TEXTURE_VARIANT_UP, hud, hasTextures);
        currentY += dim.lineHeightNormal;

        // Background opacity controls
        addString("Opacity", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char opacityValue[16];
        int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
        snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
        addCycleControl(toggleX, currentY, opacityValue, 4,
            ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud);
        currentY += dim.lineHeightNormal;

        // Scale controls
        addString("Scale", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char scaleValue[16];
        int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
        snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
        addCycleControl(toggleX, currentY, scaleValue, 4,
            ClickRegion::SCALE_DOWN, ClickRegion::SCALE_UP, hud);
        currentY += dim.lineHeightNormal;

        // Return the starting Y for right column (data toggles)
        return sectionStartY;
    };

    // Data toggle control - displays "Label: < On/Off >" format
    // labelWidth should accommodate the longest label in the group for alignment
    auto addDataToggle = [&](const char* label, uint32_t* bitfield, uint32_t flag, bool isRequired, BaseHud* hud, float yPos, int labelWidth = 12) {
        float dataX = rightColumnX;
        bool isChecked = (*bitfield & flag) != 0;
        bool enabled = !isRequired;

        // Label with padding
        char paddedLabel[32];
        snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::getNormal(),
            enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);

        // Toggle control
        float toggleX = dataX + PluginUtils::calculateMonospaceTextWidth(labelWidth, dim.fontSize);
        addToggleControl(toggleX, yPos, isChecked, ClickRegion::CHECKBOX, hud, bitfield, flag, enabled);
    };

    // Helper for grouped toggles that toggle multiple bits
    auto addGroupToggle = [&](const char* label, uint32_t* bitfield, uint32_t groupFlags, bool isRequired, BaseHud* hud, float yPos, int labelWidth = 12) {
        float dataX = rightColumnX;
        // Group is checked if all bits in group are set
        bool isChecked = (*bitfield & groupFlags) == groupFlags;
        bool enabled = !isRequired;

        // Label with padding
        char paddedLabel[32];
        snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::getNormal(),
            enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);

        // Toggle control
        float toggleX = dataX + PluginUtils::calculateMonospaceTextWidth(labelWidth, dim.fontSize);
        addToggleControl(toggleX, yPos, isChecked, ClickRegion::CHECKBOX, hud, bitfield, groupFlags, enabled);
    };

    // Render controls for active tab only
    BaseHud* activeHud = nullptr;
    float dataStartY = 0.0f;

    // Create layout context for extracted tabs
    // controlX is where the toggle controls start (labelX + 24 chars for Phase 3 descriptive labels)
    float controlX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(24, dim.fontSize);
    // Compute content area width (from contentAreaStartX to right edge of panel content)
    // This is used for row width calculations to ensure content doesn't extend past the panel
    float contentAreaWidth = (startX + panelWidth - dim.paddingH) - contentAreaStartX;
    SettingsLayoutContext layoutCtx(this, dim, leftColumnX, controlX, rightColumnX,
                                     contentAreaStartX, contentAreaWidth, currentY);

    if (const TabDescriptor* tabDesc = findTabDescriptor(m_activeTab); tabDesc && tabDesc->render) {
        // Route to the extracted per-tab renderer (settings_tab_*.cpp) via the registry.
        layoutCtx.currentY = currentY;   // Sync context cursor
        activeHud = tabDesc->render(layoutCtx);
        currentY = layoutCtx.currentY;   // Sync local cursor back
    } else {
        DEBUG_WARN_F("Invalid tab index: %d, defaulting to TAB_STANDINGS", m_activeTab);
        activeHud = m_standings;
    }

    currentY += sectionSpacing;

    // Draw hover highlight for TOOLTIP_ROW regions
    if (m_hoveredRegionIndex >= 0 && m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size())) {
        const ClickRegion& hoveredRegion = m_clickRegions[m_hoveredRegionIndex];
        if (hoveredRegion.type == ClickRegion::TOOLTIP_ROW) {
            // Draw highlight behind the hovered row (same opacity as tab hover). The
            // row region starts at the label text (labelX); extend the highlight one
            // char to the left so it has the same padding as the menu buttons (whose
            // background insets the text by a char). The right edge already reaches the
            // content edge, so only the left needs padding.
            SPluginQuad_t hoverQuad;
            float charPad = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            float hoverX = hoveredRegion.x - charPad, hoverY = hoveredRegion.y;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverQuad, hoverX, hoverY, hoveredRegion.width + charPad, hoveredRegion.height);
            hoverQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            hoverQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverQuad);
        }
    }

    // Render description or tooltip at the reserved position (replaces each other)
    // Calculate max width for word wrapping (contentAreaWidth - left margin from labels)
    float descTextWidth = layoutCtx.panelWidth - (layoutCtx.labelX - contentAreaStartX);
    int maxCharsPerLine = static_cast<int>(descTextWidth / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize));

    // Helper lambda to render up to 2 lines of word-wrapped text
    auto renderWrappedText = [&](const std::string& text, unsigned long color) {
        float lineY = layoutCtx.tooltipY;
        size_t lineStart = 0;
        int lineCount = 0;
        constexpr int MAX_LINES = 2;

        while (lineStart < text.length() && lineCount < MAX_LINES) {
            std::string wrappedLine;
            size_t lineEnd = lineStart + maxCharsPerLine;

            if (lineEnd >= text.length()) {
                // Last line - use remaining text
                wrappedLine = text.substr(lineStart);
                lineStart = text.length();
            } else {
                // Find last space before lineEnd for word wrap
                size_t lastSpace = text.rfind(' ', lineEnd);
                if (lastSpace != std::string::npos && lastSpace > lineStart) {
                    wrappedLine = text.substr(lineStart, lastSpace - lineStart);
                    lineStart = lastSpace + 1;  // Skip the space
                } else {
                    // No space found - hard break
                    wrappedLine = text.substr(lineStart, maxCharsPerLine);
                    lineStart += maxCharsPerLine;
                }

                // If this is the last line and there's more text, add ellipsis
                if (lineCount == MAX_LINES - 1 && lineStart < text.length()) {
                    if (wrappedLine.length() > 3) {
                        wrappedLine.resize(wrappedLine.length() - 3);
                        wrappedLine += "...";
                    }
                }
            }

            addString(wrappedLine.c_str(), layoutCtx.labelX, lineY, Justify::LEFT,
                Fonts::getNormal(), color, dim.fontSize);
            lineY += dim.lineHeightNormal;
            lineCount++;
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
    float buttonRowY = startY + backgroundHeight - dim.paddingV - dim.lineHeightNormal;
    float buttonAreaCenterX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
    float cw = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    bool settingsDirty = SettingsManager::getInstance().isDirty();

    // Size both buttons for the widest label they can show (Saved / Close = 5 chars + padding),
    // so the row layout is stable whether the Save button reads "Save" or "Saved".
    float saveButtonWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
    float closeButtonWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
    float buttonGap = cw;  // 1 character gap between buttons
    float totalWidth = saveButtonWidth + buttonGap + closeButtonWidth;
    float startButtonX = buttonAreaCenterX - totalWidth / 2.0f;

    // [Save] / [Saved] button
    float saveButtonX = startButtonX;
    if (settingsDirty) {
        // Unsaved changes: lit and clickable.
        size_t saveRegionIndex = m_clickRegions.size();
        m_clickRegions.push_back(ClickRegion(
            saveButtonX, buttonRowY, saveButtonWidth, dim.lineHeightNormal,
            ClickRegion::SAVE_BUTTON, nullptr, 0, false, 0
        ));
        {
            SPluginQuad_t bgQuad;
            float bgX = saveButtonX, bgY = buttonRowY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, saveButtonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(saveRegionIndex))
                ? ColorConfig::getInstance().getPositive()
                : PluginUtils::applyOpacity(ColorConfig::getInstance().getPositive(), 128.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        unsigned long saveTextColor = (m_hoveredRegionIndex == static_cast<int>(saveRegionIndex))
            ? ColorConfig::getInstance().getPrimary()
            : ColorConfig::getInstance().getPositive();
        addString("Save", saveButtonX + saveButtonWidth / 2.0f, buttonRowY, Justify::CENTER,
            Fonts::getNormal(), saveTextColor, dim.fontSize);
    } else {
        // Nothing to save: grayed out, not clickable (no click region -> no hover/click).
        {
            SPluginQuad_t bgQuad;
            float bgX = saveButtonX, bgY = buttonRowY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, saveButtonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), 64.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        addString("Saved", saveButtonX + saveButtonWidth / 2.0f, buttonRowY, Justify::CENTER,
            Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
    }

    // [Close] button
    float closeButtonX = saveButtonX + saveButtonWidth + buttonGap;
    size_t closeRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        closeButtonX, buttonRowY, closeButtonWidth, dim.lineHeightNormal,
        ClickRegion::CLOSE_BUTTON, nullptr, 0, false, 0
    ));
    {
        SPluginQuad_t bgQuad;
        float bgX = closeButtonX, bgY = buttonRowY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, closeButtonWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
            ? ColorConfig::getInstance().getAccent()
            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        m_quads.push_back(bgQuad);
    }
    unsigned long closeTextColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getAccent();
    addString("Close", closeButtonX + closeButtonWidth / 2.0f, buttonRowY, Justify::CENTER,
        Fonts::getNormal(), closeTextColor, dim.fontSize);

    // [Reset <TabName>] button - bottom left corner
    float resetTabButtonY = buttonRowY;
    char resetTabButtonText[32];
    snprintf(resetTabButtonText, sizeof(resetTabButtonText), "Reset %s", getTabName(m_activeTab));
    int resetTabButtonChars = static_cast<int>(strlen(resetTabButtonText));
    float resetTabButtonWidth = PluginUtils::calculateMonospaceTextWidth(resetTabButtonChars + 2, dim.fontSize);  // +1 char padding each side
    float resetTabButtonX = contentStartX;

    // Add click region first for hover check
    size_t resetTabRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        resetTabButtonX, resetTabButtonY, resetTabButtonWidth, dim.lineHeightNormal,
        ClickRegion::RESET_TAB_BUTTON, nullptr
    ));

    // Reset Tab button background
    {
        SPluginQuad_t bgQuad;
        float bgX = resetTabButtonX, bgY = resetTabButtonY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, resetTabButtonWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
            ? ColorConfig::getInstance().getAccent()
            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        m_quads.push_back(bgQuad);
    }

    // Reset Tab button text - PRIMARY when hovered, ACCENT when not (purple on purple)
    unsigned long resetTabTextColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getAccent();
    addString(resetTabButtonText, resetTabButtonX + resetTabButtonWidth / 2.0f, resetTabButtonY, Justify::CENTER,
        Fonts::getNormal(), resetTabTextColor, dim.fontSize);

    // Version + update status display - bottom right corner
    {
        float versionY = buttonRowY;
        float rightEdgeX = contentStartX + panelWidth - dim.paddingH - dim.paddingH;

        // Build version/status string based on update state
        char versionStr[64];
        unsigned long versionColor = ColorConfig::getInstance().getMuted();

        if (!UpdateChecker::getInstance().isEnabled()) {
            // Updates disabled - just show version
            snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
        } else {
            // Query UpdateChecker directly for current status (no duplicate state)
            UpdateChecker::Status status = UpdateChecker::getInstance().getStatus();

            switch (status) {
                case UpdateChecker::Status::IDLE:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    break;
                case UpdateChecker::Status::CHECKING:
                    snprintf(versionStr, sizeof(versionStr), "Checking...");
                    // Keep muted color (same as default)
                    break;
                case UpdateChecker::Status::UP_TO_DATE:
                    snprintf(versionStr, sizeof(versionStr), "v%s up-to-date", PluginConstants::PLUGIN_VERSION);
                    versionColor = ColorConfig::getInstance().getMuted();
                    break;
                case UpdateChecker::Status::UPDATE_AVAILABLE: {
                    // Get latest version directly from UpdateChecker
                    std::string latestVersion = UpdateChecker::getInstance().getLatestVersion();
                    // Show "installed" if downloader completed, otherwise "available"
                    if (UpdateDownloader::getInstance().getState() == UpdateDownloader::State::READY) {
                        snprintf(versionStr, sizeof(versionStr), "%s installed!", latestVersion.c_str());
                    } else {
                        // Drawn as a clickable green button below (like Reset / Check Now);
                        // the box + padding convey "button", no brackets needed.
                        snprintf(versionStr, sizeof(versionStr), "%s available!", latestVersion.c_str());
                    }
                    versionColor = ColorConfig::getInstance().getPositive();
                    break;
                }
                case UpdateChecker::Status::CHECK_FAILED:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    // Silent fail - just show version in muted
                    break;
            }
        }

        // versionWidth is the bare text width — used by the plain-text path below for
        // right-alignment. The clickable "available!" button adds +1 char of padding on
        // each side so its green box doesn't hug the text (matching the Reset button).
        float versionWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(versionStr)), dim.fontSize);
        float buttonWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(versionStr)) + 2, dim.fontSize);
        float versionX = rightEdgeX - buttonWidth;

        // Check if update is available and not yet installed. Gate on isEnabled() too: a
        // stale m_status (e.g. UPDATE_AVAILABLE from a prior check) survives when updates are
        // disabled — and the string above already collapses to the plain version in that case.
        // Without this gate the button branch would still draw the green "available" box/click
        // region around the plain version string (mixed state). Tying both to isEnabled() keeps
        // them consistent.
        bool updatesEnabled = UpdateChecker::getInstance().isEnabled();
        bool isUpdateAvailable = updatesEnabled &&
                           (UpdateChecker::getInstance().getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE);
        bool isInstalled = (isUpdateAvailable &&
                           UpdateDownloader::getInstance().getState() == UpdateDownloader::State::READY);

        // Always add click region for easter egg (and update navigation when available)
        size_t regionIndex = m_clickRegions.size();
        ClickRegion versionRegion;
        versionRegion.type = ClickRegion::VERSION_CLICK;
        versionRegion.y = versionY;
        versionRegion.height = dim.lineHeightNormal;

        // If update is available (not yet installed), show as clickable button
        if (isUpdateAvailable && !isInstalled) {
            versionRegion.x = versionX;
            versionRegion.width = buttonWidth;
            m_clickRegions.push_back(versionRegion);

            bool isHovered = m_hoveredRegionIndex == static_cast<int>(regionIndex);

            // Button background
            SPluginQuad_t bgQuad;
            float bgX = versionX, bgY = versionY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, buttonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = isHovered ? ColorConfig::getInstance().getPositive()
                : PluginUtils::applyOpacity(ColorConfig::getInstance().getPositive(), 0.5f);
            m_quads.push_back(bgQuad);

            // Text color: positive (green) when unhovered for contrast, primary when hovered
            versionColor = isHovered ? ColorConfig::getInstance().getPrimary()
                : ColorConfig::getInstance().getPositive();

            // Draw centered in button
            float textX = versionX + buttonWidth * 0.5f;
            addString(versionStr, textX, versionY, Justify::CENTER,
                Fonts::getNormal(), versionColor, dim.fontSize);
        } else {
            // Regular text (not a button) - right aligned, but still clickable for easter egg
            float textX = rightEdgeX - versionWidth;
            versionRegion.x = textX;
            versionRegion.width = versionWidth;
            m_clickRegions.push_back(versionRegion);

            addString(versionStr, textX, versionY, Justify::LEFT,
                Fonts::getNormal(), versionColor, dim.fontSize);
        }
    }
}

