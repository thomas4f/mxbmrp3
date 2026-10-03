// ============================================================================
// core/youtube_chat.h
// Pure helpers for the YouTube live chat reader: what the streamer typed, the
// channel's /live page, and the chat poll response, each turned into a
// decision. No Windows, no sockets, no singletons -- YouTubeChatManager owns
// the requests, this owns every decision about what a response MEANS, so all
// of it is reachable from tests/unit/test_youtube_chat.cpp without a network.
//
// WHY THE WEB INTERFACE AND NOT THE DATA API. The official API needs a key; a
// shipped key's daily quota is shared by every user and runs out within hours,
// and a key per streamer is too much friction. The chat popout's own interface
// needs neither. It is undocumented, so it WILL change: every parse here
// separates "not live" (a normal state) from "not recognised" (Unrecognized,
// shown as "Unavailable"), and never throws.
//
// THE PROTOCOL, as the maintained open-source readers use it in September 2026
// (YouTube.js, yt-dlp; the older pytchat, masterchat and chat-downloader agree):
//   1. GET https://www.youtube.com/@handle/live (or /channel/UC.../live).
//      The page carries ytcfg (client version, visitor data),
//      ytInitialPlayerResponse (videoDetails.isLive) and ytInitialData, whose
//      conversationBar.liveChatRenderer holds the chat's first continuation.
//   2. POST /youtubei/v1/live_chat/get_live_chat with that continuation. The
//      response carries the next continuation, the delay YouTube wants before
//      asking again (timeoutMs), and the new chat actions.
// If YouTube changes this, the fixtures under tests/fixtures/youtube/ are the
// place to start: re-capture them, and the unit test says what moved.
// ============================================================================
#pragma once

#include "chat_event.h"
#include "chat_text.h"
#include "../vendor/nlohmann/json.hpp"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

namespace YouTubeChat {

// ---------------------------------------------------------------------------
// Channel input
// ---------------------------------------------------------------------------
// The stored form, and the widest one: "@" + a 30-character handle.
constexpr size_t MAX_CHANNEL_LEN = 31;

// Reduce whatever the streamer typed or pasted to one of two stored forms:
//   "@handle"      from @handle, handle, youtube.com/@handle[/live|/streams...]
//   "UC..."        from youtube.com/channel/UC... (or a bare 24-character UC id)
// Returns "" when nothing usable remains. A video link is deliberately not a
// form: it stops working when that stream ends, where a channel is set once
// and follows every stream, like Twitch's channel. Handles keep their case
// (YouTube treats them case-insensitively, and the streamer's own spelling
// reads best).
inline std::string normalizeChannel(const std::string& input) {
    std::string s = input;
    size_t lead = 0;
    while (lead < s.size() && (s[lead] == ' ' || s[lead] == '\t' || s[lead] == '\r' || s[lead] == '\n')) ++lead;
    s.erase(0, lead);
    auto stripPrefixCI = [&](const char* p) {
        const size_t n = std::char_traits<char>::length(p);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != p[i]) return false;
        }
        s.erase(0, n);
        return true;
    };
    auto isIdChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    };
    auto takeId = [&](size_t from, size_t maxLen) {
        std::string id;
        for (size_t i = from; i < s.size() && isIdChar(s[i]) && id.size() < maxLen; ++i) id += s[i];
        return id;
    };

    stripPrefixCI("https://") || stripPrefixCI("http://");
    stripPrefixCI("www.") || stripPrefixCI("m.") || stripPrefixCI("music.");
    if (stripPrefixCI("youtu.be/")) return std::string();  // a video
    const bool hadHost = stripPrefixCI("youtube.com/");
    if (hadHost) {
        if (stripPrefixCI("channel/")) {
            const std::string id = takeId(0, 24);
            return (id.size() == 24 && id[0] == 'U' && id[1] == 'C') ? id : std::string();
        }
        // Only a handle path is a channel; anything else under the host is a
        // video (watch, live/, shorts/) or a legacy /c/ or /user/ name, which
        // YouTube no longer resolves reliably.
        if (s.empty() || s[0] != '@') return std::string();
    }
    // A bare UC id: 24 characters starting "UC" is never a handle a person
    // would type, and it is exactly what a channel's settings page shows.
    if (!hadHost && s.size() >= 24 && s[0] == 'U' && s[1] == 'C') {
        const std::string id = takeId(0, 24);
        if (id.size() == 24 && (s.size() == 24 || !isIdChar(s[24]))) return id;
    }
    // Handle: letters, digits, '_', '-', '.', 3..30 of them. Every leading '@'
    // goes: the field shows one already, so a typed or pasted one doubles it.
    while (!s.empty() && s[0] == '@') s.erase(0, 1);
    std::string handle;
    for (char c : s) {
        if (!(isIdChar(c) || c == '.')) break;
        if (handle.size() >= MAX_CHANNEL_LEN - 1) break;
        handle += c;
    }
    if (handle.size() < 3) return std::string();
    return "@" + handle;
}

// The channel field shows a fixed '@' in front of what is being typed, so the
// streamer types just the handle (and editing a stored "@handle" starts from
// "handle"). It steps aside for text that is not a bare handle: a pasted URL, a
// pasted "@handle" that brings its own, or a 24-character UC channel id.
inline bool showsHandlePrefix(const std::string& typed) {
    if (typed.empty()) return true;
    if (typed[0] == '@' || typed.find('/') != std::string::npos) return false;
    return !(typed.size() == 24 && typed[0] == 'U' && typed[1] == 'C');
}

// The field's starting text for a stored channel: a handle without its '@'
// (showsHandlePrefix supplies it), a channel id as is.
inline std::string editableChannel(const std::string& stored) {
    return (!stored.empty() && stored[0] == '@') ? stored.substr(1) : stored;
}

// The page that says whether a stored channel is live: /@handle/live or
// /channel/UC.../live.
inline std::string livePagePath(const std::string& channel) {
    if (channel.empty()) return std::string();
    if (channel[0] == '@') return "/" + channel + "/live";
    return "/channel/" + channel + "/live";
}

// ---------------------------------------------------------------------------
// JSON access that cannot throw
// ---------------------------------------------------------------------------
using Json = nlohmann::json;

inline const Json* child(const Json* j, const char* key) {
    if (!j || !j->is_object()) return nullptr;
    const auto it = j->find(key);
    return it == j->end() ? nullptr : &*it;
}
inline const Json* childPath(const Json* j, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        j = child(j, k);
        if (!j) return nullptr;
    }
    return j;
}
inline const Json* element(const Json* j, size_t i) {
    if (!j || !j->is_array() || i >= j->size()) return nullptr;
    return &(*j)[i];
}
inline std::string str(const Json* j) {
    return (j && j->is_string()) ? j->get<std::string>() : std::string();
}
// A number YouTube sends as a number or as a numeric string ("timestampUsec").
inline long long num(const Json* j, long long fallback) {
    if (!j) return fallback;
    if (j->is_number_integer()) return j->get<long long>();
    if (j->is_number()) return static_cast<long long>(j->get<double>());
    if (j->is_string()) {
        const std::string& s = j->get_ref<const std::string&>();
        char* end = nullptr;
        const long long v = std::strtoll(s.c_str(), &end, 10);
        if (end != s.c_str()) return v;
    }
    return fallback;
}
// Parse without exceptions: a discarded value (not JSON) comes back as such.
inline Json parseJson(const std::string& text, size_t from = 0, size_t len = std::string::npos) {
    if (from >= text.size()) return Json(Json::value_t::discarded);
    const size_t n = (len == std::string::npos) ? text.size() - from : len;
    const char* begin = text.data() + from;
    return Json::parse(begin, begin + n, nullptr, /*allow_exceptions=*/false);
}

// The JSON object assigned after `marker` in a page's inline script
// ("var ytInitialData = {...};", "window[\"ytInitialData\"] = {...};",
// "ytcfg.set({...});"): its [start, end) in `html`, found by matching braces
// outside JSON strings. False when the marker or a balanced object is missing.
inline bool findJsonObject(const std::string& html, const char* marker, size_t& start, size_t& end) {
    size_t at = 0;
    while ((at = html.find(marker, at)) != std::string::npos) {
        at += std::char_traits<char>::length(marker);
        size_t i = at;
        // Skip the glue between the name and the object: quotes, brackets,
        // '=', '(' and whitespace -- nothing else, so a mention of the name in
        // running script ("if (ytInitialData.x)") is not mistaken for it.
        while (i < html.size() && (html[i] == ' ' || html[i] == '"' || html[i] == '\'' || html[i] == ']' ||
                                   html[i] == '=' || html[i] == '(' || html[i] == '\n' || html[i] == '\t')) ++i;
        if (i >= html.size() || html[i] != '{') continue;
        int depth = 0;
        bool inString = false;
        for (size_t k = i; k < html.size(); ++k) {
            const char c = html[k];
            if (inString) {
                if (c == '\\') ++k;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) {
                start = i;
                end = k + 1;
                return true;
            }
        }
        return false;  // unbalanced: the page was cut off
    }
    return false;
}

// "key":"value" anywhere in a page -- for the few ytcfg strings needed, which
// sit in one of several ytcfg.set calls that are not worth parsing whole.
inline std::string findStringValue(const std::string& html, const char* key) {
    const std::string needle = std::string("\"") + key + "\":\"";
    const size_t at = html.find(needle);
    if (at == std::string::npos) return std::string();
    const size_t from = at + needle.size();
    const size_t to = html.find('"', from);
    if (to == std::string::npos || to - from > 256) return std::string();
    return html.substr(from, to - from);
}

// A response excerpt for the log: YouTube explains a refused request in its
// body ("Request contains an invalid argument.", "Precondition check
// failed."), and that one line is what tells a field report apart. Printable
// ASCII only, whitespace runs folded to one space, cut at `maxLen`.
inline std::string logExcerpt(const std::string& text, size_t maxLen) {
    std::string out;
    out.reserve(maxLen < text.size() ? maxLen : text.size());
    bool space = false;
    for (char ch : text) {
        if (out.size() >= maxLen) break;
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = !out.empty();
            continue;
        }
        if (space && out.size() + 1 < maxLen) out += ' ';
        space = false;
        out += (c >= 0x20 && c < 0x7F) ? ch : '?';
    }
    return out;
}

// What a response looked like, for the log, WITHOUT its values: logs get
// shared, and a response carries the channel, the video id, the visitor data
// and other viewers' messages. JSON gives its size, its top-level keys and an
// error's own message ("Precondition check failed.", which is what tells a
// field report apart); anything else gives its size.
inline std::string logShape(const std::string& text) {
    std::string out = std::to_string(text.size()) + " bytes";
    const Json j = Json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return out + ", not a JSON object";
    out += ", keys:";
    int shown = 0;
    for (auto it = j.begin(); it != j.end() && shown < 12; ++it, ++shown) {
        out += ' ';
        out += logExcerpt(it.key(), 40);
    }
    const auto error = j.find("error");
    if (error != j.end() && error->is_object()) {
        const auto message = error->find("message");
        if (message != error->end() && message->is_string()) {
            out += "; error: " + logExcerpt(message->get<std::string>(), 120);
        }
    }
    return out;
}

// The session id the poll's context sends back. ytcfg's VISITOR_DATA where a
// page has it; the responseContext's visitorData otherwise (the first live
// test found a watch page with only the latter).
inline std::string findVisitorData(const std::string& html) {
    std::string v = findStringValue(html, "VISITOR_DATA");
    if (v.empty()) v = findStringValue(html, "visitorData");
    return v;
}

// The chat's first continuation inside a liveChatRenderer: the "Live chat"
// view (every message, second in the view selector) when the header offers
// it, else the renderer's own first continuation, whichever kind it is.
inline std::string chatContinuation(const Json* chat) {
    const Json* items = childPath(chat, { "header", "liveChatHeaderRenderer", "viewSelector",
                                          "sortFilterSubMenuRenderer", "subMenuItems" });
    std::string c = str(childPath(element(items, 1), { "continuation", "reloadContinuationData", "continuation" }));
    if (!c.empty()) return c;
    const Json* first = element(child(chat, "continuations"), 0);
    for (const char* kind : { "invalidationContinuationData", "timedContinuationData", "reloadContinuationData" }) {
        c = str(childPath(first, { kind, "continuation" }));
        if (!c.empty()) return c;
    }
    return c;
}

// ---------------------------------------------------------------------------
// The /live page
// ---------------------------------------------------------------------------
// Used when a page carries no client version. Any recent WEB version is
// accepted; this is the one yt-dlp sent in September 2026.
constexpr const char* FALLBACK_CLIENT_VERSION = "2.20260708.00.00";

enum class PageResult : uint8_t {
    Live,          // a live stream with chat: `continuation` is set
    NotLive,       // the channel exists; nothing live (or live with chat off)
    NotFound,      // YouTube sent its home page instead of a channel
    Unrecognized,  // not the page this parser knows: Unavailable
};

struct PageInfo {
    PageResult result = PageResult::Unrecognized;
    std::string videoId;
    std::string continuation;   // proves the stream has chat; not polled with (parseChatPage)
    std::string clientVersion;  // for the poll's context
    std::string visitorData;    // optional; sent back when present
};

inline PageInfo parseLivePage(const std::string& html) {
    PageInfo info;
    size_t dataStart = 0, dataEnd = 0;
    if (!findJsonObject(html, "ytInitialData", dataStart, dataEnd)) return info;  // Unrecognized
    const Json data = parseJson(html, dataStart, dataEnd - dataStart);
    if (!data.is_object()) return info;

    info.clientVersion = findStringValue(html, "INNERTUBE_CLIENT_VERSION");
    if (info.clientVersion.empty()) info.clientVersion = FALLBACK_CLIENT_VERSION;
    info.visitorData = findVisitorData(html);

    size_t playerStart = 0, playerEnd = 0;
    Json player;
    if (findJsonObject(html, "ytInitialPlayerResponse", playerStart, playerEnd)) {
        player = parseJson(html, playerStart, playerEnd - playerStart);
    }
    const Json* details = childPath(&player, { "videoDetails" });
    if (!details) {
        // No video at all: a channel page (nothing live, or a Live tab), or --
        // for a handle that does not exist -- YouTube's home feed, whose one
        // tab is "what to watch" (yt-dlp tells the two apart the same way).
        const size_t hit = html.find("\"FEwhat_to_watch\"", dataStart);
        const bool home = hit != std::string::npos && hit < dataEnd;
        info.result = home ? PageResult::NotFound : PageResult::NotLive;
        return info;
    }
    info.videoId = str(child(details, "videoId"));
    const Json* isLive = child(details, "isLive");
    if (!(isLive && isLive->is_boolean() && isLive->get<bool>())) {
        info.result = PageResult::NotLive;  // upcoming, ended, or a plain video
        return info;
    }

    const Json* chat = childPath(&data, { "contents", "twoColumnWatchNextResults", "conversationBar",
                                          "liveChatRenderer" });
    // Only proves the stream has a chat: the watch page's continuation is a
    // short token get_live_chat refuses (HTTP 400 in the first live test).
    // The chat page (parseChatPage) carries the one to poll with.
    info.continuation = chatContinuation(chat);
    // Live, but no chat to read: chat switched off for this stream.
    info.result = info.continuation.empty() ? PageResult::NotLive : PageResult::Live;
    return info;
}

// The chat page, /live_chat?is_popout=1&v=ID: the page YouTube's own chat
// frame loads, and where chat-downloader and pytchat take the first poll's
// continuation from. Empty `continuation` when there is no chat to read:
// `noChat` when the page is one this parser knows but carries no live chat
// (the stream just ended -- the /live page lags the end by a while -- or its
// chat was switched off), else a page this parser does not know (the caller
// then reports Unavailable).
struct ChatPageInfo {
    std::string continuation;
    bool noChat = false;
    std::string clientVersion;
    std::string visitorData;
};

inline std::string chatPagePath(const std::string& videoId) {
    return "/live_chat?is_popout=1&v=" + videoId;
}

inline ChatPageInfo parseChatPage(const std::string& html) {
    ChatPageInfo info;
    size_t dataStart = 0, dataEnd = 0;
    if (!findJsonObject(html, "ytInitialData", dataStart, dataEnd)) return info;
    const Json data = parseJson(html, dataStart, dataEnd - dataStart);
    if (!data.is_object()) return info;
    const Json* contents = child(&data, "contents");
    const Json* chat = child(contents, "liveChatRenderer");
    info.continuation = chatContinuation(chat);
    // A known page with no chat renderer at all (YouTube shows a message in its
    // place). A renderer WITHOUT a continuation is a moved format, not this.
    info.noChat = contents && contents->is_object() && !chat;
    info.clientVersion = findStringValue(html, "INNERTUBE_CLIENT_VERSION");
    info.visitorData = findVisitorData(html);
    return info;
}

// The POST body for one poll.
inline std::string buildPollBody(const std::string& continuation, const std::string& clientVersion,
                                 const std::string& visitorData) {
    Json client = { { "clientName", "WEB" }, { "clientVersion", clientVersion }, { "hl", "en" } };
    if (!visitorData.empty()) client["visitorData"] = visitorData;
    Json body = { { "context", { { "client", client } } }, { "continuation", continuation } };
    return body.dump();
}

// ---------------------------------------------------------------------------
// The chat poll
// ---------------------------------------------------------------------------
// How long to wait before the next poll: YouTube's timeoutMs, kept inside
// 1..8 s. chat-downloader caps it at 8 s because one response only reaches
// back ~10 s of messages, and a busy chat loses lines past that.
constexpr int MIN_POLL_DELAY_MS = 1000;
constexpr int MAX_POLL_DELAY_MS = 8000;
constexpr int DEFAULT_POLL_DELAY_MS = 5000;

inline int clampPollDelayMs(long long timeoutMs) {
    if (timeoutMs < MIN_POLL_DELAY_MS) return MIN_POLL_DELAY_MS;
    if (timeoutMs > MAX_POLL_DELAY_MS) return MAX_POLL_DELAY_MS;
    return static_cast<int>(timeoutMs);
}

enum class PollResult : uint8_t {
    Ok,            // `continuation` for the next poll, `events` to show
    Ended,         // a well-formed response with no way to continue: stream over
    Unrecognized,  // not the response this parser knows: Unavailable
};

struct PollInfo {
    PollResult result = PollResult::Unrecognized;
    std::string continuation;
    int delayMs = DEFAULT_POLL_DELAY_MS;
    std::vector<Chat::Event> events;
};

// Badges -> Chat roles. A member's badge is the channel's own art
// (customThumbnail); the platform ones are named by iconType.
inline uint16_t parseBadges(const Json* badges) {
    uint16_t roles = Chat::ROLE_NONE;
    if (!badges || !badges->is_array()) return roles;
    for (const Json& b : *badges) {
        const Json* r = child(&b, "liveChatAuthorBadgeRenderer");
        if (!r) continue;
        const std::string icon = str(childPath(r, { "icon", "iconType" }));
        if (icon == "OWNER") roles |= Chat::ROLE_BROADCASTER;
        else if (icon == "MODERATOR") roles |= Chat::ROLE_MODERATOR;
        else if (icon == "VERIFIED" || icon == "CHECK_CIRCLE_THICK") roles |= Chat::ROLE_VERIFIED;
        else if (child(r, "customThumbnail")) roles |= Chat::ROLE_MEMBER;
    }
    return roles;
}

// A runs array -> text with custom emoji kept as their shortcut (":_name:",
// the way Twitch emote words are kept) and without them. Standard emoji are the
// Unicode character itself, which the CP1252 step drops like any other emoji.
inline void appendRuns(const Json* runs, std::string& withEmotes, std::string& withoutEmotes) {
    if (!runs || !runs->is_array()) return;
    for (const Json& run : *runs) {
        if (const Json* t = child(&run, "text")) {
            const std::string s = str(t);
            withEmotes += s;
            withoutEmotes += s;
            continue;
        }
        const Json* emoji = child(&run, "emoji");
        if (!emoji) continue;
        const Json* custom = child(emoji, "isCustomEmoji");
        if (custom && custom->is_boolean() && custom->get<bool>()) {
            const std::string shortcut = str(element(child(emoji, "shortcuts"), 0));
            withEmotes += ' ';
            withEmotes += shortcut;
            withEmotes += ' ';
            withoutEmotes += ' ';
        } else {
            const std::string id = str(child(emoji, "emojiId"));
            withEmotes += id;
            withoutEmotes += id;
        }
    }
}

inline std::string simpleText(const Json* j) {
    if (const Json* s = child(j, "simpleText")) return str(s);
    std::string a, b;
    appendRuns(child(j, "runs"), a, b);
    return b;
}

// One addChatItemAction.item. False when it is a kind this reader does not
// show (engagement banners, gift announcements...) -- skipped, not an error.
// `malformed` is set for a kind it DOES show that lacks what every one of them
// carries (an id and an author): the sign the format moved.
inline bool parseItem(const Json& item, Chat::Event& ev, bool& malformed) {
    const Json* r = nullptr;
    enum { TEXT, PAID, STICKER, MEMBERSHIP } kind = TEXT;
    if ((r = child(&item, "liveChatTextMessageRenderer"))) kind = TEXT;
    else if ((r = child(&item, "liveChatPaidMessageRenderer"))) kind = PAID;
    else if ((r = child(&item, "liveChatPaidStickerRenderer"))) kind = STICKER;
    else if ((r = child(&item, "liveChatMembershipItemRenderer"))) kind = MEMBERSHIP;
    else return false;

    ev = Chat::Event{};
    ev.kind = Chat::EventKind::Message;
    ev.platform = Chat::Platform::YouTube;
    ev.id = str(child(r, "id"));
    ev.login = str(child(r, "authorExternalChannelId"));
    if (ev.id.empty() || ev.login.empty() || (kind == TEXT && !childPath(r, { "message", "runs" }))) {
        malformed = true;
        return false;
    }
    const std::string name = simpleText(child(r, "authorName"));  // may be absent
    // "@handle" names are ASCII; a display name the font cannot draw keeps
    // whatever survives, else a neutral stand-in (YouTube has no login to fall back on).
    ev.display = ChatText::utf8ToCp1252(name);
    if (ev.display.empty()) ev.display = "Viewer";
    ev.roles = parseBadges(child(r, "authorBadges"));
    ev.sentMs = num(child(r, "timestampUsec"), 0) / 1000;

    std::string text, bare;
    appendRuns(childPath(r, { "message", "runs" }), text, bare);
    std::string prefix;
    if (kind == PAID || kind == STICKER) {
        prefix = simpleText(child(r, "purchaseAmountText"));
        if (kind == STICKER) prefix += " sticker";
    } else if (kind == MEMBERSHIP) {
        ev.roles |= Chat::ROLE_MEMBER;
        // A milestone ("Member for 6 months") has a primary header and the
        // member's own message; a new member only the subtext ("New member").
        prefix = simpleText(child(r, "headerPrimaryText"));
        if (prefix.empty()) prefix = simpleText(child(r, "headerSubtext"));
        if (prefix.empty()) prefix = "New member";
    }
    if (!prefix.empty()) {
        text = prefix + (text.empty() ? "" : " - ") + text;
        bare = prefix + (bare.empty() ? "" : " - ") + bare;
    }
    ev.text = ChatText::utf8ToCp1252(text);
    ev.textNoEmotes = ChatText::utf8ToCp1252(bare);
    return true;
}

inline PollInfo parseChatResponse(const std::string& body) {
    PollInfo info;
    const Json j = parseJson(body);
    if (!j.is_object()) return info;  // Unrecognized
    const Json* contents = child(&j, "continuationContents");
    if (!contents) {
        // A well-formed reply with nothing to continue: the stream ended, or
        // the chat was switched off or made members-only while we read it.
        info.result = child(&j, "responseContext") ? PollResult::Ended : PollResult::Unrecognized;
        return info;
    }
    const Json* chat = child(contents, "liveChatContinuation");
    if (!chat) return info;  // continuationContents in a shape we do not know

    const Json* conts = child(chat, "continuations");
    if (conts && conts->is_array()) {
        for (const Json& c : *conts) {
            for (const char* key : { "invalidationContinuationData", "timedContinuationData",
                                     "reloadContinuationData" }) {
                const Json* d = child(&c, key);
                if (!d) continue;
                info.continuation = str(child(d, "continuation"));
                info.delayMs = clampPollDelayMs(num(child(d, "timeoutMs"), DEFAULT_POLL_DELAY_MS));
                break;
            }
            if (!info.continuation.empty()) break;
        }
    }
    // Only a seek (replay) continuation, or none: the live chat is over.
    info.result = info.continuation.empty() ? PollResult::Ended : PollResult::Ok;

    const Json* actions = child(chat, "actions");  // absent when nothing new arrived
    int shown = 0, broken = 0;
    if (actions && actions->is_array()) {
        info.events.reserve(actions->size());
        for (const Json& a : *actions) {
            if (const Json* add = childPath(&a, { "addChatItemAction", "item" })) {
                Chat::Event ev;
                bool malformed = false;
                if (parseItem(*add, ev, malformed)) {
                    info.events.push_back(std::move(ev));
                    ++shown;
                } else if (malformed) {
                    ++broken;
                }
            } else if (const Json* del = child(&a, "markChatItemAsDeletedAction")) {
                std::string target = str(child(del, "targetItemId"));
                if (target.empty()) continue;
                Chat::Event ev;
                ev.kind = Chat::EventKind::ClearMsg;
                ev.platform = Chat::Platform::YouTube;
                ev.id = std::move(target);
                info.events.push_back(std::move(ev));
            } else if (const Json* ban = child(&a, "markChatItemsByAuthorAsDeletedAction")) {
                std::string author = str(child(ban, "externalChannelId"));
                if (author.empty()) continue;
                Chat::Event ev;
                ev.kind = Chat::EventKind::ClearUser;
                ev.platform = Chat::Platform::YouTube;
                ev.login = std::move(author);
                info.events.push_back(std::move(ev));
            }
        }
    }
    // Messages arrived and not one of them could be read: the message format
    // moved under us. Say so rather than show a silent chat.
    if (broken > 0 && shown == 0) {
        info.result = PollResult::Unrecognized;
        info.events.clear();
    }
    return info;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
// YouTube has no per-user name colours; its dark theme colours a name by the
// author's role. 0xRRGGBB, before ChatText::ensureReadable.
inline uint32_t nameColor(uint16_t roles) {
    if (roles & Chat::ROLE_BROADCASTER) return 0xFFD600;  // owner: YouTube's gold
    if (roles & Chat::ROLE_MODERATOR) return 0x5E84F1;    // moderator blue
    if (roles & Chat::ROLE_MEMBER) return 0x2BA640;       // member green
    return 0xAAAAAA;                                      // everyone else: grey
}

// The key the bot filter compares: YouTube shows "@nightbot", Twitch "nightbot".
inline std::string botKey(const std::string& display) {
    std::string s;
    s.reserve(display.size());
    for (char c : display) {
        if (s.empty() && c == '@') continue;
        s += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Between sessions
// ---------------------------------------------------------------------------
// How one attempt (live page, chat page, polls) ended.
enum class SessionEnd : uint8_t { Reconfigured, NetworkError, NotLive, NotFound, Unrecognized, Ended };

// What the status shows next, and how long before the next attempt.
enum class RecheckShow : uint8_t { Keep, NotLive, NotFound, Unavailable, Retrying };
struct Recheck {
    RecheckShow show;
    unsigned waitS;
};

constexpr unsigned NOT_LIVE_RECHECK_S = 60;
// Unrecognised or not found: nothing will change until YouTube (or the
// plugin, or the channel) does, so look again only now and then.
constexpr unsigned SLOW_RECHECK_S = 600;
constexpr unsigned FIRST_BACKOFF_S = 5;
constexpr unsigned MAX_BACKOFF_S = 120;
// A chat that just ended: a short look normally finds "not live" (or the next
// stream). Never zero: a watch page still reading live after the stream, or a
// members-only chat, ends every attempt at once, and an attempt is ~1 MB.
constexpr unsigned ENDED_RECHECK_S = 5;

// The worker's pacing, pure so it is testable. `connected` is whether the
// attempt read the chat at least once: a chat that ran and then ended starts
// the "ended in a row" count over.
struct RecheckPolicy {
    unsigned backoffS = 0;
    unsigned endedInRow = 0;

    void reset() {
        backoffS = 0;
        endedInRow = 0;
    }

    Recheck next(SessionEnd end, bool connected) {
        if (connected) endedInRow = 0;
        if (end != SessionEnd::Ended) endedInRow = 0;
        if (end != SessionEnd::NetworkError) backoffS = 0;
        switch (end) {
        case SessionEnd::Ended:
            // Twice in a row without reading anything: this stream's chat
            // cannot be read (members-only, or the page lags the stream's
            // end), which is "not live" as far as the streamer can act on.
            return ++endedInRow >= 2 ? Recheck{ RecheckShow::NotLive, NOT_LIVE_RECHECK_S }
                                     : Recheck{ RecheckShow::Keep, ENDED_RECHECK_S };
        case SessionEnd::NotLive:      return { RecheckShow::NotLive, NOT_LIVE_RECHECK_S };
        case SessionEnd::NotFound:     return { RecheckShow::NotFound, SLOW_RECHECK_S };
        case SessionEnd::Unrecognized: return { RecheckShow::Unavailable, SLOW_RECHECK_S };
        case SessionEnd::Reconfigured: return { RecheckShow::Keep, 0 };
        case SessionEnd::NetworkError:
        default:
            backoffS = backoffS == 0 ? FIRST_BACKOFF_S : (backoffS * 2 < MAX_BACKOFF_S ? backoffS * 2 : MAX_BACKOFF_S);
            return { RecheckShow::Retrying, backoffS };
        }
    }
};

}  // namespace YouTubeChat
