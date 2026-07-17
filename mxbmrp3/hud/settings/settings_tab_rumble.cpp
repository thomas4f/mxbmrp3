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
#include <functional>

// Member function of SettingsHud - handles click events for Rumble tab.
// Only the toggles live here: the per-effect Light/Heavy/Min/Max stepper arrows
// are shared data-driven STEPPED_UP/STEPPED_DOWN controls registered in
// renderTabRumble and applied by SettingsHud::applySteppedControl.
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
            return false;
    }
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

    // The stepped descriptors below bind raw pointers into the ACTIVE rumble
    // config resolved above. In per-bike profile mode that object changes when
    // the player swaps bikes — which can happen while this menu sits open — and
    // a click through the stale layout would then edit the PREVIOUS bike's
    // profile. Capture the bound config's identity here and validate it at
    // click time (SteppedControl::valid): on mismatch the click is swallowed
    // and the layout rebuilt against the right profile. The const getRumbleConfig
    // overload is used deliberately — it never auto-creates a profile, and it
    // falls back to the global config when the new bike has no profile yet
    // (which also compares unequal to a stale per-bike binding, as required).
    // Per-bike profiles live in a node-based map, so the bound pointer stays
    // valid (just no longer active) after a swap.
    RumbleConfig* boundConfig = &rumbleConfig;
    auto configStillActive = [boundConfig]() {
        const XInputReader& reader = XInputReader::getInstance();
        return &reader.getRumbleConfig() == boundConfig;
    };

    // Register a stepped descriptor (with the rumble post-step work and the
    // profile-binding guard above) and return its index into m_steppedControls
    // (rebuilt in lockstep with m_clickRegions).
    auto registerStepped = [&](SettingsHud::SteppedControl control,
                               const std::function<void()>& postStep) -> int {
        control.postStep = postStep;
        control.valid = configStillActive;
        ctx.parent->m_steppedControls.push_back(std::move(control));
        return static_cast<int>(ctx.parent->m_steppedControls.size()) - 1;
    };

    // Push one stepper arrow click region tied to a registered descriptor.
    auto addArrowRegion = [&](float x, bool up, int steppedIndex) {
        SettingsHud::ClickRegion region(x, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            up ? SettingsHud::ClickRegion::STEPPED_UP
               : SettingsHud::ClickRegion::STEPPED_DOWN,
            nullptr);
        region.steppedIndex = steppedIndex;
        ctx.parent->m_clickRegions.push_back(region);
    };

    // Lambda for rumble effect rows. The arrows are shared STEPPED_UP/STEPPED_DOWN
    // controls: Light/Heavy are accelerated 1% strength steppers (percentFloat);
    // Min/Max step by the fixed inputStep (no hold acceleration) up to inputLimit,
    // with Max clamping down at the effect's live Min (fixedFloatDynamicLo) - all
    // copied verbatim from the old per-effect click handlers.
    // splitInitializedFlag (front/rear rows only) latches "user has set the split
    // values" on any step so they are never reseeded from the combined effect.
    auto addRumbleRow = [&](const char* name, RumbleEffect& effect,
                            float inputStep, float inputLimit,
                            bool useIntegers = false,
                            const char* unit = "",
                            float displayFactor = 1.0f,
                            const char* tooltipId = nullptr,
                            bool* splitInitializedFlag = nullptr) {
        (void)unit;  // Unit is described in the tooltip instead of displayed inline
        // Every rumble stepper marks the per-bike profile dirty when per-bike mode
        // is active (checked at click time, exactly like the old handler did).
        std::function<void()> postStep = [splitInitializedFlag]() {
            if (XInputReader::getInstance().getGlobalRumbleConfig().usePerBikeEffects) {
                RumbleProfileManager::getInstance().markDirty();
            }
            if (splitInitializedFlag) *splitInitializedFlag = true;
        };

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
            int lightIndex = registerStepped(
                SettingsHud::SteppedControl::percentFloat(&effect.lightStrength, nullptr), postStep);
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
            addArrowRegion(currentX, false, lightIndex);
            currentX += cw * 2;
            // Use percent for color to match display logic (avoids floating point precision issues)
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), percent > 0 ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 4;
            ctx.parent->addString(" >", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            addArrowRegion(currentX, true, lightIndex);
        }

        // Heavy motor strength control
        {
            int heavyIndex = registerStepped(
                SettingsHud::SteppedControl::percentFloat(&effect.heavyStrength, nullptr), postStep);
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
            addArrowRegion(currentX, false, heavyIndex);
            currentX += cw * 2;
            // Use percent for color to match display logic (avoids floating point precision issues)
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), percent > 0 ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 4;
            ctx.parent->addString(" >", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            addArrowRegion(currentX, true, heavyIndex);
        }

        // Min input control (fixed step, clamped to [0, inputLimit])
        {
            int minIndex = registerStepped(
                SettingsHud::SteppedControl::fixedFloat(&effect.minInput,
                    inputStep, 0.0f, inputLimit, nullptr), postStep);
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
            addArrowRegion(currentX, false, minIndex);
            currentX += cw * 2;
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), effect.isEnabled() ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 6;
            ctx.parent->addString(">", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            addArrowRegion(currentX, true, minIndex);
        }

        // Max input control (fixed step, up to inputLimit; down clamps at live Min)
        {
            int maxIndex = registerStepped(
                SettingsHud::SteppedControl::fixedFloatDynamicLo(&effect.maxInput,
                    inputStep, &effect.minInput, inputLimit, nullptr), postStep);
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
            addArrowRegion(currentX, false, maxIndex);
            currentX += cw * 2;
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), effect.isEnabled() ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
            currentX += cw * 6;
            ctx.parent->addString(">", currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            addArrowRegion(currentX, true, maxIndex);
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
            1.0f, 50.0f, true, "m/s", 1.0f, "rumble.bumps",
            &rumbleConfig.suspensionSplitInitialized);
        addRumbleRow("- Rear", rumbleConfig.suspensionEffectRear,
            1.0f, 50.0f, true, "m/s", 1.0f, "rumble.bumps",
            &rumbleConfig.suspensionSplitInitialized);
    } else {
        addRumbleRow("Bumps", rumbleConfig.suspensionEffect,
            1.0f, 50.0f, true, "m/s", 1.0f, "rumble.bumps");
    }
    addRumbleRow("Slide", rumbleConfig.slideEffect,
        1.0f, 90.0f, true, "deg", 1.0f, "rumble.slide");
    addRumbleRow("Spin", rumbleConfig.wheelspinEffect,
        1.0f, 50.0f, true, "x", 1.0f, "rumble.spin");
    // Lockup (front/rear splittable)
    drawSplitMarker(rumbleConfig.brakeLockupSplit, SettingsHud::ClickRegion::RUMBLE_LOCKUP_SPLIT_TOGGLE);
    if (rumbleConfig.brakeLockupSplit) {
        drawEffectHeader("Lockup", "rumble.lockup");
        addRumbleRow("- Front", rumbleConfig.brakeLockupEffectFront,
            0.05f, 1.0f, false, "ratio", 1.0f, "rumble.lockup",
            &rumbleConfig.brakeLockupSplitInitialized);
        addRumbleRow("- Rear", rumbleConfig.brakeLockupEffectRear,
            0.05f, 1.0f, false, "ratio", 1.0f, "rumble.lockup",
            &rumbleConfig.brakeLockupSplitInitialized);
    } else {
        addRumbleRow("Lockup", rumbleConfig.brakeLockupEffect,
            0.05f, 1.0f, false, "ratio", 1.0f, "rumble.lockup");
    }
    addRumbleRow("Wheelie", rumbleConfig.wheelieEffect,
        1.0f, 90.0f, true, "deg", 1.0f, "rumble.wheelie");
    addRumbleRow("Steer", rumbleConfig.steerEffect,
        1.0f, 200.0f, true, "Nm", 1.0f, "rumble.steer");
    addRumbleRow("RPM", rumbleConfig.rpmEffect,
        100.0f, 20000.0f, true, "rpm", 1.0f, "rumble.rpm");

    // Rev Limiter: Min/Max are a percentage of the bike's real limiter RPM (auto
    // per-bike); 1% steps, allow buffer past 100
    addRumbleRow("Rev Lim", rumbleConfig.revLimiterEffect,
        1.0f, 110.0f, true, "%", 1.0f, "rumble.revlimiter");

#if GAME_HAS_PIT_LIMITER
    // Pit Limiter: binary effect; Light/Heavy set intensity (only games that report it)
    addRumbleRow("Pit Lim", rumbleConfig.pitLimiterEffect,
        0.05f, 1.0f, false, "", 1.0f, "rumble.pitlimiter");
#endif

    // Surface uses user's speed unit preference (m/s internally, max 200 ~720km/h;
    // 1.39 m/s step = ~5 km/h)
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();
        bool isKmh = speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
        const char* surfaceUnit = isKmh ? "km/h" : "mph";
        float surfaceFactor = isKmh ? 3.6f : 2.23694f;  // m/s to km/h or mph
        addRumbleRow("Surface", rumbleConfig.surfaceEffect,
            1.39f, 200.0f, true, surfaceUnit, surfaceFactor, "rumble.surface");
    }

    // Info text
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Select your controller in the General tab.", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        colors.getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
