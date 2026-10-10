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
            // Toggle effect profile mode (stored in global config), through the
            // globalConfig reference from the top of this function.
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
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);

    // Global config for master settings (enabled, blend, crashed, profile mode)
    const RumbleConfig& globalConfig = XInputReader::getInstance().getGlobalRumbleConfig();
    // Active config for effect settings (global or per-bike based on mode)
    RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
    ColorConfig& colors = ColorConfig::getInstance();
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.rowSpanWidth();

    // === RUMBLE SECTION ===
    ctx.addSectionHeading("Rumble");
    ctx.beginColumns(2, 4);   // side by side (beginColumns)

    // Master rumble enable (always from global config)
    ctx.addToggleControl("Enabled", globalConfig.enabled,
        SettingsHud::ClickRegion::RUMBLE_TOGGLE, hud, nullptr, 0, true, "rumble.enabled");

    // Stack mode (always from global config)
    ctx.addToggleControl("Stack forces", globalConfig.additiveBlend,
        SettingsHud::ClickRegion::RUMBLE_BLEND_TOGGLE, hud, nullptr, 0, true, "rumble.stack");

    // Rumble when crashed (always from global config)
    ctx.addToggleControl("When crashed", globalConfig.rumbleWhenCrashed,
        SettingsHud::ClickRegion::RUMBLE_CRASH_TOGGLE, hud, nullptr, 0, true, "rumble.crashed");

    // Effect profile (per-bike vs global) - uses global config to determine mode
    // Pass true for isOn since both options are valid active states (not on/off)
    ctx.addToggleControl("Effect profile", true,
        SettingsHud::ClickRegion::RUMBLE_EFFECT_PROFILE_TOGGLE, hud, nullptr, 0, true, "rumble.effect_profile",
        globalConfig.usePerBikeEffects ? "Per-Bike" : "Global");
    ctx.endColumns();

    // === EFFECTS SECTION ===
    const float headingY = ctx.addSectionHeading("Effects");

    // Table columns: [gutter] Effect | Light | Heavy | Min | Max, across the full
    // content width. The column labels ride on the "Effects" heading row, as on the
    // Hotkeys and Widgets tabs, over their values; the heading itself captions the
    // effect-name column. The gutter holds the split disclosure caret on splittable
    // effects (Bumps, Lockup). Every value is a slider cell: its arrows step it,
    // the track under it drags it.
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    constexpr int GUTTER_CHARS = 2;
    constexpr int NAME_CHARS = 12;      // "Rev limiter", "- Front"
    constexpr int CELL_GAP_CHARS = 2;
    const int tableChars = static_cast<int>(std::floor(rowWidth / charW + 0.01f));
    const int cellChars = std::max(9,
        (tableChars - GUTTER_CHARS - NAME_CHARS - 3 * CELL_GAP_CHARS) / 4);
    const int valueChars = cellChars - 4;   // "< " + value + " >"
    const float markerX = ctx.labelX;
    const float gutterW = charW * GUTTER_CHARS;
    const float effectX = ctx.labelX + gutterW;
    const float lightX = effectX + charW * NAME_CHARS;
    const float heavyX = lightX + charW * (cellChars + CELL_GAP_CHARS);
    const float minX = heavyX + charW * (cellChars + CELL_GAP_CHARS);
    const float maxX = minX + charW * (cellChars + CELL_GAP_CHARS);

    ctx.parent->addString("Light", lightX + charW * 2.0f, headingY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Heavy", heavyX + charW * 2.0f, headingY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Min", minX + charW * 2.0f, headingY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Max", maxX + charW * 2.0f, headingY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);

    // The stepped descriptors below bind raw pointers into the ACTIVE rumble
    // config resolved above. In per-bike profile mode that object changes when
    // the player swaps bikes — which can happen while this menu sits open — and
    // a click through the stale layout would then edit the PREVIOUS bike's
    // profile. Capture the bound config's identity here and validate it at
    // click time (SteppedControl::valid, which the slider inherits): on mismatch
    // the click is swallowed and the layout rebuilt against the right profile.
    // The const getRumbleConfig overload is used deliberately — it never
    // auto-creates a profile, and it falls back to the global config when the new
    // bike has no profile yet (which also compares unequal to a stale per-bike
    // binding, as required). Per-bike profiles live in a node-based map, so the
    // bound pointer stays valid (just no longer active) after a swap.
    RumbleConfig* boundConfig = &rumbleConfig;
    auto configStillActive = [boundConfig]() {
        const XInputReader& reader = XInputReader::getInstance();
        return &reader.getRumbleConfig() == boundConfig;
    };

    // One slider cell over a descriptor, with the rumble post-step work and the
    // profile-binding guard above.
    auto addEffectCell = [&](float x, const char* value, SettingsHud::SteppedControl control,
                             const std::function<void()>& postStep, bool muted) {
        control.postStep = postStep;
        control.valid = configStillActive;
        ctx.addInlineSteppedControl(x, value, valueChars, control, nullptr, true, nullptr, muted);
    };

    // Lambda for rumble effect rows. Light/Heavy are accelerated 1% strength
    // steppers (percentFloat); Min/Max step by the fixed inputStep (no hold
    // acceleration) up to inputLimit, with Max clamping down at the effect's live
    // Min (fixedFloatDynamicLo).
    // splitInitializedFlag (front/rear rows only) latches "user has set the split
    // values" on any step so they are never reseeded from the combined effect.
    auto addRumbleRow = [&](const char* name, RumbleEffect& effect,
                            float inputStep, float inputLimit,
                            bool useIntegers = false,
                            float displayFactor = 1.0f,
                            const char* tooltipId = nullptr,
                            bool* splitInitializedFlag = nullptr) {
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
            PluginConstants::Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);

        // Light / Heavy motor strength: "Off" at 0, else a percentage. Muted from
        // the percent, not the float, to match the text (no FP edge).
        auto strengthCell = [&](float x, float& strength) {
            char valueStr[8];
            const int percent = static_cast<int>(std::round(strength * 100.0f));
            if (percent <= 0) {
                snprintf(valueStr, sizeof(valueStr), "Off");
            } else {
                snprintf(valueStr, sizeof(valueStr), "%d%%", percent);
            }
            addEffectCell(x, valueStr, SettingsHud::SteppedControl::percentFloat(&strength, nullptr),
                postStep, percent <= 0);
        };
        strengthCell(lightX, effect.lightStrength);
        strengthCell(heavyX, effect.heavyStrength);

        // Min / Max input thresholds, in the effect's display unit
        auto formatInput = [&](char* out, size_t size, float value) {
            const float displayValue = value * displayFactor;
            if (displayFactor != 1.0f) {
                const int rounded = static_cast<int>(std::round(displayValue / 5.0f)) * 5;
                snprintf(out, size, "%d", rounded);
            } else if (useIntegers) {
                snprintf(out, size, "%d", static_cast<int>(std::round(displayValue)));
            } else {
                snprintf(out, size, "%.2f", displayValue);
            }
        };
        char minStr[12];
        formatInput(minStr, sizeof(minStr), effect.minInput);
        addEffectCell(minX, minStr, SettingsHud::SteppedControl::fixedFloat(&effect.minInput,
            inputStep, 0.0f, inputLimit, nullptr), postStep, !effect.isEnabled());
        char maxStr[12];
        formatInput(maxStr, sizeof(maxStr), effect.maxInput);
        addEffectCell(maxX, maxStr, SettingsHud::SteppedControl::fixedFloatDynamicLo(&effect.maxInput,
            inputStep, &effect.minInput, inputLimit, nullptr), postStep, !effect.isEnabled());

        ctx.currentY += ctx.lineHeightNormal;
    };

    // Draw the split disclosure caret in the gutter on a splittable row, the same
    // caret-up the dropdowns use: right = merged, down = split. With UI icons off,
    // ">"/"v" in its place, as the dropdowns do (the gutter has no room for "[+]").
    // Returns the row's y for addSplitRegion, which runs AFTER the row: hover takes
    // the first region that matches, so a caret region ahead of the row's tooltip
    // region dropped the row band and its description under the cursor (clicks skip
    // TOOLTIP_ROW, so the caret still wins the click).
    auto drawSplitMarker = [&](bool split) {
        const float caretX = markerX + charW * 0.75f;
        if (!ctx.parent->addDisclosureCaret(caretX,
                ctx.currentY + ctx.lineHeightNormal * 0.5f - (2.0f / 1080.0f) * ctx.scale,
                ctx.fontSize * 0.25f, split, colors.getAccent())) {
            ctx.parent->addString(split ? "v" : ">", caretX, ctx.currentY, PluginConstants::Justify::CENTER,
                PluginConstants::Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
        }
        return ctx.currentY;
    };
    auto addSplitRegion = [&](float rowY, SettingsHud::ClickRegion::Type toggleType) {
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            markerX, rowY, gutterW, ctx.lineHeightNormal, toggleType, nullptr));
    };

    // Draw a bare effect-name row (the parent header above the Front/Rear rows when split).
    auto drawEffectHeader = [&](const char* name, const char* tooltipId) {
        if (tooltipId && tooltipId[0] != '\0') {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, tooltipId));
        }
        ctx.parent->addString(name, effectX, ctx.currentY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;
    };

    // The panel's height is measured with both groups OPEN (the tallest state of
    // this tab), so opening one never outgrows the panel.
    const bool measuringTallest = ctx.parent->m_measuringTallest;
    const bool bumpsSplit = rumbleConfig.suspensionSplit || measuringTallest;
    const bool lockupSplit = rumbleConfig.brakeLockupSplit || measuringTallest;

    // Effect rows
    // Bumps (front/rear splittable). The marker shares the row with the single linked
    // row, or with the "Bumps" header above the Front/Rear rows when expanded.
    const float bumpsY = drawSplitMarker(bumpsSplit);
    if (bumpsSplit) {
        drawEffectHeader("Bumps", "rumble.bumps");
        addSplitRegion(bumpsY, SettingsHud::ClickRegion::RUMBLE_SUSP_SPLIT_TOGGLE);
        addRumbleRow("- Front", rumbleConfig.suspensionEffectFront,
            1.0f, 50.0f, true, 1.0f, "rumble.bumps",
            &rumbleConfig.suspensionSplitInitialized);
        addRumbleRow("- Rear", rumbleConfig.suspensionEffectRear,
            1.0f, 50.0f, true, 1.0f, "rumble.bumps",
            &rumbleConfig.suspensionSplitInitialized);
    } else {
        addRumbleRow("Bumps", rumbleConfig.suspensionEffect,
            1.0f, 50.0f, true, 1.0f, "rumble.bumps");
        addSplitRegion(bumpsY, SettingsHud::ClickRegion::RUMBLE_SUSP_SPLIT_TOGGLE);
    }
    addRumbleRow("Slide", rumbleConfig.slideEffect,
        1.0f, 90.0f, true, 1.0f, "rumble.slide");
    addRumbleRow("Wheelspin", rumbleConfig.wheelspinEffect,
        1.0f, 50.0f, true, 1.0f, "rumble.spin");
    // Lockup (front/rear splittable)
    const float lockupY = drawSplitMarker(lockupSplit);
    if (lockupSplit) {
        drawEffectHeader("Lockup", "rumble.lockup");
        addSplitRegion(lockupY, SettingsHud::ClickRegion::RUMBLE_LOCKUP_SPLIT_TOGGLE);
        addRumbleRow("- Front", rumbleConfig.brakeLockupEffectFront,
            0.05f, 1.0f, false, 1.0f, "rumble.lockup",
            &rumbleConfig.brakeLockupSplitInitialized);
        addRumbleRow("- Rear", rumbleConfig.brakeLockupEffectRear,
            0.05f, 1.0f, false, 1.0f, "rumble.lockup",
            &rumbleConfig.brakeLockupSplitInitialized);
    } else {
        addRumbleRow("Lockup", rumbleConfig.brakeLockupEffect,
            0.05f, 1.0f, false, 1.0f, "rumble.lockup");
        addSplitRegion(lockupY, SettingsHud::ClickRegion::RUMBLE_LOCKUP_SPLIT_TOGGLE);
    }
    addRumbleRow("Wheelie", rumbleConfig.wheelieEffect,
        1.0f, 90.0f, true, 1.0f, "rumble.wheelie");
    addRumbleRow("Steering", rumbleConfig.steerEffect,
        1.0f, 200.0f, true, 1.0f, "rumble.steer");
    addRumbleRow("RPM", rumbleConfig.rpmEffect,
        100.0f, 20000.0f, true, 1.0f, "rumble.rpm");

    // Rev Limiter: Min/Max are a percentage of the bike's real limiter RPM (auto
    // per-bike); 1% steps, allow buffer past 100
    addRumbleRow("Rev limiter", rumbleConfig.revLimiterEffect,
        1.0f, 110.0f, true, 1.0f, "rumble.revlimiter");

#if GAME_HAS_PIT_LIMITER
    // Pit Limiter: binary effect; Light/Heavy set intensity (only games that report it)
    addRumbleRow("Pit limiter", rumbleConfig.pitLimiterEffect,
        0.05f, 1.0f, false, 1.0f, "rumble.pitlimiter");
#endif

    // Surface uses user's speed unit preference (m/s internally, max 200 ~720km/h;
    // 1.39 m/s step = ~5 km/h); the unit is described in the tooltip
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();
        bool isKmh = speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
        float surfaceFactor = isKmh ? 3.6f : 2.23694f;  // m/s to km/h or mph
        addRumbleRow("Surface", rumbleConfig.surfaceEffect,
            1.39f, 200.0f, true, surfaceFactor, "rumble.surface");
    }

    ctx.addNote("Tip: select your controller in the General tab.");

    return hud;
}
