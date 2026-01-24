// ============================================================================
// hud/settings/settings_tab_general.cpp
// Tab renderer for General settings (preferences, profiles, reset)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/profile_manager.h"
#include "../../core/settings_manager.h"
#include "../../core/hud_manager.h"
#include "../../core/xinput_reader.h"
#include "../../core/color_config.h"
#include "../../core/ui_config.h"
#if GAME_HAS_DISCORD
#include "../../core/discord_manager.h"
#endif

using namespace PluginConstants;

// Member function of SettingsHud - handles click events for General tab
bool SettingsHud::handleClickTabGeneral(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::SPEED_UNIT_TOGGLE:
            if (m_speed) {
                auto currentUnit = m_speed->getSpeedUnit();
                m_speed->setSpeedUnit(currentUnit == SpeedWidget::SpeedUnit::MPH
                    ? SpeedWidget::SpeedUnit::KMH
                    : SpeedWidget::SpeedUnit::MPH);
                setDataDirty();
            }
            return true;

        case ClickRegion::FUEL_UNIT_TOGGLE:
            if (m_fuel) {
                auto currentUnit = m_fuel->getFuelUnit();
                m_fuel->setFuelUnit(currentUnit == FuelWidget::FuelUnit::LITERS
                    ? FuelWidget::FuelUnit::GALLONS
                    : FuelWidget::FuelUnit::LITERS);
                setDataDirty();
            }
            return true;

        case ClickRegion::TEMP_UNIT_TOGGLE:
            {
                auto currentUnit = UiConfig::getInstance().getTemperatureUnit();
                UiConfig::getInstance().setTemperatureUnit(
                    currentUnit == TemperatureUnit::CELSIUS
                        ? TemperatureUnit::FAHRENHEIT
                        : TemperatureUnit::CELSIUS);
                // Also update SessionHud since it displays temperature
                if (m_session) {
                    m_session->setDataDirty();
                }
                setDataDirty();
            }
            return true;

        case ClickRegion::GRID_SNAP_TOGGLE:
            {
                bool current = UiConfig::getInstance().getGridSnapping();
                UiConfig::getInstance().setGridSnapping(!current);
                setDataDirty();
            }
            return true;

        case ClickRegion::SCREEN_CLAMP_TOGGLE:
            {
                bool current = UiConfig::getInstance().getScreenClamping();
                UiConfig::getInstance().setScreenClamping(!current);
                setDataDirty();
            }
            return true;

        case ClickRegion::AUTOSAVE_TOGGLE:
            {
                bool current = UiConfig::getInstance().getAutoSave();
                UiConfig::getInstance().setAutoSave(!current);
                setDataDirty();
            }
            return true;

#if GAME_HAS_DISCORD
        case ClickRegion::DISCORD_TOGGLE:
            {
                bool current = DiscordManager::getInstance().isEnabled();
                DiscordManager::getInstance().setEnabled(!current);
                setDataDirty();
            }
            return true;
#endif

        // Note: PROFILE_CYCLE_UP/DOWN moved to common handlers (work from all tabs)

        case ClickRegion::AUTO_SWITCH_TOGGLE:
            {
                bool current = ProfileManager::getInstance().isAutoSwitchEnabled();
                ProfileManager::getInstance().setAutoSwitchEnabled(!current);
                setDataDirty();
            }
            return true;

        case ClickRegion::COPY_TARGET_UP:
            {
                ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
                int8_t activeIdx = static_cast<int8_t>(activeProfile);

                if (m_copyTargetProfile == -1) {
                    m_copyTargetProfile = 4;  // All
                } else if (m_copyTargetProfile == 4) {
                    m_copyTargetProfile = 0;
                    if (m_copyTargetProfile == activeIdx) {
                        m_copyTargetProfile++;
                    }
                } else {
                    m_copyTargetProfile++;
                    if (m_copyTargetProfile == activeIdx) {
                        m_copyTargetProfile++;
                    }
                    if (m_copyTargetProfile >= static_cast<int8_t>(ProfileType::COUNT)) {
                        m_copyTargetProfile = -1;
                    }
                }
                rebuildRenderData();
            }
            return true;  // Don't save - just UI state

        case ClickRegion::COPY_TARGET_DOWN:
            {
                ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
                int8_t activeIdx = static_cast<int8_t>(activeProfile);

                if (m_copyTargetProfile == -1) {
                    m_copyTargetProfile = static_cast<int8_t>(ProfileType::COUNT) - 1;
                    if (m_copyTargetProfile == activeIdx) {
                        m_copyTargetProfile--;
                    }
                } else if (m_copyTargetProfile == 4) {
                    m_copyTargetProfile = -1;
                } else if (m_copyTargetProfile == 0) {
                    m_copyTargetProfile = 4;
                } else {
                    m_copyTargetProfile--;
                    if (m_copyTargetProfile == activeIdx) {
                        m_copyTargetProfile--;
                    }
                    if (m_copyTargetProfile < 0) {
                        m_copyTargetProfile = 4;
                    }
                }
                rebuildRenderData();
            }
            return true;  // Don't save - just UI state

        case ClickRegion::RESET_PROFILE_CHECKBOX:
            m_resetProfileConfirmed = !m_resetProfileConfirmed;
            if (m_resetProfileConfirmed) {
                m_resetAllConfirmed = false;
            }
            rebuildRenderData();
            return true;  // Don't save - just UI state

        case ClickRegion::RESET_ALL_CHECKBOX:
            m_resetAllConfirmed = !m_resetAllConfirmed;
            if (m_resetAllConfirmed) {
                m_resetProfileConfirmed = false;
            }
            rebuildRenderData();
            return true;  // Don't save - just UI state

        case ClickRegion::COPY_BUTTON:
            if (m_copyTargetProfile != -1) {
                if (m_copyTargetProfile == 4) {
                    SettingsManager::getInstance().applyToAllProfiles(HudManager::getInstance());
                } else {
                    ProfileType targetProfile = static_cast<ProfileType>(m_copyTargetProfile);
                    SettingsManager::getInstance().copyToProfile(HudManager::getInstance(), targetProfile);
                }
                m_copyTargetProfile = -1;
            }
            return true;

        case ClickRegion::RESET_BUTTON:
            if (m_resetProfileConfirmed) {
                resetCurrentProfile();
                m_resetProfileConfirmed = false;
            } else if (m_resetAllConfirmed) {
                resetToDefaults();
                m_resetAllConfirmed = false;
            }
            return true;

        // Controller selection is also in General tab
        case ClickRegion::RUMBLE_CONTROLLER_UP:
            {
                RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                config.controllerIndex = (config.controllerIndex + 2) % 5 - 1;
                XInputReader::getInstance().setControllerIndex(config.controllerIndex);
                setDataDirty();
            }
            return true;

        case ClickRegion::RUMBLE_CONTROLLER_DOWN:
            {
                RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                config.controllerIndex = (config.controllerIndex + 5) % 5 - 1;
                XInputReader::getInstance().setControllerIndex(config.controllerIndex);
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabGeneral(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("general");

    ColorConfig& colorConfig = ColorConfig::getInstance();
    float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);
    // Standard value width for all controls (matches addToggleControl)
    constexpr int VALUE_WIDTH = 10;  // Standard width for vertical alignment

    // === PREFERENCES SECTION ===
    ctx.addSectionHeader("Preferences");

    // Controller selector (used by both Gamepad Widget and Rumble)
    // Cycles: Disabled -> 1 -> 2 -> 3 -> 4 -> Disabled
    {
        RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
        int controllerIdx = rumbleConfig.controllerIndex;
        bool isDisabled = (controllerIdx < 0);
        bool isConnected = !isDisabled && XInputReader::isControllerConnected(controllerIdx);
        std::string controllerName = isDisabled ? "" : XInputReader::getControllerName(controllerIdx);

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.controller"
        ));

        ctx.parent->addString("Controller", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Controller uses wider value area for device names
        // Format: "1: Xbox 360 Controlle" = slot (1) + ": " (2) + name (up to 18) = 21 max
        constexpr int CONTROLLER_VALUE_WIDTH = 21;
        float currentX = ctx.controlX;
        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::RUMBLE_CONTROLLER_DOWN, nullptr
        ));
        currentX += cw * 2;

        // Show controller status
        // Format: "Disabled" or "1: Name..." or "1: Not Connected"
        char displayStr[32];
        if (isDisabled) {
            snprintf(displayStr, sizeof(displayStr), "%-*s", CONTROLLER_VALUE_WIDTH, "Disabled");
        } else {
            int slot = controllerIdx + 1;
            char tempStr[32];
            if (!controllerName.empty()) {
                snprintf(tempStr, sizeof(tempStr), "%d: %.18s", slot, controllerName.c_str());
            } else if (isConnected) {
                snprintf(tempStr, sizeof(tempStr), "%d: Connected", slot);
            } else {
                snprintf(tempStr, sizeof(tempStr), "%d: Not Connected", slot);
            }
            snprintf(displayStr, sizeof(displayStr), "%-*s", CONTROLLER_VALUE_WIDTH, tempStr);
        }

        // Color: muted for disabled, positive for connected, muted for not connected
        uint32_t textColor = isDisabled ? colorConfig.getMuted() :
            (isConnected ? colorConfig.getPositive() : colorConfig.getMuted());
        ctx.parent->addString(displayStr, currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), textColor, ctx.fontSize);
        currentX += cw * CONTROLLER_VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::RUMBLE_CONTROLLER_UP, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Speed unit toggle
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.speed_unit"
        ));

        ctx.parent->addString("Speed Unit", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current unit with < > cycle pattern (arrows=accent, value=primary)
        bool isKmh = speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE, speedWidget
        ));
        currentX += cw * 2;

        // Left-align value within VALUE_WIDTH for consistent positioning
        std::string formattedValue = ctx.formatValue(isKmh ? "km/h" : "mph", VALUE_WIDTH, false);
        ctx.parent->addString(formattedValue.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE, speedWidget
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Fuel unit toggle
    {
        FuelWidget* fuelWidget = ctx.parent->getFuelWidget();

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.fuel_unit"
        ));

        ctx.parent->addString("Fuel Unit", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current unit with < > cycle pattern (arrows=accent, value=primary)
        bool isGallons = fuelWidget && fuelWidget->getFuelUnit() == FuelWidget::FuelUnit::GALLONS;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE, fuelWidget
        ));
        currentX += cw * 2;

        // Left-align value within VALUE_WIDTH for consistent positioning
        std::string formattedFuel = ctx.formatValue(isGallons ? "gal" : "L", VALUE_WIDTH, false);
        ctx.parent->addString(formattedFuel.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE, fuelWidget
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Temperature unit toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.temp_unit"
        ));

        ctx.parent->addString("Temp Unit", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current unit with < > cycle pattern (arrows=accent, value=primary)
        bool isFahrenheit = UiConfig::getInstance().getTemperatureUnit() == TemperatureUnit::FAHRENHEIT;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        // Left-align value within VALUE_WIDTH for consistent positioning
        std::string formattedTemp = ctx.formatValue(isFahrenheit ? "F" : "C", VALUE_WIDTH, false);
        ctx.parent->addString(formattedTemp.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Grid snap toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.grid_snap"
        ));

        ctx.parent->addString("Grid Snap", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current state with < > cycle pattern (arrows=accent, value=primary)
        bool gridSnapEnabled = UiConfig::getInstance().getGridSnapping();
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::GRID_SNAP_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        std::string formattedSnap = ctx.formatValue(gridSnapEnabled ? "On" : "Off", VALUE_WIDTH, false);
        ctx.parent->addString(formattedSnap.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), gridSnapEnabled ? colorConfig.getPrimary() : colorConfig.getMuted(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::GRID_SNAP_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Screen clamp toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.screen_clamp"
        ));

        ctx.parent->addString("Screen Clamp", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current state with < > cycle pattern (arrows=accent, value=primary)
        bool screenClampEnabled = UiConfig::getInstance().getScreenClamping();
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::SCREEN_CLAMP_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        std::string formattedClamp = ctx.formatValue(screenClampEnabled ? "On" : "Off", VALUE_WIDTH, false);
        ctx.parent->addString(formattedClamp.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), screenClampEnabled ? colorConfig.getPrimary() : colorConfig.getMuted(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::SCREEN_CLAMP_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Auto-save toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.auto_save"
        ));

        ctx.parent->addString("Auto-Save", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current state with < > cycle pattern (arrows=accent, value=primary)
        bool autoSaveEnabled = UiConfig::getInstance().getAutoSave();
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::AUTOSAVE_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        std::string formattedAutoSave = ctx.formatValue(autoSaveEnabled ? "On" : "Off", VALUE_WIDTH, false);
        ctx.parent->addString(formattedAutoSave.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), autoSaveEnabled ? colorConfig.getPrimary() : colorConfig.getMuted(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::AUTOSAVE_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

#if GAME_HAS_DISCORD
    // Discord Rich Presence toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.discord"
        ));

        ctx.parent->addString("Discord", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current state with < > cycle pattern (arrows=accent, value=primary)
        bool discordEnabled = DiscordManager::getInstance().isEnabled();
        DiscordManager::State discordState = DiscordManager::getInstance().getState();
        bool isConnecting = (discordState == DiscordManager::State::CONNECTING);
        float currentX = ctx.controlX;

        // Disable toggle arrows during CONNECTING state to prevent freeze
        uint32_t arrowColor = isConnecting ? colorConfig.getMuted() : colorConfig.getAccent();
        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), arrowColor, ctx.fontSize);
        if (!isConnecting) {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::DISCORD_TOGGLE, nullptr
            ));
        }
        currentX += cw * 2;

        // Show status: On (Connected), On (Connecting...), On (Not Available), Off
        const char* statusText;
        uint32_t statusColor;
        if (!discordEnabled) {
            statusText = "Off";
            statusColor = colorConfig.getMuted();
        } else {
            switch (discordState) {
                case DiscordManager::State::CONNECTED:
                    statusText = "On";
                    statusColor = colorConfig.getPositive();
                    break;
                case DiscordManager::State::CONNECTING:
                    statusText = "Connecting";
                    statusColor = colorConfig.getPrimary();
                    break;
                default:
                    statusText = "On";
                    statusColor = colorConfig.getMuted();
                    break;
            }
        }

        std::string formattedDiscord = ctx.formatValue(statusText, VALUE_WIDTH, false);
        ctx.parent->addString(formattedDiscord.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), statusColor, ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), arrowColor, ctx.fontSize);
        if (!isConnecting) {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::DISCORD_TOGGLE, nullptr
            ));
        }

        ctx.currentY += ctx.lineHeightNormal;
    }
#endif

    // === PROFILES SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Profiles");

    // Auto-switch toggle - use standard helper for consistency
    bool autoSwitchEnabled = ProfileManager::getInstance().isAutoSwitchEnabled();
    ctx.addToggleControl("Auto-Switch", autoSwitchEnabled,
        SettingsHud::ClickRegion::AUTO_SWITCH_TOGGLE, nullptr, nullptr, 0, true, "general.auto_switch");

    // Copy profile target cycle - use standard cycle control for consistency
    {
        const char* targetName;
        int copyTarget = ctx.parent->m_copyTargetProfile;
        bool hasTarget = (copyTarget != -1);
        if (copyTarget == -1) {
            targetName = "Select";
        } else if (copyTarget == 4) {
            targetName = "All";
        } else {
            targetName = ProfileManager::getInstance().getProfileName(static_cast<ProfileType>(copyTarget));
        }
        ctx.addCycleControl("Copy current profile to", targetName, VALUE_WIDTH,
            SettingsHud::ClickRegion::COPY_TARGET_DOWN,
            SettingsHud::ClickRegion::COPY_TARGET_UP,
            nullptr, true, !hasTarget, "general.copy_profile");

        // [Copy] button - centered like [Close] button
        ctx.currentY += ctx.lineHeightNormal * 0.5f;
        {
            float buttonWidth = PluginUtils::calculateMonospaceTextWidth(6, ctx.fontSize);
            float buttonCenterX = ctx.contentAreaStartX + (ctx.panelWidth - ctx.paddingH - ctx.paddingH) / 2.0f;
            float buttonX = buttonCenterX - buttonWidth / 2.0f;

            size_t regionIndex = ctx.parent->m_clickRegions.size();
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                buttonX, ctx.currentY, buttonWidth, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::COPY_BUTTON, nullptr
            ));

            // Button background - muted when disabled, accent when enabled
            SPluginQuad_t bgQuad;
            float bgX = buttonX, bgY = ctx.currentY;
            ctx.parent->applyOffset(bgX, bgY);
            ctx.parent->setQuadPositions(bgQuad, bgX, bgY, buttonWidth, ctx.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            if (!hasTarget) {
                bgQuad.m_ulColor = PluginUtils::applyOpacity(colorConfig.getMuted(), 64.0f / 255.0f);
            } else {
                bgQuad.m_ulColor = (ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
                    ? colorConfig.getAccent()
                    : PluginUtils::applyOpacity(colorConfig.getAccent(), 128.0f / 255.0f);
            }
            ctx.parent->m_quads.push_back(bgQuad);

            unsigned long textColor = !hasTarget ? colorConfig.getMuted()
                : (ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
                    ? colorConfig.getPrimary()
                    : colorConfig.getAccent();
            ctx.parent->addString("[Copy]", buttonCenterX, ctx.currentY, Justify::CENTER,
                Fonts::getNormal(), textColor, ctx.fontSize);

            ctx.currentY += ctx.lineHeightNormal;
        }
    }

    // Reset section - radio options + [Reset] button
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    {
        ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
        const char* activeProfileName = ProfileManager::getInstance().getProfileName(activeProfile);
        float radioWidth = PluginUtils::calculateMonospaceTextWidth(CHECKBOX_WIDTH, ctx.fontSize);

        // Reset [Profile] profile radio row
        {
            // Add tooltip row (full width for hover)
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.reset_profile"
            ));

            float clickRowWidth = radioWidth + PluginUtils::calculateMonospaceTextWidth(22, ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, clickRowWidth, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::RESET_PROFILE_CHECKBOX, nullptr
            ));

            bool resetProfileConfirmed = ctx.parent->m_resetProfileConfirmed;
            ctx.parent->addString(resetProfileConfirmed ? "(O)" : "( )", ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

            float textX = ctx.labelX + radioWidth;
            unsigned long labelColor = colorConfig.getSecondary();
            unsigned long profileColor = resetProfileConfirmed
                ? colorConfig.getPrimary()
                : colorConfig.getSecondary();

            ctx.parent->addString("Reset", textX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), labelColor, ctx.fontSize);
            textX += cw * 6;

            ctx.parent->addString(activeProfileName, textX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), profileColor, ctx.fontSize);
            textX += cw * 9;

            ctx.parent->addString("profile", textX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), labelColor, ctx.fontSize);

            ctx.currentY += ctx.lineHeightNormal;
        }

        // Reset All Settings radio row
        {
            // Add tooltip row (full width for hover)
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.reset_all"
            ));

            float clickRowWidth = radioWidth + PluginUtils::calculateMonospaceTextWidth(18, ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                ctx.labelX, ctx.currentY, clickRowWidth, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::RESET_ALL_CHECKBOX, nullptr
            ));

            bool resetAllConfirmed = ctx.parent->m_resetAllConfirmed;
            ctx.parent->addString(resetAllConfirmed ? "(O)" : "( )", ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

            unsigned long labelColor = resetAllConfirmed
                ? colorConfig.getPrimary()
                : colorConfig.getSecondary();
            ctx.parent->addString("Reset All Settings", ctx.labelX + radioWidth, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), labelColor, ctx.fontSize);

            ctx.currentY += ctx.lineHeightNormal;
        }

        // [Reset] button - centered like [Close] button
        ctx.currentY += ctx.lineHeightNormal * 0.5f;
        {
            bool resetEnabled = ctx.parent->m_resetProfileConfirmed || ctx.parent->m_resetAllConfirmed;
            float buttonWidth = PluginUtils::calculateMonospaceTextWidth(7, ctx.fontSize);
            float buttonCenterX = ctx.contentAreaStartX + (ctx.panelWidth - ctx.paddingH - ctx.paddingH) / 2.0f;
            float buttonX = buttonCenterX - buttonWidth / 2.0f;

            size_t regionIndex = ctx.parent->m_clickRegions.size();
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                buttonX, ctx.currentY, buttonWidth, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::RESET_BUTTON, nullptr
            ));

            // Button background - muted when disabled, accent when enabled
            SPluginQuad_t bgQuad;
            float bgX = buttonX, bgY = ctx.currentY;
            ctx.parent->applyOffset(bgX, bgY);
            ctx.parent->setQuadPositions(bgQuad, bgX, bgY, buttonWidth, ctx.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            if (!resetEnabled) {
                bgQuad.m_ulColor = PluginUtils::applyOpacity(colorConfig.getMuted(), 64.0f / 255.0f);
            } else {
                bgQuad.m_ulColor = (ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
                    ? colorConfig.getAccent()
                    : PluginUtils::applyOpacity(colorConfig.getAccent(), 128.0f / 255.0f);
            }
            ctx.parent->m_quads.push_back(bgQuad);

            unsigned long textColor = !resetEnabled ? colorConfig.getMuted()
                : (ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
                    ? colorConfig.getPrimary()
                    : colorConfig.getAccent();
            ctx.parent->addString("[Reset]", buttonCenterX, ctx.currentY, Justify::CENTER,
                Fonts::getNormal(), textColor, ctx.fontSize);

            ctx.currentY += ctx.lineHeightNormal;
        }
    }

    // No active HUD for general settings
    return nullptr;
}
