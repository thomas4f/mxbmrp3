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

// ----------------------------------------------------------------------------
// Click handler
// ----------------------------------------------------------------------------
bool SettingsHud::handleClickTabHelmet(const ClickRegion& region) {
    HelmetOverlayHud* hud = m_helmetOverlay;
    if (!hud) return false;

    bool dirty = false;

    switch (region.type) {
        case ClickRegion::HELMET_HELMET_TOGGLE:
            hud->m_helmetEnabled = !hud->m_helmetEnabled;
            if (hud->m_helmetEnabled && !hud->isVisible()) hud->setVisible(true);
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
    ctx.addSectionHeading("Helmet");

    ctx.addToggleControl("Visible", hud->m_helmetEnabled,
        SettingsHud::ClickRegion::HELMET_HELMET_TOGGLE, hud,
        nullptr, 0, true, "helmet.helmet_enabled");

    // Textures: Off, then each variant found -- a dropdown, as every list is.
    ctx.addCycleControl("Upper texture", fmtVariant(hud->m_helmetUpperVariant).c_str(),
        textureVariantCycle(AssetManager::getInstance().getAvailableVariants(HelmetOverlayHud::TEX_HELMET_UPPER),
            [hud]() { return hud->m_helmetUpperVariant; },
            [hud](int v) { hud->setHelmetUpperVariant(v); }, hud),
        hud, true, hud->m_helmetUpperVariant == 0, "helmet.upper_tex");
    ctx.addCycleControl("Lower texture", fmtVariant(hud->m_helmetLowerVariant).c_str(),
        textureVariantCycle(AssetManager::getInstance().getAvailableVariants(HelmetOverlayHud::TEX_HELMET_LOWER),
            [hud]() { return hud->m_helmetLowerVariant; },
            [hud](int v) { hud->setHelmetLowerVariant(v); }, hud),
        hud, true, hud->m_helmetLowerVariant == 0, "helmet.lower_tex");

    // The numbers: sliders, 1% a step (accelerated while held), as before.
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(100.0f * (1.0f + hud->m_helmetZoom))));
        ctx.addSteppedControl("Zoom", buf,
            SettingsHud::SteppedControl::stepFloat(&hud->m_helmetZoom, 0.01f,
                -HelmetOverlayHud::MAX_OVERLAY_ZOOM, HelmetOverlayHud::MAX_OVERLAY_ZOOM, hud),
            hud, true, false, "helmet.zoom");
    }
    ctx.addSteppedControl("Upper offset", fmtOffset(hud->m_helmetUpperOffsetY).c_str(),
        SettingsHud::SteppedControl::stepFloat(&hud->m_helmetUpperOffsetY, 0.01f,
            -HelmetOverlayHud::MAX_HELMET_OFFSET_Y, HelmetOverlayHud::MAX_HELMET_OFFSET_Y, hud),
        hud, true, false, "helmet.upper_offset");
    ctx.addSteppedControl("Lower offset", fmtOffset(hud->m_helmetLowerOffsetY).c_str(),
        SettingsHud::SteppedControl::stepFloat(&hud->m_helmetLowerOffsetY, 0.01f,
            -HelmetOverlayHud::MAX_HELMET_OFFSET_Y, HelmetOverlayHud::MAX_HELMET_OFFSET_Y, hud),
        hud, true, false, "helmet.lower_offset");

    // ========================================================================
    // EFFECTS SECTION
    // ========================================================================
    ctx.addSectionHeading("Effects");

    // Signed strengths read "Off" at zero.
    auto fmtSigned = [](float value) {
        char buf[16];
        const int pct = static_cast<int>(std::round(value * 100.0f));
        if (pct == 0) snprintf(buf, sizeof(buf), "Off");
        else snprintf(buf, sizeof(buf), "%+d%%", pct);
        return std::string(buf);
    };
    ctx.addSteppedControl("Tilt strength", fmtSigned(hud->m_helmetTiltStrength).c_str(),
        SettingsHud::SteppedControl::stepFloat(&hud->m_helmetTiltStrength, 0.01f, -1.0f, 1.0f, hud),
        hud, true, std::round(hud->m_helmetTiltStrength * 100.0f) == 0.0f, "helmet.tilt");
    ctx.addSteppedControl("Vibration strength", fmtSigned(hud->m_helmetVibrationStrength).c_str(),
        SettingsHud::SteppedControl::stepFloat(&hud->m_helmetVibrationStrength, 0.01f, -1.0f, 1.0f, hud),
        hud, true, std::round(hud->m_helmetVibrationStrength * 100.0f) == 0.0f, "helmet.vibration");
    ctx.addSteppedControl("Vibration sensitivity", fmtPct(hud->m_helmetVibrationSensitivity).c_str(),
        SettingsHud::SteppedControl::stepFloat(&hud->m_helmetVibrationSensitivity, 0.01f, 0.0f, 1.0f, hud),
        hud, true, hud->m_helmetVibrationSensitivity <= 0.0f, "helmet.vib_sensitivity");

    // ========================================================================
    // VISOR SECTION
    // ========================================================================
    ctx.addSectionHeading("Visor");

    {
        static const char* const kModes[] = { "Off", "Goggles", "Visor" };
        static_assert(sizeof(kModes) / sizeof(kModes[0]) == HelmetOverlayHud::VISOR_MODE_COUNT,
                      "one name per visor mode");
        SettingsHud::CycleControl mode = SettingsHud::CycleControl::enumMember(
            hud, &HelmetOverlayHud::m_visorMode, HelmetOverlayHud::VISOR_MODE_COUNT, hud, kModes);
        // A visor needs the overlay drawn: picking one shows it.
        mode.postStep = [hud]() {
            if (hud->m_visorMode != HelmetOverlayHud::VISOR_OFF && !hud->isVisible()) hud->setVisible(true);
        };
        ctx.addCycleControl("Mode", cycleName(kModes, hud->m_visorMode), mode,
            hud, true, false, "helmet.visor_mode");
    }

    // Tint does nothing without a visor: greyed out like any row whose setting
    // is moot, not drawn as an "Off" value.
    const bool visorOff = (hud->m_visorMode == HelmetOverlayHud::VISOR_OFF);
    ctx.addSteppedControl("Tint opacity", fmtPct(hud->m_visorTintOpacity).c_str(),
        SettingsHud::SteppedControl::stepFloat(&hud->m_visorTintOpacity, 0.01f, 0.0f, 1.0f, hud),
        hud, !visorOff, false, "helmet.visor_tint_opacity");
    ctx.addCycleControl("Tint color", ColorPalette::getColorName(hud->m_visorTintColor),
        paletteCycle([hud]() { return hud->m_visorTintColor; },
                     [hud](unsigned long c) { hud->m_visorTintColor = c; }, hud),
        hud, !visorOff, false, "helmet.visor_tint_color");

    return hud;
}
