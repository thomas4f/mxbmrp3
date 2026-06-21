// ============================================================================
// hud/settings/settings_tab_hotkeys.cpp
// Tab renderer for Hotkeys settings (keyboard and controller bindings)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/hotkey_manager.h"
#include "../../core/color_config.h"
#include "../../game/game_config.h"

using namespace PluginConstants;

// Member function of SettingsHud - handles click events for Hotkeys tab
bool SettingsHud::handleClickTabHotkeys(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::HOTKEY_KEYBOARD_BIND:
            {
                auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                if (actionPtr) {
                    HotkeyManager::getInstance().startCapture(*actionPtr, CaptureType::KEYBOARD);
                    setDataDirty();
                }
            }
            return true;

        case ClickRegion::HOTKEY_CONTROLLER_BIND:
            {
                auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                if (actionPtr) {
                    HotkeyManager::getInstance().startCapture(*actionPtr, CaptureType::CONTROLLER);
                    setDataDirty();
                }
            }
            return true;

        case ClickRegion::HOTKEY_KEYBOARD_CLEAR:
            {
                auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                if (actionPtr) {
                    HotkeyManager::getInstance().clearKeyboardBinding(*actionPtr);
                    setDataDirty();
                }
            }
            return true;

        case ClickRegion::HOTKEY_CONTROLLER_CLEAR:
            {
                auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                if (actionPtr) {
                    HotkeyManager::getInstance().clearControllerBinding(*actionPtr);
                    setDataDirty();
                }
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabHotkeys(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("hotkeys");

    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    ColorConfig& colorConfig = ColorConfig::getInstance();
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);

    // Column layout - wider fields for better readability
    float actionX = ctx.labelX;
    float keyboardX = actionX + charWidth * 14;  // After action name
    float controllerX = keyboardX + charWidth * 21;  // After keyboard binding (wider);
                                                     // 21 (not 22) keeps the clear "x" inside the row highlight

    // Field widths (characters inside brackets)
    constexpr int kbFieldWidth = 16;   // Fits "Ctrl+Shift+F12"
    constexpr int ctrlFieldWidth = 12; // Fits "Right Shoulder"

    ctx.parent->addString("Toggle", actionX, ctx.currentY, Justify::LEFT,
        Fonts::getStrong(), colorConfig.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Keyboard", keyboardX, ctx.currentY, Justify::LEFT,
        Fonts::getStrong(), colorConfig.getPrimary(), ctx.fontSize);
    ctx.parent->addString("Controller", controllerX, ctx.currentY, Justify::LEFT,
        Fonts::getStrong(), colorConfig.getPrimary(), ctx.fontSize);
    ctx.currentY += ctx.lineHeightNormal;

    // Store layout info for hover detection in update()
    ctx.parent->m_hotkeyContentStartY = ctx.currentY;
    ctx.parent->m_hotkeyRowHeight = ctx.lineHeightNormal;
    ctx.parent->m_hotkeyRowTops.clear();  // Refilled per row below (handles the spacer gaps)
    ctx.parent->m_hotkeyKeyboardX = keyboardX;
    ctx.parent->m_hotkeyControllerX = controllerX;
    ctx.parent->m_hotkeyFieldCharWidth = charWidth;

    // Check if we're in capture mode
    bool isCapturing = hotkeyMgr.isCapturing();
    HotkeyAction captureAction = hotkeyMgr.getCaptureAction();
    CaptureType captureType = hotkeyMgr.getCaptureType();

    // Track row index for hover detection
    int currentRowIndex = 0;

    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);

    // Helper to get tooltip ID for an action
    auto getTooltipId = [](HotkeyAction action) -> const char* {
        switch (action) {
            case HotkeyAction::TOGGLE_SETTINGS:    return "hotkeys.settings";
            case HotkeyAction::TOGGLE_STANDINGS:   return "hotkeys.standings";
            case HotkeyAction::TOGGLE_MAP:         return "hotkeys.map";
            case HotkeyAction::TOGGLE_RADAR:       return "hotkeys.radar";
            case HotkeyAction::TOGGLE_LAP_LOG:     return "hotkeys.lap_log";
            case HotkeyAction::TOGGLE_IDEAL_LAP:   return "hotkeys.ideal_lap";
            case HotkeyAction::TOGGLE_TELEMETRY:   return "hotkeys.telemetry";
            case HotkeyAction::TOGGLE_INPUT:       return "hotkeys.input";
            case HotkeyAction::TOGGLE_RECORDS:     return "hotkeys.records";
            case HotkeyAction::TOGGLE_PITBOARD:    return "hotkeys.pitboard";
            case HotkeyAction::TOGGLE_TIMING:      return "hotkeys.timing";
            case HotkeyAction::TOGGLE_GAP_BAR:     return "hotkeys.gap_bar";
            case HotkeyAction::TOGGLE_PERFORMANCE:     return "hotkeys.performance";
            case HotkeyAction::TOGGLE_LAP_CONSISTENCY: return "hotkeys.lap_consistency";
            case HotkeyAction::TOGGLE_FMX:             return "hotkeys.fmx";
            case HotkeyAction::TOGGLE_STATS:           return "hotkeys.stats";
            case HotkeyAction::TOGGLE_SESSION:         return "hotkeys.session";
            case HotkeyAction::TOGGLE_NOTICES:         return "hotkeys.notices";
            case HotkeyAction::TOGGLE_EVENT_LOG:       return "hotkeys.event_log";
            case HotkeyAction::TOGGLE_HELMET:          return "hotkeys.helmet";
            case HotkeyAction::TOGGLE_FRIENDS:         return "hotkeys.friends";
            case HotkeyAction::OVERLAY_FORCE_LAST_LAP:    return "hotkeys.overlay_last_lap";
            case HotkeyAction::OVERLAY_FORCE_FASTEST_LAP: return "hotkeys.overlay_fastest_lap";
            case HotkeyAction::OVERLAY_FORCE_DOWN_ORDER:  return "hotkeys.overlay_down_order";
            case HotkeyAction::OVERLAY_FORCE_BATTLE:      return "hotkeys.overlay_battle";
            case HotkeyAction::SEGMENT_ADD:               return "hotkeys.segment_add";
            case HotkeyAction::SEGMENT_REMOVE:            return "hotkeys.segment_remove";
            case HotkeyAction::TOGGLE_RUMBLE:      return "hotkeys.rumble";
            case HotkeyAction::TOGGLE_WIDGETS:     return "hotkeys.widgets";
            case HotkeyAction::TOGGLE_ALL_HUDS:    return "hotkeys.all_huds";
            case HotkeyAction::RELOAD_CONFIG:      return "hotkeys.reload";
            default: return nullptr;
        }
    };

    // Draw a bracketed binding field with the brackets pinned to the monospace
    // grid (a fixed columnX and close-bracket x), independent of the active font's
    // glyph advance - so the keyboard/controller columns stay aligned regardless
    // of the chosen Normal font. The binding text is truncated to the field width
    // and drawn between the fixed brackets. The matching click region still spans
    // the full field (unchanged at each call site below).
    auto drawField = [&](float columnX, int fieldWidth, const char* text, unsigned long color) {
        ctx.parent->addString("[", columnX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), color, ctx.fontSize);
        char inner[48];
        snprintf(inner, sizeof(inner), "%.*s", fieldWidth, text);  // truncate to fieldWidth chars
        ctx.parent->addString(inner, columnX + charWidth, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), color, ctx.fontSize);
        ctx.parent->addString("]", columnX + charWidth * (fieldWidth + 1), ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), color, ctx.fontSize);
    };

    // Helper to add a hotkey row
    auto addHotkeyRow = [&](HotkeyAction action) {
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);

        // Record this row's top Y so hover detection maps cursor->row exactly,
        // regardless of the half-row spacers inserted between groups.
        ctx.parent->m_hotkeyRowTops.push_back(ctx.currentY);

        // Add row-wide tooltip region
        const char* tooltipId = getTooltipId(action);
        if (tooltipId) {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, tooltipId
            ));
        }

        // Check if this row is hovered (using tracked row index)
        bool isRowHovered = (currentRowIndex == ctx.parent->m_hoveredHotkeyRow);

        // Action name
        ctx.parent->addString(getActionDisplayName(action), actionX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Keyboard binding
        bool isCapturingKeyboard = isCapturing && captureAction == action && captureType == CaptureType::KEYBOARD;
        float kbX = keyboardX;

        if (isCapturingKeyboard) {
            // Show capture prompt with real-time modifier feedback (accent color)
            ModifierFlags currentMods = hotkeyMgr.getCurrentModifiers();
            std::string modPrefix;
            if (hasModifier(currentMods, ModifierFlags::CTRL)) modPrefix += "Ctrl+";
            if (hasModifier(currentMods, ModifierFlags::SHIFT)) modPrefix += "Shift+";
            if (hasModifier(currentMods, ModifierFlags::ALT)) modPrefix += "Alt+";

            char prompt[40];
            if (modPrefix.empty()) {
                snprintf(prompt, sizeof(prompt), "Press Key...");
            } else {
                snprintf(prompt, sizeof(prompt), "%s...", modPrefix.c_str());
            }
            drawField(kbX, kbFieldWidth, prompt, colorConfig.getAccent());
        } else {
            // Show current binding
            char keyStr[32];
            formatKeyBinding(binding.keyboard, keyStr, sizeof(keyStr));

            // Determine color: hovered > bound > unbound
            bool isKbHovered = (ctx.parent->m_hoveredHotkeyRow == currentRowIndex &&
                               ctx.parent->m_hoveredHotkeyColumn == HotkeyColumn::KEYBOARD);
            unsigned long keyColor;
            if (isKbHovered) {
                keyColor = colorConfig.getAccent();
            } else if (binding.hasKeyboard()) {
                keyColor = colorConfig.getPrimary();
            } else {
                keyColor = colorConfig.getMuted();
            }
            drawField(kbX, kbFieldWidth, keyStr, keyColor);

            // Click region for keyboard binding (covers full field)
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                kbX, ctx.currentY, charWidth * (kbFieldWidth + 2), ctx.lineHeightNormal,
                SettingsHud::ClickRegion::HOTKEY_KEYBOARD_BIND, action
            ));

            // Clear button if bound (only show on hover)
            if (binding.hasKeyboard() && isRowHovered) {
                float clearX = kbX + charWidth * (kbFieldWidth + 2.5f);
                ctx.parent->addString("x", clearX, ctx.currentY, Justify::LEFT,
                    Fonts::getNormal(), colorConfig.getNegative(), ctx.fontSize);
                ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                    clearX, ctx.currentY, charWidth * 2, ctx.lineHeightNormal,
                    SettingsHud::ClickRegion::HOTKEY_KEYBOARD_CLEAR, action
                ));
            }
        }

        // Controller binding
        bool isCapturingController = isCapturing && captureAction == action && captureType == CaptureType::CONTROLLER;
        float ctrlX = controllerX;

        if (isCapturingController) {
            // Show capture prompt (accent color)
            drawField(ctrlX, ctrlFieldWidth, "Press Btn...", colorConfig.getAccent());
        } else {
            // Show current binding
            const char* btnName = getControllerButtonName(binding.controller);

            // Determine color: hovered > bound > unbound
            bool isCtrlHovered = (ctx.parent->m_hoveredHotkeyRow == currentRowIndex &&
                                 ctx.parent->m_hoveredHotkeyColumn == HotkeyColumn::CONTROLLER);
            unsigned long btnColor;
            if (isCtrlHovered) {
                btnColor = colorConfig.getAccent();
            } else if (binding.hasController()) {
                btnColor = colorConfig.getPrimary();
            } else {
                btnColor = colorConfig.getMuted();
            }
            drawField(ctrlX, ctrlFieldWidth, btnName, btnColor);

            // Click region for controller binding (covers full field)
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctrlX, ctx.currentY, charWidth * (ctrlFieldWidth + 2), ctx.lineHeightNormal,
                SettingsHud::ClickRegion::HOTKEY_CONTROLLER_BIND, action
            ));

            // Clear button if bound (only show on hover)
            if (binding.hasController() && isRowHovered) {
                float clearX = ctrlX + charWidth * (ctrlFieldWidth + 2.5f);
                ctx.parent->addString("x", clearX, ctx.currentY, Justify::LEFT,
                    Fonts::getNormal(), colorConfig.getNegative(), ctx.fontSize);
                ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                    clearX, ctx.currentY, charWidth * 2, ctx.lineHeightNormal,
                    SettingsHud::ClickRegion::HOTKEY_CONTROLLER_CLEAR, action
                ));
            }
        }

        ctx.currentY += ctx.lineHeightNormal;
        ++currentRowIndex;
    };

    // Settings Menu pinned at the top (the master toggle for this menu).
    addHotkeyRow(HotkeyAction::TOGGLE_SETTINGS);

    // NOTE: Several actions have no row here to keep the tab within the panel;
    // they remain bindable by hand-editing the [Hotkeys] section of the INI
    // (rumble_key=, timing_key=, notices_key=, stats_key=, friends_key=,
    // event_log_key=, fmx_key=, helmet_key=, performance_key=, ...).
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("HUDs");
    addHotkeyRow(HotkeyAction::TOGGLE_STANDINGS);
    addHotkeyRow(HotkeyAction::TOGGLE_MAP);
    addHotkeyRow(HotkeyAction::TOGGLE_RADAR);
    addHotkeyRow(HotkeyAction::TOGGLE_LAP_LOG);
    addHotkeyRow(HotkeyAction::TOGGLE_IDEAL_LAP);
    addHotkeyRow(HotkeyAction::TOGGLE_LAP_CONSISTENCY);
    addHotkeyRow(HotkeyAction::TOGGLE_TELEMETRY);
    addHotkeyRow(HotkeyAction::TOGGLE_RECORDS);
    addHotkeyRow(HotkeyAction::TOGGLE_PITBOARD);
    addHotkeyRow(HotkeyAction::TOGGLE_SESSION);
    addHotkeyRow(HotkeyAction::TOGGLE_GAP_BAR);

    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Segments");
    addHotkeyRow(HotkeyAction::SEGMENT_ADD);
    addHotkeyRow(HotkeyAction::SEGMENT_REMOVE);

    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Other");
    addHotkeyRow(HotkeyAction::TOGGLE_WIDGETS);
    addHotkeyRow(HotkeyAction::TOGGLE_ALL_HUDS);
    addHotkeyRow(HotkeyAction::RELOAD_CONFIG);

#if GAME_HAS_HTTP_SERVER
    // Web overlay broadcaster controls: force a bottom-slot panel to slide in now.
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Web Overlay");
    addHotkeyRow(HotkeyAction::OVERLAY_FORCE_LAST_LAP);
    addHotkeyRow(HotkeyAction::OVERLAY_FORCE_FASTEST_LAP);
    addHotkeyRow(HotkeyAction::OVERLAY_FORCE_BATTLE);
    addHotkeyRow(HotkeyAction::OVERLAY_FORCE_DOWN_ORDER);
#endif

    // Info text at bottom
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Click to rebind, ESC to cancel.", actionX, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize * 0.9f);

    // No active HUD for hotkeys settings
    return nullptr;
}
