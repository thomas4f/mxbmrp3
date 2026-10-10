// ============================================================================
// hud/settings_hud_tabbar.cpp
// The tab bar's per-row pieces: each row's on/off state and how its identity
// icon and toggle are drawn. Part of SettingsHud::buildTabBar()
// (settings_hud_render.cpp, which owns the tab registry and walks it), split out
// so that file keeps to its budget; click handling is settings_hud_input.cpp.
// ============================================================================
#include "settings_hud.h"
#include "helmet_overlay_hud.h"
#include "../core/asset_manager.h"
#include "../core/achievement_manager.h"
#include "../core/color_config.h"
#include "../core/director_manager.h"
#include "../core/hud_manager.h"
#include "../core/plugin_utils.h"
#include "../core/spotter_manager.h"
#include "../core/ui_config.h"
#include "../core/update_checker.h"
#include "../core/xinput_reader.h"

using namespace PluginConstants;

bool SettingsHud::tabToggleState(int tabId, BaseHud* tabHud, bool* enabled) const {
    // Per-HUD checkboxes show the focused surface's on/off (companion vs game); the
    // manager/global toggles (widgets/rumble/updates/director) are shared, not
    // decoupled.
    //
    // The helmet reads its GAME flag even on the companion, and that is correct
    // rather than an oversight: it never renders on the companion at all
    // (BaseHud::rendersOnCompanion), so the game flag is its only visibility.
    if (tabHud) {
        *enabled = tabHud->isVisibleOnActiveSurface();
    } else if (tabId == TAB_WIDGETS) {
        *enabled = HudManager::getInstance().areWidgetsEnabled();
    } else if (tabId == TAB_RUMBLE) {
        *enabled = XInputReader::getInstance().getGlobalRumbleConfig().enabled;
    } else if (tabId == TAB_HELMET) {
        *enabled = m_helmetOverlay && m_helmetOverlay->isVisible();
    } else if (tabId == TAB_UPDATES) {
        *enabled = UpdateChecker::getInstance().isEnabled();
    } else if (tabId == TAB_DIRECTOR) {
        *enabled = DirectorManager::getInstance().isEnabled();
    } else if (tabId == TAB_SPOTTER) {
        // The SPOKEN-AUDIO master, matching the tab's own first toggle.
        // Subtitles are deliberately not part of this reading: they are a
        // standalone mode (silent, captioned), so a lit checkbox here
        // means "you will hear it".
        *enabled = SpotterManager::getInstance().isEnabled();
    } else if (tabId == TAB_ACHIEVEMENTS) {
        // The toast master. Tracking is unconditional; the checkbox says
        // whether an unlock is shown.
        *enabled = AchievementManager::getInstance().isToastsEnabled();
    } else {
        return false;   // General, Appearance... : no toggle on the row
    }
    return true;
}

// The click region type a tab's on/off goes through, and the HUD it names --
// wherever the toggle is drawn: the sidebar row's icon and the More page's
// < On/Off >. One table, so the two cannot drift. Only meaningful where
// tabToggleState() says the tab has a toggle.
//
// Each master toggle is handled by the COMMON switch, not a tab-scoped handler:
// a sidebar checkbox is clicked from whatever tab is open, so a tab-scoped
// handler would leave it dead everywhere but its own tab.
SettingsHud::ClickRegion::Type SettingsHud::tabToggleType(int tabId, BaseHud* tabHud,
                                                          BaseHud** target) const {
    *target = nullptr;
    if (tabHud) { *target = tabHud; return ClickRegion::HUD_TOGGLE; }
    switch (tabId) {
        case TAB_WIDGETS:      return ClickRegion::WIDGETS_TOGGLE;
        case TAB_RUMBLE:       return ClickRegion::RUMBLE_TOGGLE;
        case TAB_HELMET:       *target = m_helmetOverlay; return ClickRegion::HELMET_OVERLAY_TOGGLE;
        case TAB_UPDATES:      return ClickRegion::UPDATE_CHECK_TOGGLE;
        case TAB_DIRECTOR:     return ClickRegion::DIRECTOR_ENABLE_TOGGLE;
        // The spoken-audio master, the same region type as the tab's own toggle.
        case TAB_SPOTTER:      return ClickRegion::SPOTTER_ENABLED_TOGGLE;
        // The achievement-toast master.
        case TAB_ACHIEVEMENTS: return ClickRegion::ACHIEVEMENTS_TOASTS_TOGGLE;
        default:               return ClickRegion::TAB;   // unreachable with a toggle
    }
}

// A tab's identity icon: its HUD's own, else the master toggle's, else the
// registry's sectionIcon. nullptr = none.
const char* SettingsHud::tabIconName(int tabId, BaseHud* tabHud) const {
    if (tabHud) return tabHud->getIconName();
    switch (tabId) {
        case TAB_WIDGETS:      return "hud-widgets";
        case TAB_RUMBLE:       return "hud-rumble";
        // Game-specific: the helmet shape differs per game.
#if defined(GAME_MXBIKES)
        case TAB_HELMET:       return "hud-helmet-mx";
#else
        case TAB_HELMET:       return "hud-helmet";
#endif
        case TAB_UPDATES:      return "hud-updates";
        // hud-video, not the outlined marker "video": every other row in this list
        // is a flat hud-* glyph, and the marker set carries a baked 2px outline for
        // contrast over the track (see assets/icons/README.md), which read as one
        // heavier icon among twenty. Both files exist; this is the identity one.
        case TAB_DIRECTOR:     return "hud-video";
        // A headset, for the voice in your ear.
        case TAB_SPOTTER:      return "hud-spotter";
        // A medal, the flat copy of the podium marker.
        case TAB_ACHIEVEMENTS: return "hud-achievements";
        default: {
            const TabDescriptor* desc = findTabDescriptor(tabId);
            return desc ? desc->sectionIcon : nullptr;
        }
    }
}

// ==========================================================================
// Tab-bar drawing helpers, split out of rebuildRenderData().
//
// Members, not lambdas: each needs TWO parameters beyond its original arguments
// (the scaled dimensions and the checkbox cell width). Everything else they touch
// -- addIcon, addString, m_clickRegions, m_hoveredRegionIndex -- is a member,
// which a member function gets for free and a lambda would have to capture by
// reference.
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
        // amber disappears and the selected tab reads as having no icon at all.
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
        // Text checkbox when the tab has no icon to stand in for one (UI icons off)
        addString(enabled ? "[x]" : "[ ]", x, y, Justify::LEFT,
            Fonts::getNormal(), iconColor, dim.fontSize);
    }

}

// Rumble's split-effect disclosure caret, the same caret-up the dropdown boxes
// draw: turned 90 degrees (right, closed) or 180 (down, open). With UI icons off it draws nothing and returns false, so the
// caller puts its text stand-in in the caret's place. With them on, a missing
// sprite also draws nothing (but returns true): an install without its icons
// gets the broken-install warning (HudManager::buildInstallWarning) instead.
bool SettingsHud::addDisclosureCaret(float cx, float cy, float halfSize, bool open,
                                     unsigned long color) {
    if (!UiConfig::getInstance().getTitleIcons()) return false;
    const int caret = AssetManager::getInstance().getIconSpriteIndex("caret-up");
    if (caret <= 0) return true;
    // Rotation is in uniform space: (cos, sin) of the turn, clockwise on screen.
    addRotatedSpriteQuad(cx, cy, halfSize, open ? -1.0f : 0.0f, open ? 0.0f : 1.0f, caret, color);
    return true;
}
