// ============================================================================
// core/color_config.h
// User-configurable color settings for HUD elements
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include "plugin_utils.h"

// Predefined color palette that users can cycle through
// Note: Game uses ABGR format: (A << 24) | (B << 16) | (G << 8) | R
namespace ColorPalette {
    // Basic colors (using makeColor for correct ABGR format)
    constexpr unsigned long WHITE = PluginUtils::makeColor(255, 255, 255);      // #ffffff
    constexpr unsigned long LIGHT_GRAY = PluginUtils::makeColor(190, 190, 190); // #bebebe
    constexpr unsigned long GRAY = PluginUtils::makeColor(140, 140, 140);       // #8c8c8c
    // GRAPHITE, not "Dark Gray": #646464 is exactly what the Graphite pit board and
    // gamepad skins are painted in, so this is the ninth pack colour -- it just
    // happened to be in the palette already, under a name no player would search
    // for when trying to match a board. A fourth rename in the same spirit as the
    // three below, and the reason there is no separate GRAPHITE entry (a second
    // constant with this value is a duplicate `case` label, i.e. a build error).
    constexpr unsigned long GRAPHITE = PluginUtils::makeColor(100, 100, 100);  // #646464 - Stark
    constexpr unsigned long BLACK = PluginUtils::makeColor(0, 0, 0);            // #000000

    // Accent colors
    constexpr unsigned long RED = PluginUtils::makeColor(255, 0, 0);            // #ff0000
    constexpr unsigned long GREEN = PluginUtils::makeColor(0, 255, 0);          // #00ff00
    constexpr unsigned long BLUE = PluginUtils::makeColor(0, 0, 255);           // #0000ff
    // RENAMED, not recoloured: these three kept their values and gave up their
    // names to the pack colours below, which are the ones a player can actually
    // match a pit board or a gamepad to. A name is display-only here (colours
    // persist as hex and getColorName maps value -> label), so a rename costs
    // nobody their setting -- whereas repointing one of these at a pack colour
    // would silently move every WARNING slot, every yellow flag and the gear
    // readout, and cycleColor snaps an off-palette colour to White on the first
    // click. The rename landed on its own first, so the compiler had to find
    // every call site before the names were reused.
    constexpr unsigned long BRIGHT_YELLOW = PluginUtils::makeColor(255, 255, 0); // #ffff00
    constexpr unsigned long AMBER = PluginUtils::makeColor(255, 165, 0);         // #ffa500
    constexpr unsigned long AQUA = PluginUtils::makeColor(0, 255, 255);          // #00ffff
    constexpr unsigned long PURPLE = PluginUtils::makeColor(200, 0, 255);       // #c800ff
    constexpr unsigned long PINK = PluginUtils::makeColor(255, 105, 180);       // #ff69b4
    constexpr unsigned long BROWN = PluginUtils::makeColor(139, 90, 43);        // #8b5a2b

    // PACK COLOURS -- the exact hues the shipped pit board and gamepad skins are
    // painted in, under the names those packs are shown by.
    //
    // The point is EXACT matching, which is why they are here rather than
    // approximated by the accent colours above: a player who runs the Crimson
    // board can now set their primary text to the same #de1c21 and have it agree,
    // instead of eyeballing a nearby red. Each value is a BrandColors entry
    // (the skins were tinted from that table); the static_asserts in
    // color_config.cpp are what stop the two drifting apart, since this header
    // cannot include plugin_constants.h -- that include runs the other way.
    //
    // NO GRAPHITE. The Graphite skin is #646464, which is DARK_GRAY above,
    // bit-for-bit -- so it is already selectable and a second entry would be a
    // duplicate `case` label in getColorName, i.e. a build error rather than a
    // subtle one. The palette is a set of colours, not a list of skins.
    constexpr unsigned long CRIMSON = PluginUtils::makeColor(222, 28, 33);      // #de1c21 - Honda
    constexpr unsigned long ORANGE = PluginUtils::makeColor(255, 102, 0);       // #ff6600 - KTM
    constexpr unsigned long YELLOW = PluginUtils::makeColor(254, 242, 0);       // #fef200 - Suzuki
    constexpr unsigned long LIME = PluginUtils::makeColor(102, 204, 51);        // #66cc33 - Kawasaki
    constexpr unsigned long CYAN = PluginUtils::makeColor(0, 175, 241);         // #00aff1 - TM
    constexpr unsigned long NAVY = PluginUtils::makeColor(39, 58, 96);          // #273a60 - Husqvarna
    constexpr unsigned long ROYAL = PluginUtils::makeColor(27, 62, 144);        // #1b3e90 - Yamaha
    constexpr unsigned long SILVER = PluginUtils::makeColor(200, 200, 200);     // #c8c8c8 - Alta

    // The full palette for cycling through. The pack colours are APPENDED as a
    // contiguous block rather than interleaved by hue: it keeps every existing
    // colour where it was in the cycle, and it puts the eight a player would be
    // matching a board or a pad to next to each other, which is how they are used.
    //
    // Growing this also moves TrackedRidersManager::getNextColor(), which is
    // `count % size` -- already-assigned rider colours are stored and unaffected,
    // but the Nth new one differs from what it would have been.
    constexpr std::array<unsigned long, 22> ALL_COLORS = {
        WHITE, LIGHT_GRAY, GRAY, GRAPHITE, BLACK,
        RED, GREEN, BLUE, BRIGHT_YELLOW, AMBER, AQUA, PURPLE, PINK, BROWN,
        CRIMSON, ORANGE, YELLOW, LIME, CYAN, NAVY, ROYAL, SILVER
    };

    // Color names for display in settings UI.
    //
    // INLINE, so the palette can be exercised by a unit test: both of these are
    // pure functions over the constants above, but they used to live in
    // color_config.cpp beside the ColorConfig singleton, which drags in
    // AssetManager and the logger and does not link into the headless unit build.
    // That is why a palette this many call sites read had no coverage at all --
    // the pieces worth testing were on the wrong side of a translation unit. Same
    // move the project makes elsewhere (blue_flag_detect.h, proximity_tuning.h).
    inline const char* getColorName(unsigned long color) {
        switch (color) {
            case WHITE:      return "White";
            case LIGHT_GRAY: return "Light Gray";
            case GRAY:       return "Gray";
            case GRAPHITE:   return "Graphite";
            case BLACK:      return "Black";
            case RED:        return "Red";
            case GREEN:      return "Green";
            case BLUE:       return "Blue";
            case BRIGHT_YELLOW: return "Bright Yellow";
            case AMBER:         return "Amber";
            case AQUA:          return "Aqua";
            case PURPLE:     return "Purple";
            case PINK:       return "Pink";
            case BROWN:      return "Brown";
            // The pack colours. These labels are the CONTRACT: they are what the
            // Pitboard/Gamepad Texture column shows for the matching skin, which
            // is the whole point of the eight entries. palette_test.cpp censuses
            // them against the shipped packs, so renaming a pack without renaming
            // the colour (or the reverse) fails there rather than quietly leaving
            // a player unable to find the match.
            case CRIMSON:    return "Crimson";
            case ORANGE:     return "Orange";
            case YELLOW:     return "Yellow";
            case LIME:       return "Lime";
            case CYAN:       return "Cyan";
            case NAVY:       return "Navy";
            case ROYAL:      return "Royal";
            case SILVER:     return "Silver";
            default:         return "Custom";
        }
    }

    // Get index of a color in the palette (-1 if not found)
    inline int getColorIndex(unsigned long color) {
        for (size_t i = 0; i < ALL_COLORS.size(); ++i) {
            if (ALL_COLORS[i] == color) {
                return static_cast<int>(i);
            }
        }
        return -1;  // Not in palette
    }
}

// Color slot identifiers for the 10 configurable colors
enum class ColorSlot {
    PRIMARY = 0,      // Main text color
    SECONDARY,        // Secondary text color
    TERTIARY,         // Tertiary text color
    MUTED,            // Muted/disabled text color
    BACKGROUND,       // Background color
    POSITIVE,         // Positive/good indicator (e.g., faster times)
    WARNING,          // Warning indicator
    NEUTRAL,          // Neutral indicator
    NEGATIVE,         // Negative/bad indicator (e.g., slower times)
    ACCENT,           // Button/interactive element backgrounds
    COUNT
};

// Slot from the ini key that names it ("primary"), or -1. The names are the same
// ones the settings file's [Colors] section uses, so a theme's palette and a user's
// override are written identically -- one vocabulary, two files.
int colorSlotFromName(const char* name);

class ColorConfig {
public:
    static ColorConfig& getInstance();

    // Get color for a specific slot
    unsigned long getColor(ColorSlot slot) const;

    // Convenience getters for each slot
    unsigned long getPrimary() const { return getColor(ColorSlot::PRIMARY); }
    unsigned long getSecondary() const { return getColor(ColorSlot::SECONDARY); }
    unsigned long getTertiary() const { return getColor(ColorSlot::TERTIARY); }
    unsigned long getMuted() const { return getColor(ColorSlot::MUTED); }
    unsigned long getBackground() const { return getColor(ColorSlot::BACKGROUND); }
    unsigned long getPositive() const { return getColor(ColorSlot::POSITIVE); }
    unsigned long getWarning() const { return getColor(ColorSlot::WARNING); }
    unsigned long getNeutral() const { return getColor(ColorSlot::NEUTRAL); }
    unsigned long getNegative() const { return getColor(ColorSlot::NEGATIVE); }
    unsigned long getAccent() const { return getColor(ColorSlot::ACCENT); }

    // Set color for a specific slot. This is a USER OVERRIDE: it pins the slot, so
    // it survives a theme change and is what gets written to the settings file.
    void setColor(ColorSlot slot, unsigned long color);

    // Whether the user has pinned this slot, i.e. whether getColor() is answering
    // from the override rather than from the theme or the built-in default.
    //
    // Load-bearing for PERSISTENCE, not just for the UI: only pinned slots are
    // written, so a palette the user never touched keeps following whatever theme
    // they pick. Writing all ten unconditionally is what would freeze the first
    // theme's colours into the settings file forever.
    bool isOverridden(ColorSlot slot) const;
    void clearOverride(ColorSlot slot);

    // The colour in effect for a slot when the user has NOT pinned it: the active
    // theme's if it states one, else the built-in default.
    static unsigned long getThemeOrDefaultColor(ColorSlot slot);

    // Cycle to next/previous color in the palette for a slot
    void cycleColor(ColorSlot slot, bool forward = true);

    // Reset all colors to defaults
    void resetToDefaults();


    // Get slot name for display
    static const char* getSlotName(ColorSlot slot);

    // Get default color for a slot
    static unsigned long getDefaultColor(ColorSlot slot);

private:
    ColorConfig();
    ~ColorConfig() = default;
    ColorConfig(const ColorConfig&) = delete;
    ColorConfig& operator=(const ColorConfig&) = delete;

    std::array<unsigned long, static_cast<size_t>(ColorSlot::COUNT)> m_colors;
    // Per-slot: has the user set this, or is it following the theme? See
    // isOverridden().
    std::array<bool, static_cast<size_t>(ColorSlot::COUNT)> m_overridden{};
};
