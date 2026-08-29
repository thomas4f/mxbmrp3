// ============================================================================
// core/color_config.cpp
// User-configurable color settings for HUD elements
// ============================================================================
#include "color_config.h"
#include "asset_manager.h"
#include "plugin_constants.h"   // BrandColors, for the pack-colour static_asserts below
#include "ui_config.h"
#include "../diagnostics/logger.h"

// Memoised theme lookup for this config's hot path.
//
// getThemeByName() is a linear string scan, and BaseHud::activeTheme()'s comment
// already calls it out as needing a memo -- it just memoised the HUD side and left
// these two, which are the ones actually in the inner loops: getColor runs per
// column per rider on a full grid, and getFont at every addString.
//
// Keyed on the same global generation counter, so discovery, a config reload and a
// change of selected theme all invalidate it; the name is compared too because the
// per-HUD override can ask for a different theme than the last caller did.
static const ThemeAsset* mxbResolveThemeMemo(const std::string& name) {
    static const ThemeAsset* s_cached = nullptr;
    static unsigned int s_gen = 0;              // 0 is never a live generation
    static std::string s_name;
    const unsigned int gen = mxbThemeGeneration();
    if (gen == s_gen && name == s_name) return s_cached;
    s_cached = AssetManager::getInstance().getThemeByName(name);
    s_gen = gen;
    s_name = name;
    return s_cached;
}


// ONE DEFINITION PER VALUE, enforced. The eight pack colours in color_config.h
// are written as literals because that header cannot include plugin_constants.h
// -- the include runs the other way (SemanticColors is built from ColorPalette).
// A .cpp has no such problem, so the equality is asserted here instead: the
// shipped skins were tinted from BrandColors, and if either side is edited alone
// this stops compiling rather than leaving a player's "Crimson" text a shade off
// their Crimson board.
//
// Graphite has no entry of its OWN: #646464 was already in the palette (as
// "Dark Gray" until this change renamed it), so the ninth skin colour is the
// one that needed a rename rather than an addition.
namespace {
using namespace PluginConstants::BrandColors;
static_assert(ColorPalette::CRIMSON == HONDA,     "Crimson must be the Honda hue the crimson skins use");
static_assert(ColorPalette::ORANGE  == KTM,       "Orange must be the KTM hue the orange skins use");
static_assert(ColorPalette::YELLOW  == SUZUKI,    "Yellow must be the Suzuki hue the yellow skins use");
static_assert(ColorPalette::LIME    == KAWASAKI,  "Lime must be the Kawasaki hue the lime skins use");
static_assert(ColorPalette::CYAN    == TM,        "Cyan must be the TM hue the cyan skins use");
static_assert(ColorPalette::NAVY    == HUSQVARNA, "Navy must be the Husqvarna hue the navy skins use");
static_assert(ColorPalette::ROYAL   == YAMAHA,    "Royal must be the Yamaha hue the royal skins use");
static_assert(ColorPalette::SILVER  == ALTA,      "Silver must be the Alta hue the silver skins use");
static_assert(ColorPalette::GRAPHITE == STARK,    "Graphite must be the Stark hue the graphite skins use");
}  // namespace

ColorConfig& ColorConfig::getInstance() {
    static ColorConfig instance;
    return instance;
}

ColorConfig::ColorConfig() {
    resetToDefaults();
}

// THE PRECEDENCE, in one place: built-in default -> the active theme's -> the
// user's. Same three steps the layout vocabulary has, so "where did this come from"
// has one answer across the whole appearance surface.
unsigned long ColorConfig::getThemeOrDefaultColor(ColorSlot slot) {
    const size_t index = static_cast<size_t>(slot);
    if (index >= static_cast<size_t>(ColorSlot::COUNT)) return ColorPalette::WHITE;
    // The APPEARANCE theme, deliberately, not a per-HUD override. A HUD's theme
    // override picks its sprites and its layout; letting it also repaint that one
    // panel's text would make a mixed-theme screen unreadable rather than themed.
    const std::string& name = UiConfig::getInstance().getThemeName();
    if (!name.empty()) {
        if (const ThemeAsset* theme = mxbResolveThemeMemo(name)) {
            if (theme->colorSet[index]) return theme->colors[index];
        }
    }
    return getDefaultColor(slot);
}

unsigned long ColorConfig::getColor(ColorSlot slot) const {
    size_t index = static_cast<size_t>(slot);
    if (index >= m_colors.size()) return ColorPalette::WHITE;  // Fallback
    return m_overridden[index] ? m_colors[index] : getThemeOrDefaultColor(slot);
}

bool ColorConfig::isOverridden(ColorSlot slot) const {
    const size_t index = static_cast<size_t>(slot);
    return index < m_overridden.size() && m_overridden[index];
}

void ColorConfig::clearOverride(ColorSlot slot) {
    const size_t index = static_cast<size_t>(slot);
    if (index < m_overridden.size()) {
        m_overridden[index] = false;
        m_colors[index] = getThemeOrDefaultColor(slot);
    }
}

void ColorConfig::setColor(ColorSlot slot, unsigned long color) {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colors.size()) {
        m_colors[index] = color;
        m_overridden[index] = true;
        DEBUG_INFO_F("ColorConfig: %s set to %s (0x%08lX)",
            getSlotName(slot), ColorPalette::getColorName(color), color);
    }
}

void ColorConfig::cycleColor(ColorSlot slot, bool forward) {
    size_t slotIndex = static_cast<size_t>(slot);
    if (slotIndex >= m_colors.size()) return;

    // THE RING HAS ONE MORE STOP THAN THE PALETTE: "Default", meaning the slot is
    // not pinned and follows the active theme (or the built-in value where the theme
    // says nothing). It is a real position, entered by wrapping off either end.
    //
    // Without it, one press of an arrow was irreversible. A theme's colour is almost
    // never a palette entry -- Inspector's panel grey and ink are not -- so there was
    // no index to cycle back TO, and the only way to un-pin a slot was the reset that
    // clears all ten. Reported as exactly that: cycle away from a theme's colour and
    // there is no way back to it.
    //
    // constexpr + static_assert rather than a runtime guard: ALL_COLORS is a
    // constexpr std::array, so `if (paletteSize <= 0)` is a constant condition --
    // MSVC rejects it under C4127-as-error, and it was never a real check anyway.
    // The requirement is real, so it is stated where it can be proved.
    constexpr int paletteSize = static_cast<int>(ColorPalette::ALL_COLORS.size());
    static_assert(paletteSize > 0, "the colour ring needs at least one palette entry");

    // On the Default stop: step onto the first (or last) palette entry.
    if (!isOverridden(slot)) {
        setColor(slot, ColorPalette::ALL_COLORS[forward ? 0 : paletteSize - 1]);
        return;
    }

    // The EFFECTIVE colour, not the raw slot. Once a theme can supply a palette, an
    // un-overridden slot's raw value is whatever was last written there and is not
    // what is on screen -- so cycling from it started from the wrong entry.
    int paletteIndex = ColorPalette::getColorIndex(getColor(slot));
    if (paletteIndex < 0) {
        // Pinned to something off-palette (a hand-edited ini). Land on a known entry
        // rather than guessing a neighbour it has no position among.
        setColor(slot, ColorPalette::ALL_COLORS[0]);
        return;
    }

    paletteIndex += forward ? 1 : -1;
    if (paletteIndex >= paletteSize || paletteIndex < 0) {
        // Off the end in either direction -> the Default stop. clearOverride is what
        // makes the slot follow the theme again AND what stops it being written to
        // the settings file (see isOverridden).
        clearOverride(slot);
        return;
    }

    // Through setColor(), which is what MARKS THE SLOT AS OVERRIDDEN. Writing
    // m_colors directly stored the new value where getColor() would never read it:
    // the slot was still following the theme, so the Appearance menu's colour arrows
    // did nothing at all -- the value changed and the screen did not.
    setColor(slot, ColorPalette::ALL_COLORS[paletteIndex]);
}

void ColorConfig::resetToDefaults() {
    // Clears the OVERRIDES, which is what "reset" means once a theme can state a
    // palette: the slots go back to following the theme, not back to the built-in
    // white-on-grey. Pinning them to the built-ins would make "reset" the one action
    // that permanently opts you out of every theme's colours.
    for (size_t i = 0; i < m_colors.size(); ++i) {
        m_overridden[i] = false;
        m_colors[i] = getThemeOrDefaultColor(static_cast<ColorSlot>(i));
    }
    DEBUG_INFO("ColorConfig: Reset to defaults");
}

// The ini spelling of a slot, lower case. Derived from getSlotName() rather than
// listed twice: the two would drift the day a slot is renamed, and a theme's
// palette silently losing a colour is exactly the failure this feature must not have.
int colorSlotFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
        const char* slot = ColorConfig::getSlotName(static_cast<ColorSlot>(i));
        size_t k = 0;
        for (; slot[k] && name[k]; ++k) {
            const char a = (slot[k] >= 'A' && slot[k] <= 'Z') ? static_cast<char>(slot[k] + 32) : slot[k];
            const char b = (name[k] >= 'A' && name[k] <= 'Z') ? static_cast<char>(name[k] + 32) : name[k];
            if (a != b) break;
        }
        if (slot[k] == '\0' && name[k] == '\0') return i;
    }
    return -1;
}

const char* ColorConfig::getSlotName(ColorSlot slot) {
    switch (slot) {
        case ColorSlot::PRIMARY:    return "Primary";
        case ColorSlot::SECONDARY:  return "Secondary";
        case ColorSlot::TERTIARY:   return "Tertiary";
        case ColorSlot::MUTED:      return "Muted";
        case ColorSlot::BACKGROUND: return "Background";
        case ColorSlot::POSITIVE:   return "Positive";
        case ColorSlot::WARNING:    return "Warning";
        case ColorSlot::NEUTRAL:    return "Neutral";
        case ColorSlot::NEGATIVE:   return "Negative";
        case ColorSlot::ACCENT:     return "Accent";
        default:                    return "Unknown";
    }
}

unsigned long ColorConfig::getDefaultColor(ColorSlot slot) {
    // Default values match the original TextColors and SemanticColors
    switch (slot) {
        case ColorSlot::PRIMARY:    return ColorPalette::WHITE;       // #ffffff
        case ColorSlot::SECONDARY:  return ColorPalette::LIGHT_GRAY;  // #bebebe
        case ColorSlot::TERTIARY:   return ColorPalette::GRAY;        // #8c8c8c
        case ColorSlot::MUTED:      return ColorPalette::GRAPHITE;    // #646464
        case ColorSlot::BACKGROUND: return ColorPalette::BLACK;       // #000000
        case ColorSlot::POSITIVE:   return ColorPalette::GREEN;       // #00ff00
        case ColorSlot::WARNING:    return ColorPalette::AMBER;         // #ffa500
        case ColorSlot::NEUTRAL:    return ColorPalette::BRIGHT_YELLOW; // #ffff00
        case ColorSlot::NEGATIVE:   return ColorPalette::RED;         // #ff0000
        case ColorSlot::ACCENT:     return ColorPalette::PINK;        // #ff69b4
        default:                    return ColorPalette::WHITE;
    }
}
