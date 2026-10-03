// ============================================================================
// core/chat_event.h
// The one message stream the stream chat HUD reads, whichever platform a line
// came from. TwitchIrc::interpret() and YouTubeChat::parseChatResponse() both
// produce Chat::Event; each manager queues them, StreamChatHud drains both
// queues into one list, and every decision after that (dedupe, moderation,
// role icons, filters) keys on the event's platform where the platforms differ.
//
// ROLES ARE ONE BITMASK FOR BOTH PLATFORMS. A role both platforms have shares a
// bit (YouTube's channel owner is ROLE_BROADCASTER, its moderator
// ROLE_MODERATOR); the HUD tints a badge per platform, so the shared bit still
// draws in the right platform's colour. The rest are one platform's alone.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace Chat {

enum class Platform : uint8_t {
    Twitch = 0,
    YouTube = 1,
};
constexpr int PLATFORM_COUNT = 2;

// ---------------------------------------------------------------------------
// Roles (badges)
// ---------------------------------------------------------------------------
enum Role : uint16_t {
    ROLE_NONE        = 0,
    ROLE_BROADCASTER = 1 << 0,   // Twitch broadcaster, YouTube channel owner
    ROLE_MODERATOR   = 1 << 1,   // both platforms
    ROLE_VIP         = 1 << 2,
    ROLE_SUBSCRIBER  = 1 << 3,   // Twitch's default sub badge (channels may use their own art)
    ROLE_STAFF       = 1 << 4,   // Twitch employees (and the retired admin/global_mod)
    ROLE_PARTNER     = 1 << 5,   // verified partner streamer
    ROLE_LEAD_MOD    = 1 << 6,   // lead moderator (a moderator the broadcaster promoted)
    ROLE_PRIME       = 1 << 7,   // Prime Gaming ("premium")
    ROLE_TURBO       = 1 << 8,
    ROLE_ARTIST      = 1 << 9,   // channel artist ("artist-badge")
    ROLE_FOUNDER     = 1 << 10,  // one of a channel's first subscribers
    ROLE_MEMBER      = 1 << 11,  // YouTube channel member (the channel's own badge art)
    ROLE_VERIFIED    = 1 << 12,  // YouTube verified channel
};

// The single role shown beside a name: the highest one held, or ROLE_NONE.
// Both platforms show every badge; one icon column shows the one that says most
// about who is talking in THIS channel -- the channel's own roles first, then
// the platform-wide statuses (partner/verified, then the paid ones). Founder,
// then subscriber/member, last: the most common badge says the least.
inline uint16_t primaryRole(uint16_t roles, uint16_t enabledMask) {
    const uint16_t r = roles & enabledMask;
    static constexpr uint16_t kRank[] = {
        ROLE_BROADCASTER, ROLE_STAFF, ROLE_LEAD_MOD, ROLE_MODERATOR, ROLE_VIP,
        ROLE_PARTNER, ROLE_VERIFIED, ROLE_ARTIST, ROLE_PRIME, ROLE_TURBO, ROLE_FOUNDER,
        ROLE_MEMBER, ROLE_SUBSCRIBER,
    };
    for (uint16_t role : kRank) {
        if (r & role) return role;
    }
    return ROLE_NONE;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
enum class EventKind : uint8_t {
    None,        // nothing the reader acts on
    Message,     // a chat line (Twitch PRIVMSG, YouTube chat item)
    ClearUser,   // ban/timeout -- drop that user's lines (Twitch CLEARCHAT <user>,
                 // YouTube markChatItemsByAuthorAsDeletedAction)
    ClearAll,    // drop every line of `platform` (Twitch /clear, a channel switch)
    ClearMsg,    // one message deleted by a moderator
    Joined,      // Twitch ROOMSTATE: the channel exists and we are in it
    Ping,        // Twitch PING: must be answered with PONG <param>
    Reconnect,   // Twitch RECONNECT: server asks us to reconnect
    Notice,      // Twitch NOTICE: e.g. suspended channel; text in `text`
};

struct Event {
    EventKind kind = EventKind::None;
    Platform platform = Platform::Twitch;
    std::string id;         // message id (Message / ClearMsg target)
    std::string login;      // sender: Twitch login, YouTube channel id (ClearUser target)
    std::string display;    // CP1252 display name (falls back to login)
    std::string text;       // CP1252 message text, emotes kept as words
    std::string textNoEmotes;  // CP1252 message text with emotes removed
    uint32_t rgb = 0;       // 0xRRGGBB of the user's chosen colour (Twitch only)
    bool hasColor = false;  // false = no colour chosen (the HUD picks one)
    uint16_t roles = ROLE_NONE;
    bool isAction = false;  // Twitch "/me" message
    long long sentMs = 0;   // ms since epoch, 0 if absent
    uint64_t arrival = 0;   // nextArrival() when queued: the order across platforms
};

// One counter for both managers, taken under each one's queue lock as an event
// is queued. The HUD drains the platforms one after the other, so within one
// frame this -- not the drain order -- is what keeps the chat in arrival order.
inline uint64_t nextArrival() {
    static std::atomic<uint64_t> counter{ 0 };
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace Chat
