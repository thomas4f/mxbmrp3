// ============================================================================
// core/settings_serde.h
// Free helper functions for the settings layer that do NOT reference concrete
// HUD classes: generic enum<->string converters, value validators, bitmask
// (named-key) primitives, base-HUD capture/apply/write, icon-shape lookup and
// section-name parsing. The per-HUD-typed helpers (enum converters for
// StandingsHud/MapHud/... and the per-HUD column/row bitmask save/loads) live
// in settings_serde_hud.h, which includes this file — so a TU that touches no
// concrete HUD type (settings_hud_profiles.cpp) no longer rebuilds when a HUD
// header changes. Extracted from settings_manager.cpp so the SettingsManager
// translation units and the per-HUD registry (settings_hud_registry) share ONE
// definition.
//
// These live in `namespace Settings` alongside the key constants (settings_keys.h)
// so their function-local `using namespace Keys::X;` directives resolve into
// `Settings` (nested below global) — this is load-bearing: at global scope those
// directives would leak names like RecordsCols::DATE into the global namespace and
// collide with <windows.h> (typedef double DATE). Keep helpers and Keys co-located.
//
// Definitions are `inline` (header-defined) so multiple TUs may include this
// without ODR violations; the linker folds the COMDAT copies.
// ============================================================================
#pragma once

#include "settings_manager.h"
#include "settings_keys.h"
#include "profile_manager.h"
#include "plugin_constants.h"
#include "ui_config.h"
#include "color_config.h"
#include "font_config.h"
#include "asset_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/base_hud.h"
#include "../game/game_config.h"
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>

namespace Settings {

    // ========================================================================
    // Generic enum<->string converters (types from ui_config.h)
    // ========================================================================
    // TemperatureUnit
    inline const char* tempUnitToString(TemperatureUnit unit) {
        switch (unit) {
            case TemperatureUnit::CELSIUS: return "CELSIUS";
            case TemperatureUnit::FAHRENHEIT: return "FAHRENHEIT";
            default: return "CELSIUS";
        }
    }

    inline TemperatureUnit stringToTempUnit(const std::string& str, TemperatureUnit defaultVal = TemperatureUnit::CELSIUS) {
        if (str == "CELSIUS") return TemperatureUnit::CELSIUS;
        if (str == "FAHRENHEIT") return TemperatureUnit::FAHRENHEIT;
        DEBUG_WARN_F("Unknown TemperatureUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // PBScope
    inline const char* pbScopeToString(PBScope scope) {
        switch (scope) {
            case PBScope::BIKE: return "BIKE";
            case PBScope::CATEGORY: return "CATEGORY";
            default: return "BIKE";
        }
    }

    inline PBScope stringToPBScope(const std::string& str, PBScope defaultVal = PBScope::CATEGORY) {
        if (str == "BIKE") return PBScope::BIKE;
        if (str == "CATEGORY") return PBScope::CATEGORY;
        DEBUG_WARN_F("Unknown PBScope '%s', using default", str.c_str());
        return defaultVal;
    }

    // DisplayTarget (where the HUD is drawn: in-game / companion window / both)
    inline const char* displayTargetToString(DisplayTarget t) {
        switch (t) {
            case DisplayTarget::COMPANION: return "COMPANION";
            case DisplayTarget::BOTH:      return "BOTH";
            case DisplayTarget::IN_GAME:   return "IN_GAME";
            default: return "IN_GAME";
        }
    }

    inline DisplayTarget stringToDisplayTarget(const std::string& str, DisplayTarget defaultVal = DisplayTarget::IN_GAME) {
        if (str == "IN_GAME")   return DisplayTarget::IN_GAME;
        if (str == "COMPANION") return DisplayTarget::COMPANION;
        if (str == "BOTH")      return DisplayTarget::BOTH;
        DEBUG_WARN_F("Unknown DisplayTarget '%s', using default", str.c_str());
        return defaultVal;
    }

    // ========================================================================

    // Parse a float from an INI string, rejecting non-finite results. A malformed
    // string still throws via std::stof (handled by the caller's existing catch), but
    // "nan"/"inf" parse successfully and would otherwise slip past every "< MIN || > MAX"
    // range check (NaN compares false both ways) and poison live quad/rumble math — and
    // auto-save would re-persist the bad value. Returns `fallback` for non-finite input.
    inline float parseFiniteFloat(const std::string& value, float fallback = 0.0f) {
        float parsed = std::stof(value, nullptr);
        return std::isfinite(parsed) ? parsed : fallback;
    }

    // Validation helper functions
    inline float validateScale(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite scale value, using 1.0");
            return 1.0f;
        }
        if (value < MIN_SCALE || value > MAX_SCALE) {
            DEBUG_WARN_F("Invalid scale value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_SCALE, MAX_SCALE);
            return (value < MIN_SCALE) ? MIN_SCALE : MAX_SCALE;
        }
        return value;
    }

    inline uint8_t validateDisplayMode(int value) {
        if (value < 0 || value > 255) {
            DEBUG_WARN_F("Invalid display mode value %d (must be 0-255), using default 0", value);
            return 0;
        }
        return static_cast<uint8_t>(value);
    }

    inline float validateOpacity(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite opacity value, using %.2f", MAX_OPACITY);
            return MAX_OPACITY;
        }
        if (value < MIN_OPACITY || value > MAX_OPACITY) {
            DEBUG_WARN_F("Invalid opacity value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OPACITY, MAX_OPACITY);
            return (value < MIN_OPACITY) ? MIN_OPACITY : MAX_OPACITY;
        }
        return value;
    }

    inline float validateOffset(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite offset value, using 0.0");
            return 0.0f;
        }
        if (value < MIN_OFFSET || value > MAX_OFFSET) {
            DEBUG_WARN_F("Invalid offset value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OFFSET, MAX_OFFSET);
            return (value < MIN_OFFSET) ? MIN_OFFSET : MAX_OFFSET;
        }
        return value;
    }

    inline int validateDisplayRows(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_ROWS || value > MAX_DISPLAY_ROWS) {
            DEBUG_WARN_F("Invalid display row count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_ROWS, MAX_DISPLAY_ROWS);
            return (value < MIN_DISPLAY_ROWS) ? MIN_DISPLAY_ROWS : MAX_DISPLAY_ROWS;
        }
        return value;
    }

    inline int validateDisplayLaps(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_LAPS || value > MAX_DISPLAY_LAPS) {
            DEBUG_WARN_F("Invalid display lap count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_LAPS, MAX_DISPLAY_LAPS);
            return (value < MIN_DISPLAY_LAPS) ? MIN_DISPLAY_LAPS : MAX_DISPLAY_LAPS;
        }
        return value;
    }

    // Icon shape helpers - convert between shape index and filename
    // Shape index is 1-based offset into icon list (0 = off/none)
    inline std::string shapeIndexToFilename(int shapeIndex) {
        if (shapeIndex <= 0) return "Off";
        const auto& assetMgr = AssetManager::getInstance();
        // Base index: same reason as filenameToShapeIndex below -- the saved name is
        // the vocabulary's, not the active theme's.
        int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
        std::string filename = assetMgr.getIconFilename(spriteIndex);
        return filename.empty() ? "Off" : filename;
    }

    inline int filenameToShapeIndex(const std::string& filename, int defaultShape) {
        if (filename.empty() || filename == "Off") return 0;
        const auto& assetMgr = AssetManager::getInstance();
        // BASE index: the saved name has to resolve to the same shape whatever theme
        // is on, or switching one would renumber every stored marker.
        int spriteIndex = assetMgr.getBaseIconSpriteIndex(filename);
        if (spriteIndex <= 0) return defaultShape;
        return assetMgr.shapeIndexForSprite(spriteIndex);
    }

    // Helper to format a section name with profile index
    inline std::string formatSectionName(const char* hudName, ProfileType profile) {
        return std::string(hudName) + ":" + std::to_string(static_cast<int>(profile));
    }

    // Parse section name to extract HUD name and profile index
    // Returns true if successfully parsed, false if no profile index (global section)
    // Convert profile name to ProfileType (returns -1 if not found)
    inline int profileNameToIndex(const std::string& name) {
        if (name == "Practice") return static_cast<int>(ProfileType::PRACTICE);
        if (name == "Qualify")  return static_cast<int>(ProfileType::QUALIFY);
        if (name == "Race")     return static_cast<int>(ProfileType::RACE);
        if (name == "Spectate") return static_cast<int>(ProfileType::SPECTATE);
        return -1;
    }

    // Parse section name into HUD name and profile index
    // Supports both old format [HudName:0] and new format [HudName:Practice]
    // Returns true if this is a profile-specific section, false if base/global section
    inline bool parseSectionName(const std::string& section, std::string& hudName, int& profileIndex) {
        size_t colonPos = section.find(':');
        if (colonPos == std::string::npos) {
            hudName = section;
            profileIndex = -1;  // Base/global section (no profile suffix)
            return false;
        }
        hudName = section.substr(0, colonPos);
        std::string suffix = section.substr(colonPos + 1);

        // Try parsing as profile name first (new format: "Practice", "Qualify", etc.)
        profileIndex = profileNameToIndex(suffix);
        if (profileIndex >= 0) {
            return true;
        }

        // Fall back to numeric index (old format: "0", "1", etc.) - for migration
        try {
            profileIndex = std::stoi(suffix);
            return true;
        } catch (...) {
            hudName = section;
            profileIndex = -1;
            return false;
        }
    }

    // Helper to capture base HUD properties to a settings map
    // Set includePosition=false for HUDs that use anchor-based positioning (e.g., MapHud)
    // Color slot key names for per-HUD INI overrides (color_primary, color_secondary, etc.)
    //
    // When ColorSlot or FontCategory grows, both the toKey() and parseKey() lookups below
    // must be updated in lock-step. These static_asserts catch the easy mistake of adding
    // a new enum value without updating the INI key mappings.
    static_assert(static_cast<int>(ColorSlot::COUNT) == 10,
                  "ColorSlot enum changed — update colorSlotToKey() and parseColorKey() below.");
    static_assert(static_cast<int>(FontCategory::COUNT) == 6,
                  "FontCategory enum changed — update fontCategoryToKey() and parseFontKey() below.");

    inline const char* colorSlotToKey(ColorSlot slot) {
        switch (slot) {
            case ColorSlot::PRIMARY:    return "color_primary";
            case ColorSlot::SECONDARY:  return "color_secondary";
            case ColorSlot::TERTIARY:   return "color_tertiary";
            case ColorSlot::MUTED:      return "color_muted";
            case ColorSlot::BACKGROUND: return "color_background";
            case ColorSlot::POSITIVE:   return "color_positive";
            case ColorSlot::WARNING:    return "color_warning";
            case ColorSlot::NEUTRAL:    return "color_neutral";
            case ColorSlot::NEGATIVE:   return "color_negative";
            case ColorSlot::ACCENT:     return "color_accent";
            default:                    return nullptr;
        }
    }

    // Font category key names for per-HUD INI overrides (font_title, font_normal, etc.)
    inline const char* fontCategoryToKey(FontCategory category) {
        switch (category) {
            case FontCategory::TITLE:   return "font_title";
            case FontCategory::NORMAL:  return "font_normal";
            case FontCategory::STRONG:  return "font_strong";
            case FontCategory::DIGITS:  return "font_digits";
            case FontCategory::MARKER:  return "font_marker";
            case FontCategory::SMALL:   return "font_small";
            default:                    return nullptr;
        }
    }

    // Parse color slot from INI key name (returns ColorSlot::COUNT if not a color key)
    inline ColorSlot parseColorKey(const std::string& key) {
        if (key == "color_primary")    return ColorSlot::PRIMARY;
        if (key == "color_secondary")  return ColorSlot::SECONDARY;
        if (key == "color_tertiary")   return ColorSlot::TERTIARY;
        if (key == "color_muted")      return ColorSlot::MUTED;
        if (key == "color_background") return ColorSlot::BACKGROUND;
        if (key == "color_positive")   return ColorSlot::POSITIVE;
        if (key == "color_warning")    return ColorSlot::WARNING;
        if (key == "color_neutral")    return ColorSlot::NEUTRAL;
        if (key == "color_negative")   return ColorSlot::NEGATIVE;
        if (key == "color_accent")     return ColorSlot::ACCENT;
        return ColorSlot::COUNT;
    }

    // Parse font category from INI key name (returns FontCategory::COUNT if not a font key)
    inline FontCategory parseFontKey(const std::string& key) {
        if (key == "font_title")   return FontCategory::TITLE;
        if (key == "font_normal")  return FontCategory::NORMAL;
        if (key == "font_strong")  return FontCategory::STRONG;
        if (key == "font_digits")  return FontCategory::DIGITS;
        if (key == "font_marker")  return FontCategory::MARKER;
        if (key == "font_small")   return FontCategory::SMALL;
        return FontCategory::COUNT;
    }

    inline void captureBaseHudSettings(SettingsManager::HudSettings& settings, const BaseHud& hud, bool includePosition = true) {
        using namespace Keys::Base;
        settings[VISIBLE] = std::to_string(hud.isVisible() ? 1 : 0);
        settings[SHOW_TITLE] = std::to_string(hud.getShowTitle() ? 1 : 0);
        settings[SHOW_BG_TEXTURE] = std::to_string(hud.getShowBackgroundTexture() ? 1 : 0);
        settings[TEXTURE_VARIANT] = std::to_string(hud.getTextureVariant());
        // ALWAYS CAPTURED, empty included -- the sparseness that matters happens one
        // layer up, where buildHudSection() diffs a profile against the base section and
        // drops what matches. Emitting here only when non-empty conflated two different
        // absences and broke the one that has to work:
        //
        //   A HUD whose factory default is a real theme cannot be set back to "Default".
        //   GamepadWidget and RadarHud both reset to THEME_NONE (they opt out of theming
        //   -- the texture IS the panel), so their BASE section carries `theme=none`.
        //   Choosing "Default" clears the override to empty, this line then emitted
        //   nothing, and an absent profile key cannot override a present base one -- so
        //   the base `theme=none` was re-applied on the next load and the choice never
        //   stuck. Reachable from the settings menu alone, no hand-editing.
        //
        // The clear is expressible on disk (`theme=` with an empty value; applyBaseHudSettings
        // sets sawTheme and applies it), so capturing it unconditionally is all that was
        // missing. An untouched HUD still follows Appearance: its profile value equals the
        // base value and the diff drops it.
        settings[THEME] = hud.getThemeOverride();
        settings[BG_OPACITY] = std::to_string(hud.getBackgroundOpacity());
        settings[SCALE] = std::to_string(hud.getScale());
        if (includePosition) {
            settings[OFFSET_X] = std::to_string(hud.getOffsetX());
            settings[OFFSET_Y] = std::to_string(hud.getOffsetY());
        }
        // Companion instance — persist only when it has actually DIVERGED from the game,
        // not merely when configured. Opening the companion window snapshots the game
        // layout into EVERY HUD (decouple-from-the-start), so gating on isCompanionConfigured()
        // alone would write companion keys for HUDs the user never touched on the companion —
        // bloating the INI and, worse, pinning those HUDs to their old position so a changed
        // default in a later version wouldn't take effect on the companion. A snapshot copies
        // the game values verbatim, so an untouched companion compares exactly equal here and
        // is left unpersisted — it simply re-snapshots from the game on the next open, keeping
        // the save sparse and upgrade-safe. A HUD moved/toggled on the companion (or one whose
        // game surface moved after the companion opened, so the two genuinely differ) persists.
        if (hud.isCompanionConfigured() &&
            (hud.getCompanionVisible() != hud.isVisible() ||
             hud.getCompanionOffsetX() != hud.getOffsetX() ||
             hud.getCompanionOffsetY() != hud.getOffsetY())) {
            settings[COMPANION_CONFIGURED] = "1";
            settings[COMPANION_VISIBLE] = std::to_string(hud.getCompanionVisible() ? 1 : 0);
            settings[COMPANION_X] = std::to_string(hud.getCompanionOffsetX());
            settings[COMPANION_Y] = std::to_string(hud.getCompanionOffsetY());
        }

        // Capture per-HUD color overrides (only if set)
        for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
            ColorSlot slot = static_cast<ColorSlot>(i);
            if (hud.hasColorOverride(slot)) {
                const char* key = colorSlotToKey(slot);
                if (key) {
                    settings[key] = PluginUtils::formatColorHex(hud.getColorOverrideValue(slot));
                }
            }
        }

        // Capture per-HUD font overrides (only if set)
        for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
            FontCategory category = static_cast<FontCategory>(i);
            if (hud.hasFontOverride(category)) {
                const char* key = fontCategoryToKey(category);
                if (key) {
                    settings[key] = hud.getFontOverrideName(category);
                }
            }
        }

        // Capture per-HUD drop-shadow override (only if set)
        if (hud.hasDropShadowOverride()) {
            settings["dropShadow"] = hud.getDropShadowOverrideValue() ? "1" : "0";
        }
    }

    // Helper to write base HUD properties to file
    inline void writeBaseHudSettings(std::ostream& file, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        static const std::array<const char*, 9> baseKeys = {
            VISIBLE, SHOW_TITLE, SHOW_BG_TEXTURE, TEXTURE_VARIANT, THEME,
            BG_OPACITY, SCALE, OFFSET_X, OFFSET_Y
        };
        for (const auto& key : baseKeys) {
            auto it = settings.find(key);
            if (it != settings.end()) {
                file << key << "=" << it->second << "\n";
            }
        }
    }

    // Helper to check if a key is a base HUD property
    inline bool isBaseKey(const std::string& key) {
        using namespace Keys::Base;
        return key == VISIBLE || key == SHOW_TITLE || key == SHOW_BG_TEXTURE ||
               key == TEXTURE_VARIANT || key == THEME || key == BG_OPACITY ||
               key == SCALE || key == OFFSET_X || key == OFFSET_Y;
    }

    // Helper to get IniOnly setting description by HUD name and key
    // Returns nullptr if not an IniOnly setting (no description available)
    inline const char* getIniOnlyDescription(const std::string& hudName, const std::string& key) {
        using namespace IniOnly;

        // Keys SHARED by several HUDs, answered before the per-HUD chain: the chain
        // is keyed on one name, so a shared key would otherwise need its description
        // repeated once per HUD -- and a repeated description is one that drifts.
        if (key == Marker::LABEL_ANCHOR.key) return Marker::LABEL_ANCHOR.description;

        if (hudName == "SpeedoWidget") {
            if (key == Speedo::NEEDLE_COLOR.key) return Speedo::NEEDLE_COLOR.description;
            if (key == Speedo::SHOW_ODOMETER.key) return Speedo::SHOW_ODOMETER.description;
            if (key == Speedo::SHOW_TRIPMETER.key) return Speedo::SHOW_TRIPMETER.description;
        } else if (hudName == "TachoWidget") {
            if (key == Tacho::NEEDLE_COLOR.key) return Tacho::NEEDLE_COLOR.description;
        } else if (hudName == "SpeedWidget") {
            if (key == Speed::ROW_UNITS.key) return Speed::ROW_UNITS.description;
        } else if (hudName == "GearWidget") {
            if (key == Gear::SHOW_SHIFT_COLOR.key) return Gear::SHOW_SHIFT_COLOR.description;
            if (key == Gear::SHOW_LIMITER_CIRCLE.key) return Gear::SHOW_LIMITER_CIRCLE.description;
        } else if (hudName == "ClockWidget") {
            if (key == Clock::SHOW_UTC.key) return Clock::SHOW_UTC.description;
            if (key == Clock::UTC_ON_TOP.key) return Clock::UTC_ON_TOP.description;
        } else if (hudName == "RumbleHud") {
            if (key == Rumble::SHOW_MAX_MARKERS.key) return Rumble::SHOW_MAX_MARKERS.description;
            if (key == Rumble::MAX_MARKER_LINGER_FRAMES.key) return Rumble::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "LeanWidget") {
            if (key == Lean::FILL_COLOR_MODE.key) return Lean::FILL_COLOR_MODE.description;
            if (key == Lean::ARC_FILL_COLOR.key) return Lean::ARC_FILL_COLOR.description;
            if (key == Lean::ROW_ARC.key) return Lean::ROW_ARC.description;
            if (key == Lean::ROW_LEAN_VALUE.key) return Lean::ROW_LEAN_VALUE.description;
            if (key == Lean::ROW_STEER_BAR.key) return Lean::ROW_STEER_BAR.description;
            if (key == Lean::ROW_STEER_VALUE.key) return Lean::ROW_STEER_VALUE.description;
            if (key == Lean::SHOW_MAX_MARKERS.key) return Lean::SHOW_MAX_MARKERS.description;
            if (key == Lean::MAX_MARKER_LINGER_FRAMES.key) return Lean::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "GForceWidget") {
            if (key == GForce::MAX_SCALE.key) return GForce::MAX_SCALE.description;
            if (key == GForce::SHOW_MAX_TEXT.key) return GForce::SHOW_MAX_TEXT.description;
            if (key == GForce::SHOW_MAX_MARKER.key) return GForce::SHOW_MAX_MARKER.description;
            if (key == GForce::MAX_MARKER_LINGER_FRAMES.key) return GForce::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "CompassWidget") {
            if (key == Compass::STYLE.key) return Compass::STYLE.description;
        } else if (hudName == "BarsWidget") {
            if (key == Bars::COL_THROTTLE.key) return Bars::COL_THROTTLE.description;
            if (key == Bars::COL_BRAKE.key) return Bars::COL_BRAKE.description;
            if (key == Bars::COL_CLUTCH.key) return Bars::COL_CLUTCH.description;
            if (key == Bars::COL_RPM.key) return Bars::COL_RPM.description;
            if (key == Bars::COL_SUSPENSION.key) return Bars::COL_SUSPENSION.description;
            if (key == Bars::COL_FUEL.key) return Bars::COL_FUEL.description;
            if (key == Bars::COL_ENGINE_TEMP.key) return Bars::COL_ENGINE_TEMP.description;
            if (key == Bars::COL_WATER_TEMP.key) return Bars::COL_WATER_TEMP.description;
            if (key == Bars::SHOW_LABELS.key) return Bars::SHOW_LABELS.description;
            if (key == Bars::SHOW_MAX_MARKERS.key) return Bars::SHOW_MAX_MARKERS.description;
            if (key == Bars::MAX_MARKER_LINGER_FRAMES.key) return Bars::MAX_MARKER_LINGER_FRAMES.description;
        }
#if GAME_HAS_TYRE_TEMP
        else if (hudName == "TyreTempWidget") {
            if (key == TyreTemp::COLD_THRESHOLD.key) return TyreTemp::COLD_THRESHOLD.description;
            if (key == TyreTemp::HOT_THRESHOLD.key) return TyreTemp::HOT_THRESHOLD.description;
            if (key == TyreTemp::ROW_BARS.key) return TyreTemp::ROW_BARS.description;
            if (key == TyreTemp::ROW_VALUES.key) return TyreTemp::ROW_VALUES.description;
            if (key == TyreTemp::SHOW_LABELS.key) return TyreTemp::SHOW_LABELS.description;
        }
#endif
#if GAME_HAS_ECU
        else if (hudName == "EcuWidget") {
            if (key == Ecu::ROW_MAP.key) return Ecu::ROW_MAP.description;
            if (key == Ecu::ROW_TC.key) return Ecu::ROW_TC.description;
            if (key == Ecu::ROW_EB.key) return Ecu::ROW_EB.description;
            if (key == Ecu::ROW_AW.key) return Ecu::ROW_AW.description;
            if (key == Ecu::SHOW_LABELS.key) return Ecu::SHOW_LABELS.description;
        }
#endif
        else if (hudName == "FuelWidget") {
            if (key == Fuel::ROW_FUEL.key) return Fuel::ROW_FUEL.description;
            if (key == Fuel::ROW_USED.key) return Fuel::ROW_USED.description;
            if (key == Fuel::ROW_AVG.key) return Fuel::ROW_AVG.description;
            if (key == Fuel::ROW_EST.key) return Fuel::ROW_EST.description;
        } else if (hudName == "NoticesHud") {
            if (key == Notices::WRONG_WAY.key) return Notices::WRONG_WAY.description;
            if (key == Notices::BLUE_FLAG.key) return Notices::BLUE_FLAG.description;
            if (key == Notices::LAST_LAP.key) return Notices::LAST_LAP.description;
            if (key == Notices::FINISHED.key) return Notices::FINISHED.description;
            if (key == Notices::ALLTIME_PB.key) return Notices::ALLTIME_PB.description;
            if (key == Notices::FASTEST_LAP.key) return Notices::FASTEST_LAP.description;
            if (key == Notices::SESSION_PB.key) return Notices::SESSION_PB.description;
            if (key == Notices::DEFAULT_SETUP.key) return Notices::DEFAULT_SETUP.description;
            if (key == Notices::PB_DURATION.key) return Notices::PB_DURATION.description;
        } else if (hudName == "StandingsHud") {
            if (key == Standings::TOP_POSITIONS.key) return Standings::TOP_POSITIONS.description;
            if (key == Standings::PLAYER_ROW_HIGHLIGHT.key) return Standings::PLAYER_ROW_HIGHLIGHT.description;
            if (key == Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key) return Standings::PLAYER_ROW_HIGHLIGHT_BRAND.description;
            if (key == Standings::ANIMATION_DURATION_MS.key) return Standings::ANIMATION_DURATION_MS.description;
            if (key == Standings::CLASSIC_LAYOUT.key) return Standings::CLASSIC_LAYOUT.description;
            if (key == Standings::NAME_MODE.key) return Standings::NAME_MODE.description;
            if (key == Standings::SHORT_NAME_CHARS.key) return Standings::SHORT_NAME_CHARS.description;
        }
#if GAME_HAS_RECORDS_PROVIDER
        else if (hudName == "RecordsHud") {
            if (key == Records::SHOW_FOOTER.key) return Records::SHOW_FOOTER.description;
        }
#endif
        else if (hudName == "CrashWidget") {
            if (key == Crash::SHOW_RESET_BUTTON.key) return Crash::SHOW_RESET_BUTTON.description;
        }
        else if (hudName == "GamepadWidget") {
            if (key == Gamepad::TRIGGER_FILL_MODE.key) return Gamepad::TRIGGER_FILL_MODE.description;
        }
#if GAME_HAS_FMX
        else if (hudName == "FmxHud") {
            // Per-trick disable flags share a common prefix; one description covers them all.
            if (key.rfind(Keys::Fmx::TRICK_ENABLED_PREFIX, 0) == 0) {
                return "Track this trick (1=enabled, 0=ignore)";
            }
        }
#endif
        else if (hudName == "SessionChartsHud") {
            if (key == SessionCharts::OUTLIER_FACTOR.key) return SessionCharts::OUTLIER_FACTOR.description;
        }

        // Per-HUD color/font overrides apply to all HUDs (checked last so HUD-specific
        // keys starting with "color_" or "font_" can be matched first if ever added)
        if (key.length() > 6 && key.substr(0, 6) == "color_") return "Per-HUD color override (hex ABGR, e.g. 0xff00ff00)";
        if (key.length() > 5 && key.substr(0, 5) == "font_") return "Per-HUD font override (font filename without extension)";
        if (key == "dropShadow") return "Per-HUD drop shadow override (0=off, 1=on; absent=inherit global)";

        return nullptr;
    }

    // Helper to write a setting with optional inline comment for IniOnly settings
    inline void writeSettingWithComment(std::ostream& file, const std::string& hudName,
                                 const std::string& key, const std::string& value) {
        const char* description = getIniOnlyDescription(hudName, key);
        if (description) {
            file << key << "=" << value << " ; " << description << "\n";
        } else {
            file << key << "=" << value << "\n";
        }
    }

    // ========================================================================
    // Named key helpers for bitmask fields
    // ========================================================================

    // Helper to save a single bit as a named key
    inline void saveBitAsKey(SettingsManager::HudSettings& settings, const char* key, uint32_t bitmask, uint32_t bit) {
        settings[key] = (bitmask & bit) ? "1" : "0";
    }

    // Helper to load a single bit from a named key
    inline void loadBitFromKey(const SettingsManager::HudSettings& settings, const char* key, uint32_t& bitmask, uint32_t bit) {
        auto it = settings.find(key);
        if (it != settings.end()) {
            if (it->second == "1") {
                bitmask |= bit;
            } else {
                bitmask &= ~bit;
            }
        }
        // If key is missing, leave bitmask unchanged (uses default)
    }

    // Helper to apply base HUD settings from a map
    // Buffers position to apply X/Y together atomically
    inline void applyBaseHudSettings(BaseHud& hud, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        float pendingOffsetX = 0, pendingOffsetY = 0;
        bool hasOffsetX = false, hasOffsetY = false;
        // Companion instance: buffered so it applies as a unit after the loop. Absent
        // keys => the companion mirrors the game (authoritative apply, like colors).
        bool compConfigured = false, compVisible = true;
        bool sawTheme = false;
        float compX = 0, compY = 0;

        // Each profile's cache is the complete intended state for the HUD (base keys
        // are merged into every profile on load), so an absent color_/font_ key means
        // "no override". Track which ones we see and clear the rest below — without
        // this, a per-HUD override would leak across profile switches and survive a
        // reset, since these private BaseHud members aren't touched by resetToDefaults().
        std::array<bool, static_cast<size_t>(ColorSlot::COUNT)> colorSeen{};
        std::array<bool, static_cast<size_t>(FontCategory::COUNT)> fontSeen{};
        bool dropShadowSeen = false;

        for (const auto& [key, value] : settings) {
            try {
                if (key == VISIBLE) {
                    hud.setVisible(std::stoi(value) != 0);
                } else if (key == SHOW_TITLE) {
                    hud.setShowTitle(std::stoi(value) != 0);
                } else if (key == SHOW_BG_TEXTURE) {
                    hud.setShowBackgroundTexture(std::stoi(value) != 0);
                } else if (key == TEXTURE_VARIANT) {
                    hud.setTextureVariant(std::stoi(value));
                } else if (key == THEME) {
                    // Stored verbatim; validated at render time, where an unknown
                    // name falls back to the global theme.
                    hud.setThemeOverride(value);
                    sawTheme = true;
                } else if (key == BG_OPACITY) {
                    hud.setBackgroundOpacity(validateOpacity(parseFiniteFloat(value)));
                } else if (key == SCALE) {
                    hud.setScale(validateScale(parseFiniteFloat(value)));
                } else if (key == OFFSET_X) {
                    pendingOffsetX = validateOffset(parseFiniteFloat(value));
                    hasOffsetX = true;
                } else if (key == OFFSET_Y) {
                    pendingOffsetY = validateOffset(parseFiniteFloat(value));
                    hasOffsetY = true;
                } else if (key == COMPANION_CONFIGURED) {
                    compConfigured = std::stoi(value) != 0;
                } else if (key == COMPANION_VISIBLE) {
                    compVisible = std::stoi(value) != 0;
                } else if (key == COMPANION_X) {
                    compX = validateOffset(parseFiniteFloat(value));
                } else if (key == COMPANION_Y) {
                    compY = validateOffset(parseFiniteFloat(value));
                } else {
                    // Per-HUD color/font overrides (power user INI feature)
                    ColorSlot colorSlot = parseColorKey(key);
                    if (colorSlot != ColorSlot::COUNT) {
                        hud.setColorOverride(colorSlot, PluginUtils::parseColorHex(value));
                        colorSeen[static_cast<size_t>(colorSlot)] = true;
                        continue;
                    }
                    FontCategory fontCategory = parseFontKey(key);
                    if (fontCategory != FontCategory::COUNT) {
                        hud.setFontOverride(fontCategory, value);
                        fontSeen[static_cast<size_t>(fontCategory)] = true;
                        continue;
                    }
                    if (key == "dropShadow") {
                        hud.setDropShadowOverride(std::stoi(value) != 0);
                        dropShadowSeen = true;
                        continue;
                    }
                }
            } catch (...) {
                DEBUG_WARN_F("Failed to parse base setting '%s=%s'", key.c_str(), value.c_str());
            }
        }

        // Clear any override not present in the applied settings (authoritative apply).
        for (size_t i = 0; i < colorSeen.size(); ++i) {
            if (!colorSeen[i]) hud.clearColorOverride(static_cast<ColorSlot>(i));
        }
        for (size_t i = 0; i < fontSeen.size(); ++i) {
            if (!fontSeen[i]) hud.clearFontOverride(static_cast<FontCategory>(i));
        }
        if (!dropShadowSeen) hud.clearDropShadowOverride();
        // Apply buffered position
        if (hasOffsetX || hasOffsetY) {
            float finalX = hasOffsetX ? pendingOffsetX : hud.getOffsetX();
            float finalY = hasOffsetY ? pendingOffsetY : hud.getOffsetY();
            hud.setPosition(finalX, finalY);
        }
        // Apply the companion instance authoritatively: configured => use saved
        // values; absent => mirror the game.
        if (compConfigured) hud.applyCompanionState(compVisible, compX, compY);
        else hud.clearCompanionState();

        // Same rule for the per-HUD theme override, and it is a rule rather than a
        // nicety: capture writes the key ONLY when the HUD has diverged, so absence
        // means "follow the global theme" and has to be applied as such. Assigning
        // only on presence made the override sticky -- pin Standings to "none", then
        // Reset to Defaults, and the factory snapshot (captured at startup with an
        // empty override) carries no theme key, so nothing ever cleared it and the
        // HUD stayed pinned. Profile switching had the same hole.
        //
        // The companion block above is the model this was meant to copy and only
        // half did. Pinned by theme_override_test.cpp.
        if (!sawTheme) hud.clearThemeOverride();
    }

} // namespace Settings
