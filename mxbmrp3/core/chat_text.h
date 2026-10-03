// ============================================================================
// core/chat_text.h
// Text shaping shared by every chat platform the stream chat HUD reads: UTF-8
// to the in-game font's code page, readable name colours, the filters and the
// word wrap. Pure (no Windows, no sockets); the platform parsers
// (core/twitch_irc.h, core/youtube_chat.h) call it off the game thread, and the
// HUD wraps with it only when a rebuild is due. Pinned by
// tests/unit/test_twitch_irc.cpp and tests/unit/test_youtube_chat.cpp.
//
// THE FONT. The game's .fnt renderer is a byte-indexed CP1252 table, so text is
// converted here, once, off the game thread: Latin-1 and the CP1252 specials
// survive, everything else (emoji, CJK, Cyrillic) is dropped, and the spaces a
// dropped emoji leaves behind are collapsed.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ChatText {

// ---------------------------------------------------------------------------
// Colour (0xRRGGBB throughout; the HUD converts to the game's ABGR)
// ---------------------------------------------------------------------------
inline unsigned rgbLuma(uint32_t rgb) {
    const unsigned r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (299u * r + 587u * g + 114u * b) / 1000u;
}

// Lift a colour toward white until its luma reaches `minLuma`, keeping its hue.
// The HUD background is dark, and Twitch users pick navy and black; Twitch's own
// dark theme does the same adjustment. Colours already bright enough are returned
// unchanged, so this is a no-op for most names. The floor (70) sits just under
// Twitch's own default reds and greens (#FF0000 = 76, #008000 = 75), so the
// palette's saturated colours survive while blue (29), navy and black lift.
inline uint32_t ensureReadable(uint32_t rgb, unsigned minLuma = 70) {
    if (rgbLuma(rgb) >= minLuma) return rgb;
    unsigned r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    // Luma is linear in the blend factor f (x' = x + (255 - x) f), so solve it
    // directly instead of iterating.
    const unsigned l0 = rgbLuma(rgb);
    const float f = static_cast<float>(minLuma - l0) / static_cast<float>(255u - l0);
    r = static_cast<unsigned>(r + (255u - r) * f + 0.5f);
    g = static_cast<unsigned>(g + (255u - g) * f + 0.5f);
    b = static_cast<unsigned>(b + (255u - b) * f + 0.5f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
// Decode UTF-8 into code points. Invalid sequences become U+FFFD (which the
// CP1252 step then drops), so a malformed byte never swallows the next character.
inline std::vector<uint32_t> decodeUtf8(const std::string& s) {
    std::vector<uint32_t> cps;
    cps.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { cps.push_back(0xFFFD); ++i; continue; }
        if (i + len > s.size()) { cps.push_back(0xFFFD); ++i; continue; }
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { cps.push_back(0xFFFD); ++i; continue; }
        cps.push_back(cp);
        i += len;
    }
    return cps;
}

// One code point -> its CP1252 byte, or 0 when the font cannot draw it.
inline unsigned char toCp1252(uint32_t cp) {
    if (cp == '\t') return ' ';
    if (cp >= 0x20 && cp < 0x7F) return static_cast<unsigned char>(cp);
    if (cp >= 0xA0 && cp <= 0xFF) return static_cast<unsigned char>(cp);
    switch (cp) {
    case 0x20AC: return 0x80; case 0x201A: return 0x82; case 0x0192: return 0x83;
    case 0x201E: return 0x84; case 0x2026: return 0x85; case 0x2020: return 0x86;
    case 0x2021: return 0x87; case 0x02C6: return 0x88; case 0x2030: return 0x89;
    case 0x0160: return 0x8A; case 0x2039: return 0x8B; case 0x0152: return 0x8C;
    case 0x017D: return 0x8E; case 0x2018: return 0x91; case 0x2019: return 0x92;
    case 0x201C: return 0x93; case 0x201D: return 0x94; case 0x2022: return 0x95;
    case 0x2013: return 0x96; case 0x2014: return 0x97; case 0x02DC: return 0x98;
    case 0x2122: return 0x99; case 0x0161: return 0x9A; case 0x203A: return 0x9B;
    case 0x0153: return 0x9C; case 0x017E: return 0x9E; case 0x0178: return 0x9F;
    default: return 0;
    }
}

// Code points -> CP1252, dropping what the font cannot draw, collapsing runs of
// spaces (a removed emoji or emote leaves "a  b") and trimming both ends.
inline std::string codepointsToCp1252(const std::vector<uint32_t>& cps) {
    std::string out;
    out.reserve(cps.size());
    for (uint32_t cp : cps) {
        const unsigned char b = toCp1252(cp);
        if (b == 0) continue;
        if (b == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += static_cast<char>(b);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

inline std::string utf8ToCp1252(const std::string& s) {
    return codepointsToCp1252(decodeUtf8(s));
}

// True when every character of a UTF-8 string survives the CP1252 conversion --
// the test for "can this display name be drawn, or fall back to the login".
inline bool isFullyRenderable(const std::string& s) {
    for (uint32_t cp : decodeUtf8(s)) {
        if (toCp1252(cp) == 0) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------
// "!command ..." -- chat-bot commands, which are noise on a stream overlay.
inline bool isCommand(const std::string& text) {
    return !text.empty() && text[0] == '!';
}

// A deliberately small, conservative link test: a scheme, "www.", or a word
// ending in one of the few TLDs spam and self-promotion actually use. False
// positives only hide a line, so the list stays short rather than clever.
inline bool containsLink(const std::string& text) {
    std::string s;
    s.reserve(text.size());
    for (char c : text) s += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    if (s.find("://") != std::string::npos || s.find("www.") != std::string::npos) return true;
    static const char* const kTlds[] = { ".com", ".tv", ".gg", ".net", ".org", ".io", ".ly", ".me", ".co" };
    for (const char* tld : kTlds) {
        const std::string t(tld);
        size_t at = s.find(t);
        while (at != std::string::npos) {
            const size_t after = at + t.size();
            const bool endsWord = after >= s.size() || s[after] == ' ' || s[after] == '/';
            const bool hasName = at > 0 && s[at - 1] != ' ' && s[at - 1] != '.';
            if (endsWord && hasName) return true;
            at = s.find(t, at + 1);
        }
    }
    return false;
}

// Common chat bots, by login (Twitch) or lower-cased display name (YouTube,
// where the same bots post under these names). Their messages are mostly command replies and
// timers, which is exactly what a streamer does not want over the track.
inline bool isKnownBot(const std::string& login) {
    static const char* const kBots[] = {
        "nightbot", "streamelements", "streamlabs", "moobot", "fossabot",
        "wizebot", "sery_bot", "soundalerts", "kofistreambot", "botrixoficial",
        "commanderroot", "streamstickers", "pokemoncommunitygame",
    };
    for (const char* bot : kBots) {
        if (login == bot) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Wrapping
// ---------------------------------------------------------------------------
// Greedy word wrap of `text` into rows where the FIRST row has `firstWidth`
// characters (the rest of the row after "Name: ") and every later row has
// `restWidth`. A word longer than a row is hard-broken. Returns at least one row
// (possibly empty) so a message with an empty body still owns its name row.
inline std::vector<std::string> wrapChat(const std::string& text, int firstWidth, int restWidth) {
    std::vector<std::string> rows;
    const size_t w0 = firstWidth > 0 ? static_cast<size_t>(firstWidth) : 0;
    const size_t w1 = restWidth > 0 ? static_cast<size_t>(restWidth) : 1;
    size_t pos = 0;
    const size_t n = text.size();
    while (pos < n && text[pos] == ' ') ++pos;
    bool first = true;
    while (pos < n) {
        const size_t width = first ? w0 : w1;
        if (width == 0) {  // the name filled the first row: text starts on the next
            rows.emplace_back();
            first = false;
            continue;
        }
        if (n - pos <= width) {
            rows.push_back(text.substr(pos));
            pos = n;
            break;
        }
        const size_t limit = pos + width;
        size_t brk = text.rfind(' ', limit);
        if (brk == std::string::npos || brk <= pos) {
            // No space inside the row. On the FIRST row, if the word would fit on
            // a full row, move it down rather than splitting it mid-word.
            if (first && w1 > w0) {
                size_t wordEnd = text.find(' ', pos);
                if (wordEnd == std::string::npos) wordEnd = n;
                if (wordEnd - pos <= w1) {
                    rows.emplace_back();
                    first = false;
                    continue;
                }
            }
            rows.push_back(text.substr(pos, width));
            pos += width;
        } else {
            rows.push_back(text.substr(pos, brk - pos));
            pos = brk + 1;
        }
        while (pos < n && text[pos] == ' ') ++pos;
        first = false;
    }
    if (rows.empty()) rows.emplace_back();
    return rows;
}

}  // namespace ChatText
