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
#if GAME_HAS_ANALYTICS
#include "../../core/analytics_manager.h"
#endif
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <cstring>  // strlen (link URL width)

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

#if GAME_HAS_ANALYTICS
        case ClickRegion::ANALYTICS_TOGGLE:
            {
                // Toggle persists for next launch; the beacon only fires once at
                // startup, so flipping it mid-session changes future runs only.
                bool current = AnalyticsManager::getInstance().isEnabled();
                // When opting OUT, send the event BEFORE flipping the flag off:
                // trackEvent() no-ops once m_enabled is false, so a post-disable
                // call would be silently dropped and we'd never learn opt-outs.
                if (current) {
                    AnalyticsManager::getInstance().trackEvent("analytics_disabled");
                }
                AnalyticsManager::getInstance().setEnabled(!current);
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

        case ClickRegion::OPEN_LINK_DOCS:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "docs"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://thomas4f.github.io/mxbmrp3", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case ClickRegion::OPEN_LINK_COMMUNITY:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "community"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://mxb-mods.com/mxbmrp3", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case ClickRegion::OPEN_LINK_KOFI:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "donate"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://ko-fi.com/thomas4f", nullptr, nullptr, SW_SHOWNORMAL);
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

    // PB scope. Both arrows drive the same 2-state toggle.
    {
        // "Class" is what the game calls this grouping (MX1, MX2, …). The plugin's
        // internals follow the PiBoSo API, which names the same field m_szCategory —
        // so PBScope::CATEGORY and the persisted pbScope=CATEGORY value keep the API
        // spelling while every user-facing string says Class.
        const bool isCategory = UiConfig::getInstance().getPBScope() == PBScope::CATEGORY;
        ctx.addCycleControl("PB Scope", isCategory ? "Class" : "Bike", VALUE_WIDTH,
            SettingsHud::ClickRegion::PB_SCOPE_TOGGLE,
            SettingsHud::ClickRegion::PB_SCOPE_TOGGLE,
            nullptr, true, false, "general.pb_scope");
    }

    // Controller selector (used by both Gamepad Widget and Rumble)
    // Cycles: Disabled -> 1 -> 2 -> 3 -> 4 -> Disabled
    {
        RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
        int controllerIdx = rumbleConfig.controllerIndex;
        bool isDisabled = (controllerIdx < 0);
        // Cached (I/O-thread) state, not a live XInput poll — a settings rebuild
        // must never hit the slow disconnected-slot enumeration path.
        bool isConnected = !isDisabled && XInputReader::getInstance().isControllerConnectedCached(controllerIdx);
        std::string controllerName = isDisabled ? "" : XInputReader::getControllerName(controllerIdx);

        // Value text: "Disabled", or "<slot>: <name>" / ": OK" / ": N/C".
        // formatValue() pads and truncates to VALUE_WIDTH, so the name is cut to
        // fit here only to keep the slot prefix readable.
        char displayStr[32];
        if (isDisabled) {
            snprintf(displayStr, sizeof(displayStr), "%s", "Disabled");
        } else {
            int slot = controllerIdx + 1;
            if (!controllerName.empty()) {
                snprintf(displayStr, sizeof(displayStr), "%d: %.7s", slot, controllerName.c_str());
            } else if (isConnected) {
                snprintf(displayStr, sizeof(displayStr), "%d: OK", slot);
            } else {
                snprintf(displayStr, sizeof(displayStr), "%d: N/C", slot);
            }
        }

        // Three-state value colour (connected reads green), which the standard
        // primary/muted pair can't express — hence the override.
        const unsigned long valueColor = (!isDisabled && isConnected)
            ? colorConfig.getPositive() : colorConfig.getMuted();

        ctx.addCycleControl("Controller", displayStr, VALUE_WIDTH,
            SettingsHud::ClickRegion::RUMBLE_CONTROLLER_DOWN,
            SettingsHud::ClickRegion::RUMBLE_CONTROLLER_UP,
            nullptr, true, false, "general.controller", valueColor);
    }

    // Auto-save toggle
    ctx.addToggleControl("Auto-Save", UiConfig::getInstance().getAutoSave(),
        SettingsHud::ClickRegion::AUTOSAVE_TOGGLE, nullptr, nullptr, 0, true,
        "general.auto_save");

    // Note: the menu-only-cursor toggle moved to the Pointer row on the Widgets tab
    // (rendered as the pointer's On/Off, since it controls whether the pointer shows
    // during play). Still persisted under [Display] as menuOnlyCursor.

    // HUD placement toggles (moved here from the Appearance tab; persisted under [Display])
    ctx.addToggleControl("Grid Snap", UiConfig::getInstance().getGridSnapping(),
        SettingsHud::ClickRegion::GRID_SNAP_TOGGLE, nullptr, nullptr, 0, true,
        "general.grid_snap");
    ctx.addToggleControl("Screen Clamp", UiConfig::getInstance().getScreenClamping(),
        SettingsHud::ClickRegion::SCREEN_CLAMP_TOGGLE, nullptr, nullptr, 0, true,
        "general.screen_clamp");

#if GAME_HAS_STEAM_FRIENDS || GAME_HAS_DISCORD || GAME_HAS_HTTP_SERVER || GAME_HAS_ANALYTICS
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

        // Value: Off (muted) / On green when actually hooked / On muted when
        // enabled but Steam isn't ready — a third state the primary/muted pair
        // can't express, hence the colour override. When the runtime is absent
        // entirely the row renders disabled (muted label + arrows, no click
        // regions), which is what `enabled` already does.
        const char* statusText = "N/A";
        unsigned long valueColor = colorConfig.getMuted();
        if (steamAvailable) {
            const bool steamEnabled = SteamFriendsManager::getInstance().isEnabled();
            const bool hooked =
                SteamFriendsManager::getInstance().getStatus() == SteamFriendsManager::Status::CONNECTED;
            statusText = steamEnabled ? "On" : "Off";
            if (steamEnabled && hooked) valueColor = colorConfig.getPositive();
        }

        ctx.addCycleControl("Steam", statusText, VALUE_WIDTH,
            SettingsHud::ClickRegion::STEAM_FRIENDS_TOGGLE,
            SettingsHud::ClickRegion::STEAM_FRIENDS_TOGGLE,
            nullptr, steamAvailable, false, "general.steam_friends", valueColor);
    }
#endif

#if GAME_HAS_DISCORD
    // Discord Rich Presence toggle.
    //
    // DELIBERATELY hand-rolled rather than ctx.addCycleControl: while CONNECTING
    // the arrows must go muted and non-clickable (clicking mid-connect froze the
    // game) while the LABEL stays secondary. The helper derives label, arrow
    // colour and clickability from one `enabled` flag, so routing this through it
    // would also mute the label — a visual change in a state no headless test can
    // reach (Discord is compiled out of MXBMRP3_TEST_BUILD). Give the helper a
    // separate label-enable if a second control ever needs this split.
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

#if GAME_HAS_ANALYTICS
    // Anonymous usage analytics toggle (opt-out). Simple On/Off; no connection
    // state — the beacon is a single fire-and-forget send at startup.
    {
        // Green when on (the beacon is live), muted when off — a positive/muted
        // pair rather than the standard primary/muted, hence the override.
        const bool analyticsEnabled = AnalyticsManager::getInstance().isEnabled();
        ctx.addCycleControl("Analytics", analyticsEnabled ? "On" : "Off", VALUE_WIDTH,
            SettingsHud::ClickRegion::ANALYTICS_TOGGLE,
            SettingsHud::ClickRegion::ANALYTICS_TOGGLE,
            nullptr, true, false, "general.analytics",
            analyticsEnabled ? colorConfig.getPositive() : colorConfig.getMuted());
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
            ctx.parent->addString("Copy", buttonCenterX, ctx.currentY, Justify::CENTER,
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
            ctx.parent->addString("Reset", buttonCenterX, ctx.currentY, Justify::CENTER,
                Fonts::getNormal(), textColor, ctx.fontSize);

            ctx.currentY += ctx.lineHeightNormal;
        }
    }

    // Help & Community footer — clickable links that open the browser via ShellExecute.
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Help & Community");

    // Each row: fixed-width muted label + the URL (the only clickable/hoverable part).
    // Prefixes are padded to 18 chars so the URLs align vertically with a gap.
    struct LinkRow {
        const char* prefix;  // 18-char padded label (≥1-char gap before the URL)
        const char* url;     // full URL including scheme
        SettingsHud::ClickRegion::Type regionType;
    };
    static const LinkRow links[] = {
        { "Docs & guides:    ", "https://thomas4f.github.io/mxbmrp3", SettingsHud::ClickRegion::OPEN_LINK_DOCS      },
        { "Discussion:       ", "https://mxb-mods.com/mxbmrp3",       SettingsHud::ClickRegion::OPEN_LINK_COMMUNITY },
        { "Support thomas4f: ", "https://ko-fi.com/thomas4f",          SettingsHud::ClickRegion::OPEN_LINK_KOFI      },
    };
    float linkFontSize = ctx.fontSize * 0.9f;
    float prefixWidth = PluginUtils::calculateMonospaceTextWidth(18, linkFontSize);
    for (const auto& link : links) {
        // Click/hover region covers only the URL text (not the muted label), so only
        // the link lights up and only clicking the link opens the browser.
        float urlX = ctx.labelX + prefixWidth;
        float urlWidth = PluginUtils::calculateMonospaceTextWidth(
            static_cast<int>(strlen(link.url)), linkFontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            urlX, ctx.currentY, urlWidth, ctx.lineHeightNormal,
            link.regionType, nullptr
        ));
        bool hovered = (ctx.parent->m_hoveredRegionIndex >= 0 &&
                        ctx.parent->m_hoveredRegionIndex == static_cast<int>(ctx.parent->m_clickRegions.size()) - 1);
        ctx.parent->addString(link.prefix, ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getMuted(), linkFontSize);
        uint32_t urlColor = hovered ? PluginUtils::lightenColor(colorConfig.getAccent(), 0.25f) : colorConfig.getAccent();
        ctx.parent->addString(link.url, urlX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), urlColor, linkFontSize);
        ctx.currentY += ctx.lineHeightNormal;
    }

    // No active HUD for general settings
    return nullptr;
}
