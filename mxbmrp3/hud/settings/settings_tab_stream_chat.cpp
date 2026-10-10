// ============================================================================
// hud/settings/settings_tab_stream_chat.cpp
// Tab renderer and click handler for the stream chat (GLOBAL tab, "Stream
// Chat"): the Twitch and YouTube connections and the chat HUD they feed.
//
// Everything here persists in [Twitch] and [YouTube]
// (settings_manager_stream_chat.cpp), not the per-profile cache, because the chat
// HUD is global. The tab returns the HUD so the positioning preview and the
// standard Appearance rows reach it.
//
// THE CHANNEL FIELDS are the plugin's only text inputs. Clicking one starts a
// HotkeyManager TEXT capture (typing, Ctrl+V, cursor keys) and records
// which field it is for (SettingsHud::m_textField); Enter or a second click
// commits, ESC cancels -- the commit is picked up in SettingsHud::update() and
// normalized by that platform's manager, so a pasted URL is reduced there.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../stream_chat_hud.h"
#include "../../core/hud_manager.h"
#include "../../core/hotkey_manager.h"
#include "../../core/twitch_chat_manager.h"
#include "../../core/twitch_irc.h"
#include "../../core/youtube_chat_manager.h"
#include "../../core/youtube_chat.h"
#include "../../core/text_edit.h"
#include "../../core/color_config.h"
#include "../../core/plugin_utils.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace PluginConstants;

namespace {

SettingsHud::CycleControl boolCycle(bool* value, BaseHud* hud) {
    SettingsHud::CycleControl c;
    c.get = [value]() { return *value ? 1 : 0; };
    c.set = [value](int v) { *value = (v != 0); };
    c.count = 2;
    c.dirtyHud = hud;
    return c;
}

}  // namespace

bool SettingsHud::handleClickTabStreamChat(const ClickRegion& region) {
    const bool twitchToggle = region.type == ClickRegion::TWITCH_ENABLED_TOGGLE;
    const bool youtubeToggle = region.type == ClickRegion::YOUTUBE_ENABLED_TOGGLE;
    if (twitchToggle || youtubeToggle) {
        TwitchChatManager& twitch = TwitchChatManager::getInstance();
        YouTubeChatManager& youtube = YouTubeChatManager::getInstance();
        const bool enabling = twitchToggle ? !twitch.isEnabled() : !youtube.isEnabled();
        if (twitchToggle) twitch.setEnabled(enabling);
        else youtube.setEnabled(enabling);
        if (enabling) {
            // Switching a chat on is asking to SEE it: show the HUD on the
            // surface the menu is on, if it is not already. (Switching off does
            // not hide it -- the panel then says so -- and hiding never
            // disconnects: the switches are independent after this.)
            StreamChatHud* hud = &HudManager::getInstance().getStreamChatHud();
            if (!hud->isVisibleOnActiveSurface()) toggleHudOnActiveSurface(hud);
            // On with no channel cannot connect: open the field so the next
            // thing typed is the name (the row reads "No channel" in warning).
            const bool noChannel = twitchToggle ? twitch.getChannel().empty() : youtube.getChannel().empty();
            if (noChannel) {
                HotkeyManager& hk = HotkeyManager::getInstance();
                hk.cancelCapture();
                if (twitchToggle) {
                    m_textField = TextField::TWITCH_CHANNEL;
                    hk.startTextCapture("", TwitchIrc::MAX_CHANNEL_LEN, false);
                } else {
                    m_textField = TextField::YOUTUBE_CHANNEL;
                    hk.startTextCapture("", YouTubeChat::MAX_CHANNEL_LEN - 1, true);
                }
            }
        }
        setDataDirty();
        return true;
    }
    const bool twitchEdit = region.type == ClickRegion::TWITCH_CHANNEL_EDIT;
    const bool youtubeEdit = region.type == ClickRegion::YOUTUBE_CHANNEL_EDIT;
    if (!twitchEdit && !youtubeEdit) return false;
    const TextField field = twitchEdit ? TextField::TWITCH_CHANNEL : TextField::YOUTUBE_CHANNEL;
    HotkeyManager& hk = HotkeyManager::getInstance();
    if (hk.isCapturing() && hk.getCaptureType() == CaptureType::TEXT && m_textField == field) {
        hk.commitTextCapture();  // second click = Enter
    } else {
        // A key-binding capture, or the other field's typing, ends here unsaved.
        hk.cancelCapture();
        m_textField = field;
        if (twitchEdit) {
            hk.startTextCapture(TwitchChatManager::getInstance().getChannel(), TwitchIrc::MAX_CHANNEL_LEN, false);
        } else {
            // A handle is edited without its '@' (the field shows one), so the
            // typed part is at most 30 characters; a pasted URL goes through the
            // paste path, which is capped separately (HotkeyManager).
            hk.startTextCapture(YouTubeChat::editableChannel(YouTubeChatManager::getInstance().getChannel()),
                YouTubeChat::MAX_CHANNEL_LEN - 1, true);
        }
    }
    setDataDirty();
    return true;
}

BaseHud* SettingsHud::renderTabStreamChat(SettingsLayoutContext& ctx) {
    StreamChatHud* hud = &HudManager::getInstance().getStreamChatHud();
    TwitchChatManager& twitch = TwitchChatManager::getInstance();
    YouTubeChatManager& youtube = YouTubeChatManager::getInstance();
    ColorConfig& colors = ColorConfig::getInstance();
    const HotkeyManager& hk = HotkeyManager::getInstance();
    const bool textCapture = hk.isCapturing() && hk.getCaptureType() == CaptureType::TEXT;

    ctx.addTabTooltip("stream_chat");

    // One "<Platform> [name]" row. `editing` = the running text capture is this
    // field's; `handlePrefix` = YouTube's fixed '@' in front of a typed handle.
    auto addChannelField = [&](const char* label, bool editing, const std::string& channel,
                               SettingsHud::ClickRegion::Type regionType, const char* tooltipId,
                               bool handlePrefix) {
        const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);

        ctx.parent->addString(label, ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);

        // The field's own region goes in BEFORE the row-wide tooltip region: hover
        // resolves to the FIRST region under the pointer, so a row region pushed
        // first would shadow the field and it would never light up.
        const size_t fieldRegion = ctx.parent->m_clickRegions.size();
        const bool hovered = ctx.parent->m_hoveredRegionIndex == static_cast<int>(fieldRegion);

        // Visible characters: the value and the dropdown caret's cell, so the box
        // starts and ends where a dropdown box does and the text sits on the value
        // column. The longest Twitch name (25) fits; a longer handle or pasted URL
        // shows its tail while editing.
        const int fieldChars = ctx.valueChars() + 1;
        char inner[64];
        unsigned long color;
        int cursorColumn = -1;  // editing: the caret's column in the field
        if (editing) {
            // The text scrolls to keep the cursor in view (TextEdit::viewStart);
            // the caret is drawn before its column, past the end when appending.
            const std::string& text = hk.getCaptureText();
            const bool prefix = handlePrefix && YouTubeChat::showsHandlePrefix(text);
            const size_t cols = static_cast<size_t>(fieldChars - (prefix ? 1 : 0));
            const size_t cursor = hk.getCaptureCursor();
            const size_t from = TextEdit::viewStart(text.size(), cursor, cols);
            snprintf(inner, sizeof(inner), "%s%.*s", prefix ? "@" : "",
                static_cast<int>(std::min(text.size() - from, cols)), text.c_str() + from);
            cursorColumn = static_cast<int>(cursor - from) + (prefix ? 1 : 0);
            color = colors.getAccent();
        } else if (channel.empty()) {
            snprintf(inner, sizeof(inner), "Click to set");
            color = hovered ? colors.getAccent() : colors.getMuted();
        } else {
            // A stored handle can be longer than the field (YouTube's run to 31):
            // cut it at the field.
            snprintf(inner, sizeof(inner), "%.*s", fieldChars, channel.c_str());
            color = hovered ? colors.getAccent() : colors.getPrimary();
        }
        const float fx = ctx.controlX + cw;
        ctx.addInputField(fx, fieldChars, inner, color, editing, cursorColumn);
        SettingsHud::ClickRegion field(fx, ctx.currentY, cw * (fieldChars + 1), ctx.lineHeightNormal,
            regionType, nullptr);
        field.tooltipId = tooltipId;
        ctx.parent->m_clickRegions.push_back(field);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, ctx.rowSpanWidth(), ctx.lineHeightNormal, tooltipId));
        ctx.nextLine();
    };

    // === APPEARANCE ===
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);

    // === CHANNELS === Both platforms in one section, each a channel field and
    // its Status toggle, under ONE note: the tab is the tallest on the menu,
    // and a second heading and note pushed the themed panel past the bottom of
    // the screen (settings_render_test / theme_geometry_test).
    ctx.addSectionHeading("Channels");
    const bool editingTwitch = textCapture && ctx.parent->m_textField == TextField::TWITCH_CHANNEL;
    const bool editingYouTube = textCapture && ctx.parent->m_textField == TextField::YOUTUBE_CHANNEL;
    const std::string& twitchChannel = twitch.getChannel();
    const std::string& youtubeChannel = youtube.getChannel();
    const TwitchChatManager::Status twitchStatus = twitch.getStatus();
    const YouTubeChatManager::Status youtubeStatus = youtube.getStatus();
    {
        addChannelField("Twitch", editingTwitch, twitchChannel, SettingsHud::ClickRegion::TWITCH_CHANNEL_EDIT,
            "twitch.channel", /*handlePrefix=*/false);

        // Status: the Discord row's shape and colour language -- a < value >
        // toggle whose value is the live state (muted off, primary while working
        // on it, green connected, warning when something is wrong). Clicking it
        // switches the CONNECTION, which is independent of the HUD's visibility:
        // a hidden chat keeps its conversation, and Show mode only hides.
        const char* statusText = "Off";
        unsigned long statusColor = colors.getMuted();
        if (!twitch.isEnabled()) {
            statusText = "Off";
        } else if (twitchChannel.empty()) {
            statusText = "No channel";           // on, but nothing to connect to
            statusColor = colors.getWarning();
        } else {
            switch (twitchStatus) {
            case TwitchChatManager::Status::OFF:        statusText = "Off"; break;
            case TwitchChatManager::Status::CONNECTING: statusText = "Connecting"; statusColor = colors.getPrimary(); break;
            case TwitchChatManager::Status::JOINING:    statusText = "Joining"; statusColor = colors.getPrimary(); break;
            case TwitchChatManager::Status::CONNECTED:  statusText = "Connected"; statusColor = colors.getPositive(); break;
            case TwitchChatManager::Status::NOT_FOUND:  statusText = "Not found?"; statusColor = colors.getWarning(); break;
            case TwitchChatManager::Status::RETRYING:   statusText = "Retrying"; statusColor = colors.getWarning(); break;
            }
        }
        ctx.addCycleControl("Status", statusText,
            SettingsHud::ClickRegion::TWITCH_ENABLED_TOGGLE, SettingsHud::ClickRegion::TWITCH_ENABLED_TOGGLE,
            nullptr, /*enabled=*/true, /*isOff=*/false, "twitch.status", statusColor);
    }
    {
        // The Twitch rows' shape; the states YouTube adds are "not live", which
        // is normal between streams, and "Can't read" -- the chat panel's
        // "YouTube Chat Unavailable" -- a format this build cannot read.
        addChannelField("YouTube", editingYouTube, youtubeChannel, SettingsHud::ClickRegion::YOUTUBE_CHANNEL_EDIT,
            "youtube.channel", /*handlePrefix=*/true);

        const char* statusText = "Off";
        unsigned long statusColor = colors.getMuted();
        if (!youtube.isEnabled()) {
            statusText = "Off";
        } else if (youtubeChannel.empty()) {
            statusText = "No channel";
            statusColor = colors.getWarning();
        } else {
            switch (youtubeStatus) {
            case YouTubeChatManager::Status::OFF:         statusText = "Off"; break;
            case YouTubeChatManager::Status::CONNECTING:  statusText = "Connecting"; statusColor = colors.getPrimary(); break;
            case YouTubeChatManager::Status::CONNECTED:   statusText = "Connected"; statusColor = colors.getPositive(); break;
            case YouTubeChatManager::Status::NOT_LIVE:    statusText = "Not live"; statusColor = colors.getPrimary(); break;
            case YouTubeChatManager::Status::NOT_FOUND:   statusText = "Not found?"; statusColor = colors.getWarning(); break;
            case YouTubeChatManager::Status::RETRYING:    statusText = "Retrying"; statusColor = colors.getWarning(); break;
            case YouTubeChatManager::Status::UNAVAILABLE: statusText = "Can't read"; statusColor = colors.getWarning(); break;
            }
        }
        ctx.addCycleControl("Status", statusText,
            SettingsHud::ClickRegion::YOUTUBE_ENABLED_TOGGLE, SettingsHud::ClickRegion::YOUTUBE_ENABLED_TOGGLE,
            nullptr, /*enabled=*/true, /*isOff=*/false, "youtube.status", statusColor);
    }
    {
        // The one note: the field being edited, else what to do about the first
        // problem a Status row names (the states the chat panel reports), else
        // the unofficial-support note.
        const bool twitchOn = twitch.isEnabled();
        const bool youtubeOn = youtube.isEnabled();
        const char* note = "Note: YouTube chat is unofficial.";
        if (editingTwitch) {
            note = "Type or Ctrl+V a name or URL. Enter saves, ESC cancels.";
        } else if (editingYouTube) {
            note = "Type or Ctrl+V a @handle or URL. Enter saves, ESC cancels.";
        } else if (twitchOn && twitchChannel.empty()) {
            note = "Enter your Twitch channel name above to connect.";
        } else if (twitchOn && twitchStatus == TwitchChatManager::Status::NOT_FOUND) {
            note = "Twitch didn't confirm this channel - check the spelling.";
        } else if (twitchOn && twitchStatus == TwitchChatManager::Status::RETRYING) {
            note = "Twitch connection lost - retrying automatically.";
        } else if (youtubeOn && youtubeChannel.empty()) {
            note = "Enter your YouTube @handle above to connect.";
        } else if (youtubeOn && youtubeStatus == YouTubeChatManager::Status::UNAVAILABLE) {
            note = "YouTube chat can't be read. Support is unofficial.";
        } else if (youtubeOn && youtubeStatus == YouTubeChatManager::Status::NOT_FOUND) {
            note = "YouTube has no such channel - check the spelling.";
        } else if (youtubeOn && youtubeStatus == YouTubeChatManager::Status::RETRYING) {
            note = "YouTube connection lost - retrying automatically.";
        } else if (youtubeOn && youtubeStatus == YouTubeChatManager::Status::NOT_LIVE) {
            note = "YouTube: not live right now - checking every minute.";
        }
        ctx.addNote(note);
    }

    // === LAYOUT === (the Event Log's controls, same wording)
    ctx.addSectionHeading("Layout");
    ctx.beginColumns(2, 6);   // side by side (beginColumns)
    static const char* const kModes[] = { "Off", "Always", "Auto-hide" };
    const bool modeOff = (hud->m_displayMode == StreamChatHud::DisplayMode::OFF);
    ctx.addCycleControl("Show mode", cycleName(kModes, static_cast<int>(hud->m_displayMode)),
        SettingsHud::CycleControl::enumMember(hud, &StreamChatHud::m_displayMode, 3, hud, kModes),
        hud, true, modeOff, "stream_chat.display_mode", /*tooltipOnArrows=*/false);

    char buf[16];
    snprintf(buf, sizeof(buf), "%ds", hud->m_autoHideDurationMs / 1000);
    ctx.addSteppedControl("Duration", buf,
        SettingsHud::SteppedControl::wrapInt(&hud->m_autoHideDurationMs,
            StreamChatHud::AUTO_HIDE_STEP_MS, StreamChatHud::MIN_AUTO_HIDE_MS,
            StreamChatHud::MAX_AUTO_HIDE_MS, hud),
        hud, hud->m_displayMode == StreamChatHud::DisplayMode::AUTO_HIDE, false,
        "stream_chat.duration", /*tooltipOnArrows=*/false);

    // Display order: Newest / Oldest, the Event Log's row
    const char* orderStr = (hud->m_displayOrder == StreamChatHud::DisplayOrder::NEWEST_FIRST)
        ? "Newest" : "Oldest";
    ctx.addCycleControl("Display order", orderStr,
        SettingsHud::CycleControl::enumMember(hud, &StreamChatHud::m_displayOrder, 2, hud),
        hud, true, false, "stream_chat.order", /*tooltipOnArrows=*/false);

    snprintf(buf, sizeof(buf), "%d", hud->m_maxRows);
    ctx.addSteppedControl("Lines to show", buf,
        SettingsHud::SteppedControl::clampInt(&hud->m_maxRows, 1,
            StreamChatHud::MIN_ROWS, StreamChatHud::MAX_ROWS, hud),
        hud, true, false, "stream_chat.max_lines", /*tooltipOnArrows=*/false);

    // Characters, not Gap Bar's percent: the wrap counts characters, so the
    // number shown is exactly how long a line of chat can be.
    snprintf(buf, sizeof(buf), "%d", hud->m_widthChars);
    ctx.addSteppedControl("Width", buf,
        SettingsHud::SteppedControl::clampInt(&hud->m_widthChars, 1,
            StreamChatHud::MIN_WIDTH_CHARS, StreamChatHud::MAX_WIDTH_CHARS, hud),
        hud, true, false, "stream_chat.width", /*tooltipOnArrows=*/false);

    ctx.addCycleControl("Timestamp", hud->m_showTimestamps ? "Clock" : "Off",
        boolCycle(&hud->m_showTimestamps, hud), hud, true, !hud->m_showTimestamps,
        "stream_chat.timestamp", /*tooltipOnArrows=*/false);

    ctx.endColumns();

    // === MESSAGES ===
    ctx.addSectionHeading("Messages");
    ctx.beginColumns(2, 8);
    auto addBool = [&](const char* label, bool* value, const char* tooltipId) {
        ctx.addCycleControl(label, *value ? "On" : "Off", boolCycle(value, hud),
            hud, true, !*value, tooltipId, /*tooltipOnArrows=*/false);
    };
    {
        static const char* const kIcons[] = { "Off", "On", "Auto" };
        ctx.addCycleControl("Platform icons", cycleName(kIcons, static_cast<int>(hud->m_platformIcons)),
            SettingsHud::CycleControl::enumMember(hud, &StreamChatHud::m_platformIcons, 3, hud, kIcons),
            hud, true, hud->m_platformIcons == StreamChatHud::PlatformIcons::OFF,
            "stream_chat.platform_icons", /*tooltipOnArrows=*/false);
    }
    addBool("Name colors", &hud->m_nameColors, "stream_chat.name_colors");
    addBool("Message colors", &hud->m_textColors, "stream_chat.text_colors");
    {
        // One switch for every role icon; the INI keeps the per-role bits.
        SettingsHud::CycleControl roles;
        roles.get = [hud]() { return hud->m_roleMask != 0 ? 1 : 0; };
        roles.set = [hud](int v) { hud->m_roleMask = v ? StreamChatHud::ROLES_ALL : 0; };
        roles.count = 2;
        roles.dirtyHud = hud;
        const bool on = hud->m_roleMask != 0;
        ctx.addCycleControl("Role icons", on ? "On" : "Off", roles, hud, true, !on,
            "stream_chat.roles", /*tooltipOnArrows=*/false);
    }
    addBool("Emote names", &hud->m_showEmotes, "stream_chat.emotes");
    addBool("Hide commands", &hud->m_hideCommands, "stream_chat.hide_commands");
    addBool("Hide bots", &hud->m_hideBots, "stream_chat.hide_bots");
    addBool("Hide links", &hud->m_hideLinks, "stream_chat.hide_links");
    ctx.endColumns();

    return hud;
}
