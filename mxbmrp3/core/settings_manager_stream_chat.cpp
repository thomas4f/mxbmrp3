// ============================================================================
// core/settings_manager_stream_chat.cpp
// The stream chat's three global INI sections: [StreamChat] (the chat HUD, fed
// by both platforms), [Twitch] and [YouTube] (each platform's switch and
// channel).
//
// GLOBAL, NOT PER PROFILE. The chat HUD is configured on a global settings tab,
// so its base properties are written here with the same key names a per-profile
// section uses (visible/offsetX/...), and applied key by key like the Director
// button's geometry. Companion-surface decoupling and per-HUD colour/font
// overrides are not persisted for it, the same limitation the other global
// widgets (Director, Achievements) have.
//
// Reset: all three sections are part of the startup snapshot writeGlobalSettings()
// captures, so the Stream Chat tab's Reset replays them (SettingsHud::resetTabStreamChat).
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde.h"
#include "hud_manager.h"
#include "twitch_chat_manager.h"
#include "youtube_chat_manager.h"
#include "../hud/stream_chat_hud.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <ostream>

using namespace Settings;

void SettingsManager::writeStreamChatSettings(std::ostream& out, const HudManager& hudManager) const {
    const StreamChatHud& hud = hudManager.getStreamChatHud();
    using namespace Keys::Base;
    out << "[StreamChat]\n";
    out << VISIBLE << "=" << (hud.isVisible() ? 1 : 0) << "\n";
    out << SHOW_TITLE << "=" << (hud.getShowTitle() ? 1 : 0) << "\n";
    out << TEXTURE_VARIANT << "=" << hud.getTextureVariant() << "\n";
    // Always written, empty included: an empty value is "follow the global
    // theme", and the key-by-key apply below needs to see it to clear a pin.
    out << THEME << "=" << hud.getThemeOverride() << "\n";
    out << BG_OPACITY << "=" << hud.getBackgroundOpacity() << "\n";
    out << SCALE << "=" << hud.getOwnScale() << "\n";
    out << OFFSET_X << "=" << hud.getOffsetX() << "\n";
    out << OFFSET_Y << "=" << hud.getOffsetY() << "\n";
    out << "showMode=" << static_cast<int>(hud.m_displayMode) << " ; 0=off 1=always 2=auto-hide\n";
    out << "rows=" << hud.m_maxRows << "\n";
    out << "width=" << hud.m_widthChars << " ; characters\n";
    out << "autoHideMs=" << hud.m_autoHideDurationMs << "\n";
    out << "displayOrder=" << static_cast<int>(hud.m_displayOrder) << " ; 0=newest at the bottom 1=newest at the top\n";
    out << "timestamps=" << (hud.m_showTimestamps ? 1 : 0) << "\n";
    out << "nameColors=" << (hud.m_nameColors ? 1 : 0) << "\n";
    out << "textColors=" << (hud.m_textColors ? 1 : 0) << "\n";
    out << "roles=" << hud.m_roleMask << " ; bits: 1=broadcaster/owner 2=moderator 4=VIP 8=subscriber 16=staff 32=partner 64=lead mod 128=prime 256=turbo 512=artist 1024=founder 2048=member 4096=verified\n";
    out << "platformIcons=" << static_cast<int>(hud.m_platformIcons) << " ; 0=off 1=on 2=auto (while both platforms are on)\n";
    out << "emotes=" << (hud.m_showEmotes ? 1 : 0) << "\n";
    out << "hideCommands=" << (hud.m_hideCommands ? 1 : 0) << "\n";
    out << "hideBots=" << (hud.m_hideBots ? 1 : 0) << "\n";
    out << "hideLinks=" << (hud.m_hideLinks ? 1 : 0) << "\n\n";
}

void SettingsManager::applyStreamChatLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    StreamChatHud& hud = hudManager.getStreamChatHud();
    using namespace Keys::Base;
    try {
        if (key == VISIBLE) {
            hud.setVisible(std::stoi(value) != 0);
        } else if (key == SHOW_TITLE) {
            hud.setShowTitle(std::stoi(value) != 0);
        } else if (key == TEXTURE_VARIANT) {
            hud.setTextureVariant(std::stoi(value));
        } else if (key == THEME) {
            hud.setThemeOverride(value);
        } else if (key == BG_OPACITY) {
            hud.setBackgroundOpacity(validateOpacity(parseFiniteFloat(value, hud.getBackgroundOpacity())));
        } else if (key == SCALE) {
            hud.setScale(validateScale(parseFiniteFloat(value, hud.getOwnScale())));
        } else if (key == OFFSET_X) {
            hud.setPosition(validateOffset(parseFiniteFloat(value, hud.getOffsetX())), hud.getOffsetY());
        } else if (key == OFFSET_Y) {
            hud.setPosition(hud.getOffsetX(), validateOffset(parseFiniteFloat(value, hud.getOffsetY())));
        } else if (key == "showMode") {
            hud.m_displayMode = static_cast<StreamChatHud::DisplayMode>(std::clamp(std::stoi(value), 0, 2));
        } else if (key == "rows") {
            hud.m_maxRows = std::clamp(std::stoi(value), StreamChatHud::MIN_ROWS, StreamChatHud::MAX_ROWS);
        } else if (key == "width") {
            hud.m_widthChars = std::clamp(std::stoi(value),
                StreamChatHud::MIN_WIDTH_CHARS, StreamChatHud::MAX_WIDTH_CHARS);
        } else if (key == "autoHideMs") {
            hud.m_autoHideDurationMs = std::clamp(std::stoi(value),
                StreamChatHud::MIN_AUTO_HIDE_MS, StreamChatHud::MAX_AUTO_HIDE_MS);
        } else if (key == "displayOrder") {
            hud.m_displayOrder = (std::stoi(value) == 1)
                ? StreamChatHud::DisplayOrder::NEWEST_FIRST
                : StreamChatHud::DisplayOrder::OLDEST_FIRST;
        } else if (key == "timestamps") {
            hud.m_showTimestamps = std::stoi(value) != 0;
        } else if (key == "nameColors") {
            hud.m_nameColors = std::stoi(value) != 0;
        } else if (key == "textColors") {
            hud.m_textColors = std::stoi(value) != 0;
        } else if (key == "roles") {
            hud.m_roleMask = static_cast<uint32_t>(std::stoul(value)) & StreamChatHud::ROLES_ALL;
        } else if (key == "platformIcons") {
            hud.m_platformIcons = static_cast<StreamChatHud::PlatformIcons>(std::clamp(std::stoi(value), 0, 2));
        } else if (key == "emotes") {
            hud.m_showEmotes = std::stoi(value) != 0;
        } else if (key == "hideCommands") {
            hud.m_hideCommands = std::stoi(value) != 0;
        } else if (key == "hideBots") {
            hud.m_hideBots = std::stoi(value) != 0;
        } else if (key == "hideLinks") {
            hud.m_hideLinks = std::stoi(value) != 0;
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("StreamChat: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
    hud.setDataDirty();
}

void SettingsManager::writeTwitchSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const TwitchChatManager& mgr = TwitchChatManager::getInstance();
    out << "[Twitch]\n";
    out << "enabled=" << (mgr.isEnabled() ? 1 : 0)
        << " ; connect to the channel (independent of the chat HUD being shown)\n";
    out << "channel=" << mgr.getChannel() << "\n\n";
}

void SettingsManager::applyTwitchLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    try {
        if (key == "enabled") {
            TwitchChatManager::getInstance().setEnabled(std::stoi(value) != 0);
        } else if (key == "channel") {
            TwitchChatManager::getInstance().setChannel(value);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Twitch: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
    hudManager.getStreamChatHud().setDataDirty();
}

void SettingsManager::writeYouTubeSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const YouTubeChatManager& mgr = YouTubeChatManager::getInstance();
    out << "[YouTube]\n";
    out << "enabled=" << (mgr.isEnabled() ? 1 : 0)
        << " ; read the channel's live chat (unofficial interface)\n";
    out << "channel=" << mgr.getChannel() << "\n\n";  // @handle or UC... channel id
}

void SettingsManager::applyYouTubeLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    try {
        if (key == "enabled") {
            YouTubeChatManager::getInstance().setEnabled(std::stoi(value) != 0);
        } else if (key == "channel") {
            YouTubeChatManager::getInstance().setChannel(value);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("YouTube: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
    hudManager.getStreamChatHud().setDataDirty();
}
