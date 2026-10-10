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
#include "../gl_confirm_hud.h"
#include "../freeze_duration.h"
#include "../../core/plugin_data.h"
#include "../../core/xinput_reader.h"
#include "../../core/color_config.h"
#include "../../core/ui_config.h"
#include "../../core/system_messages.h"
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
#include <cmath>
#include <cstdio>
#include <cstring>  // strlen (link URL width)
#include <memory>
#include <string>
#include <vector>

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
                // grid-snap-exempt: TOGGLES the gate from its own checkbox.
                bool current = UiConfig::getInstance().getGridSnapping();
                UiConfig::getInstance().setGridSnapping(!current);
                setDataDirty();
            }
            return true;

        case ClickRegion::DIRECT_GL_TOGGLE:
            {
                // Toggling is the retry gesture after a latched GL failure,
                // which is why clearGlFailLatch is here: a backend that stood
                // down needs a way back without a restart, and the row that
                // reports "Failed" is the obvious place to ask for it.
                UiConfig& uiConfig = UiConfig::getInstance();
                const bool turningOn = !uiConfig.getGlInGame();
                uiConfig.setGlInGame(turningOn);
                HudManager::getInstance().clearGlFailLatch();
                rebuildRenderData();
                // ARM THE DEAD-MAN'S SWITCH on the way ON, and only then. This is
                // the moment a user can strand themselves: if the backend renders
                // wrongly on their driver, the HUD stays laid out and clickable but
                // becomes unreadable, so they cannot find this row again to undo it.
                // See hud/gl_confirm_hud.h. Turning it OFF needs no confirmation -
                // they are on their way back to the renderer that works.
                if (turningOn) HudManager::getInstance().getGlConfirmHud().arm();
                else           HudManager::getInstance().getGlConfirmHud().cancel();
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

        // ARM, then PERFORM. These two region types are the buttons themselves: an
        // unarmed click arms (and disarms its opposite), an armed click does the
        // thing. The two-step is the whole
        // protection -- neither of these is undoable -- so a handler that ever
        // performs on the first click has removed it.
        case ClickRegion::RESET_PROFILE_CHECKBOX:
            if (m_resetProfileConfirmed) {
                resetCurrentProfile();
                m_resetProfileConfirmed = false;
            } else {
                m_resetProfileConfirmed = true;
                m_resetAllConfirmed = false;
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RESET_ALL_CHECKBOX:
            if (m_resetAllConfirmed) {
                resetToDefaults();
                m_resetAllConfirmed = false;
            } else {
                m_resetAllConfirmed = true;
                m_resetProfileConfirmed = false;
                rebuildRenderData();
            }
            return true;

        // Armed like the Reset pair below it: a copy overwrites another profile
        // (or all of them) and is not undoable either.
        case ClickRegion::COPY_BUTTON:
            if (m_copyTargetProfile != -1 && !m_copyConfirmed) {
                m_copyConfirmed = true;
                m_resetProfileConfirmed = false;
                m_resetAllConfirmed = false;
                rebuildRenderData();
            } else if (m_copyTargetProfile != -1) {
                const char* from = ProfileManager::getProfileName(ProfileManager::getInstance().getActiveProfile());
                SystemMessages::Toast t;
                if (m_copyTargetProfile == 4) {
                    SettingsManager::getInstance().applyToAllProfiles(HudManager::getInstance());
                    snprintf(t.detail, sizeof(t.detail), "%s copied to every profile", from);
                } else {
                    ProfileType targetProfile = static_cast<ProfileType>(m_copyTargetProfile);
                    SettingsManager::getInstance().copyToProfile(HudManager::getInstance(), targetProfile);
                    snprintf(t.detail, sizeof(t.detail), "%s copied to %s", from,
                             ProfileManager::getProfileName(targetProfile));
                }
                m_copyTargetProfile = -1;
                m_copyConfirmed = false;
                // The menu shows nothing different after a copy: the copy lands
                // in a profile that is not on screen.
                snprintf(t.title, sizeof(t.title), "Profile copied");
                snprintf(t.icon, sizeof(t.icon), "clone");
                t.key = SystemMessages::KEY_PROFILE_COPIED;
                SystemMessages::getInstance().post(t);
                rebuildRenderData();
            }
            return true;

        // No shared Reset button: each outcome has its own button and performs from
        // its own case above. The type stays in the enum so no region
        // ordinal moves (settings_layout_test's golden encodes them raw).
        case ClickRegion::RESET_BUTTON:
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabGeneral(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("general");

    ColorConfig& colorConfig = ColorConfig::getInstance();

    // === PREFERENCES SECTION ===
    // (Speed/fuel/temp units and clock format are on the Appearance tab; persisted
    // under [Display].)
    ctx.addSectionHeading("Preferences");
    // The short switches side by side (beginColumns), with room for "Session PB";
    // the controller row stays full width below them, since its value is a device name.
    constexpr int PREFERENCE_LABEL_CHARS = 14;
    ctx.beginColumns(2, 7, PREFERENCE_LABEL_CHARS);

    // PB scope. Both arrows drive the same 2-state toggle.
    {
        // "Class" is what the game calls this grouping (MX1, MX2, …). The plugin's
        // internals follow the PiBoSo API, which names the same field m_szCategory —
        // so PBScope::CATEGORY and the persisted pbScope=CATEGORY value keep the API
        // spelling while every user-facing string says Class.
        const bool isCategory = UiConfig::getInstance().getPBScope() == PBScope::CATEGORY;
        ctx.addCycleControl("PB scope", isCategory ? "Class" : "Bike",
            SettingsHud::ClickRegion::PB_SCOPE_TOGGLE,
            SettingsHud::ClickRegion::PB_SCOPE_TOGGLE,
            nullptr, true, false, "general.pb_scope");
    }

    // The defaults every HUD set to "Default" follows (hud_defaults.h): the
    // reference lap, and how long an official gap is held.
    {
        static const char* const kReferences[] = { "Session PB", "All-time", "Last lap" };
        SettingsHud::CycleControl reference;
        reference.count = 3;
        reference.get = []() { return UiConfig::getInstance().getDefaultReference(); };
        reference.set = [](int v) { UiConfig::getInstance().setDefaultReference(v); };
        reference.nameOf = [](int i) { return std::string(kReferences[i]); };
        reference.postStep = []() { HudManager::getInstance().markAllHudsDirty(); };
        ctx.addCycleControl("Reference", kReferences[reference.get()], reference,
                            nullptr, true, false, "general.reference");

        const int freezeMs = UiConfig::getInstance().getDefaultFreezeMs();
        char freezeValue[16];
        if (freezeMs == 0) snprintf(freezeValue, sizeof(freezeValue), "Off");
        else snprintf(freezeValue, sizeof(freezeValue), "%ds", freezeMs / 1000);
        SettingsHud::SteppedControl freeze = SettingsHud::SteppedControl::accessor(
                [] { return static_cast<float>(UiConfig::getInstance().getDefaultFreezeMs()); },
                [](float v) { UiConfig::getInstance().setDefaultFreezeMs(static_cast<int>(std::lround(v))); },
                static_cast<float>(FreezeDuration::STEP_MS), static_cast<float>(FreezeDuration::MIN_MS),
                static_cast<float>(FreezeDuration::MAX_MS), nullptr);
        freeze.postStep = []() { HudManager::getInstance().markAllHudsDirty(); };
        ctx.addSteppedControl("Freeze", freezeValue, freeze, nullptr, true, freezeMs == 0, "general.freeze");
    }

    ctx.addToggleControl("Auto-save", UiConfig::getInstance().getAutoSave(),
        SettingsHud::ClickRegion::AUTOSAVE_TOGGLE, nullptr, nullptr, 0, true,
        "general.auto_save");

    // Note: the menu-only-cursor toggle is the Pointer row on the Widgets tab
    // (rendered as the pointer's On/Off, since it controls whether the pointer shows
    // during play). Persisted under [Display] as menuOnlyCursor.

    // HUD placement toggles (persisted under [Display])
    // grid-snap-exempt: renders the setting's own checkbox -- it reads the gate to
    // DISPLAY it, which is the one place that legitimately does.
    ctx.addToggleControl("Grid snap", UiConfig::getInstance().getGridSnapping(),
        SettingsHud::ClickRegion::GRID_SNAP_TOGGLE, nullptr, nullptr, 0, true,
        "general.grid_snap");
    ctx.addToggleControl("Screen clamp", UiConfig::getInstance().getScreenClamping(),
        SettingsHud::ClickRegion::SCREEN_CLAMP_TOGGLE, nullptr, nullptr, 0, true,
        "general.screen_clamp");

    // Direct GL Rendering: draw the in-game HUD ourselves, inside the GAME'S OWN
    // GL context, instead of handing primitives to the engine. Each word of the
    // name earns its place -- "direct" = no handoff to the engine and no
    // intermediate window, "GL" = the actual mechanism, which also quietly
    // explains why it can do nothing on a setup whose game does not render with
    // OpenGL. It is NOT named for being faster: that is the consequence, and a
    // name that describes an outcome ages badly if the outcome ever changes.
    //
    // Measured +36% avg FPS and +31% on 1% lows on a real driver, replicated
    // across both render modes. The value shows the SETTING -- if the backend
    // cannot run it latches off, the HUD stays engine-drawn, and nothing about
    // this row changes.
    // A STATUS row, not a toggle, and the difference is the whole point. This
    // backend can stand down on its own - a driver it cannot use, or an injected
    // GL layer leaving errors in the queue - and a plain toggle would keep
    // showing "on" while nothing happened: the switch looks enabled, the HUD is
    // engine-drawn, and the only evidence is a line in a log file no player
    // opens. Same shape as the Discord row above, and for the same reason: three
    // states where a boolean can only say two.
    {
        HudManager& hm = HudManager::getInstance();
        const bool glOn = UiConfig::getInstance().getGlInGame();
        const char* glStatus;
        uint32_t glColor;
        if (!glOn) {
            glStatus = "Off";
            glColor = colorConfig.getMuted();
        } else if (hm.glLatchedOff()) {
            // NEGATIVE, deliberately: this is the one state the user must not
            // mistake for working. Toggling the row off and on is the retry.
            glStatus = "Failed";
            glColor = colorConfig.getNegative();
        } else if (hm.glStatusCode() == 2) {
            // Has drawn at least once. Deliberately STICKY rather than
            // "drew last frame": that flickers on any frame with nothing to
            // draw, which would blink the colour and churn the rebuild the
            // status-change check in produceFrame triggers.
            glStatus = "On";
            glColor = colorConfig.getPositive();
        } else {
            // Asked for, nothing drawn yet. Muted rather than green - we do not
            // claim it works until it has.
            glStatus = "On";
            glColor = colorConfig.getMuted();
        }
        ctx.addCycleControl("Direct GL", glStatus,
            SettingsHud::ClickRegion::DIRECT_GL_TOGGLE,
            SettingsHud::ClickRegion::DIRECT_GL_TOGGLE,
            nullptr, /*enabled=*/true, /*isOff=*/false,
            "general.direct_gl", glColor);
    }
    ctx.endColumns();

    // Controller selector (used by both Gamepad Widget and Rumble)
    // Cycles: Off -> 1 -> 2 -> 3 -> 4 -> Off
    {
        RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
        int controllerIdx = rumbleConfig.controllerIndex;
        bool isDisabled = (controllerIdx < 0);
        // Cached (I/O-thread) state, not a live XInput poll — a settings rebuild
        // must never hit the slow disconnected-slot enumeration path.
        bool isConnected = !isDisabled && XInputReader::getInstance().isControllerConnectedCached(controllerIdx);
        std::string controllerName = isDisabled ? "" : XInputReader::getControllerName(controllerIdx);

        // Value text: "Off" (muted), or "<slot>: <name>" / ": OK" / ": N/C".
        // formatValue() pads and cuts it to the row's value field.
        // Room for a full device name: the row is full width, and a 32-byte
        // buffer cut "Xbox 360 Controller for Windows" well short of the field.
        char displayStr[96];
        if (isDisabled) {
            snprintf(displayStr, sizeof(displayStr), "%s", "Off");
        } else {
            int slot = controllerIdx + 1;
            if (!controllerName.empty()) {
                snprintf(displayStr, sizeof(displayStr), "%d: %s", slot, controllerName.c_str());
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

        // Off, then the four XInput slots: the stored index is the position less one.
        SettingsHud::CycleControl controller;
        controller.count = 5;
        controller.get = []() { return XInputReader::getInstance().getRumbleConfig().controllerIndex + 1; };
        controller.set = [](int i) {
            RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
            config.controllerIndex = i - 1;
            XInputReader::getInstance().setControllerIndex(config.controllerIndex);
        };
        controller.nameOf = [](int i) {
            if (i <= 0) return std::string("Off");
            const std::string name = XInputReader::getControllerName(i - 1);
            const bool connected = XInputReader::getInstance().isControllerConnectedCached(i - 1);
            return std::to_string(i) + ": " + (!name.empty() ? name : (connected ? "OK" : "N/C"));
        };
        // Full width for the device name, its arrows in the switches' column above.
        ctx.setRowLabelChars(PREFERENCE_LABEL_CHARS);
        ctx.addCycleControl("Controller", displayStr, controller,
            nullptr, true, false, "general.controller", true, valueColor);
        ctx.setRowLabelChars(0);
    }

    // Auto-save toggle

#if GAME_HAS_STEAM_FRIENDS || GAME_HAS_DISCORD || GAME_HAS_HTTP_SERVER || GAME_HAS_ANALYTICS
    // === INTEGRATIONS SECTION ===
    ctx.addSectionHeading("Integrations");
#endif

#if GAME_HAS_STEAM_FRIENDS
    // Steam Friends toggle (broadcast presence + read friends in-game)
    {
        // The standalone (non-Steam) build of the game doesn't load
        // steam_api64.dll, so the feature can't work. Show the control but
        // disabled (greyed, non-interactive) rather than hiding it, so players
        // can see the feature exists and what's needed to use it.
        const bool steamAvailable = SteamFriendsManager::isSteamRuntimeAvailable();

        // Value: Off (muted) / Connected green when actually hooked (the word the
        // Twitch/YouTube rows use) / On muted when enabled but Steam isn't ready
        // — a third state the primary/muted pair can't express, hence the colour
        // override. When the runtime is absent entirely the row renders disabled
        // (muted label + arrows, no click regions), which is what `enabled`
        // already does.
        const char* statusText = "N/A";
        unsigned long valueColor = colorConfig.getMuted();
        if (steamAvailable) {
            const bool steamEnabled = SteamFriendsManager::getInstance().isEnabled();
            const bool hooked =
                SteamFriendsManager::getInstance().getStatus() == SteamFriendsManager::Status::CONNECTED;
            statusText = steamEnabled ? (hooked ? "Connected" : "On") : "Off";
            if (steamEnabled && hooked) valueColor = colorConfig.getPositive();
        }

        ctx.addCycleControl("Steam", statusText,
            SettingsHud::ClickRegion::STEAM_FRIENDS_TOGGLE,
            SettingsHud::ClickRegion::STEAM_FRIENDS_TOGGLE,
            nullptr, steamAvailable, false, "general.steam_friends", valueColor);
    }
#endif

#if GAME_HAS_DISCORD
    // Discord Rich Presence toggle.
    //
    // DELIBERATELY hand-rolled rather than ctx.addCycleControl: while CONNECTING
    // the arrows must go muted and non-clickable (clicking mid-connect freezes the
    // game) while the LABEL stays secondary. The helper derives label, arrow
    // colour and clickability from one `enabled` flag, so routing this through it
    // would also mute the label — a visual change in a state no headless test can
    // reach (Discord is compiled out of MXBMRP3_TEST_BUILD). Give the helper a
    // separate label-enable if a second control ever needs this split.
    {
        bool discordEnabled = DiscordManager::getInstance().isEnabled();
        DiscordManager::State discordState = DiscordManager::getInstance().getState();
        // The arrows go dead during CONNECTING, to prevent a freeze.
        bool isConnecting = (discordState == DiscordManager::State::CONNECTING);

        // Off / Connected / Connecting / On (not available). The connection
        // state is exactly what addCycleControl's valueColour override is for --
        // three states where enabled/isOff can only say two.
        const char* statusText;
        uint32_t statusColor;
        if (!discordEnabled) {
            statusText = "Off";
            statusColor = colorConfig.getMuted();
        } else {
            switch (discordState) {
                case DiscordManager::State::CONNECTED:
                    statusText = "Connected";
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

        ctx.addCycleControl("Discord", statusText,
            SettingsHud::ClickRegion::DISCORD_TOGGLE,
            SettingsHud::ClickRegion::DISCORD_TOGGLE,
            nullptr, /*enabled=*/!isConnecting, /*isOff=*/false,
            "general.discord", statusColor);
    }
#endif

#if GAME_HAS_ANALYTICS
    // Anonymous usage analytics toggle (opt-out). Simple On/Off; no connection
    // state — the beacon is a single fire-and-forget send at startup.
    {
        // Green when on (the beacon is live), muted when off — a positive/muted
        // pair rather than the standard primary/muted, hence the override.
        const bool analyticsEnabled = AnalyticsManager::getInstance().isEnabled();
        // "Usage survey" rather than "Analytics", matching the installer's Privacy
        // page (and Debian popcon, which both borrow from). The INI key stays
        // `analytics=` -- it is persisted, and renaming it would orphan every
        // existing setting for a cosmetic gain.
        ctx.addCycleControl("Usage survey", analyticsEnabled ? "On" : "Off",
            SettingsHud::ClickRegion::ANALYTICS_TOGGLE,
            SettingsHud::ClickRegion::ANALYTICS_TOGGLE,
            nullptr, true, false, "general.analytics",
            analyticsEnabled ? colorConfig.getPositive() : colorConfig.getMuted());
    }
#endif

#if GAME_HAS_HTTP_SERVER
    // Web Server toggle
    {
        bool serverEnabled = HttpServer::getInstance().isEnabled();
        bool serverRunning = HttpServer::getInstance().isRunning();

        // Off / On / Error -- a third state, so the value colour is overridden
        // (see the Discord row above). Port info moves to the note line below.
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

        ctx.addCycleControl("Web server", statusStr.c_str(),
            SettingsHud::ClickRegion::WEB_SERVER_TOGGLE,
            SettingsHud::ClickRegion::WEB_SERVER_TOGGLE,
            nullptr, /*enabled=*/true, /*isOff=*/false,
            "general.web_server", statusColor);

        // Port control
        {
            char portBuf[8];
            snprintf(portBuf, sizeof(portBuf), "%d", HttpServer::getInstance().getPort());
            ctx.addCycleControl("Web server port", portBuf,
                SettingsHud::ClickRegion::WEB_SERVER_PORT_DOWN,
                SettingsHud::ClickRegion::WEB_SERVER_PORT_UP,
                nullptr, true, !serverEnabled, "general.web_port");
        }

        // THE ADDRESS, always shown and a link only while the server is serving:
        // muted and unclickable otherwise. A row that existed only while serving
        // made everything below it JUMP as the server was toggled. addLinkRow owns
        // the style, so a link looks like a link wherever it appears. A label,
        // not "Live overlay at ": the URL sits at a fixed column, and a sentence
        // running into it collides in a font wider than the monospace estimate.
        {
            const std::string url = "http://localhost:"
                + std::to_string(HttpServer::getInstance().getPort());
            ctx.addLinkRow("Live overlay", url.c_str(), 16,
                           SettingsHud::ClickRegion::OPEN_LINK_OVERLAY, 0.9f, serverRunning);
        }
    }
#endif

    // === PROFILES SECTION ===
    ctx.addSectionHeading("Profiles");

    // Auto-switch toggle - use standard helper for consistency
    bool autoSwitchEnabled = ProfileManager::getInstance().isAutoSwitchEnabled();
    ctx.addToggleControl("Auto-switch", autoSwitchEnabled,
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
        // Select (no target), All, then every profile but the active one. UI state
        // only: nothing is written until the Copy button.
        auto targets = std::make_shared<std::vector<int>>();
        targets->push_back(-1);
        targets->push_back(4);
        const int activeIdx = static_cast<int>(ProfileManager::getInstance().getActiveProfile());
        for (int p = 0; p < static_cast<int>(ProfileType::COUNT); ++p) {
            if (p != activeIdx) targets->push_back(p);
        }
        SettingsHud* settings = ctx.parent;
        SettingsHud::CycleControl copy;
        copy.count = static_cast<int>(targets->size());
        copy.get = [targets, settings]() {
            for (size_t i = 0; i < targets->size(); ++i) {
                if ((*targets)[i] == settings->m_copyTargetProfile) return static_cast<int>(i);
            }
            return 0;
        };
        copy.set = [targets, settings](int i) {
            settings->m_copyConfirmed = false;   // an arm is for the target it was made on
            settings->m_copyTargetProfile = static_cast<int8_t>((*targets)[static_cast<size_t>(i)]);
        };
        copy.nameOf = [targets](int i) {
            const int t = (*targets)[static_cast<size_t>(i)];
            if (t == -1) return std::string("Select");
            if (t == 4) return std::string("All");
            return std::string(ProfileManager::getInstance().getProfileName(static_cast<ProfileType>(t)));
        };
        ctx.addCycleControl("Copy profile to", targetName, copy,
            nullptr, true, !hasTarget, "general.copy_profile");

        // [Copy] button - centered like [Close] button
        ctx.addSpacing();
        ctx.addActionButton(ctx.parent->m_copyConfirmed ? "Confirm?" : "Copy", 8,
                            SettingsHud::ClickRegion::COPY_BUTTON,
                            SettingsLayoutContext::ButtonRole::Accent, hasTarget,
                            "general.copy_button");
    }

    // === RESET SECTION ===
    ctx.addSectionHeading("Reset");
    {
        // TWO BUTTONS, not two radios plus a shared one: a radio row each, a gap,
        // and a Reset button whose meaning depends on which radio is lit spends four
        // rows to express two outcomes. A button per outcome is one row and says
        // which one you are about to get.
        //
        // The confirmation is ARMING: the first click lights that button and renames
        // it, the second performs it. The two-step (choose, then perform) is the
        // protection against a single stray click, and arming either disarms the
        // other so the two can never be lit at once. Both buttons are Negative:
        // neither of these is a safe act, and a pair where one wore the accent would
        // read as the default.
        const bool armProfile = ctx.parent->m_resetProfileConfirmed;
        const bool armAll = ctx.parent->m_resetAllConfirmed;
        ctx.addActionButtonPair(
            armProfile ? "Confirm?" : "Profile",
            SettingsHud::ClickRegion::RESET_PROFILE_CHECKBOX,
            SettingsLayoutContext::ButtonRole::Negative, true,
            armAll ? "Confirm?" : "Everything",
            SettingsHud::ClickRegion::RESET_ALL_CHECKBOX,
            SettingsLayoutContext::ButtonRole::Negative, true,
            /*labelChars=*/12, "general.reset_profile", "general.reset_all");
        // Inline notes draw at 0.9x font with NO wrapping, and the settings
        // width fits roughly 60 characters at that size — keep all three
        // variants under that or the tail runs off the panel background.
        ctx.addInlineNote(armProfile
            ? "Resets this profile's settings. Click again to confirm."
            : armAll ? "Resets ALL profiles and globals. Click again to confirm."
                     : "Profile resets this profile only; Everything resets all.");
    }

    // No active HUD for general settings
    return nullptr;
}
