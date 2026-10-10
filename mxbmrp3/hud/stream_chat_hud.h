// ============================================================================
// hud/stream_chat_hud.h
// Stream Chat HUD - the streamer's Twitch and YouTube chat, merged, in game.
//
// Sized and laid out like EventLogHud (same default 43-character body, same row
// height, same Show mode / row-count controls), so the two read as one family
// and a streamer can stack them. Newest message at the bottom by default, as on
// both platforms; Display order flips it (newest on top), with the Event Log's
// and Lap Log's wording. Configured on the GLOBAL Stream Chat
// tab and persisted in [StreamChat] (layout), [Twitch] and [YouTube] (each
// platform's switch and channel), never per profile: a profile switch must not move the
// chat or drop a channel.
//
// DATA FLOW. TwitchChatManager's and YouTubeChatManager's workers parse and
// CP1252-convert every message into a Chat::Event; update() drains each queue
// only when its atomic says something is there, turns each message into a
// StreamChatEntry (colours resolved, time formatted -- once), and applies
// moderation (a ban/timeout/deleted message removes the entry) within the
// platform it came from. Wrapping happens in rebuildRenderData(), which runs
// only when the entries or a setting changed -- never per frame.
//
// ONE LIST, ARRIVAL ORDER. Lines are shown in the order they were drained, not
// sorted by their platform timestamps: the two platforms' clocks and delivery
// delays differ (YouTube arrives in batches every few seconds), and re-sorting
// would move lines the streamer has already read.
//
// VISIBILITY IS NOT THE CONNECTION. Each platform stays connected while
// enabled (its manager's switch, its Status row on the Stream Chat tab) whether or not
// the HUD is shown; update() keeps draining while hidden, so showing it again
// brings the conversation back as it is.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/chat_event.h"
#include <chrono>
#include <deque>
#include <string>
#include <vector>

// One chat message, fully resolved at arrival.
struct StreamChatEntry {
    Chat::Platform platform = Chat::Platform::Twitch;
    std::string id;          // message id (deleted-message target), unique per platform
    std::string login;       // Twitch login / YouTube channel id (ban target)
    std::string display;     // CP1252 display name
    std::string text;        // CP1252, emote words kept
    std::string textNoEmotes;
    char time[8] = {};       // "HH:MM", local time of sending
    unsigned long nameColor = 0;  // ABGR: the Twitch colour or YouTube role colour, lifted for readability
    uint16_t roles = 0;
    bool isAction = false;
    bool isCommand = false;
    bool isBot = false;
    bool hasLink = false;
};

class StreamChatHud : public BaseHud {
public:
    enum class DisplayMode : uint8_t {
        OFF = 0,        // Hidden (the connections stay up)
        ON = 1,         // Always visible
        AUTO_HIDE = 2   // Shown when a message arrives, hidden after a timeout
    };

    // Display order, as on EventLogHud/LapLogHud. Either way a wrapped message
    // reads top to bottom; only the order of the messages flips.
    enum class DisplayOrder : uint8_t {
        OLDEST_FIRST = 0,  // Newest at the bottom, as on both platforms (default)
        NEWEST_FIRST = 1   // Newest at the top
    };

    // The per-line platform icon. Auto shows it while more than one platform
    // is switched on with a channel, i.e. exactly when a line's platform is
    // not obvious -- so a Twitch-only chat keeps its layout.
    enum class PlatformIcons : uint8_t { OFF = 0, ON = 1, AUTO = 2 };

    static constexpr int MIN_ROWS = 1;
    static constexpr int MAX_ROWS = 50;
    static constexpr int DEFAULT_ROWS = 6;
    // Panel width in characters, which is what the wrap counts in. The default is
    // Event Log's footprint (icon 3 + timestamp 9 + message 31 = 43; here role
    // icon 3 + "HH:MM " 6 + name and text 34). The minimum still leaves a line
    // of text beside the icon and time columns; a name wider than what is left
    // is truncated rather than drawn past the panel edge.
    static constexpr int MIN_WIDTH_CHARS = 30;
    static constexpr int DEFAULT_WIDTH_CHARS = 43;
    static constexpr int MAX_WIDTH_CHARS = 80;
    static constexpr int MIN_AUTO_HIDE_MS = 1000;
    static constexpr int DEFAULT_AUTO_HIDE_MS = 10000;
    static constexpr int MAX_AUTO_HIDE_MS = 60000;
    static constexpr int AUTO_HIDE_STEP_MS = 1000;
    // Messages kept for wrapping into rows. More than MAX_ROWS single-row
    // messages is never visible, so this is the most that can matter.
    static constexpr size_t MAX_ENTRIES = 64;

    // Role badge flags (Chat::Role) shown beside names.
    static constexpr uint32_t ROLES_ALL = Chat::ROLE_BROADCASTER | Chat::ROLE_MODERATOR |
                                         Chat::ROLE_VIP | Chat::ROLE_SUBSCRIBER |
                                         Chat::ROLE_STAFF | Chat::ROLE_PARTNER |
                                         Chat::ROLE_LEAD_MOD | Chat::ROLE_PRIME |
                                         Chat::ROLE_TURBO | Chat::ROLE_ARTIST |
                                         Chat::ROLE_FOUNDER | Chat::ROLE_MEMBER |
                                         Chat::ROLE_VERIFIED;

    StreamChatHud();
    virtual ~StreamChatHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType) const override { return false; }
    const char* getIconName() const override { return "hud-chat"; }
    void resetToDefaults();

    // Test-only observables (see the MXBMRP3_Test_Twitch* / _YouTube* / _Chat* hooks).
    int testEntryCount() const { return static_cast<int>(m_entries.size()); }
    int testRenderedRowCount() const { return m_renderedRows; }
    // Row `i` (0 = top) as "name|text" -- what the player reads, for asserting
    // wrap, filters and moderation without pixel tests. "" past the end.
#ifdef MXBMRP3_TEST_BUILD
    std::string testRowText(int i) const;
    // Row `i`'s platform and role icon sprites (0 = none drawn); false past the end.
    bool testRowSprites(int i, int& platformSprite, int& roleSprite) const;
#endif
    unsigned long testNameColor(int entryIndex) const;
    // Sprite a role's badge resolves to (0 = none): pins that every shipped
    // role icon is discovered and mapped to its role.
    int testBadgeSprite(Chat::Platform platform, uint16_t role) const {
        return getBadgeForRole(platform, role, nullptr);
    }
    int testPlatformSprite(Chat::Platform platform) const { return getPlatformIcon(platform, nullptr); }

    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;
    void clearRendered();
    void ingest(std::vector<Chat::Event>& events);
    bool isFiltered(const StreamChatEntry& e) const;

    // What the panel says about one platform's connection. Cheap (no
    // formatting), so update() can ask every frame.
    enum class Notice : uint8_t { NONE, OFF, NO_CHANNEL, CONNECTING, RETRYING, NOT_FOUND, NOT_LIVE, UNAVAILABLE };
    Notice platformNotice(int platform) const;
    static bool isBlocking(Notice n);
    bool anyPlatformOn() const;
    bool allOnAreBlocked() const;  // every switched-on platform is in a blocking state
    bool anyProblem() const;       // a notice worth holding auto-hide open for (not Off, not "not live")
    bool showPlatformIcons() const;
    int getBadgeForRole(Chat::Platform platform, uint16_t role, unsigned long* color) const;
    int getPlatformIcon(Chat::Platform platform, unsigned long* color) const;

    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int ICON_COL_WIDTH = 3;
    static constexpr int TIMESTAMP_WIDTH = 6;
    static constexpr float ICON_BASE_SIZE = 0.006f;  // matches EventLogHud

    // Settings
    DisplayMode m_displayMode = DisplayMode::ON;
    int m_maxRows = DEFAULT_ROWS;
    int m_widthChars = DEFAULT_WIDTH_CHARS;
    int m_autoHideDurationMs = DEFAULT_AUTO_HIDE_MS;
    DisplayOrder m_displayOrder = DisplayOrder::OLDEST_FIRST;
    bool m_showTimestamps = true;
    bool m_nameColors = true;         // off: names in the palette's primary colour
    bool m_textColors = false;        // on: message text in the sender's colour too
    uint32_t m_roleMask = ROLES_ALL;  // which role badges are drawn (Chat::Role bits; uint32_t for the CHECKBOX handler)
    PlatformIcons m_platformIcons = PlatformIcons::AUTO;
    bool m_showEmotes = false;        // keep emote words ("Kappa") instead of dropping them
    bool m_hideCommands = true;       // "!command" lines
    bool m_hideBots = true;           // Nightbot & co.
    bool m_hideLinks = false;

    // Data
    std::deque<StreamChatEntry> m_entries;
    std::vector<Chat::Event> m_drainBuffer;  // reused, reserved once

    // Auto-hide
    std::chrono::steady_clock::time_point m_lastMessageTime;
    bool m_hasRecent = false;

    int m_renderedRows = 0;
    // Each platform's connection as last drawn, indexed by Chat::Platform:
    // its manager's Status (0 = OFF), its switch, and whether it has a channel.
    int m_lastStatus[Chat::PLATFORM_COUNT] = {};
    bool m_lastEnabled[Chat::PLATFORM_COUNT] = {};
    bool m_lastHasChannel[Chat::PLATFORM_COUNT] = {};
#ifdef MXBMRP3_TEST_BUILD
    std::vector<std::string> m_testRows;  // "name|text" per rendered row (test builds only)
    struct TestRowSprites { int platform; int role; };
    std::vector<TestRowSprites> m_testRowSprites;  // parallel to m_testRows
#endif

    struct BadgeIcon { uint16_t role; const char* icon; uint32_t rgb; const char* file; };
    static const BadgeIcon kTwitchBadgeIcons[];
    static const BadgeIcon kYouTubeBadgeIcons[];
    struct CachedBadges {
        int sprite[Chat::PLATFORM_COUNT][16] = {};           // by platform, role bit; 0 = no icon
        unsigned long color[Chat::PLATFORM_COUNT][16] = {};  // game ABGR
        int platformSprite[Chat::PLATFORM_COUNT] = {};
        unsigned long platformColor[Chat::PLATFORM_COUNT] = {};
        bool initialized = false;
        void ensureInitialized();
    };
    mutable CachedBadges m_badgeCache;
};
