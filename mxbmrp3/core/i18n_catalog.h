// ============================================================================
// core/i18n_catalog.h
// One language's translations: the gettext .po reader, the UTF-8 -> CP1252 step,
// the pseudo-locale, and the lookup table TR() resolves against (core/i18n.h).
// Pure (no Windows, no files), so the unit suite drives it directly
// (tests/unit/test_i18n.cpp).
//
// WHY GETTEXT .po AND NOT A PACK INI. The other packs describe art; this one is
// two thousand sentences a player translates by hand, and the standard tools for
// that already exist: Poedit shows the English beside the translation and flags
// what is missing, msgmerge marks a translation FUZZY when its English changes,
// msgfmt --check catches a lost %d, and xgettext extracts every TR() from the
// source (tools/i18n/pot.sh). An INI would mean rebuilding all four. The pack's
// identity still lives in its language.ini like every other pack type.
//
// THE KEY is the English text itself, plus an optional context for the rare
// English word that needs two translations (TRC). One English string is one
// entry, so "Off" is the same word on every row. A translation whose English
// changed is marked fuzzy by msgmerge and IGNORED here, so the player sees the
// new English rather than a translation of the old one.
//
// THE FONT. The .fnt renderer is a byte-indexed table in the plugin's code page,
// CP1252 plus the six Turkish letters CP1252 lacks (see chat_text.h and
// font_codepage.h), so every translation is converted ONCE, here, at load. An
// entry holding a character the code page cannot draw is dropped whole -- it
// falls back to English -- rather than drawn with holes in it; the loader counts
// them so the log says so.
// ============================================================================
#pragma once

#include "chat_text.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace I18n {

// One translated entry as the .po states it (UTF-8, unescaped).
struct PoEntry {
    std::string context;   // msgctxt; empty = none
    std::string english;   // msgid
    std::string text;      // msgstr (msgstr[0] for a plural entry)
    bool fuzzy = false;
};

// Unescape one quoted .po string literal's body: \n \t \r \" \\ (anything else
// keeps the character after the backslash, which is what msgfmt would warn on).
inline std::string unescapePo(const std::string& body) {
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c != '\\' || i + 1 >= body.size()) { out += c; continue; }
        const char n = body[++i];
        switch (n) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            default:  out += n;    break;
        }
    }
    return out;
}

// The body of the first "..." on a line (the closing quote is the LAST quote, so
// an escaped \" inside survives), or false when the line holds no string.
inline bool quotedBody(const std::string& line, size_t from, std::string& body) {
    const size_t open = line.find('"', from);
    const size_t close = line.rfind('"');
    if (open == std::string::npos || close == std::string::npos || close <= open) return false;
    body = unescapePo(line.substr(open + 1, close - open - 1));
    return true;
}

// Parse a .po file's text into its entries. The header entry (empty msgid),
// obsolete entries (#~) and untranslated entries (empty msgstr) are left out;
// fuzzy entries are kept with the flag set, so a caller can count them.
inline std::vector<PoEntry> parsePo(const std::string& text) {
    std::vector<PoEntry> out;
    PoEntry cur;
    enum class Field { None, Context, Id, Plural, Str, StrOther } field = Field::None;
    bool pendingFuzzy = false;
    bool haveId = false;

    auto flush = [&]() {
        if (haveId && !cur.english.empty() && !cur.text.empty()) out.push_back(cur);
        cur = PoEntry();
        haveId = false;
        field = Field::None;
    };

    size_t pos = 0;
    // A UTF-8 byte-order mark (Notepad writes one) is not part of the first line.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        pos = 3;
    }
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        line.erase(0, lead);

        if (line.empty()) continue;
        if (line[0] == '#') {
            // A comment after a complete entry starts the next one.
            if (field == Field::Str || field == Field::StrOther) flush();
            if (line.compare(0, 2, "#,") == 0 && line.find("fuzzy") != std::string::npos) pendingFuzzy = true;
            continue;
        }

        std::string body;
        auto keyword = [&](const char* kw) {
            const size_t n = std::strlen(kw);
            return line.compare(0, n, kw) == 0 && line.size() > n && (line[n] == ' ' || line[n] == '"');
        };
        if (line[0] == '"') {
            if (!quotedBody(line, 0, body)) continue;
            switch (field) {
                case Field::Context:  cur.context += body; break;
                case Field::Id:       cur.english += body; break;
                case Field::Str:      cur.text += body;    break;
                default: break;   // msgid_plural / msgstr[n>0]: not used
            }
        } else if (keyword("msgctxt")) {
            if (field == Field::Str || field == Field::StrOther) flush();
            quotedBody(line, 7, cur.context);
            field = Field::Context;
        } else if (keyword("msgid_plural")) {
            field = Field::Plural;
        } else if (keyword("msgid")) {
            if (field == Field::Str || field == Field::StrOther) flush();
            quotedBody(line, 5, cur.english);
            cur.fuzzy = pendingFuzzy;
            pendingFuzzy = false;
            haveId = true;
            field = Field::Id;
        } else if (line.compare(0, 9, "msgstr[0]") == 0) {
            quotedBody(line, 9, cur.text);
            field = Field::Str;
        } else if (line.compare(0, 7, "msgstr[") == 0) {
            field = Field::StrOther;
        } else if (keyword("msgstr")) {
            quotedBody(line, 6, cur.text);
            field = Field::Str;
        }
    }
    flush();
    return out;
}

// UTF-8 -> the font code page that refuses rather than drops: false when any
// character has no byte (ChatText::toCp1252 returns 0). Unlike
// ChatText::utf8ToCp1252 it keeps every space and line break as written -- a
// translation's spacing is the translator's.
inline bool utf8ToCp1252Exact(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (uint32_t cp : ChatText::decodeUtf8(in)) {
        if (cp == '\n') { out += '\n'; continue; }
        const unsigned char b = ChatText::toCp1252(cp);
        if (b == 0) return false;
        out += static_cast<char>(b);
    }
    return true;
}

// THE PSEUDO-LOCALE: every string bracketed, accented and ~35% longer, e.g.
// "Show gap" -> "[Šhöw gåp~~~]". Picked in the Language list with developer mode
// on, it answers two questions no translator should have to: which text on
// screen is NOT wrapped in TR() yet (it stays plain English), and which box
// overflows when a language runs longer. printf conversions (%d, %.1f, %%) and
// {placeholders} pass through untouched, so formatting keeps working. Output is
// CP1252 already.
inline std::string pseudoLocalize(const char* english) {
    std::string out = "[";
    int letters = 0;
    for (const char* p = english; *p; ++p) {
        const char c = *p;
        if (c == '%') {
            out += c;
            if (p[1] == '%') { out += *++p; continue; }
            while (p[1] && !std::strchr("diouxXeEfFgGaAcspn", p[1])) out += *++p;
            if (p[1]) out += *++p;
            continue;
        }
        if (c == '{') {
            while (*p && *p != '}') out += *p++;
            if (*p) out += *p;
            else break;
            continue;
        }
        unsigned char m = static_cast<unsigned char>(c);
        switch (c) {
            case 'a': m = 0xE5; break;  case 'A': m = 0xC5; break;
            case 'c': m = 0xE7; break;  case 'C': m = 0xC7; break;
            case 'e': m = 0xE9; break;  case 'E': m = 0xC9; break;
            case 'i': m = 0xEF; break;  case 'I': m = 0xCF; break;
            case 'n': m = 0xF1; break;  case 'N': m = 0xD1; break;
            case 'o': m = 0xF6; break;  case 'O': m = 0xD6; break;
            case 'u': m = 0xFC; break;  case 'U': m = 0xDC; break;
            case 'y': m = 0xFD; break;  case 'Y': m = 0xDD; break;
            case 'S': m = 0x8A; break;  case 's': m = 0x9A; break;
            default: break;
        }
        out += static_cast<char>(m);
        if (c != ' ' && c != '\n') ++letters;
    }
    for (int i = 0, pad = (letters * 35 + 99) / 100; i < pad; ++i) out += '~';
    out += ']';
    return out;
}

// One loaded language. Strings live in a deque, so a pointer handed out stays
// valid for the catalogue's whole life -- the TR() slots and the settings Find
// index both hold them.
class Catalog {
public:
    explicit Catalog(bool pseudo = false) : m_pseudo(pseudo) {}

    bool isPseudo() const { return m_pseudo; }
    size_t size() const { return m_byKey.size(); }

    // Add (or replace -- a pack layered over its base wins) one translation,
    // already in CP1252.
    void add(const std::string& context, const std::string& english, const std::string& text) {
        m_text.push_back(text);
        m_byKey[key(context.empty() ? nullptr : context.c_str(), english.c_str())] = m_text.back().c_str();
    }

    // The translation of `english` (in `context`, nullptr for none), or nullptr
    // when this catalogue has none. The pseudo-locale has one for everything.
    // Records which English literal each shown string came from (englishOf).
    const char* find(const char* context, const char* english) {
        const std::string k = key(context, english);
        auto it = m_byKey.find(k);
        const char* hit = nullptr;
        if (it != m_byKey.end()) {
            hit = it->second;
        } else if (m_pseudo) {
            // Stretched, but never past the line's room: the pseudo-locale shows
            // the longest a translation may be, so its screens are the fit check.
            std::string p = pseudoLocalize(english);
            const int max = context && context[0] ? 0 : limitFor(english);
            if (max > 0 && static_cast<int>(p.size()) > max) p.resize(static_cast<size_t>(max));
            m_text.push_back(std::move(p));
            hit = m_text.back().c_str();
            m_byKey.emplace(k, hit);
        }
        if (hit) m_englishOf[hit] = english;
        return hit;
    }

    // The English a shown translation came from, by POINTER (it is what a label
    // was drawn from), or nullptr when `shown` is not one of ours.
    const char* englishOf(const char* shown) const {
        auto it = m_englishOf.find(shown);
        return it == m_englishOf.end() ? nullptr : it->second;
    }

    // The most characters a translation of `english` may have ("#. max N" in
    // the template, see parseLimits), 0 = no limit.
    void setLimits(std::unordered_map<std::string, int> limits) { m_limits = std::move(limits); }
    int limitFor(const std::string& english) const {
        auto it = m_limits.find(english);
        return it == m_limits.end() ? 0 : it->second;
    }

private:
    // gettext's own join: context, EOT, id.
    static std::string key(const char* context, const char* english) {
        std::string k;
        if (context && context[0]) { k = context; k += '\x04'; }
        k += english;
        return k;
    }

    bool m_pseudo;
    std::deque<std::string> m_text;
    std::unordered_map<std::string, const char*> m_byKey;
    std::unordered_map<const char*, const char*> m_englishOf;
    std::unordered_map<std::string, int> m_limits;
};

// The printf conversions a string asks for, in order: each spec's length
// modifier, '*' and conversion letter, with flags, width and precision digits
// dropped ("%-5.2f" and "%f" read the same argument). "%%" consumes nothing and
// is left out. A translation goes to snprintf with the ENGLISH's arguments, so
// its signature must equal the English's -- one "%s" where the English has "%d"
// is undefined behaviour, likely a crash. msgfmt --check catches that only for
// the shipped packs; a player's pack in Documents is checked here, at load.
inline std::string formatSignature(const std::string& s) {
    std::string sig;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        if (i + 1 < s.size() && s[i + 1] == '%') { ++i; continue; }
        size_t j = i + 1;
        while (j < s.size() && std::strchr("-+ #0", s[j])) ++j;
        for (; j < s.size(); ++j) {
            const char c = s[j];
            if (c >= '0' && c <= '9') continue;
            if (c == '.') continue;
            sig += c;
            if (!std::strchr("*hlLzjtI", c)) break;   // the conversion letter ends the spec
        }
        sig += ';';
        i = j;
    }
    return sig;
}

// False when a printf spec asks for a width or precision of more than two
// digits. formatSignature lets both differ from the English on purpose (a
// translation may pad), but snprintf still GENERATES every character it is
// asked for before truncating: "%.999999999f" took 12 s in one call, which
// drawn on a HUD freezes the game. No label needs a field wider than 99.
inline bool formatFieldsBounded(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        if (i + 1 < s.size() && s[i + 1] == '%') { ++i; continue; }
        size_t j = i + 1;
        while (j < s.size() && std::strchr("-+ #0", s[j])) ++j;
        int run = 0;
        for (; j < s.size(); ++j) {
            const char c = s[j];
            if (c >= '0' && c <= '9') {
                if (++run > 2) return false;
                continue;
            }
            run = 0;
            if (c == '.') continue;
            if (!std::strchr("*hlLzjtI", c)) break;   // the conversion letter ends the spec
        }
        i = j;
    }
    return true;
}

// What loading one .po into a catalogue found, for the log line.
struct LoadStats {
    int loaded = 0;
    int fuzzy = 0;          // skipped: the English changed since it was translated
    int unrenderable = 0;   // skipped: holds a character the font cannot draw
    int badFormat = 0;      // skipped: its printf codes differ from the English's, or ask for a 3+ digit field
    int tooLong = 0;        // skipped: longer than its "#. max N" room
};

// The template's "#. max N" notes (tools/i18n/pot.sh writes them from the room
// each settings label has): msgid -> N. Only context-free, single-line msgids
// carry one. Read from the SHIPPED template, so a pack's stale copy of a note
// cannot loosen a limit.
inline std::unordered_map<std::string, int> parseLimits(const std::string& potText) {
    std::unordered_map<std::string, int> out;
    int pending = 0;
    size_t pos = 0;
    while (pos < potText.size()) {
        size_t end = potText.find('\n', pos);
        if (end == std::string::npos) end = potText.size();
        std::string line = potText.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = end + 1;
        if (line.rfind("#. max ", 0) == 0) {
            pending = std::atoi(line.c_str() + 7);
        } else if (line.rfind("msgid \"", 0) == 0) {
            std::string id;
            if (pending > 0 && quotedBody(line, 5, id)) out[id] = pending;
            pending = 0;
        } else if (line.empty() || line.rfind("msgctxt", 0) == 0) {
            pending = 0;
        }
    }
    return out;
}

// Add every usable entry of a .po's text to `cat`.
inline LoadStats loadPo(const std::string& poText, Catalog& cat) {
    LoadStats s;
    std::string cp;
    for (const PoEntry& e : parsePo(poText)) {
        if (e.fuzzy) { ++s.fuzzy; continue; }
        if (!utf8ToCp1252Exact(e.text, cp)) { ++s.unrenderable; continue; }
        // On the CONVERTED text: those are the bytes snprintf will read, so no
        // encoding of a '%' can pass here unseen.
        if (formatSignature(cp) != formatSignature(e.english) || !formatFieldsBounded(cp)) {
            ++s.badFormat;
            continue;
        }
        const int max = e.context.empty() ? cat.limitFor(e.english) : 0;
        if (max > 0 && static_cast<int>(cp.size()) > max) { ++s.tooLong; continue; }
        cat.add(e.context, e.english, cp);
        ++s.loaded;
    }
    return s;
}

}  // namespace I18n
