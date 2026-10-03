// ============================================================================
// core/twitch_irc.h
// Pure helpers for the Twitch chat reader: IRC line parsing (IRCv3 tags) and
// the Twitch-specific interpretation (PRIVMSG / CLEARCHAT / CLEARMSG /
// ROOMSTATE / PING / RECONNECT) into the platform-neutral Chat::Event. No
// Windows, no sockets, no singletons -- TwitchChatManager owns the connection,
// this owns every decision about what a line MEANS, so all of it is reachable
// from tests/unit/test_twitch_irc.cpp without a network. The text shaping both
// platforms share (CP1252, wrap, filters) is core/chat_text.h.
//
// WHY IRC AND NOT HELIX/EVENTSUB. Twitch's REST/EventSub chat needs a registered
// application and a user OAuth token -- friction for a streamer and a secret in
// the INI. The chat server still accepts an ANONYMOUS read-only login
// (NICK justinfanNNNNN, no PASS), and with the twitch.tv/tags capability every
// message carries display name, colour, badges, emote ranges and a server
// timestamp. If Twitch ever retires anonymous IRC, only the transport in
// twitch_chat_manager.cpp changes; interpret() is the seam.
//
// ============================================================================
#pragma once

#include "chat_event.h"
#include "chat_text.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace TwitchIrc {

// ---------------------------------------------------------------------------
// Raw IRC line
// ---------------------------------------------------------------------------
struct Line {
    std::vector<std::pair<std::string, std::string>> tags;  // unescaped values
    std::string prefix;   // "nick!user@host" (without the ':'), may be empty
    std::string command;  // "PRIVMSG", "PING", "001", ...
    std::vector<std::string> params;  // trailing parameter (after " :") is the last one

    // A repeated tag key takes its LAST value (IRCv3 message-tags).
    const std::string* tag(const char* key) const {
        const std::string* found = nullptr;
        for (const auto& kv : tags) {
            if (kv.first == key) found = &kv.second;
        }
        return found;
    }
    // Nick part of the prefix ("nick!user@host" -> "nick").
    std::string nick() const {
        const size_t bang = prefix.find('!');
        return bang == std::string::npos ? prefix : prefix.substr(0, bang);
    }
};

// IRCv3 tag-value unescaping: \: -> ;  \s -> space  \\ -> \  \r \n -> CR LF.
// A trailing lone backslash is dropped, an unknown escape keeps its character.
inline std::string unescapeTagValue(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (c != '\\') { out += c; continue; }
        if (i + 1 >= v.size()) break;
        const char n = v[++i];
        switch (n) {
        case ':': out += ';'; break;
        case 's': out += ' '; break;
        case '\\': out += '\\'; break;
        case 'r': out += '\r'; break;
        case 'n': out += '\n'; break;
        default: out += n; break;
        }
    }
    return out;
}

// Parse one IRC line (without its CRLF). Returns false for an empty or
// command-less line; `out` is reset either way.
inline bool parseLine(const std::string& raw, Line& out) {
    out = Line{};
    size_t pos = 0;
    const size_t n = raw.size();
    auto skipSpaces = [&]() { while (pos < n && raw[pos] == ' ') ++pos; };

    skipSpaces();
    if (pos < n && raw[pos] == '@') {
        const size_t end = raw.find(' ', pos);
        const std::string tagStr = raw.substr(pos + 1, (end == std::string::npos ? n : end) - pos - 1);
        size_t t = 0;
        while (t <= tagStr.size()) {
            size_t semi = tagStr.find(';', t);
            if (semi == std::string::npos) semi = tagStr.size();
            const std::string item = tagStr.substr(t, semi - t);
            if (!item.empty()) {
                const size_t eq = item.find('=');
                if (eq == std::string::npos) {
                    out.tags.emplace_back(item, std::string());
                } else {
                    out.tags.emplace_back(item.substr(0, eq), unescapeTagValue(item.substr(eq + 1)));
                }
            }
            t = semi + 1;
        }
        if (end == std::string::npos) return false;
        pos = end;
        skipSpaces();
    }
    if (pos < n && raw[pos] == ':') {
        const size_t end = raw.find(' ', pos);
        if (end == std::string::npos) return false;
        out.prefix = raw.substr(pos + 1, end - pos - 1);
        pos = end;
        skipSpaces();
    }
    {
        const size_t end = raw.find(' ', pos);
        out.command = raw.substr(pos, (end == std::string::npos ? n : end) - pos);
        pos = (end == std::string::npos) ? n : end;
    }
    if (out.command.empty()) return false;
    while (pos < n) {
        skipSpaces();
        if (pos >= n) break;
        if (raw[pos] == ':') {
            out.params.push_back(raw.substr(pos + 1));
            break;
        }
        const size_t end = raw.find(' ', pos);
        out.params.push_back(raw.substr(pos, (end == std::string::npos ? n : end) - pos));
        pos = (end == std::string::npos) ? n : end;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Channel name
// ---------------------------------------------------------------------------
constexpr size_t MAX_CHANNEL_LEN = 25;  // Twitch login names: 4..25 of [a-z0-9_]

// Reduce whatever the streamer typed or pasted to a bare login name:
// "https://www.twitch.tv/SomeOne/videos?x=1" -> "someone", "@SomeOne" -> "someone",
// "#someone" -> "someone". Characters outside [a-z0-9_] end the name (a pasted
// trailing space or slash), and the result is capped at MAX_CHANNEL_LEN. Returns
// "" when nothing usable remains.
inline std::string normalizeChannel(const std::string& input) {
    std::string s;
    s.reserve(input.size());
    for (char c : input) s += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    auto trimFront = [&]() {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        s.erase(0, i);
    };
    trimFront();
    auto stripPrefix = [&](const char* p) {
        const std::string pre(p);
        if (s.compare(0, pre.size(), pre) == 0) { s.erase(0, pre.size()); return true; }
        return false;
    };
    stripPrefix("https://") || stripPrefix("http://");
    stripPrefix("www.") || stripPrefix("m.");
    stripPrefix("twitch.tv/");
    // A popout chat URL: twitch.tv/popout/<name>/chat
    stripPrefix("popout/");
    while (!s.empty() && (s[0] == '@' || s[0] == '#')) s.erase(0, 1);

    std::string out;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) break;
        if (out.size() >= MAX_CHANNEL_LEN) break;
        out += c;
    }
    return out;
}

// "broadcaster/1,subscriber/12,vip/1" -> bitmask. The retired admin and
// global_mod badges read as staff, the role that replaced them. Channel-only
// badges (bits, sub-gifter, predictions...) carry no role.
inline uint16_t parseBadges(const std::string& badges) {
    uint16_t roles = Chat::ROLE_NONE;
    size_t pos = 0;
    while (pos < badges.size()) {
        size_t comma = badges.find(',', pos);
        if (comma == std::string::npos) comma = badges.size();
        const std::string item = badges.substr(pos, comma - pos);
        const std::string name = item.substr(0, item.find('/'));
        if (name == "broadcaster") roles |= Chat::ROLE_BROADCASTER;
        else if (name == "lead_moderator") roles |= Chat::ROLE_LEAD_MOD;
        else if (name == "moderator") roles |= Chat::ROLE_MODERATOR;
        else if (name == "staff" || name == "admin" || name == "global_mod") roles |= Chat::ROLE_STAFF;
        else if (name == "partner") roles |= Chat::ROLE_PARTNER;
        else if (name == "vip") roles |= Chat::ROLE_VIP;
        else if (name == "subscriber") roles |= Chat::ROLE_SUBSCRIBER;
        else if (name == "founder") roles |= Chat::ROLE_FOUNDER;
        else if (name == "premium") roles |= Chat::ROLE_PRIME;
        else if (name == "turbo") roles |= Chat::ROLE_TURBO;
        else if (name == "artist-badge") roles |= Chat::ROLE_ARTIST;
        pos = comma + 1;
    }
    return roles;
}

// ---------------------------------------------------------------------------
// Colour (0xRRGGBB, as chat_text.h)
// ---------------------------------------------------------------------------
// "#1E90FF" -> 0x1E90FF. False for an empty tag (a user who never picked one) or
// anything that is not exactly '#' + 6 hex digits.
inline bool parseHexColor(const std::string& s, uint32_t& rgb) {
    if (s.size() != 7 || s[0] != '#') return false;
    uint32_t v = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    rgb = v;
    return true;
}

// Twitch's default name palette for users who never chose a colour. The web
// client derives it from the login; this is the long-standing community
// reproduction (first + last character code, mod 15) that tmi.js/BTTV use, so a
// colourless name gets the same colour here as in most overlays.
inline uint32_t defaultNameColor(const std::string& login) {
    static constexpr uint32_t kPalette[15] = {
        0xFF0000, 0x0000FF, 0x008000, 0xB22222, 0xFF7F50,
        0x9ACD32, 0xFF4500, 0x2E8B57, 0xDAA520, 0xD2691E,
        0x5F9EA0, 0x1E90FF, 0xFF69B4, 0x8A2BE2, 0x00FF7F,
    };
    if (login.empty()) return kPalette[0];
    const unsigned first = static_cast<unsigned char>(login.front());
    const unsigned last = static_cast<unsigned char>(login.back());
    return kPalette[(first + last) % 15];
}

// Remove the emote ranges named by the `emotes` tag ("25:0-4,12-16/1902:6-10")
// from a message. Twitch's ranges index CODE POINTS, inclusive at both ends.
// Out-of-range or malformed ranges are ignored rather than trusted.
inline std::vector<uint32_t> stripEmoteRanges(const std::vector<uint32_t>& cps, const std::string& emotesTag) {
    if (emotesTag.empty()) return cps;
    std::vector<bool> drop(cps.size(), false);
    size_t pos = 0;
    while (pos < emotesTag.size()) {
        size_t slash = emotesTag.find('/', pos);
        if (slash == std::string::npos) slash = emotesTag.size();
        const std::string emote = emotesTag.substr(pos, slash - pos);
        const size_t colon = emote.find(':');
        if (colon != std::string::npos) {
            size_t r = colon + 1;
            while (r < emote.size()) {
                size_t comma = emote.find(',', r);
                if (comma == std::string::npos) comma = emote.size();
                const std::string range = emote.substr(r, comma - r);
                const size_t dash = range.find('-');
                if (dash != std::string::npos && dash > 0 && dash + 1 < range.size()) {
                    char* endA = nullptr;
                    char* endB = nullptr;
                    const long a = std::strtol(range.c_str(), &endA, 10);
                    const long b = std::strtol(range.c_str() + dash + 1, &endB, 10);
                    if (endA == range.c_str() + dash && *endB == '\0' && a >= 0 && b >= a) {
                        for (long k = a; k <= b && static_cast<size_t>(k) < cps.size(); ++k) {
                            drop[static_cast<size_t>(k)] = true;
                        }
                    }
                }
                r = comma + 1;
            }
        }
        pos = slash + 1;
    }
    std::vector<uint32_t> out;
    out.reserve(cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        if (!drop[i]) out.push_back(cps[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Interpretation
// ---------------------------------------------------------------------------
// Interpret a parsed line. Pure: the caller decides what to send and what to show.
inline Chat::Event interpret(const Line& line) {
    Chat::Event ev;
    ev.platform = Chat::Platform::Twitch;
    const std::string& cmd = line.command;
    if (cmd == "PING") {
        ev.kind = Chat::EventKind::Ping;
        ev.text = line.params.empty() ? std::string("tmi.twitch.tv") : line.params.back();
        return ev;
    }
    if (cmd == "RECONNECT") { ev.kind = Chat::EventKind::Reconnect; return ev; }
    if (cmd == "ROOMSTATE") { ev.kind = Chat::EventKind::Joined; return ev; }
    if (cmd == "NOTICE") {
        ev.kind = Chat::EventKind::Notice;
        if (!line.params.empty()) ev.text = ChatText::utf8ToCp1252(line.params.back());
        // The machine-readable reason ("msg_channel_suspended", ...): Twitch
        // says to compare this, not the text, which may change.
        if (const std::string* id = line.tag("msg-id")) ev.id = *id;
        return ev;
    }
    if (cmd == "CLEARCHAT") {
        if (line.params.size() >= 2 && !line.params[1].empty()) {
            ev.kind = Chat::EventKind::ClearUser;
            ev.login = line.params[1];
        } else {
            ev.kind = Chat::EventKind::ClearAll;
        }
        return ev;
    }
    if (cmd == "CLEARMSG") {
        const std::string* id = line.tag("target-msg-id");
        if (!id || id->empty()) return ev;
        ev.kind = Chat::EventKind::ClearMsg;
        ev.id = *id;
        return ev;
    }
    if (cmd != "PRIVMSG" || line.params.size() < 2) return ev;

    ev.kind = Chat::EventKind::Message;
    ev.login = line.nick();
    if (const std::string* id = line.tag("id")) ev.id = *id;
    if (const std::string* c = line.tag("color")) ev.hasColor = parseHexColor(*c, ev.rgb);
    if (const std::string* b = line.tag("badges")) ev.roles = parseBadges(*b);
    if (const std::string* ts = line.tag("tmi-sent-ts")) ev.sentMs = std::atoll(ts->c_str());

    // A non-Latin display name ("日本語") cannot be drawn; the login always can.
    const std::string* dn = line.tag("display-name");
    if (dn && !dn->empty() && ChatText::isFullyRenderable(*dn)) ev.display = ChatText::utf8ToCp1252(*dn);
    else ev.display = ev.login;

    std::string body = line.params.back();
    // CTCP ACTION: "\x01ACTION waves\x01" is "/me waves".
    static const char kAction[] = "\x01" "ACTION ";
    if (body.compare(0, sizeof(kAction) - 1, kAction) == 0) {
        ev.isAction = true;
        body.erase(0, sizeof(kAction) - 1);
        if (!body.empty() && body.back() == '\x01') body.pop_back();
    }
    // Emote ranges index the body AFTER the ACTION wrapper is removed.
    const std::vector<uint32_t> cps = ChatText::decodeUtf8(body);
    ev.text = ChatText::codepointsToCp1252(cps);
    const std::string* emotes = line.tag("emotes");
    ev.textNoEmotes = (emotes && !emotes->empty())
        ? ChatText::codepointsToCp1252(stripEmoteRanges(cps, *emotes))
        : ev.text;
    return ev;
}

}  // namespace TwitchIrc
