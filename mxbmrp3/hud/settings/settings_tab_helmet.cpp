// ============================================================================
// hud/settings/settings_tab_helmet.cpp
// Tab renderer and click handler for the Helmet overlay.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../helmet_overlay_hud.h"
#include "../../core/asset_manager.h"
#include "../../core/color_config.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/settings_manager.h"
#include "../../core/plugin_manager.h"
#include "../../core/hud_manager.h"
#include "../../core/ui_config.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace PluginConstants;

// Helper: clamp + round helpers for sliders
static float adjustClamped(float value, float step, bool increase, float lo, float hi) {
    value += increase ? step : -step;
    value = std::clamp(value, lo, hi);
    // Snap to step grid
    return std::round(value / step) * step;
}

// Cycle a tint color through ColorPalette::ALL_COLORS
static void cycleTintColor(unsigned long& color, bool forward) {
    int idx = ColorPalette::getColorIndex(color);
    if (idx < 0) idx = 0;
    const int n = static_cast<int>(ColorPalette::ALL_COLORS.size());
    if (forward) {
        idx = (idx + 1) % n;
    } else {
        idx = (idx - 1 + n) % n;
    }
    color = ColorPalette::ALL_COLORS[idx];
}

// ----------------------------------------------------------------------------
// Click handler
// ----------------------------------------------------------------------------
bool SettingsHud::handleClickTabHelmet(const ClickRegion& region) {
    HelmetOverlayHud* hud = m_helmetOverlay;
    if (!hud) return false;

    const int sm = getHoldStepMultiplier();
    bool dirty = false;

    switch (region.type) {
        case ClickRegion::HELMET_HELMET_TOGGLE:
            hud->m_helmetEnabled = !hud->m_helmetEnabled;
            if (hud->m_helmetEnabled && !hud->isVisible()) hud->setVisible(true);
            dirty = true;
            break;
        case ClickRegion::HELMET_VISOR_MODE_DOWN:
            hud->m_visorMode = (hud->m_visorMode - 1 + HelmetOverlayHud::VISOR_MODE_COUNT) % HelmetOverlayHud::VISOR_MODE_COUNT;
            if (hud->m_visorMode != HelmetOverlayHud::VISOR_OFF && !hud->isVisible()) hud->setVisible(true);
            dirty = true;
            break;
        case ClickRegion::HELMET_VISOR_MODE_UP:
            hud->m_visorMode = (hud->m_visorMode + 1) % HelmetOverlayHud::VISOR_MODE_COUNT;
            if (hud->m_visorMode != HelmetOverlayHud::VISOR_OFF && !hud->isVisible()) hud->setVisible(true);
            dirty = true;
            break;

        case ClickRegion::HELMET_UPPER_TEX_DOWN:
            hud->cycleHelmetUpperVariant(false);
            dirty = true;
            break;
        case ClickRegion::HELMET_UPPER_TEX_UP:
            hud->cycleHelmetUpperVariant(true);
            dirty = true;
            break;
        case ClickRegion::HELMET_LOWER_TEX_DOWN:
            hud->cycleHelmetLowerVariant(false);
            dirty = true;
            break;
        case ClickRegion::HELMET_LOWER_TEX_UP:
            hud->cycleHelmetLowerVariant(true);
            dirty = true;
            break;

        case ClickRegion::HELMET_UPPER_OFFSET_DOWN:
            hud->m_helmetUpperOffsetY = adjustClamped(hud->m_helmetUpperOffsetY, 0.01f * sm, false,
                -HelmetOverlayHud::MAX_HELMET_OFFSET_Y, HelmetOverlayHud::MAX_HELMET_OFFSET_Y);
            dirty = true;
            break;
        case ClickRegion::HELMET_UPPER_OFFSET_UP:
            hud->m_helmetUpperOffsetY = adjustClamped(hud->m_helmetUpperOffsetY, 0.01f * sm, true,
                -HelmetOverlayHud::MAX_HELMET_OFFSET_Y, HelmetOverlayHud::MAX_HELMET_OFFSET_Y);
            dirty = true;
            break;
        case ClickRegion::HELMET_LOWER_OFFSET_DOWN:
            hud->m_helmetLowerOffsetY = adjustClamped(hud->m_helmetLowerOffsetY, 0.01f * sm, false,
                -HelmetOverlayHud::MAX_HELMET_OFFSET_Y, HelmetOverlayHud::MAX_HELMET_OFFSET_Y);
            dirty = true;
            break;
        case ClickRegion::HELMET_LOWER_OFFSET_UP:
            hud->m_helmetLowerOffsetY = adjustClamped(hud->m_helmetLowerOffsetY, 0.01f * sm, true,
                -HelmetOverlayHud::MAX_HELMET_OFFSET_Y, HelmetOverlayHud::MAX_HELMET_OFFSET_Y);
            dirty = true;
            break;

        case ClickRegion::HELMET_TILT_DOWN:
            hud->m_helmetTiltStrength = adjustClamped(hud->m_helmetTiltStrength, 0.01f * sm, false, -1.0f, 1.0f);
            dirty = true;
            break;
        case ClickRegion::HELMET_TILT_UP:
            hud->m_helmetTiltStrength = adjustClamped(hud->m_helmetTiltStrength, 0.01f * sm, true, -1.0f, 1.0f);
            dirty = true;
            break;
        case ClickRegion::HELMET_VIBRATION_DOWN:
            hud->m_helmetVibrationStrength = adjustClamped(hud->m_helmetVibrationStrength, 0.01f * sm, false, -1.0f, 1.0f);
            dirty = true;
            break;
        case ClickRegion::HELMET_VIBRATION_UP:
            hud->m_helmetVibrationStrength = adjustClamped(hud->m_helmetVibrationStrength, 0.01f * sm, true, -1.0f, 1.0f);
            dirty = true;
            break;

        case ClickRegion::HELMET_VIB_SENS_DOWN:
            hud->m_helmetVibrationSensitivity = adjustClamped(hud->m_helmetVibrationSensitivity, 0.01f * sm, false, 0.0f, 1.0f);
            dirty = true;
            break;
        case ClickRegion::HELMET_VIB_SENS_UP:
            hud->m_helmetVibrationSensitivity = adjustClamped(hud->m_helmetVibrationSensitivity, 0.01f * sm, true, 0.0f, 1.0f);
            dirty = true;
            break;

        case ClickRegion::HELMET_ZOOM_DOWN:
            hud->m_helmetZoom = adjustClamped(hud->m_helmetZoom, 0.01f * sm, false,
                -HelmetOverlayHud::MAX_OVERLAY_ZOOM, HelmetOverlayHud::MAX_OVERLAY_ZOOM);
            dirty = true;
            break;
        case ClickRegion::HELMET_ZOOM_UP:
            hud->m_helmetZoom = adjustClamped(hud->m_helmetZoom, 0.01f * sm, true,
                -HelmetOverlayHud::MAX_OVERLAY_ZOOM, HelmetOverlayHud::MAX_OVERLAY_ZOOM);
            dirty = true;
            break;

        case ClickRegion::HELMET_VISOR_TINT_COLOR_DOWN:
            cycleTintColor(hud->m_visorTintColor, false);
            dirty = true;
            break;
        case ClickRegion::HELMET_VISOR_TINT_COLOR_UP:
            cycleTintColor(hud->m_visorTintColor, true);
            dirty = true;
            break;
        case ClickRegion::HELMET_VISOR_TINT_OPACITY_DOWN:
            hud->m_visorTintOpacity = adjustClamped(hud->m_visorTintOpacity, 0.01f * sm, false, 0.0f, 1.0f);
            dirty = true;
            break;
        case ClickRegion::HELMET_VISOR_TINT_OPACITY_UP:
            hud->m_visorTintOpacity = adjustClamped(hud->m_visorTintOpacity, 0.01f * sm, true, 0.0f, 1.0f);
            dirty = true;
            break;

        default:
            return false;
    }

    if (dirty) {
        hud->setDataDirty();
        setDataDirty();
    }
    return true;
}

// ----------------------------------------------------------------------------
// Tab renderer
// ----------------------------------------------------------------------------
BaseHud* SettingsHud::renderTabHelmet(SettingsLayoutContext& ctx) {
    HelmetOverlayHud* hud = ctx.parent->getHelmetOverlayHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("helmet");

    ColorConfig& colors = ColorConfig::getInstance();

    // Helper: format a normalized 0..1 value as a percentage string
    auto fmtPct = [](float value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(value * 100.0f)));
        return std::string(buf);
    };

    // Helper: format a signed offset as screen percentage (value IS the screen fraction)
    auto fmtOffset = [](float value) {
        char buf[16];
        const int pct = static_cast<int>(std::round(value * 100.0f));
        if (pct == 0) {
            snprintf(buf, sizeof(buf), "0%%");
        } else {
            snprintf(buf, sizeof(buf), "%+d%%", pct);
        }
        return std::string(buf);
    };

    // Helper: format texture variant - "Off" or just the variant number (matches other tabs)
    auto fmtVariant = [](int variant) {
        if (variant <= 0) return std::string("Off");
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", variant);
        return std::string(buf);
    };

    // ========================================================================
    // HELMET SECTION
    // ========================================================================
    ctx.addSectionHeader("Helmet");

    ctx.addToggleControl("Visible", hud->m_helmetEnabled,
        SettingsHud::ClickRegion::HELMET_HELMET_TOGGLE, hud,
        nullptr, 0, true, "helmet.helmet_enabled");

    {
        std::string val = fmtVariant(hud->m_helmetUpperVariant);
        ctx.addCycleControl("Upper texture", val.c_str(), 10,
            SettingsHud::ClickRegion::HELMET_UPPER_TEX_DOWN,
            SettingsHud::ClickRegion::HELMET_UPPER_TEX_UP,
            hud, true, hud->m_helmetUpperVariant == 0,
            "helmet.upper_tex");
    }

    {
        std::string val = fmtVariant(hud->m_helmetLowerVariant);
        ctx.addCycleControl("Lower texture", val.c_str(), 10,
            SettingsHud::ClickRegion::HELMET_LOWER_TEX_DOWN,
            SettingsHud::ClickRegion::HELMET_LOWER_TEX_UP,
            hud, true, hud->m_helmetLowerVariant == 0,
            "helmet.lower_tex");
    }

    {
        char buf[16];
        const int pct = static_cast<int>(std::round(100.0f * (1.0f + hud->m_helmetZoom)));
        snprintf(buf, sizeof(buf), "%d%%", pct);
        ctx.addCycleControl("Zoom", buf, 10,
            SettingsHud::ClickRegion::HELMET_ZOOM_DOWN,
            SettingsHud::ClickRegion::HELMET_ZOOM_UP,
            hud, true, false, "helmet.zoom");
    }

    {
        std::string val = fmtOffset(hud->m_helmetUpperOffsetY);
        ctx.addCycleControl("Upper offset", val.c_str(), 10,
            SettingsHud::ClickRegion::HELMET_UPPER_OFFSET_DOWN,
            SettingsHud::ClickRegion::HELMET_UPPER_OFFSET_UP,
            hud, true, false, "helmet.upper_offset");
    }

    {
        std::string val = fmtOffset(hud->m_helmetLowerOffsetY);
        ctx.addCycleControl("Lower offset", val.c_str(), 10,
            SettingsHud::ClickRegion::HELMET_LOWER_OFFSET_DOWN,
            SettingsHud::ClickRegion::HELMET_LOWER_OFFSET_UP,
            hud, true, false, "helmet.lower_offset");
    }

    // ========================================================================
    // EFFECTS SECTION
    // ========================================================================
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Effects");

    {
        char buf[16];
        int pct = static_cast<int>(std::round(hud->m_helmetTiltStrength * 100.0f));
        if (pct == 0) {
            snprintf(buf, sizeof(buf), "Off");
        } else {
            snprintf(buf, sizeof(buf), "%+d%%", pct);
        }
        ctx.addCycleControl("Tilt strength", buf, 10,
            SettingsHud::ClickRegion::HELMET_TILT_DOWN,
            SettingsHud::ClickRegion::HELMET_TILT_UP,
            hud, true, pct == 0,
            "helmet.tilt");
    }

    {
        char buf[16];
        int pct = static_cast<int>(std::round(hud->m_helmetVibrationStrength * 100.0f));
        if (pct == 0) {
            snprintf(buf, sizeof(buf), "Off");
        } else {
            snprintf(buf, sizeof(buf), "%+d%%", pct);
        }
        ctx.addCycleControl("Vibration strength", buf, 10,
            SettingsHud::ClickRegion::HELMET_VIBRATION_DOWN,
            SettingsHud::ClickRegion::HELMET_VIBRATION_UP,
            hud, true, pct == 0,
            "helmet.vibration");
    }

    {
        std::string val = fmtPct(hud->m_helmetVibrationSensitivity);
        ctx.addCycleControl("Vibration sensitivity", val.c_str(), 10,
            SettingsHud::ClickRegion::HELMET_VIB_SENS_DOWN,
            SettingsHud::ClickRegion::HELMET_VIB_SENS_UP,
            hud, true, hud->m_helmetVibrationSensitivity <= 0.0f,
            "helmet.vib_sensitivity");
    }

    // ========================================================================
    // VISOR SECTION
    // ========================================================================
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Visor");

    {
        static const char* modeNames[] = { "Off", "Goggles", "Visor" };
        ctx.addCycleControl("Mode", modeNames[hud->m_visorMode], 10,
            SettingsHud::ClickRegion::HELMET_VISOR_MODE_DOWN,
            SettingsHud::ClickRegion::HELMET_VISOR_MODE_UP,
            hud, true, false,
            "helmet.visor_mode");
    }

    {
        bool off = (hud->m_visorMode == HelmetOverlayHud::VISOR_OFF);
        std::string val = fmtPct(hud->m_visorTintOpacity);
        ctx.addCycleControl("Tint opacity", val.c_str(), 10,
            SettingsHud::ClickRegion::HELMET_VISOR_TINT_OPACITY_DOWN,
            SettingsHud::ClickRegion::HELMET_VISOR_TINT_OPACITY_UP,
            hud, true, off,
            "helmet.visor_tint_opacity");
    }

    {
        bool off = (hud->m_visorMode == HelmetOverlayHud::VISOR_OFF);
        const char* colorName = ColorPalette::getColorName(hud->m_visorTintColor);
        ctx.addCycleControl("Tint color", colorName, 10,
            SettingsHud::ClickRegion::HELMET_VISOR_TINT_COLOR_DOWN,
            SettingsHud::ClickRegion::HELMET_VISOR_TINT_COLOR_UP,
            hud, true, off,
            "helmet.visor_tint_color");
    }

    return hud;
}
