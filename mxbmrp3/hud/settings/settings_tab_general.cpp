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
#include "../../core/plugin_data.h"
#include "../../core/xinput_reader.h"
#include "../../core/color_config.h"
#include "../../core/ui_config.h"
#if GAME_HAS_DISCORD
#include "../../core/discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "../../core/steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "../../core/http_server.h"
#endif

using namespace PluginConstants;

// Member function of SettingsHud - handles click events for General tab
bool SettingsHud::handleClickTabGeneral(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::AUTOSAVE_TOGGLE:
            {
                bool current = UiConfig::getInstance().getAutoSave();
                UiConfig::getInstance().setAutoSave(!current);
                setDataDirty();
            }
            return true;

#if GAME_HAS_STEAM_FRIENDS
        case ClickRegion::STEAM_FRIENDS_TOGGLE:
            {
                bool current = SteamFriendsManager::getInstance().isEnabled();
                SteamFriendsManager::getInstance().setEnabled(!current);
                setDataDirty();
            }
            return true;
#endif

#if GAME_HAS_DISCORD
        case ClickRegion::DISCORD_TOGGLE:
            {
                bool current = DiscordManager::getInstance().isEnabled();
                DiscordManager::getInstance().setEnabled(!current);
                setDataDirty();
            }
            return true;
#endif

#if GAME_HAS_HTTP_SERVER
        case ClickRegion::WEB_SERVER_TOGGLE:
            {
                bool current = HttpServer::getInstance().isEnabled();
                HttpServer::getInstance().setEnabled(!current);
                setDataDirty();
            }
            return true;
        case ClickRegion::WEB_SERVER_PORT_DOWN:
        case ClickRegion::WEB_SERVER_PORT_UP:
            {
                auto& server = HttpServer::getInstance();
                int port = server.getPort();
                int step = getHoldStepMultiplier();
                port += (region.type == ClickRegion::WEB_SERVER_PORT_UP) ? step : -step;
                port = std::clamp(port, 1024, 65535);
                if (port != server.getPort()) {
                    if (server.isRunning()) server.stop();
                    server.setPort(port);
                    setDataDirty();
                }
            }
            return true;
#endif

        case ClickRegion::PB_SCOPE_TOGGLE:
            {
                auto current = UiConfig::getInstance().getPBScope();
                UiConfig::getInstance().setPBScope(
                    current == PBScope::BIKE ? PBScope::CATEGORY : PBScope::BIKE);
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        // Note: CLOCK_FORMAT_TOGGLE is in common handlers (works from all tabs)

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
    // (Display section — speed/fuel/temp units + clock format — moved to the
    // Appearance tab; persisted under [Display].)
    ctx.addSectionHeader("Preferences");

    // PB scope toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.pb_scope"
        ));

        ctx.parent->addString("PB Scope", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        bool isCategory = UiConfig::getInstance().getPBScope() == PBScope::CATEGORY;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::PB_SCOPE_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        std::string formattedValue = ctx.formatValue(isCategory ? "Category" : "Bike", VALUE_WIDTH, false);
        ctx.parent->addString(formattedValue.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::PB_SCOPE_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

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

        // Controller uses standard value width (truncates long device names)
        // Format: "1: Name.." = slot (1) + ": " (2) + name (up to 7) = 10 max
        float currentX = ctx.controlX;
        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::RUMBLE_CONTROLLER_DOWN, nullptr
        ));
        currentX += cw * 2;

        // Show controller status
        // Format: "Disabled" or "1: Name.." or "1: N/C"
        char displayStr[32];
        if (isDisabled) {
            snprintf(displayStr, sizeof(displayStr), "%-*s", VALUE_WIDTH, "Disabled");
        } else {
            int slot = controllerIdx + 1;
            char tempStr[16];
            if (!controllerName.empty()) {
                snprintf(tempStr, sizeof(tempStr), "%d: %.7s", slot, controllerName.c_str());
            } else if (isConnected) {
                snprintf(tempStr, sizeof(tempStr), "%d: OK", slot);
            } else {
                snprintf(tempStr, sizeof(tempStr), "%d: N/C", slot);
            }
            snprintf(displayStr, sizeof(displayStr), "%-*s", VALUE_WIDTH, tempStr);
        }

        // Color: muted for disabled, positive for connected, muted for not connected
        uint32_t textColor = isDisabled ? colorConfig.getMuted() :
            (isConnected ? colorConfig.getPositive() : colorConfig.getMuted());
        ctx.parent->addString(displayStr, currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), textColor, ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::RUMBLE_CONTROLLER_UP, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Auto-save toggle
    ctx.addToggleControl("Auto-Save", UiConfig::getInstance().getAutoSave(),
        SettingsHud::ClickRegion::AUTOSAVE_TOGGLE, nullptr, nullptr, 0, true,
        "general.auto_save");

#if GAME_HAS_STEAM_FRIENDS || GAME_HAS_DISCORD || GAME_HAS_HTTP_SERVER
    // === INTEGRATIONS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Integrations");
#endif

#if GAME_HAS_STEAM_FRIENDS
    // Steam Friends toggle (broadcast presence + read friends in-game)
    {
        // The standalone (non-Steam) build of the game doesn't load
        // steam_api64.dll, so the feature can't work. Show the control but
        // disabled (greyed, non-interactive) rather than hiding it, so players
        // can see the feature exists and what's needed to use it.
        const bool steamAvailable = SteamFriendsManager::isSteamRuntimeAvailable();

        // Add tooltip row (kept even when disabled so hover still explains it)
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.steam_friends"
        ));

        ctx.parent->addString("Steam", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), steamAvailable ? colorConfig.getSecondary() : colorConfig.getMuted(), ctx.fontSize);

        float currentX = ctx.controlX;

        if (!steamAvailable) {
            // Disabled: muted arrows + "N/A", no click regions.
            ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
            currentX += cw * 2;

            std::string formattedSteam = ctx.formatValue("N/A", VALUE_WIDTH, false);
            ctx.parent->addString(formattedSteam.c_str(), currentX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
            currentX += cw * VALUE_WIDTH;

            ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
        } else {
            bool steamEnabled = SteamFriendsManager::getInstance().isEnabled();
            SteamFriendsManager::Status steamStatus = SteamFriendsManager::getInstance().getStatus();

            ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::STEAM_FRIENDS_TOGGLE, nullptr
            ));
            currentX += cw * 2;

            // Off when disabled; On (green) when enabled and hooked; On (muted)
            // when enabled but not yet hooked (Steam present but not ready).
            const char* statusText;
            uint32_t statusColor;
            if (!steamEnabled) {
                statusText = "Off";
                statusColor = colorConfig.getMuted();
            } else if (steamStatus == SteamFriendsManager::Status::CONNECTED) {
                statusText = "On";
                statusColor = colorConfig.getPositive();
            } else {
                statusText = "On";
                statusColor = colorConfig.getMuted();
            }

            std::string formattedSteam = ctx.formatValue(statusText, VALUE_WIDTH, false);
            ctx.parent->addString(formattedSteam.c_str(), currentX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), statusColor, ctx.fontSize);
            currentX += cw * VALUE_WIDTH;

            ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
                SettingsHud::ClickRegion::STEAM_FRIENDS_TOGGLE, nullptr
            ));
        }

        ctx.currentY += ctx.lineHeightNormal;
    }
#endif

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

#if GAME_HAS_HTTP_SERVER
    // Web Server toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "general.web_server"
        ));

        ctx.parent->addString("Web Server", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        bool serverEnabled = HttpServer::getInstance().isEnabled();
        bool serverRunning = HttpServer::getInstance().isRunning();
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::WEB_SERVER_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        // Show status: On / Off (port info moves to the note line below)
        std::string statusStr;
        uint32_t statusColor;
        if (!serverEnabled) {
            statusStr = "Off";
            statusColor = colorConfig.getMuted();
        } else if (serverRunning) {
            statusStr = "On";
            statusColor = colorConfig.getPositive();
        } else {
            statusStr = "Error";
            statusColor = colorConfig.getWarning();
        }

        std::string formattedValue = ctx.formatValue(statusStr.c_str(), VALUE_WIDTH, false);
        ctx.parent->addString(formattedValue.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), statusColor, ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::WEB_SERVER_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;

        // Port control
        {
            char portBuf[8];
            snprintf(portBuf, sizeof(portBuf), "%d", HttpServer::getInstance().getPort());
            ctx.addCycleControl("Web Server Port", portBuf, 10,
                SettingsHud::ClickRegion::WEB_SERVER_PORT_DOWN,
                SettingsHud::ClickRegion::WEB_SERVER_PORT_UP,
                nullptr, true, !serverEnabled, "general.web_port");
        }

        // Helper note below the controls.
        ctx.currentY += ctx.lineHeightNormal * 0.5f;
        std::string noteStr;
        if (!serverEnabled) {
            noteStr = "Enable to serve a live web overlay.";
        } else if (serverRunning) {
            noteStr = "Live overlay at http://localhost:"
                + std::to_string(HttpServer::getInstance().getPort());
        } else {
            noteStr = "Port " + std::to_string(HttpServer::getInstance().getPort())
                + " may be in use. Try a different port.";
        }
        ctx.parent->addString(noteStr.c_str(), ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize * 0.9f);
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

    // === RESET SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Reset");
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

    // Help & Community footer - muted hints under a shared header (matches the note style
    // used on other tabs). The overlay can't open a browser, so these are read-and-type
    // addresses. The tab has ~5 rows of slack above the Close button, so float the footer
    // most of the way down (leaving a ~1-row margin, like the Hotkeys tab's bottom note).
    ctx.addSpacing(3.5f);
    ctx.addSectionHeader("Help & Community");
    // Labels padded so the URLs line up vertically in the monospace Normal font.
    ctx.parent->addString("Documentation & guides:  thomas4f.github.io/mxbmrp3",
        ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;
    ctx.parent->addString("Community discussion:    mxb-mods.com/mxbmrp3",
        ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;
    ctx.parent->addString("Source & issues:         github.com/thomas4f/mxbmrp3",
        ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;
    ctx.parent->addString("Support development:     ko-fi.com/thomas4f",
        ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;

    // No active HUD for general settings
    return nullptr;
}
