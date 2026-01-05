// ============================================================================
// hud/settings/settings_tab_rumble.cpp
// Tab renderer for Rumble/Controller settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../rumble_hud.h"
#include "../speed_widget.h"
#include "../../core/xinput_reader.h"
#include <cmath>
#include <algorithm>

// Helper to adjust rumble effect strengths (used by many click handlers)
static void adjustEffectStrength(float& value, bool increase) {
    constexpr float STRENGTH_STEP = 0.10f;
    if (increase) {
        value = std::min(value + STRENGTH_STEP, 1.0f);
    } else {
        value = std::max(value - STRENGTH_STEP, 0.0f);
    }
}

// Member function of SettingsHud - handles click events for Rumble tab
bool SettingsHud::handleClickTabRumble(const ClickRegion& region) {
    RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();

    // Lambda for effect adjustments - avoids massive switch statement
    auto handleEffectControl = [&](RumbleEffect& effect,
        ClickRegion::Type lightDown, ClickRegion::Type lightUp,
        ClickRegion::Type heavyDown, ClickRegion::Type heavyUp,
        ClickRegion::Type minDown, ClickRegion::Type minUp,
        ClickRegion::Type maxDown, ClickRegion::Type maxUp,
        float minStep = 1.0f, float maxStep = 1.0f) -> bool {
        if (region.type == lightUp) {
            adjustEffectStrength(effect.lightStrength, true);
            setDataDirty();
            return true;
        } else if (region.type == lightDown) {
            adjustEffectStrength(effect.lightStrength, false);
            setDataDirty();
            return true;
        } else if (region.type == heavyUp) {
            adjustEffectStrength(effect.heavyStrength, true);
            setDataDirty();
            return true;
        } else if (region.type == heavyDown) {
            adjustEffectStrength(effect.heavyStrength, false);
            setDataDirty();
            return true;
        } else if (region.type == minUp) {
            effect.minInput += minStep;
            setDataDirty();
            return true;
        } else if (region.type == minDown) {
            effect.minInput = std::max(effect.minInput - minStep, 0.0f);
            setDataDirty();
            return true;
        } else if (region.type == maxUp) {
            effect.maxInput += maxStep;
            setDataDirty();
            return true;
        } else if (region.type == maxDown) {
            effect.maxInput = std::max(effect.maxInput - maxStep, effect.minInput);
            setDataDirty();
            return true;
        }
        return false;
    };

    switch (region.type) {
        case ClickRegion::RUMBLE_TOGGLE:
            config.enabled = !config.enabled;
            setDataDirty();
            return true;

        case ClickRegion::RUMBLE_BLEND_TOGGLE:
            config.additiveBlend = !config.additiveBlend;
            setDataDirty();
            return true;

        case ClickRegion::RUMBLE_CRASH_TOGGLE:
            config.rumbleWhenCrashed = !config.rumbleWhenCrashed;
            setDataDirty();
            return true;

        case ClickRegion::RUMBLE_HUD_TOGGLE:
            if (m_rumble) {
                m_rumble->setVisible(!m_rumble->isVisible());
                rebuildRenderData();
            }
            return true;

        default:
            break;
    }

    // Handle suspension effect controls
    if (handleEffectControl(config.suspensionEffect,
        ClickRegion::RUMBLE_SUSP_LIGHT_DOWN, ClickRegion::RUMBLE_SUSP_LIGHT_UP,
        ClickRegion::RUMBLE_SUSP_HEAVY_DOWN, ClickRegion::RUMBLE_SUSP_HEAVY_UP,
        ClickRegion::RUMBLE_SUSP_MIN_DOWN, ClickRegion::RUMBLE_SUSP_MIN_UP,
        ClickRegion::RUMBLE_SUSP_MAX_DOWN, ClickRegion::RUMBLE_SUSP_MAX_UP)) {
        return true;
    }

    // Handle wheelspin effect controls
    if (handleEffectControl(config.wheelspinEffect,
        ClickRegion::RUMBLE_WHEEL_LIGHT_DOWN, ClickRegion::RUMBLE_WHEEL_LIGHT_UP,
        ClickRegion::RUMBLE_WHEEL_HEAVY_DOWN, ClickRegion::RUMBLE_WHEEL_HEAVY_UP,
        ClickRegion::RUMBLE_WHEEL_MIN_DOWN, ClickRegion::RUMBLE_WHEEL_MIN_UP,
        ClickRegion::RUMBLE_WHEEL_MAX_DOWN, ClickRegion::RUMBLE_WHEEL_MAX_UP)) {
        return true;
    }

    // Handle brake lockup effect controls
    if (handleEffectControl(config.brakeLockupEffect,
        ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN, ClickRegion::RUMBLE_LOCKUP_LIGHT_UP,
        ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN, ClickRegion::RUMBLE_LOCKUP_HEAVY_UP,
        ClickRegion::RUMBLE_LOCKUP_MIN_DOWN, ClickRegion::RUMBLE_LOCKUP_MIN_UP,
        ClickRegion::RUMBLE_LOCKUP_MAX_DOWN, ClickRegion::RUMBLE_LOCKUP_MAX_UP,
        0.05f, 0.05f)) {  // Lockup uses smaller steps (ratio values)
        return true;
    }

    // Handle wheelie effect controls
    if (handleEffectControl(config.wheelieEffect,
        ClickRegion::RUMBLE_WHEELIE_LIGHT_DOWN, ClickRegion::RUMBLE_WHEELIE_LIGHT_UP,
        ClickRegion::RUMBLE_WHEELIE_HEAVY_DOWN, ClickRegion::RUMBLE_WHEELIE_HEAVY_UP,
        ClickRegion::RUMBLE_WHEELIE_MIN_DOWN, ClickRegion::RUMBLE_WHEELIE_MIN_UP,
        ClickRegion::RUMBLE_WHEELIE_MAX_DOWN, ClickRegion::RUMBLE_WHEELIE_MAX_UP)) {
        return true;
    }

    // Handle RPM effect controls
    if (handleEffectControl(config.rpmEffect,
        ClickRegion::RUMBLE_RPM_LIGHT_DOWN, ClickRegion::RUMBLE_RPM_LIGHT_UP,
        ClickRegion::RUMBLE_RPM_HEAVY_DOWN, ClickRegion::RUMBLE_RPM_HEAVY_UP,
        ClickRegion::RUMBLE_RPM_MIN_DOWN, ClickRegion::RUMBLE_RPM_MIN_UP,
        ClickRegion::RUMBLE_RPM_MAX_DOWN, ClickRegion::RUMBLE_RPM_MAX_UP,
        100.0f, 100.0f)) {  // RPM uses larger steps
        return true;
    }

    // Handle slide effect controls
    if (handleEffectControl(config.slideEffect,
        ClickRegion::RUMBLE_SLIDE_LIGHT_DOWN, ClickRegion::RUMBLE_SLIDE_LIGHT_UP,
        ClickRegion::RUMBLE_SLIDE_HEAVY_DOWN, ClickRegion::RUMBLE_SLIDE_HEAVY_UP,
        ClickRegion::RUMBLE_SLIDE_MIN_DOWN, ClickRegion::RUMBLE_SLIDE_MIN_UP,
        ClickRegion::RUMBLE_SLIDE_MAX_DOWN, ClickRegion::RUMBLE_SLIDE_MAX_UP)) {
        return true;
    }

    // Handle surface effect controls
    if (handleEffectControl(config.surfaceEffect,
        ClickRegion::RUMBLE_SURFACE_LIGHT_DOWN, ClickRegion::RUMBLE_SURFACE_LIGHT_UP,
        ClickRegion::RUMBLE_SURFACE_HEAVY_DOWN, ClickRegion::RUMBLE_SURFACE_HEAVY_UP,
        ClickRegion::RUMBLE_SURFACE_MIN_DOWN, ClickRegion::RUMBLE_SURFACE_MIN_UP,
        ClickRegion::RUMBLE_SURFACE_MAX_DOWN, ClickRegion::RUMBLE_SURFACE_MAX_UP,
        1.39f, 1.39f)) {  // Surface uses speed-based steps (5 km/h in m/s)
        return true;
    }

    // Handle steer effect controls
    if (handleEffectControl(config.steerEffect,
        ClickRegion::RUMBLE_STEER_LIGHT_DOWN, ClickRegion::RUMBLE_STEER_LIGHT_UP,
        ClickRegion::RUMBLE_STEER_HEAVY_DOWN, ClickRegion::RUMBLE_STEER_HEAVY_UP,
        ClickRegion::RUMBLE_STEER_MIN_DOWN, ClickRegion::RUMBLE_STEER_MIN_UP,
        ClickRegion::RUMBLE_STEER_MAX_DOWN, ClickRegion::RUMBLE_STEER_MAX_UP)) {
        return true;
    }

    return false;
}

// Static member function of SettingsHud - inherits friend access to RumbleHud
BaseHud* SettingsHud::renderTabRumble(SettingsLayoutContext& ctx) {
    RumbleHud* hud = ctx.parent->getRumbleHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("rumble");

    // Add standard HUD controls (Visible, Title, Texture, Opacity, Scale)
    ctx.addStandardHudControls(hud);

    RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
    float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    ColorConfig& colors = ColorConfig::getInstance();
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);

    // === RUMBLE SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Rumble");

    // Master rumble enable
    ctx.addToggleControl("Enabled", rumbleConfig.enabled,
        SettingsHud::ClickRegion::RUMBLE_TOGGLE, hud, nullptr, 0, true, "rumble.enabled");

    // Stack mode
    ctx.addToggleControl("Stack Forces", rumbleConfig.additiveBlend,
        SettingsHud::ClickRegion::RUMBLE_BLEND_TOGGLE, hud, nullptr, 0, true, "rumble.stack");

    // Rumble when crashed
    ctx.addToggleControl("When Crashed", rumbleConfig.rumbleWhenCrashed,
        SettingsHud::ClickRegion::RUMBLE_CRASH_TOGGLE, hud, nullptr, 0, true, "rumble.crashed");

    // === EFFECTS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Effects");

    // Table header - columns: Effect | Light | Heavy | Min | Max
    float effectX = ctx.labelX;
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
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), effect.lightStrength > 0 ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
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
            ctx.parent->addString(valueStr, currentX, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), effect.heavyStrength > 0 ? colors.getPrimary() : colors.getMuted(), ctx.fontSize);
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
                PluginConstants::Fonts::getNormal(), colors.getPrimary(), ctx.fontSize);
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
                PluginConstants::Fonts::getNormal(), colors.getPrimary(), ctx.fontSize);
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

    // Effect rows
    addRumbleRow("Bumps", rumbleConfig.suspensionEffect,
        SettingsHud::ClickRegion::RUMBLE_SUSP_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_SUSP_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_SUSP_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_SUSP_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_SUSP_MAX_UP, true, "m/s", 1.0f, "rumble.bumps");
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
    addRumbleRow("Lockup", rumbleConfig.brakeLockupEffect,
        SettingsHud::ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_LIGHT_UP,
        SettingsHud::ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_HEAVY_UP,
        SettingsHud::ClickRegion::RUMBLE_LOCKUP_MIN_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_MIN_UP,
        SettingsHud::ClickRegion::RUMBLE_LOCKUP_MAX_DOWN, SettingsHud::ClickRegion::RUMBLE_LOCKUP_MAX_UP, false, "ratio", 1.0f, "rumble.lockup");
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
    ctx.parent->addString("Select your controller in the General tab", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        colors.getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
