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
#include <cstring>

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

        default:
            return false;
    }
}

// A right-click on a binding field clears it: the fields sit two bindings to a
// row, with no room for a clear button beside each. True when it was one.
bool SettingsHud::handleRightClickTabHotkeys(const ClickRegion& region) {
    if (region.type != ClickRegion::HOTKEY_KEYBOARD_BIND &&
        region.type != ClickRegion::HOTKEY_CONTROLLER_BIND) {
        return false;
    }
    if (auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer)) {
        HotkeyManager& hotkeys = HotkeyManager::getInstance();
        if (region.type == ClickRegion::HOTKEY_KEYBOARD_BIND) {
            hotkeys.clearKeyboardBinding(*actionPtr);
        } else {
            hotkeys.clearControllerBinding(*actionPtr);
        }
        setDataDirty();
        markSettingsDirty();
    }
    return true;
}

// The binding fields are short (two bindings share a row), so a long chord drops
// its modifiers to initials: "Ctrl+Shift+F12" reads "C+S+F12". Short ones keep
// the words.
static void formatCompactKeyBinding(const KeyBinding& binding, char* buffer, size_t bufferSize,
                                    size_t fieldChars) {
    formatKeyBinding(binding, buffer, bufferSize);
    if (strlen(buffer) <= fieldChars) return;
    snprintf(buffer, bufferSize, "%s%s%s%s",
        hasModifier(binding.modifiers, ModifierFlags::CTRL) ? "C+" : "",
        hasModifier(binding.modifiers, ModifierFlags::SHIFT) ? "S+" : "",
        hasModifier(binding.modifiers, ModifierFlags::ALT) ? "A+" : "",
        getKeyName(binding.keyCode));
}

// The pad field holds eight characters: the D-pad directions drop "D-Pad", which
// the column needs no reminder of -- nothing else on a pad is a direction.
static const char* compactButtonName(ControllerButton button) {
    switch (button) {
        case ControllerButton::DPAD_UP:    return "Up";
        case ControllerButton::DPAD_DOWN:  return "Down";
        case ControllerButton::DPAD_LEFT:  return "Left";
        case ControllerButton::DPAD_RIGHT: return "Right";
        default: return getControllerButtonName(button);
    }
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabHotkeys(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("hotkeys");

    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    ColorConfig& colorConfig = ColorConfig::getInstance();
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);

    // TWO BINDINGS TO A ROW. Each cell is a name, a keyboard field and a pad field: a 9-character
    // name column (the action names are kept that short in getActionDisplayName),
    // then two input boxes of the same width, filling the grid cell exactly.
    constexpr int NAME_CHARS = 10;
    constexpr int kbFieldWidth = HOTKEY_KEY_FIELD;
    constexpr int ctrlFieldWidth = HOTKEY_PAD_FIELD;
    const auto keyboardXAt = [&](float cellLeft) { return cellLeft + charWidth * NAME_CHARS; };
    const auto controllerXAt = [&](float cellLeft) {
        return keyboardXAt(cellLeft) + charWidth * (kbFieldWidth + 2);
    };

    // Store layout info for hover detection in update()
    ctx.parent->m_hotkeyRowHeight = ctx.lineHeightNormal;
    ctx.parent->m_hotkeyCells.clear();  // Refilled per binding below
    ctx.parent->m_hotkeyFieldCharWidth = charWidth;

    // Check if we're in capture mode
    bool isCapturing = hotkeyMgr.isCapturing();
    HotkeyAction captureAction = hotkeyMgr.getCaptureAction();
    CaptureType captureType = hotkeyMgr.getCaptureType();

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
            case HotkeyAction::TOGGLE_SESSION_CHARTS:     return "hotkeys.session_charts";
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
            case HotkeyAction::SPOTTER_CUE:               return "hotkeys.spotter_cue";
            case HotkeyAction::CRASH_RESET:               return "hotkeys.crash_reset";
            case HotkeyAction::OVERLAY_FORCE_SECTORS:     return "hotkeys.overlay_sectors";
            case HotkeyAction::OVERLAY_FORCE_CHARTS:      return "hotkeys.overlay_charts";
            case HotkeyAction::SEGMENT_ADD:               return "hotkeys.segment_add";
            case HotkeyAction::SEGMENT_REMOVE:            return "hotkeys.segment_remove";
            case HotkeyAction::DIRECTOR_TOGGLE:           return "hotkeys.director_toggle";
            case HotkeyAction::DIRECTOR_LOCK:             return "hotkeys.director_lock";
            case HotkeyAction::TOGGLE_STREAM_CHAT:        return "hotkeys.stream_chat";
            case HotkeyAction::TOGGLE_DELTA_TRACE:        return "hotkeys.delta_trace";
            case HotkeyAction::TOGGLE_RUMBLE:      return "hotkeys.rumble";
            case HotkeyAction::TOGGLE_WIDGETS:     return "hotkeys.widgets";
            case HotkeyAction::TOGGLE_ALL_HUDS:    return "hotkeys.all_huds";
            case HotkeyAction::RELOAD_CONFIG:      return "hotkeys.reload";
            default: return nullptr;
        }
    };

    // Helper to add one binding: a cell of the current two-column run
    auto addHotkeyRow = [&](HotkeyAction action) {
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);
        const int cellIndex = static_cast<int>(ctx.parent->m_hotkeyCells.size());
        const float kbX = keyboardXAt(ctx.labelX);
        const float ctrlX = controllerXAt(ctx.labelX);
        ctx.parent->m_hotkeyCells.push_back({ctx.currentY, kbX, ctrlX});

        // Tooltip region (endRow trims it to the cell)
        const char* tooltipId = getTooltipId(action);
        if (tooltipId) {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, ctx.rowSpanWidth(), ctx.lineHeightNormal, tooltipId
            ));
        }

        const bool isCellHovered = (cellIndex == ctx.parent->m_hoveredHotkeyRow);

        // Action name
        ctx.parent->addString(getActionDisplayName(action), ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Keyboard binding
        if (isCapturing && captureAction == action && captureType == CaptureType::KEYBOARD) {
            // Capture prompt with real-time modifier feedback (accent color)
            ModifierFlags currentMods = hotkeyMgr.getCurrentModifiers();
            char mods[8];
            snprintf(mods, sizeof(mods), "%s%s%s",
                hasModifier(currentMods, ModifierFlags::CTRL) ? "C+" : "",
                hasModifier(currentMods, ModifierFlags::SHIFT) ? "S+" : "",
                hasModifier(currentMods, ModifierFlags::ALT) ? "A+" : "");
            char prompt[16];
            if (mods[0]) snprintf(prompt, sizeof(prompt), "%s...", mods);
            else snprintf(prompt, sizeof(prompt), "Press key");
            ctx.addInputField(kbX, kbFieldWidth, prompt, colorConfig.getAccent(), true);
        } else {
            char keyStr[32];
            formatCompactKeyBinding(binding.keyboard, keyStr, sizeof(keyStr), kbFieldWidth);
            // Determine color: hovered > bound > unbound
            unsigned long keyColor;
            if (isCellHovered && ctx.parent->m_hoveredHotkeyColumn == HotkeyColumn::KEYBOARD) {
                keyColor = colorConfig.getAccent();
            } else if (binding.hasKeyboard()) {
                keyColor = colorConfig.getPrimary();
            } else {
                keyColor = colorConfig.getMuted();
            }
            ctx.addInputField(kbX, kbFieldWidth, keyStr, keyColor);
        }
        // Click region for the keyboard field (covers the full field); a right-click
        // on it clears the binding (SettingsHud::handleRightClick)
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            kbX, ctx.currentY, charWidth * (kbFieldWidth + 1), ctx.lineHeightNormal,
            SettingsHud::ClickRegion::HOTKEY_KEYBOARD_BIND, action
        ));

        // Controller binding
        if (isCapturing && captureAction == action && captureType == CaptureType::CONTROLLER) {
            ctx.addInputField(ctrlX, ctrlFieldWidth, "Press", colorConfig.getAccent(), true);
        } else {
            unsigned long btnColor;
            if (isCellHovered && ctx.parent->m_hoveredHotkeyColumn == HotkeyColumn::CONTROLLER) {
                btnColor = colorConfig.getAccent();
            } else if (binding.hasController()) {
                btnColor = colorConfig.getPrimary();
            } else {
                btnColor = colorConfig.getMuted();
            }
            ctx.addInputField(ctrlX, ctrlFieldWidth, compactButtonName(binding.controller), btnColor);
        }
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctrlX, ctx.currentY, charWidth * (ctrlFieldWidth + 1), ctx.lineHeightNormal,
            SettingsHud::ClickRegion::HOTKEY_CONTROLLER_BIND, action
        ));

        ctx.endRow();
    };

    // A section of bindings, two to a row, filled down the left column first
    auto addHotkeySection = [&](const HotkeyAction* actions, int count) {
        ctx.beginColumns(2, count);
        for (int i = 0; i < count; ++i) addHotkeyRow(actions[i]);
        ctx.endColumns();
    };

    // The field captions ride on the first card's heading row, over both columns'
    // fields, on their text column. "Gamepad" rather than "Controller": the
    // caption has the pad field's eight characters to sit in.
    const float headingY = ctx.addSectionHeading("Toggles");
    for (int col = 0; col < 2; ++col) {
        const float cellLeft = ctx.cellX(col, 2);
        ctx.parent->addString("Keyboard", keyboardXAt(cellLeft) + charWidth, headingY, Justify::LEFT,
            Fonts::getStrong(), colorConfig.getPrimary(), ctx.fontSize);
        ctx.parent->addString("Gamepad", controllerXAt(cellLeft) + charWidth, headingY, Justify::LEFT,
            Fonts::getStrong(), colorConfig.getPrimary(), ctx.fontSize);
    }

    // Every HUD's toggle, in the sidebar's order (its own tabs, then the More
    // page's, then the global ones). Only the crash widget's Reset stays INI-only
    // (crash_reset_key=): the widget carries its own Reset button.
    static const HotkeyAction kHuds[] = {
        HotkeyAction::TOGGLE_SETTINGS,
        HotkeyAction::TOGGLE_MAP,
        HotkeyAction::TOGGLE_NOTICES,
        HotkeyAction::TOGGLE_STANDINGS,
#if GAME_HAS_STEAM_FRIENDS
        HotkeyAction::TOGGLE_FRIENDS,
#endif
        HotkeyAction::TOGGLE_TIMING,
        HotkeyAction::TOGGLE_LAP_LOG,
        HotkeyAction::TOGGLE_GAP_BAR,
        HotkeyAction::TOGGLE_DELTA_TRACE,
        HotkeyAction::TOGGLE_PITBOARD,
        HotkeyAction::TOGGLE_RADAR,
        HotkeyAction::TOGGLE_IDEAL_LAP,
        HotkeyAction::TOGGLE_SESSION,
        HotkeyAction::TOGGLE_PERFORMANCE,
        HotkeyAction::TOGGLE_STATS,
        HotkeyAction::TOGGLE_TELEMETRY,
#if GAME_HAS_RECORDS_PROVIDER
        HotkeyAction::TOGGLE_RECORDS,
#endif
#if GAME_HAS_FMX
        HotkeyAction::TOGGLE_FMX,
#endif
        HotkeyAction::TOGGLE_EVENT_LOG,
        HotkeyAction::TOGGLE_SESSION_CHARTS,
        HotkeyAction::TOGGLE_RUMBLE,
        HotkeyAction::TOGGLE_HELMET,
        HotkeyAction::TOGGLE_STREAM_CHAT,
    };
    addHotkeySection(kHuds, static_cast<int>(sizeof(kHuds) / sizeof(kHuds[0])));

    // Broadcast: the casting tools - auto-director + web-overlay panel forces. The
    // spotter cue speaks whatever the active pack defines as `hotkey_triggered`, so
    // it is also the way to hear a template you are editing.
    static const HotkeyAction kBroadcast[] = {
        HotkeyAction::DIRECTOR_TOGGLE,
        HotkeyAction::DIRECTOR_LOCK,
#if GAME_HAS_HTTP_SERVER
        HotkeyAction::OVERLAY_FORCE_LAST_LAP,
        HotkeyAction::OVERLAY_FORCE_FASTEST_LAP,
        HotkeyAction::OVERLAY_FORCE_SECTORS,
        HotkeyAction::OVERLAY_FORCE_CHARTS,
        HotkeyAction::OVERLAY_FORCE_DOWN_ORDER,
#endif
        HotkeyAction::SPOTTER_CUE,
    };
    ctx.addSectionHeading("Broadcast");
    addHotkeySection(kBroadcast, static_cast<int>(sizeof(kBroadcast) / sizeof(kBroadcast[0])));

    static const HotkeyAction kOther[] = {
        HotkeyAction::SEGMENT_ADD,
        HotkeyAction::SEGMENT_REMOVE,
        HotkeyAction::TOGGLE_WIDGETS,
        HotkeyAction::TOGGLE_ALL_HUDS,
        HotkeyAction::RELOAD_CONFIG,
    };
    ctx.addSectionHeading("Other");
    addHotkeySection(kOther, static_cast<int>(sizeof(kOther) / sizeof(kOther[0])));

    // No active HUD for hotkeys settings
    return nullptr;
}
