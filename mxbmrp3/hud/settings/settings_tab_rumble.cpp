// ============================================================================
// hud/settings/settings_tab_rumble.cpp
// Tab renderer for Rumble/Controller settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../rumble_hud.h"
#include "../speed_widget.h"
#include "../../core/xinput_reader.h"
#include "../../core/rumble_profile_manager.h"
#include "../../core/settings_manager.h"
#include "../../core/plugin_manager.h"
#include "../../core/hud_manager.h"
#include "../../core/ui_config.h"
#include "../../game/game_config.h"
#include <cmath>
#include <algorithm>

// Helper to adjust rumble effect strengths (used by many click handlers)
static void adjustEffectStrength(float& value, bool increase, int stepMultiplier = 1) {
    float step = 0.01f * stepMultiplier;
    if (increase) {
        value = std::round(std::min(value + step, 1.0f) * 100.0f) / 100.0f;
    } else {
        value = std::round(std::max(value - step, 0.0f) * 100.0f) / 100.0f;
    }
}

// Member function of SettingsHud - handles click events for Rumble tab
bool SettingsHud::handleClickTabRumble(const ClickRegion& region) {
    // Use per-bike or global config for effect settings
    RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
    // Global config for master settings (enabled, blend mode, crashed) - these are never per-bike
    RumbleConfig& globalConfig = XInputReader::getInstance().getGlobalRumbleConfig();
    const bool isPerBikeMode = globalConfig.usePerBikeEffects;

    // Helper to mark profile dirty when modifying effects in per-bike mode
    auto markProfileDirty = [isPerBikeMode]() {
        if (isPerBikeMode) {
            RumbleProfileManager::getInstance().markDirty();
        }
    };

    // Lambda for effect adjustments - avoids massive switch statement
    auto handleEffectControl = [&](RumbleEffect& effect,
        ClickRegion::Type lightDown, ClickRegion::Type lightUp,
        ClickRegion::Type heavyDown, ClickRegion::Type heavyUp,
        ClickRegion::Type minDown, ClickRegion::Type minUp,
        ClickRegion::Type maxDown, ClickRegion::Type maxUp,
        float minStep = 1.0f, float maxStep = 1.0f, float maxLimit = 100.0f) -> bool {
        int sm = getHoldStepMultiplier();
        if (region.type == lightUp) {
            adjustEffectStrength(effect.lightStrength, true, sm);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == lightDown) {
            adjustEffectStrength(effect.lightStrength, false, sm);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == heavyUp) {
            adjustEffectStrength(effect.heavyStrength, true, sm);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == heavyDown) {
            adjustEffectStrength(effect.heavyStrength, false, sm);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == minUp) {
            effect.minInput = std::min(effect.minInput + minStep, maxLimit);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == minDown) {
            effect.minInput = std::max(effect.minInput - minStep, 0.0f);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == maxUp) {
            effect.maxInput = std::min(effect.maxInput + maxStep, maxLimit);
            markProfileDirty();
            setDataDirty();
            return true;
        } else if (region.type == maxDown) {
            effect.maxInput = std::max(effect.maxInput - maxStep, effect.minInput);
            markProfileDirty();
            setDataDirty();
            return true;
        }
        return false;
    };

    switch (region.type) {
        // Note: RUMBLE_TOGGLE is handled in common handlers (settings_hud.cpp)
        // so it works regardless of which tab is active

        // Master settings always use global config (never stored per-bike)
        case ClickRegion::RUMBLE_BLEND_TOGGLE:
            globalConfig.additiveBlend = !globalConfig.additiveBlend;
            setDataDirty();
            return true;

        case ClickRegion::RUMBLE_CRASH_TOGGLE:
            globalConfig.rumbleWhenCrashed = !globalConfig.rumbleWhenCrashed;
            setDataDirty();
            return true;

        case ClickRegion::RUMBLE_EFFECT_PROFILE_TOGGLE: {
            // Toggle effect profile mode (stored in global config)
            RumbleConfig& globalConfig = XInputReader::getInstance().getGlobalRumbleConfig();
            bool wasPerBike = globalConfig.usePerBikeEffects;
            globalConfig.usePerBikeEffects = !globalConfig.usePerBikeEffects;

            // Save rumble effects if switching from per-bike to global
            if (wasPerBike && !globalConfig.usePerBikeEffects) {
                RumbleProfileManager::getInstance().save();
            }

            // Mark settings dirty to persist the mode change (deferred to leave-track / Save).
            SettingsManager::getInstance().markDirty();
            setDataDirty();
            return true;
        }

        // Front/rear split toggles (effect property, so per-bike-aware like the effects).
        // Combined and front/rear are stored independently. The first time an effect is
        // split, the front/rear inherit the combined value as their starting point; after
        // that they keep whatever the user tuned them to and are never reseeded.
        case ClickRegion::RUMBLE_SUSP_SPLIT_TOGGLE:
            config.suspensionSplit = !config.suspensionSplit;
            if (config.suspensionSplit && !config.suspensionSplitInitialized) {
                config.suspensionEffectFront = config.suspensionEffect;
                config.suspensionEffectRear = config.suspensionEffect;
                config.suspensionSplitInitialized = true;
            }
            markProfileDirty();
            setDataDirty();
            return true;

        case ClickRegion::RUMBLE_LOCKUP_SPLIT_TOGGLE:
            config.brakeLockupSplit = !config.brakeLockupSplit;
            if (config.brakeLockupSplit && !config.brakeLockupSplitInitialized) {
                config.brakeLockupEffectFront = config.brakeLockupEffect;
                config.brakeLockupEffectRear = config.brakeLockupEffect;
                config.brakeLockupSplitInitialized = true;
            }
            markProfileDirty();
            setDataDirty();
            return true;

        // Note: RUMBLE_HUD_TOGGLE is handled in common handlers (settings_hud.cpp)
        // so it works regardless of which tab is active

        default:
            break;
    }

    // Handle suspension effect controls (m/s, max 50)
    if (handleEffectControl(config.suspensionEffect,
        ClickRegion::RUMBLE_SUSP_LIGHT_DOWN, ClickRegion::RUMBLE_SUSP_LIGHT_UP,
        ClickRegion::RUMBLE_SUSP_HEAVY_DOWN, ClickRegion::RUMBLE_SUSP_HEAVY_UP,
        ClickRegion::RUMBLE_SUSP_MIN_DOWN, ClickRegion::RUMBLE_SUSP_MIN_UP,
        ClickRegion::RUMBLE_SUSP_MAX_DOWN, ClickRegion::RUMBLE_SUSP_MAX_UP,
        1.0f, 1.0f, 50.0f)) {
        return true;
    }

    // Handle suspension FRONT controls (used only when split)
    if (handleEffectControl(config.suspensionEffectFront,
        ClickRegion::RUMBLE_SUSP_FRONT_LIGHT_DOWN, ClickRegion::RUMBLE_SUSP_FRONT_LIGHT_UP,
        ClickRegion::RUMBLE_SUSP_FRONT_HEAVY_DOWN, ClickRegion::RUMBLE_SUSP_FRONT_HEAVY_UP,
        ClickRegion::RUMBLE_SUSP_FRONT_MIN_DOWN, ClickRegion::RUMBLE_SUSP_FRONT_MIN_UP,
        ClickRegion::RUMBLE_SUSP_FRONT_MAX_DOWN, ClickRegion::RUMBLE_SUSP_FRONT_MAX_UP,
        1.0f, 1.0f, 50.0f)) {
        config.suspensionSplitInitialized = true;  // user has set the split values; don't reseed
        return true;
    }

    // Handle suspension REAR controls (used only when split)
    if (handleEffectControl(config.suspensionEffectRear,
        ClickRegion::RUMBLE_SUSP_REAR_LIGHT_DOWN, ClickRegion::RUMBLE_SUSP_REAR_LIGHT_UP,
        ClickRegion::RUMBLE_SUSP_REAR_HEAVY_DOWN, ClickRegion::RUMBLE_SUSP_REAR_HEAVY_UP,
        ClickRegion::RUMBLE_SUSP_REAR_MIN_DOWN, ClickRegion::RUMBLE_SUSP_REAR_MIN_UP,
        ClickRegion::RUMBLE_SUSP_REAR_MAX_DOWN, ClickRegion::RUMBLE_SUSP_REAR_MAX_UP,
        1.0f, 1.0f, 50.0f)) {
        config.suspensionSplitInitialized = true;
        return true;
    }

    // Handle wheelspin effect controls (ratio, max 50)
    if (handleEffectControl(config.wheelspinEffect,
        ClickRegion::RUMBLE_WHEEL_LIGHT_DOWN, ClickRegion::RUMBLE_WHEEL_LIGHT_UP,
        ClickRegion::RUMBLE_WHEEL_HEAVY_DOWN, ClickRegion::RUMBLE_WHEEL_HEAVY_UP,
        ClickRegion::RUMBLE_WHEEL_MIN_DOWN, ClickRegion::RUMBLE_WHEEL_MIN_UP,
        ClickRegion::RUMBLE_WHEEL_MAX_DOWN, ClickRegion::RUMBLE_WHEEL_MAX_UP,
        1.0f, 1.0f, 50.0f)) {
        return true;
    }

    // Handle brake lockup effect controls (ratio 0-1, max 1.0)
    if (handleEffectControl(config.brakeLockupEffect,
        ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN, ClickRegion::RUMBLE_LOCKUP_LIGHT_UP,
        ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN, ClickRegion::RUMBLE_LOCKUP_HEAVY_UP,
        ClickRegion::RUMBLE_LOCKUP_MIN_DOWN, ClickRegion::RUMBLE_LOCKUP_MIN_UP,
        ClickRegion::RUMBLE_LOCKUP_MAX_DOWN, ClickRegion::RUMBLE_LOCKUP_MAX_UP,
        0.05f, 0.05f, 1.0f)) {
        return true;
    }

    // Handle brake lockup FRONT controls (used only when split)
    if (handleEffectControl(config.brakeLockupEffectFront,
        ClickRegion::RUMBLE_LOCKUP_FRONT_LIGHT_DOWN, ClickRegion::RUMBLE_LOCKUP_FRONT_LIGHT_UP,
        ClickRegion::RUMBLE_LOCKUP_FRONT_HEAVY_DOWN, ClickRegion::RUMBLE_LOCKUP_FRONT_HEAVY_UP,
        ClickRegion::RUMBLE_LOCKUP_FRONT_MIN_DOWN, ClickRegion::RUMBLE_LOCKUP_FRONT_MIN_UP,
        ClickRegion::RUMBLE_LOCKUP_FRONT_MAX_DOWN, ClickRegion::RUMBLE_LOCKUP_FRONT_MAX_UP,
        0.05f, 0.05f, 1.0f)) {
        config.brakeLockupSplitInitialized = true;  // user has set the split values; don't reseed
        return true;
    }

    // Handle brake lockup REAR controls (used only when split)
    if (handleEffectControl(config.brakeLockupEffectRear,
        ClickRegion::RUMBLE_LOCKUP_REAR_LIGHT_DOWN, ClickRegion::RUMBLE_LOCKUP_REAR_LIGHT_UP,
        ClickRegion::RUMBLE_LOCKUP_REAR_HEAVY_DOWN, ClickRegion::RUMBLE_LOCKUP_REAR_HEAVY_UP,
        ClickRegion::RUMBLE_LOCKUP_REAR_MIN_DOWN, ClickRegion::RUMBLE_LOCKUP_REAR_MIN_UP,
        ClickRegion::RUMBLE_LOCKUP_REAR_MAX_DOWN, ClickRegion::RUMBLE_LOCKUP_REAR_MAX_UP,
        0.05f, 0.05f, 1.0f)) {
        config.brakeLockupSplitInitialized = true;
        return true;
    }

    // Handle wheelie effect controls (degrees, max 90)
    if (handleEffectControl(config.wheelieEffect,
        ClickRegion::RUMBLE_WHEELIE_LIGHT_DOWN, ClickRegion::RUMBLE_WHEELIE_LIGHT_UP,
        ClickRegion::RUMBLE_WHEELIE_HEAVY_DOWN, ClickRegion::RUMBLE_WHEELIE_HEAVY_UP,
        ClickRegion::RUMBLE_WHEELIE_MIN_DOWN, ClickRegion::RUMBLE_WHEELIE_MIN_UP,
        ClickRegion::RUMBLE_WHEELIE_MAX_DOWN, ClickRegion::RUMBLE_WHEELIE_MAX_UP,
        1.0f, 1.0f, 90.0f)) {
        return true;
    }

    // Handle RPM effect controls (RPM, max 20000)
    if (handleEffectControl(config.rpmEffect,
        ClickRegion::RUMBLE_RPM_LIGHT_DOWN, ClickRegion::RUMBLE_RPM_LIGHT_UP,
        ClickRegion::RUMBLE_RPM_HEAVY_DOWN, ClickRegion::RUMBLE_RPM_HEAVY_UP,
        ClickRegion::RUMBLE_RPM_MIN_DOWN, ClickRegion::RUMBLE_RPM_MIN_UP,
        ClickRegion::RUMBLE_RPM_MAX_DOWN, ClickRegion::RUMBLE_RPM_MAX_UP,
        100.0f, 100.0f, 20000.0f)) {
        return true;
    }

    // Handle slide effect controls (degrees, max 90)
    if (handleEffectControl(config.slideEffect,
        ClickRegion::RUMBLE_SLIDE_LIGHT_DOWN, ClickRegion::RUMBLE_SLIDE_LIGHT_UP,
        ClickRegion::RUMBLE_SLIDE_HEAVY_DOWN, ClickRegion::RUMBLE_SLIDE_HEAVY_UP,
        ClickRegion::RUMBLE_SLIDE_MIN_DOWN, ClickRegion::RUMBLE_SLIDE_MIN_UP,
        ClickRegion::RUMBLE_SLIDE_MAX_DOWN, ClickRegion::RUMBLE_SLIDE_MAX_UP,
        1.0f, 1.0f, 90.0f)) {
        return true;
    }

    // Handle surface effect controls (m/s, max 200 ~720km/h)
    if (handleEffectControl(config.surfaceEffect,
        ClickRegion::RUMBLE_SURFACE_LIGHT_DOWN, ClickRegion::RUMBLE_SURFACE_LIGHT_UP,
        ClickRegion::RUMBLE_SURFACE_HEAVY_DOWN, ClickRegion::RUMBLE_SURFACE_HEAVY_UP,
        ClickRegion::RUMBLE_SURFACE_MIN_DOWN, ClickRegion::RUMBLE_SURFACE_MIN_UP,
        ClickRegion::RUMBLE_SURFACE_MAX_DOWN, ClickRegion::RUMBLE_SURFACE_MAX_UP,
        1.39f, 1.39f, 200.0f)) {
        return true;
    }

    // Handle steer effect controls (Nm, max 200)
    if (handleEffectControl(config.steerEffect,
        ClickRegion::RUMBLE_STEER_LIGHT_DOWN, ClickRegion::RUMBLE_STEER_LIGHT_UP,
        ClickRegion::RUMBLE_STEER_HEAVY_DOWN, ClickRegion::RUMBLE_STEER_HEAVY_UP,
        ClickRegion::RUMBLE_STEER_MIN_DOWN, ClickRegion::RUMBLE_STEER_MIN_UP,
        ClickRegion::RUMBLE_STEER_MAX_DOWN, ClickRegion::RUMBLE_STEER_MAX_UP,
        1.0f, 1.0f, 200.0f)) {
        return true;
    }

    // Handle rev limiter controls (percent of limiter RPM, 1% steps, allow buffer past 100)
    if (handleEffectControl(config.revLimiterEffect,
        ClickRegion::RUMBLE_REVLIM_LIGHT_DOWN, ClickRegion::RUMBLE_REVLIM_LIGHT_UP,
        ClickRegion::RUMBLE_REVLIM_HEAVY_DOWN, ClickRegion::RUMBLE_REVLIM_HEAVY_UP,
        ClickRegion::RUMBLE_REVLIM_MIN_DOWN, ClickRegion::RUMBLE_REVLIM_MIN_UP,
        ClickRegion::RUMBLE_REVLIM_MAX_DOWN, ClickRegion::RUMBLE_REVLIM_MAX_UP,
        1.0f, 1.0f, 110.0f)) {
        return true;
    }

    // Handle pit limiter controls (binary input; min/max barely matter, Light/Heavy do)
    if (handleEffectControl(config.pitLimiterEffect,
        ClickRegion::RUMBLE_PITLIM_LIGHT_DOWN, ClickRegion::RUMBLE_PITLIM_LIGHT_UP,
        ClickRegion::RUMBLE_PITLIM_HEAVY_DOWN, ClickRegion::RUMBLE_PITLIM_HEAVY_UP,
        ClickRegion::RUMBLE_PITLIM_MIN_DOWN, ClickRegion::RUMBLE_PITLIM_MIN_UP,
        ClickRegion::RUMBLE_PITLIM_MAX_DOWN, ClickRegion::RUMBLE_PITLIM_MAX_UP,
        0.05f, 0.05f, 1.0f)) {
        return true;
    }

    return false;
}

// Static member function of SettingsHud - inherits friend access to RumbleHud
BaseHud* SettingsHud::renderTabRumble(SettingsLayoutContext& ctx) {
    RumbleHud* hud = ctx.parent->getRumbleHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("rumble");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // Global config for master settings (enabled, blend, crashed, profile mode)
    const RumbleConfig& globalConfig = XInputReader::getInstance().getGlobalRumbleConfig();
    // Active config for effect settings (global or per-bike based on mode)
    RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
    float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    ColorConfig& colors = ColorConfig::getInstance();
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);

    // === RUMBLE SECTION ===
    ctx.addSectionHeader("Rumble");

    // Master rumble enable (always from global config)
    ctx.addToggleControl("Enabled", globalConfig.enabled,
        SettingsHud::ClickRegion::RUMBLE_TOGGLE, hud, nullptr, 0, true, "rumble.enabled");

    // Stack mode (always from global config)
    ctx.addToggleControl("Stack Forces", globalConfig.additiveBlend,
        SettingsHud::ClickRegion::RUMBLE_BLEND_TOGGLE, hud, nullptr, 0, true, "rumble.stack");

    // Rumble when crashed (always from global config)
    ctx.addToggleControl("When Crashed", globalConfig.rumbleWhenCrashed,
        SettingsHud::ClickRegion::RUMBLE_CRASH_TOGGLE, hud, nullptr, 0, true, "rumble.crashed");

    // Effect profile (per-bike vs global) - uses global config to determine mode
    // Pass true for isOn since both options are valid active states (not on/off)
    ctx.addToggleControl("Effect Profile", true,
        SettingsHud::ClickRegion::RUMBLE_EFFECT_PROFILE_TOGGLE, hud, nullptr, 0, true, "rumble.effect_profile",
        globalConfig.usePerBikeEffects ? "Per-Bike" : "Global");

    // === EFFECTS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Effects");

    // Table header - columns: [gutter] Effect | Light | Heavy | Min | Max
    // The gutter holds the [+]/[-] split disclosure on splittable effects (Bumps, Lockup).
    // We shift the whole table right rather than adding a column on the right edge, which
    // would overflow the panel.
    float markerX = ctx.labelX;
    float gutterW = PluginUtils::calculateMonospaceTextWidth(4, ctx.fontSize);
    float effectX = ctx.labelX + gutterW;
    float lightX = effectX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float heavyX = lightX + PluginUtils::calculateMonospaceTextWidth(9, ctx.fontSize);
    float minX = heavyX + PluginUtils::calculateMonospaceTextWidth(9, ctx.fontSize);
    float maxX = minX + PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);

    ctx.parent->addString("Effect", effectX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Light", lightX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Heavy", heavyX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Min", minX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Max", maxX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.currentY += ctx.lineHeightNormal;

    // Lambda for rumble effect rows
    auto addRumbleRow = [&](const char* name, RumbleEffect& effect,
                            SettingsHud::ClickRegion::Type lightDown,
                            SettingsHud::ClickRegion::Type lightUp,
                            SettingsHud::ClickRegion::Type heavyDown,
                            SettingsHud::ClickRegion::Type heavyUp,
                            SettingsHud::ClickRegion::Type minDown,
                            SettingsHud::ClickRegion::Type minUp,
                            SettingsHud::ClickRegion::Type maxDown,
                            SettingsHud::ClickRegion::Type maxUp,
                            bool useIntegers = false,
                            const char* unit = "",
                            float displayFactor = 1.0f,
                            const char* tooltipId = nullptr) {
        // Add row-wide tooltip region if tooltipId is provided
        if (tooltipId && tooltipId[0] != '\0') {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, tooltipId
            ));
        }

        // Effect name
        ctx.parent->addString(name, effectX, ctx.currentY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::getNormal(), colors.getPrimary(), ctx.fontSize);

        // Light motor strength control
        {
            char valueStr[8];
            int percent = static_cast<int>(std::round(effect.lightStrength * 100.0f));
            if (percent <= 0) {
                snprintf(valueStr, sizeof(valueStr), "%-4s", "Off");
            } else {
                char tempStr[8];
                snprintf(tempStr, sizeof(tempStr), "%d%%", percent);
                snprintf(valueStr, sizeof(valueStr), "%-4s", tempStr);
            }
            float currentX = lightX;
            ctx.parent->addString("<", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                lightDown, nullptr
            ));
            currentX += cw * 2;
            // Use percent for color to match display logic (avoids floating point precision issues)
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), percent > 0 ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 4;
            ctx.parent->addString(" >", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                lightUp, nullptr
            ));
        }

        // Heavy motor strength control
        {
            char valueStr[8];
            int percent = static_cast<int>(std::round(effect.heavyStrength * 100.0f));
            if (percent <= 0) {
                snprintf(valueStr, sizeof(valueStr), "%-4s", "Off");
            } else {
                char tempStr[8];
                snprintf(tempStr, sizeof(tempStr), "%d%%", percent);
                snprintf(valueStr, sizeof(valueStr), "%-4s", tempStr);
            }
            float currentX = heavyX;
            ctx.parent->addString("<", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                heavyDown, nullptr
            ));
            currentX += cw * 2;
            // Use percent for color to match display logic (avoids floating point precision issues)
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), percent > 0 ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 4;
            ctx.parent->addString(" >", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                heavyUp, nullptr
            ));
        }

        // Min input control
        {
            char valueStr[8];
            float displayValue = effect.minInput * displayFactor;
            if (displayFactor != 1.0f) {
                int rounded = static_cast<int>(std::round(displayValue / 5.0f)) * 5;
                snprintf(valueStr, sizeof(valueStr), "%d", rounded);
            } else if (useIntegers) {
                snprintf(valueStr, sizeof(valueStr), "%d", static_cast<int>(std::round(displayValue)));
            } else {
                snprintf(valueStr, sizeof(valueStr), "%.1f", displayValue);
            }
            float currentX = minX;
            ctx.parent->addString("<", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                minDown, nullptr
            ));
            currentX += cw * 2;
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), effect.isEnabled() ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 6;
            ctx.parent->addString(">", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                minUp, nullptr
            ));
        }

        // Max input control
        {
            char valueStr[8];
            float displayValue = effect.maxInput * displayFactor;
            if (displayFactor != 1.0f) {
                int rounded = static_cast<int>(std::round(displayValue / 5.0f)) * 5;
                snprintf(valueStr, sizeof(valueStr), "%d", rounded);
            } else if (useIntegers) {
                snprintf(valueStr, sizeof(valueStr), "%d", static_cast<int>(std::round(displayValue)));
            } else {
                snprintf(valueStr, sizeof(valueStr), "%.1f", displayValue);
            }
            float currentX = maxX;
            ctx.parent->addString("<", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                maxDown, nullptr
            ));
            currentX += cw * 2;
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), effect.isEnabled() ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 6;
            ctx.parent->addString(">", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                maxUp, nullptr
            ));
            // Unit is now described in tooltip instead of displayed inline
        }

        ctx.currentY += ctx.lineHeightNormal;
    };

    // Draw the [+]/[-] split disclosure in the gutter (with a click region) on a splittable row.
    auto drawSplitMarker = [&](bool split, SettingsHud::ClickRegion::Type toggleType) {
        ctx.parent->addString(split ? "[-]" : "[+]", markerX, ctx.currentY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            markerX, ctx.currentY, gutterW, ctx.lineHeightNormal, toggleType, nullptr));
    };

    // Draw a bare effect-name row (the parent header above the Front/Rear rows when split).
    auto drawEffectHeader = [&](const char* name, const char* tooltipId) {
        if (tooltipId && tooltipId[0] != '\0') {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, tooltipId));
        }
        ctx.parent->addString(name, effectX, ctx.currentY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::getNormal(), colors.getPrimary(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;
    };

    // Effect rows
    // Bumps (front/rear splittable). The marker shares the row with the single linked
    // row, or with the "Bumps" header above the Front/Rear rows when expanded.
    drawSplitMarker(rumbleConfig.suspensionSplit, SettingsHud::ClickRegion::RUMBLE_SUSP_SPLIT_TOGGLE);
    if (rumbleConfig.suspensionSplit) {
        drawEffectHeader("Bumps", "rumble.bumps");
        addRumbleRow("- Front", rumbleConfig.suspensionEffectFront,
            SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_FRONT_MAX_UP, true, "m/s", 1.0f, "rumble.bumps");
        addRumbleRow("- Rear", rumbleConfig.suspensionEffectRear,
            SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_REAR_MAX_UP, true, "m/s", 1.0f, "rumble.bumps");
    } else {
        addRumbleRow("Bumps", rumbleConfig.suspensionEffect,
            SettingsHud::ClickRegion::RUMBLE_SUSP_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_SUSP_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_MAX_UP, true, "m/s", 1.0f, "rumble.bumps");
    }
    addRumbleRow("Slide", rumbleConfig.slideEffect,
        SettingsHud::ClickRegion::RUMBLE_SLIDE_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_SLIDE_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_SLIDE_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_SLIDE_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_SLIDE_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_SLIDE_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_SLIDE_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_SLIDE_MAX_UP, true, "deg", 1.0f, "rumble.slide");
    addRumbleRow("Spin", rumbleConfig.wheelspinEffect,
        SettingsHud::ClickRegion::RUMBLE_WHEEL_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEEL_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_WHEEL_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEEL_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_WHEEL_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEEL_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_WHEEL_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEEL_MAX_UP, true, "x", 1.0f, "rumble.spin");
    // Lockup (front/rear splittable)
    drawSplitMarker(rumbleConfig.brakeLockupSplit, SettingsHud::ClickRegion::RUMBLE_LOCKUP_SPLIT_TOGGLE);
    if (rumbleConfig.brakeLockupSplit) {
        drawEffectHeader("Lockup", "rumble.lockup");
        addRumbleRow("- Front", rumbleConfig.brakeLockupEffectFront,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_FRONT_MAX_UP, false, "ratio", 1.0f, "rumble.lockup");
        addRumbleRow("- Rear", rumbleConfig.brakeLockupEffectRear,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_REAR_MAX_UP, false, "ratio", 1.0f, "rumble.lockup");
    } else {
        addRumbleRow("Lockup", rumbleConfig.brakeLockupEffect,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_LOCKUP_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_MAX_UP, false, "ratio", 1.0f, "rumble.lockup");
    }
    addRumbleRow("Wheelie", rumbleConfig.wheelieEffect,
        SettingsHud::ClickRegion::RUMBLE_WHEELIE_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEELIE_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_WHEELIE_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEELIE_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_WHEELIE_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEELIE_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_WHEELIE_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_WHEELIE_MAX_UP, true, "deg", 1.0f, "rumble.wheelie");
    addRumbleRow("Steer", rumbleConfig.steerEffect,
        SettingsHud::ClickRegion::RUMBLE_STEER_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_STEER_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_STEER_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_STEER_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_STEER_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_STEER_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_STEER_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_STEER_MAX_UP, true, "Nm", 1.0f, "rumble.steer");
    addRumbleRow("RPM", rumbleConfig.rpmEffect,
        SettingsHud::ClickRegion::RUMBLE_RPM_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_RPM_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_RPM_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_RPM_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_RPM_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_RPM_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_RPM_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_RPM_MAX_UP, true, "rpm", 1.0f, "rumble.rpm");

    // Rev Limiter: Min/Max are a percentage of the bike's real limiter RPM (auto per-bike)
    addRumbleRow("Rev Lim", rumbleConfig.revLimiterEffect,
        SettingsHud::ClickRegion::RUMBLE_REVLIM_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_REVLIM_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_REVLIM_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_REVLIM_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_REVLIM_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_REVLIM_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_REVLIM_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_REVLIM_MAX_UP, true, "%", 1.0f, "rumble.revlimiter");

#if GAME_HAS_PIT_LIMITER
    // Pit Limiter: binary effect; Light/Heavy set intensity (only games that report it)
    addRumbleRow("Pit Lim", rumbleConfig.pitLimiterEffect,
        SettingsHud::ClickRegion::RUMBLE_PITLIM_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_PITLIM_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_PITLIM_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_PITLIM_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_PITLIM_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_PITLIM_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_PITLIM_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_PITLIM_MAX_UP, false, "", 1.0f, "rumble.pitlimiter");
#endif

    // Surface uses user's speed unit preference
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();
        bool isKmh = speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
        const char* surfaceUnit = isKmh ? "km/h" : "mph";
        float surfaceFactor = isKmh ? 3.6f : 2.23694f;  // m/s to km/h or mph
        addRumbleRow("Surface", rumbleConfig.surfaceEffect,
            SettingsHud::ClickRegion::RUMBLE_SURFACE_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_SURFACE_LIGHT_UP,
            SettingsHud::ClickRegion::RUMBLE_SURFACE_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_SURFACE_HEAVY_UP,
            SettingsHud::ClickRegion::RUMBLE_SURFACE_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_SURFACE_MIN_UP,
            SettingsHud::ClickRegion::RUMBLE_SURFACE_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_SURFACE_MAX_UP, true, surfaceUnit, surfaceFactor, "rumble.surface");
    }

    // Info text
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Select your controller in the General tab.", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        colors.getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
