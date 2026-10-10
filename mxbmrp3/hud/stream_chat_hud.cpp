// ============================================================================
// hud/stream_chat_hud.cpp
// Stream Chat HUD - see the header for the data flow and why it is global.
// ============================================================================

#include "stream_chat_hud.h"
#include "../core/twitch_chat_manager.h"
#include "../core/twitch_irc.h"
#include "../core/youtube_chat_manager.h"
#include "../core/youtube_chat.h"
#include "../core/chat_text.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/asset_manager.h"
#include <algorithm>
#include <cstring>
#include <ctime>

namespace {
unsigned long rgbToAbgr(uint32_t rgb) {
    return PluginUtils::makeColor(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                                  static_cast<uint8_t>((rgb >> 8) & 0xFF),
                                  static_cast<uint8_t>(rgb & 0xFF));
}
constexpr const char* TITLE = "Stream Chat";
constexpr const char* SETTINGS_HINT = "Check MXBMRP3 Settings > Stream Chat";
constexpr int TWITCH = static_cast<int>(Chat::Platform::Twitch);
constexpr int YOUTUBE = static_cast<int>(Chat::Platform::YouTube);
const char* platformName(int p) { return p == YOUTUBE ? "YouTube" : "Twitch"; }
}  // namespace

StreamChatHud::StreamChatHud() {
    setTextureBaseName("stream_chat_hud");
    setDraggable(true);
    m_drainBuffer.reserve(TwitchChatManager::MAX_QUEUED_EVENTS + YouTubeChatManager::MAX_QUEUED_EVENTS);
    resetToDefaults();
    m_bContentCard = true;  // content block under a title, like EventLogHud
}

void StreamChatHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.80f;
    setScale(1.0f);
    setPosition(cellsX(133), cellsY(59));  // right-column tower, after Performance
    m_displayMode = DisplayMode::ON;
    m_maxRows = DEFAULT_ROWS;
    m_widthChars = DEFAULT_WIDTH_CHARS;
    m_autoHideDurationMs = DEFAULT_AUTO_HIDE_MS;
    m_displayOrder = DisplayOrder::OLDEST_FIRST;
    m_showTimestamps = true;
    m_nameColors = true;
    m_textColors = false;
    m_roleMask = ROLES_ALL;
    m_platformIcons = PlatformIcons::AUTO;
    m_showEmotes = false;
    m_hideCommands = true;
    m_hideBots = true;
    m_hideLinks = false;
    setDataDirty();
}

void StreamChatHud::clearRendered() {
    if (!m_quads.empty() || !m_strings.empty()) {
        m_quads.clear();
        clearStrings();
    }
    m_renderedRows = 0;
}

void StreamChatHud::update() {
    // The connections do not depend on the HUD being shown: tick() keeps each
    // session up whenever its platform is enabled, and messages are drained and
    // held while hidden, so showing the HUD brings the conversation back as it
    // is. (Moderation that arrived meanwhile has been applied too.)
    TwitchChatManager& twitch = TwitchChatManager::getInstance();
    YouTubeChatManager& youtube = YouTubeChatManager::getInstance();
    twitch.tick();
    youtube.tick();
    const bool gotTwitch = twitch.drain(m_drainBuffer);
    const bool gotYouTube = youtube.drain(m_drainBuffer);
    if (gotTwitch || gotYouTube) {
        // Both drained into one buffer, Twitch's first: put them back in the
        // order they arrived. Only when both had something (rare per frame),
        // and in place -- no allocation.
        if (gotTwitch && gotYouTube) {
            std::sort(m_drainBuffer.begin(), m_drainBuffer.end(),
                      [](const Chat::Event& a, const Chat::Event& b) { return a.arrival < b.arrival; });
        }
        ingest(m_drainBuffer);
        m_drainBuffer.clear();
    }

    const bool shown = isVisibleAnySurface() && m_displayMode != DisplayMode::OFF;
    if (!shown) {
        clearRendered();
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Each connection's state is drawn in the panel, so a change on a worker
    // thread -- or of a switch or channel, which can change while the status
    // stays OFF -- is a rebuild. Two atomic loads per frame.
    const int status[Chat::PLATFORM_COUNT] = { static_cast<int>(twitch.getStatus()),
                                               static_cast<int>(youtube.getStatus()) };
    const bool enabled[Chat::PLATFORM_COUNT] = { twitch.isEnabled(), youtube.isEnabled() };
    const bool hasChannel[Chat::PLATFORM_COUNT] = { !twitch.getChannel().empty(), !youtube.getChannel().empty() };
    for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
        if (status[p] != m_lastStatus[p] || enabled[p] != m_lastEnabled[p] || hasChannel[p] != m_lastHasChannel[p]) {
            m_lastStatus[p] = status[p];
            m_lastEnabled[p] = enabled[p];
            m_lastHasChannel[p] = hasChannel[p];
            setDataDirty();
        }
    }

    // Auto-hide still shows the panel while it has a connection problem (or a
    // connection in progress) to report: otherwise "not found" would never be
    // seen in the mode a streamer is most likely to leave it in. "Stream chat
    // off" and "not live" are not problems: auto-hide stays hidden for them.
    if (m_displayMode == DisplayMode::AUTO_HIDE && anyProblem()) {
        if (m_quads.empty()) setDataDirty();
    } else if (m_displayMode == DisplayMode::AUTO_HIDE) {
        if (!m_hasRecent) {
            clearRendered();
            clearDataDirty();
            clearLayoutDirty();
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_lastMessageTime).count();
        if (elapsed >= m_autoHideDurationMs) {
            m_hasRecent = false;
            clearRendered();
            clearDataDirty();
            clearLayoutDirty();
            return;
        }
        // Coming back from hidden: the primitives were dropped, so rebuild.
        if (m_quads.empty()) setDataDirty();
    }

    processDirtyFlags();
}

void StreamChatHud::ingest(std::vector<Chat::Event>& events) {
    bool shownChanged = false;
    for (auto& ev : events) {
        const Chat::Platform platform = ev.platform;
        switch (ev.kind) {
        case Chat::EventKind::Message: {
            // Twitch may resend a message it thinks was not received, and a
            // YouTube reconnect can return lines already shown; the id is the
            // same, so the second copy is dropped. Ids are only unique within a
            // platform. A linear scan of at most MAX_ENTRIES, per message
            // received -- not per frame.
            if (!ev.id.empty() && std::any_of(m_entries.begin(), m_entries.end(),
                    [&](const StreamChatEntry& x) { return x.platform == platform && x.id == ev.id; })) {
                break;
            }
            StreamChatEntry e;
            e.platform = platform;
            e.id = std::move(ev.id);
            e.login = std::move(ev.login);
            e.display = std::move(ev.display);
            e.text = std::move(ev.text);
            e.textNoEmotes = std::move(ev.textNoEmotes);
            e.roles = ev.roles;
            e.isAction = ev.isAction;
            e.isCommand = ChatText::isCommand(e.text);
            e.hasLink = ChatText::containsLink(e.text);
            uint32_t rgb;
            if (platform == Chat::Platform::YouTube) {
                // YouTube has no chosen colours; its names are coloured by role.
                e.isBot = ChatText::isKnownBot(YouTubeChat::botKey(e.display));
                rgb = YouTubeChat::nameColor(e.roles);
            } else {
                e.isBot = ChatText::isKnownBot(e.login);
                rgb = ev.hasColor ? ev.rgb : TwitchIrc::defaultNameColor(e.login);
            }
            e.nameColor = rgbToAbgr(ChatText::ensureReadable(rgb));

            // Local time of sending; the arrival time when it was not stamped.
            std::time_t t = ev.sentMs > 0 ? static_cast<std::time_t>(ev.sentMs / 1000)
                                          : std::time(nullptr);
            struct tm lt;
            // localtime_s writes lt (output param); cppcheck can't model that.
            // cppcheck-suppress uninitvar
            localtime_s(&lt, &t);
            snprintf(e.time, sizeof(e.time), "%02d:%02d", lt.tm_hour, lt.tm_min);

            if (!isFiltered(e)) {
                m_lastMessageTime = std::chrono::steady_clock::now();
                m_hasRecent = true;
                shownChanged = true;
            }
            m_entries.push_back(std::move(e));
            if (m_entries.size() > MAX_ENTRIES) m_entries.pop_front();
            break;
        }
        case Chat::EventKind::ClearUser: {
            const size_t before = m_entries.size();
            m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                [&](const StreamChatEntry& e) { return e.platform == platform && e.login == ev.login; }),
                m_entries.end());
            shownChanged |= (m_entries.size() != before);
            break;
        }
        case Chat::EventKind::ClearMsg: {
            const size_t before = m_entries.size();
            m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                [&](const StreamChatEntry& e) { return e.platform == platform && e.id == ev.id; }),
                m_entries.end());
            shownChanged |= (m_entries.size() != before);
            break;
        }
        case Chat::EventKind::ClearAll: {
            // One platform's /clear or channel switch leaves the other's lines.
            const size_t before = m_entries.size();
            m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                [&](const StreamChatEntry& e) { return e.platform == platform; }), m_entries.end());
            shownChanged |= (m_entries.size() != before);
            break;
        }
        default:
            break;
        }
    }
    if (shownChanged) setDataDirty();
}

bool StreamChatHud::isFiltered(const StreamChatEntry& e) const {
    if (m_hideCommands && e.isCommand) return true;
    if (m_hideBots && e.isBot) return true;
    if (m_hideLinks && e.hasLink) return true;
    // A message that was nothing but emotes (or emoji) has nothing left to draw.
    const std::string& body = m_showEmotes ? e.text : e.textNoEmotes;
    return body.empty();
}

// Role icons: licensed Font Awesome glyphs from the plugin's own icon set, each
// the nearest to its platform's badge, tinted with that badge's colour (sampled
// from the platform's art). NOT the platforms' badge images: Twitch's developer
// terms allow displaying those only when fetched at runtime and cached for at
// most a day, never redistributed in an installer, and the plugin loads assets
// once at startup with an anonymous (token-less) connection that cannot list
// them; YouTube's member badges are each channel's own art.
//
// Staff is white: Twitch's badge is a white wrench on black, and black on the
// HUD panel would vanish. Twitch's moderator sword is not in Font Awesome Free,
// hence the shield. YouTube's moderator badge IS a wrench, and its owner has no
// badge of its own on YouTube (the name is highlighted), hence the camera the
// Twitch broadcaster uses.
//
// USER-SUPPLIED BADGES WIN. A streamer who wants the real art can put it in
// their textures folder themselves as twitch_<badge>_1.tga or
// youtube_<role>_1.tga (the `file` column); a role with such a file draws it
// untinted instead of its icon. The plugin ships none of those files -- see
// above for why.
//
// Pinned by twitch_chat_test ("every role shows its own icon") and
// youtube_chat_test ("every YouTube role shows its own icon"), which hold their
// own role -> icon lists, so a mix-up here fails against a second statement of
// the mapping rather than restating this one.
const StreamChatHud::BadgeIcon StreamChatHud::kTwitchBadgeIcons[] = {
    { Chat::ROLE_BROADCASTER, "video",          0xE91916, "twitch_broadcaster" },
    { Chat::ROLE_STAFF,       "wrench",         0xFFFFFF, "twitch_staff" },
    { Chat::ROLE_LEAD_MOD,    "gavel",          0x00AD03, "twitch_lead_moderator" },
    { Chat::ROLE_MODERATOR,   "shield",         0x00AD03, "twitch_moderator" },
    { Chat::ROLE_VIP,         "gem",            0xE005B9, "twitch_vip" },
    { Chat::ROLE_PARTNER,     "certificate",    0x9146FF, "twitch_partner" },
    { Chat::ROLE_ARTIST,      "paintbrush",     0x1E69FF, "twitch_artist" },
    { Chat::ROLE_PRIME,       "crown",          0x0096D6, "twitch_premium" },
    { Chat::ROLE_TURBO,       "bolt-lightning", 0x59399A, "twitch_turbo" },
    { Chat::ROLE_FOUNDER,     "award",          0x9345FE, "twitch_founder" },
    { Chat::ROLE_SUBSCRIBER,  "star",           0x8205B4, "twitch_subscriber" },
};
// YouTube's dark-theme colours: owner gold, moderator blue, member green,
// verified grey (the same ones YouTubeChat::nameColor gives their names).
const StreamChatHud::BadgeIcon StreamChatHud::kYouTubeBadgeIcons[] = {
    { Chat::ROLE_BROADCASTER, "video",        0xFFD600, "youtube_owner" },
    { Chat::ROLE_MODERATOR,   "wrench",       0x5E84F1, "youtube_moderator" },
    { Chat::ROLE_VERIFIED,    "circle-check", 0xAAAAAA, "youtube_verified" },
    { Chat::ROLE_MEMBER,      "star",         0x2BA640, "youtube_member" },
};

void StreamChatHud::CachedBadges::ensureInitialized() {
    if (initialized) return;
    const AssetManager& assets = AssetManager::getInstance();
    auto load = [&](int platform, const BadgeIcon* table, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const BadgeIcon& b = table[i];
            for (int bit = 0; bit < 16; ++bit) {
                if (b.role != (1u << bit)) continue;
                const int userBadge = assets.getSpriteIndex(b.file, 1);
                if (userBadge > 0) {
                    sprite[platform][bit] = userBadge;               // full-colour art: no tint
                    color[platform][bit] = ColorPalette::WHITE;
                } else {
                    sprite[platform][bit] = assets.getIconSpriteIndex(b.icon);
                    color[platform][bit] = rgbToAbgr(ChatText::ensureReadable(b.rgb));
                }
            }
        }
    };
    load(TWITCH, kTwitchBadgeIcons, sizeof(kTwitchBadgeIcons) / sizeof(kTwitchBadgeIcons[0]));
    load(YOUTUBE, kYouTubeBadgeIcons, sizeof(kYouTubeBadgeIcons) / sizeof(kYouTubeBadgeIcons[0]));
    // Platform marks: the brands' official full-colour logos (textures, not
    // icons), drawn untinted - both brands' rules forbid recolouring them. See
    // assets/textures/README.md.
    platformSprite[TWITCH] = assets.getSpriteIndex("twitch", 1);
    platformColor[TWITCH] = ColorPalette::WHITE;
    platformSprite[YOUTUBE] = assets.getSpriteIndex("youtube", 1);
    platformColor[YOUTUBE] = ColorPalette::WHITE;
    initialized = true;
}

// Sprite for a role on a platform, 0 for none; its tint in `color`.
int StreamChatHud::getBadgeForRole(Chat::Platform platform, uint16_t role, unsigned long* color) const {
    m_badgeCache.ensureInitialized();
    const int p = static_cast<int>(platform);
    for (int bit = 0; bit < 16; ++bit) {
        if (role != (1u << bit)) continue;
        if (color) *color = m_badgeCache.color[p][bit];
        return m_badgeCache.sprite[p][bit] > 0 ? m_badgeCache.sprite[p][bit] : 0;
    }
    return 0;
}

int StreamChatHud::getPlatformIcon(Chat::Platform platform, unsigned long* color) const {
    m_badgeCache.ensureInitialized();
    const int p = static_cast<int>(platform);
    if (color) *color = m_badgeCache.platformColor[p];
    return m_badgeCache.platformSprite[p] > 0 ? m_badgeCache.platformSprite[p] : 0;
}

// What the panel has to say about one platform's connection, if anything.
// Read from the values last seen in update() so a rebuild draws the state that
// triggered it.
StreamChatHud::Notice StreamChatHud::platformNotice(int p) const {
    if (!m_lastEnabled[p]) return Notice::OFF;
    if (!m_lastHasChannel[p]) return Notice::NO_CHANNEL;
    if (p == TWITCH) {
        switch (static_cast<TwitchChatManager::Status>(m_lastStatus[p])) {
        case TwitchChatManager::Status::CONNECTING:
        case TwitchChatManager::Status::JOINING:   return Notice::CONNECTING;
        case TwitchChatManager::Status::RETRYING:  return Notice::RETRYING;
        case TwitchChatManager::Status::NOT_FOUND: return Notice::NOT_FOUND;
        default:                                   return Notice::NONE;
        }
    }
    switch (static_cast<YouTubeChatManager::Status>(m_lastStatus[p])) {
    case YouTubeChatManager::Status::CONNECTING:  return Notice::CONNECTING;
    case YouTubeChatManager::Status::RETRYING:    return Notice::RETRYING;
    case YouTubeChatManager::Status::NOT_FOUND:   return Notice::NOT_FOUND;
    case YouTubeChatManager::Status::NOT_LIVE:    return Notice::NOT_LIVE;
    case YouTubeChatManager::Status::UNAVAILABLE: return Notice::UNAVAILABLE;
    default:                                      return Notice::NONE;
    }
}

// A state a platform cannot get past by itself (no channel, not found, a
// format this build does not know). When EVERY switched-on platform is in one,
// the panel REPLACES the chat with a centred headline and a hint, the way the
// Gamepad widget says "Controller N Not Connected" -- same colours, same shape.
// Otherwise each platform's state is one line over the chat, so the
// conversation stays readable while it sorts itself out.
bool StreamChatHud::isBlocking(Notice n) {
    return n == Notice::NO_CHANNEL || n == Notice::NOT_FOUND || n == Notice::UNAVAILABLE;
}

bool StreamChatHud::anyPlatformOn() const {
    for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
        if (m_lastEnabled[p]) return true;
    }
    return false;
}

bool StreamChatHud::allOnAreBlocked() const {
    bool any = false;
    for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
        const Notice n = platformNotice(p);
        if (n == Notice::OFF) continue;
        if (!isBlocking(n)) return false;
        any = true;
    }
    return any;
}

bool StreamChatHud::anyProblem() const {
    for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
        const Notice n = platformNotice(p);
        if (n != Notice::NONE && n != Notice::OFF && n != Notice::NOT_LIVE) return true;
    }
    return false;
}

bool StreamChatHud::showPlatformIcons() const {
    if (m_platformIcons == PlatformIcons::ON) return true;
    if (m_platformIcons == PlatformIcons::OFF) return false;
    int live = 0;
    for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
        if (m_lastEnabled[p] && m_lastHasChannel[p]) ++live;
    }
    return live > 1;
}

void StreamChatHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();
#ifdef MXBMRP3_TEST_BUILD
    m_testRows.clear();
    m_testRowSprites.clear();
#endif
    m_renderedRows = 0;

    auto dim = getScaledDimensions();
    const bool showIcons = (m_roleMask != 0);
    const bool showPlatforms = showPlatformIcons();

    // Layout: [platform icon] [role icon] [HH:MM] Name: text -- icon and time
    // columns optional, the name+text column takes whatever of the width is left.
    const int textCols = m_widthChars - (showPlatforms ? ICON_COL_WIDTH : 0)
                                     - (showIcons ? ICON_COL_WIDTH : 0)
                                     - (m_showTimestamps ? TIMESTAMP_WIDTH : 0);

    // How much of a name is drawn: all of it, unless the Width setting leaves
    // less room than the name plus its ": " -- then it is cut to fit rather than
    // drawn past the panel edge. Wrap and render both use this one answer.
    auto nameCharsFor = [textCols](const StreamChatEntry& e) {
        const int room = std::max(1, textCols - (e.isAction ? 3 : 2));
        return std::min(static_cast<int>(e.display.size()), room);
    };

    // One rendered row of one message.
    struct Row {
        const StreamChatEntry* entry;
        bool first;          // carries icons, time and name
        std::string text;    // this row's slice of the message
    };
    std::vector<Row> rows;
    rows.reserve(static_cast<size_t>(m_maxRows) + 4);

    // The connection notices. With nothing switched on, or every switched-on
    // platform stuck, the chat is replaced (no rows); otherwise each platform
    // with something to say takes a top row. With both platforms on, each line
    // names its platform.
    int platformsOn = 0;
    for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) platformsOn += m_lastEnabled[p] ? 1 : 0;
    const bool blocking = !anyPlatformOn() || allOnAreBlocked();

    struct StatusLine { char text[96]; unsigned long color; };
    StatusLine statusLines[Chat::PLATFORM_COUNT];
    int statusCount = 0;
    if (!blocking) {
        for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
            const Notice n = platformNotice(p);
            const char* msg = nullptr;
            char connecting[64];
            switch (n) {
            case Notice::CONNECTING:
                if (p == TWITCH) {
                    snprintf(connecting, sizeof(connecting), "Connecting to #%s...",
                             TwitchChatManager::getInstance().getChannel().c_str());
                } else {
                    snprintf(connecting, sizeof(connecting), "Connecting to %s...",
                             YouTubeChatManager::getInstance().getChannel().c_str());
                }
                msg = connecting;
                break;
            case Notice::RETRYING:    msg = "Connection lost - retrying..."; break;
            case Notice::NOT_LIVE:    msg = "Not live - checking every minute"; break;
            case Notice::NO_CHANNEL:  msg = "No channel set"; break;
            case Notice::NOT_FOUND:   msg = "Channel not found"; break;
            case Notice::UNAVAILABLE: msg = "Unavailable"; break;
            default: break;
            }
            if (!msg) continue;
            StatusLine& line = statusLines[statusCount++];
            if (platformsOn > 1) snprintf(line.text, sizeof(line.text), "%s: %s", platformName(p), msg);
            else snprintf(line.text, sizeof(line.text), "%s", msg);
            // A notice that blocks the platform reads NEGATIVE, the colour of the same
            // notice as a blocking headline below; a transient retry stays WARNING.
            line.color = (n == Notice::CONNECTING || n == Notice::NOT_LIVE) ? getColor(ColorSlot::MUTED)
                       : (n == Notice::RETRYING) ? getColor(ColorSlot::WARNING)
                                                 : getColor(ColorSlot::NEGATIVE);
        }
    }
    const int statusRows = std::min(statusCount, m_maxRows);
    const int chatRowBudget = blocking ? 0 : m_maxRows - statusRows;

    // Walk newest to oldest, wrapping each message, until the rows are full. A
    // message that only partly fits keeps the rows nearest the newest end: its
    // head scrolls off the top with newest at the bottom (as on both platforms),
    // its tail off the bottom with newest on top.
    const bool newestFirst = (m_displayOrder == DisplayOrder::NEWEST_FIRST);
    for (auto it = m_entries.rbegin(); it != m_entries.rend() && static_cast<int>(rows.size()) < chatRowBudget; ++it) {
        const StreamChatEntry& e = *it;
        if (isFiltered(e)) continue;
        const std::string& body = m_showEmotes ? e.text : e.textNoEmotes;
        // "Name: text", or "* Name text" for /me.
        const int prefixLen = nameCharsFor(e) + (e.isAction ? 3 : 2);
        std::vector<std::string> wrapped = ChatText::wrapChat(body, textCols - prefixLen, textCols);
        std::vector<Row> msgRows;
        msgRows.reserve(wrapped.size());
        for (size_t i = 0; i < wrapped.size(); ++i) {
            msgRows.push_back({ &e, i == 0, std::move(wrapped[i]) });
        }
        const int room = chatRowBudget - static_cast<int>(rows.size());
        const int take = std::min(room, static_cast<int>(msgRows.size()));
        if (newestFirst) {
            // rows is already in reading order: this message's head first.
            for (int k = 0; k < take; ++k) {
                rows.push_back(std::move(msgRows[static_cast<size_t>(k)]));
            }
        } else {
            // rows is built bottom-up, so push this message's rows in reverse.
            for (int k = 0; k < take; ++k) {
                rows.push_back(std::move(msgRows[msgRows.size() - 1 - static_cast<size_t>(k)]));
            }
        }
    }
    // Top-to-bottom reading order: oldest message first, newest at the bottom.
    if (!newestFirst) std::reverse(rows.begin(), rows.end());

    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(m_widthChars, dim.fontSize);
    want.sectionH = { m_maxRows * dim.lineHeightNormal };
    want.captionW = planTitleWidth(dim, TITLE, TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& plan = planPanel(dim, want);
    addPlanBackground(plan, START_X, START_Y);
    addPlanTitle(plan, TITLE, getColor(ColorSlot::PRIMARY));

    const float contentX = plan.contentX();
    const float iconColWidth = PluginUtils::calculateMonospaceTextWidth(ICON_COL_WIDTH, dim.fontSize);
    const float timeWidth = PluginUtils::calculateMonospaceTextWidth(TIMESTAMP_WIDTH, dim.fontSize);
    const float platformX = contentX;
    const float iconX = platformX + (showPlatforms ? iconColWidth : 0.0f);
    const float timeX = iconX + (showIcons ? iconColWidth : 0.0f);
    const float nameX = timeX + (m_showTimestamps ? timeWidth : 0.0f);
    const float iconHalfSize = ICON_BASE_SIZE * m_fScale;
    const float iconHalfWidth = iconHalfSize / PluginConstants::UI_ASPECT_RATIO;

    // Author names in the normal font, like rider names in every other HUD; the
    // name colour and trailing colon already set them apart from the message.
    const int textFont = getFont(FontCategory::NORMAL);
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long timeColor = getColor(ColorSlot::TERTIARY);

    if (blocking) {
        // Centred in the chat area, headline(s) over a hint -- the Gamepad
        // widget's disconnected message, down to its offsets. One headline per
        // stuck platform (named when both are on); the hint only if it fits.
        const char* headlines[Chat::PLATFORM_COUNT];
        char named[Chat::PLATFORM_COUNT][64];
        int headlineCount = 0;
        bool onlyUnavailable = true;
        if (!anyPlatformOn()) {
            headlines[headlineCount++] = "Stream chat off";
            onlyUnavailable = false;
        } else {
            for (int p = 0; p < Chat::PLATFORM_COUNT; ++p) {
                const Notice n = platformNotice(p);
                if (n == Notice::OFF) continue;
                const char* h = (n == Notice::NO_CHANNEL) ? "No channel set"
                              : (n == Notice::NOT_FOUND) ? "Channel not found"
                              : "YouTube chat unavailable";
                if (n != Notice::UNAVAILABLE) onlyUnavailable = false;
                if (platformsOn > 1 && n != Notice::UNAVAILABLE) {
                    snprintf(named[headlineCount], sizeof(named[headlineCount]), "%s: %s", platformName(p), h);
                    headlines[headlineCount] = named[headlineCount];
                } else {
                    headlines[headlineCount] = h;
                }
                ++headlineCount;
            }
        }
        // Nothing in Settings fixes a format this build cannot read, so the hint
        // says what it is rather than sending the player there. No promise of a
        // fix: YouTube's chat is unofficial.
        const char* hint = onlyUnavailable ? "YouTube's chat can't be read" : SETTINGS_HINT;

        const float lineH = dim.lineHeightNormal;
        const float centerX = contentX + PluginUtils::calculateMonospaceTextWidth(m_widthChars, dim.fontSize) * 0.5f;
        const float centerY = plan.contentY() + m_maxRows * lineH * 0.5f;
        const int shownHeadlines = std::min(headlineCount, m_maxRows);
        const bool withHint = m_maxRows > shownHeadlines;
        const int lines = shownHeadlines + (withHint ? 1 : 0);
        // Line i's centre, the block's middle on the chat area's middle. A
        // one-row panel has room for its headline only, drawn on that row.
        auto lineY = [&](int i, float fontSize) {
            if (m_maxRows == 1) return plan.contentY();
            return centerY + (static_cast<float>(i) - (lines - 1) * 0.5f) * lineH - rowCenterOffset(fontSize);
        };
        for (int i = 0; i < shownHeadlines; ++i) {
            addString(headlines[i], centerX, lineY(i, dim.fontSize),
                      PluginConstants::Justify::CENTER, getFont(FontCategory::NORMAL),
                      getColor(ColorSlot::NEGATIVE), dim.fontSize);
#ifdef MXBMRP3_TEST_BUILD
            m_testRows.push_back(std::string("!|") + headlines[i]);
            m_testRowSprites.push_back({ 0, 0 });
#endif
        }
        if (withHint) {
            addString(hint, centerX, lineY(shownHeadlines, dim.fontSize * 0.8f),
                      PluginConstants::Justify::CENTER, getFont(FontCategory::NORMAL),
                      getColor(ColorSlot::MUTED), dim.fontSize * 0.8f);
#ifdef MXBMRP3_TEST_BUILD
            m_testRows.push_back(std::string("!|") + hint);
            m_testRowSprites.push_back({ 0, 0 });
#endif
        }
        m_renderedRows = lines;
        setBounds(START_X, START_Y, START_X + plan.width(), START_Y + plan.height());
        return;
    }

    auto addIcon = [&](float colX, float rowY, int sprite, unsigned long color) {
        float cx = colX + iconColWidth * 0.25f;
        float cy = rowY + dim.lineHeightNormal * 0.5f;
        applyOffset(cx, cy);
        SPluginQuad_t quad;
        quad.m_aafPos[0][0] = cx - iconHalfWidth; quad.m_aafPos[0][1] = cy - iconHalfSize;
        quad.m_aafPos[1][0] = cx - iconHalfWidth; quad.m_aafPos[1][1] = cy + iconHalfSize;
        quad.m_aafPos[2][0] = cx + iconHalfWidth; quad.m_aafPos[2][1] = cy + iconHalfSize;
        quad.m_aafPos[3][0] = cx + iconHalfWidth; quad.m_aafPos[3][1] = cy - iconHalfSize;
        quad.m_iSprite = sprite;
        quad.m_ulColor = color;
        m_quads.push_back(quad);
    };

    // Chat sits at the newest end of the panel with any empty space beyond its
    // oldest line -- at the bottom by default, the way both platforms fill; the
    // status lines take the rows at the OTHER end, away from where new messages
    // land.
    const int emptyRows = chatRowBudget - static_cast<int>(rows.size());
    float y = plan.contentY();
    auto drawStatusLines = [&]() {
        for (int i = 0; i < statusRows; ++i) {
            char line[96];
            snprintf(line, sizeof(line), "%.*s", m_widthChars, statusLines[i].text);
            addString(line, contentX, y, PluginConstants::Justify::LEFT,
                      getFont(FontCategory::NORMAL), statusLines[i].color, dim.fontSize);
#ifdef MXBMRP3_TEST_BUILD
            m_testRows.push_back(std::string("~|") + line);
            m_testRowSprites.push_back({ 0, 0 });
#endif
            y += dim.lineHeightNormal;
        }
    };
    if (!newestFirst) {
        drawStatusLines();
        y += emptyRows * dim.lineHeightNormal;
    }

    for (const Row& row : rows) {
        const StreamChatEntry& e = *row.entry;
        const unsigned long nameColor = m_nameColors ? e.nameColor : primary;
        const unsigned long textColor = m_textColors ? e.nameColor : primary;

        if (row.first) {
            int platformSprite = 0;
            if (showPlatforms) {
                unsigned long platformColor = primary;
                platformSprite = getPlatformIcon(e.platform, &platformColor);
                if (platformSprite > 0) addIcon(platformX, y, platformSprite, platformColor);
            }
            const uint16_t role = Chat::primaryRole(e.roles, static_cast<uint16_t>(m_roleMask));
            unsigned long badgeColor = primary;
            const int sprite = showIcons ? getBadgeForRole(e.platform, role, &badgeColor) : 0;
            if (sprite > 0) addIcon(iconX, y, sprite, badgeColor);
            if (m_showTimestamps) {
                addString(e.time, timeX, y, PluginConstants::Justify::LEFT,
                          getFont(FontCategory::DIGITS), timeColor, dim.fontSize);
            }
            char name[MAX_WIDTH_CHARS + 4];   // nameCharsFor() can grant up to the full width
            snprintf(name, sizeof(name), e.isAction ? "* %.*s" : "%.*s:", nameCharsFor(e), e.display.c_str());
            addString(name, nameX, y, PluginConstants::Justify::LEFT, textFont, nameColor, dim.fontSize);
            const float textX = nameX + PluginUtils::calculateMonospaceTextWidth(
                static_cast<int>(strlen(name)) + 1, dim.fontSize);
            if (!row.text.empty()) {
                addString(row.text.c_str(), textX, y, PluginConstants::Justify::LEFT,
                          textFont, e.isAction && !m_textColors ? nameColor : textColor, dim.fontSize);
            }
#ifdef MXBMRP3_TEST_BUILD
            m_testRows.push_back(std::string(name) + "|" + row.text);
            m_testRowSprites.push_back({ platformSprite, sprite });
#endif
        } else {
            // Continuation rows hang under the name column.
            addString(row.text.c_str(), nameX, y, PluginConstants::Justify::LEFT,
                      textFont, e.isAction && !m_textColors ? nameColor : textColor, dim.fontSize);
#ifdef MXBMRP3_TEST_BUILD
            m_testRows.push_back("|" + row.text);
            m_testRowSprites.push_back({ 0, 0 });
#endif
        }
        y += dim.lineHeightNormal;
    }
    if (newestFirst) {
        y += emptyRows * dim.lineHeightNormal;
        drawStatusLines();
    }
    m_renderedRows = static_cast<int>(rows.size()) + statusRows;

    setBounds(START_X, START_Y, START_X + plan.width(), START_Y + plan.height());
}

#ifdef MXBMRP3_TEST_BUILD
std::string StreamChatHud::testRowText(int i) const {
    if (i < 0 || i >= static_cast<int>(m_testRows.size())) return std::string();
    return m_testRows[static_cast<size_t>(i)];
}

bool StreamChatHud::testRowSprites(int i, int& platformSprite, int& roleSprite) const {
    if (i < 0 || i >= static_cast<int>(m_testRowSprites.size())) return false;
    platformSprite = m_testRowSprites[static_cast<size_t>(i)].platform;
    roleSprite = m_testRowSprites[static_cast<size_t>(i)].role;
    return true;
}
#endif

unsigned long StreamChatHud::testNameColor(int entryIndex) const {
    if (entryIndex < 0 || entryIndex >= static_cast<int>(m_entries.size())) return 0;
    return m_entries[static_cast<size_t>(entryIndex)].nameColor;
}
