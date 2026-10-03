// ============================================================================
// core/analytics_identity.h
// Recovering the anonymous install id from an analytics file that no longer
// parses -- the pure half of AnalyticsManager::loadAndUpdateIdentity.
//
// THE BUG THIS ENDS. mxbmrp3_analytics.json holds the install id and the launch
// counter. When the file existed but could not be parsed, the plugin used a
// throwaway id for the launch and left the file alone, so that a transient
// failure would not rotate the real id. A file that is CORRUPT rather than
// locked never parses again, though, and the plugin never rewrote it -- so from
// that launch on every launch of that install carried a fresh id, and the
// install vanished from the usage survey: each of its launches arrived as a
// brand-new install that never returned. The survey learnt to drop those
// launches, but the installs behind them were lost to it for good, and their
// share was rising week over week, which is what a population that only ever
// grows looks like.
//
// A truncated or otherwise damaged JSON file usually still contains the id: the
// writer sorts its keys, so it is the second one, a few dozen bytes in after
// firstSeen, and it is 36 characters of hex and dashes that nothing else in the
// file looks like. So a parse failure is answered by scanning the text for it,
// and the file is then REWRITTEN cleanly with that id; only when no id can be
// found does a new one get minted -- one rotation, then stable, instead of one
// per launch forever. An open failure (a lock) is still treated as transient
// and still leaves the file alone.
//
// WHOLE VALUES ONLY. A value the truncation fell inside is not a value: half an
// id is refused rather than adopted, and a number cut short is not the smaller
// number its surviving digits spell. That last one is the trap the key order
// sets: firstSeen is the FIRST key, so a file cut inside it read "17" for an
// epoch, which the repair then persisted for the life of the install and
// reported as an install age of some twenty thousand days; sessionStart is the
// LAST key, the likeliest place for a cut to land, and read as a prefix it made
// a crash on the next launch look days long. A digit run counts only when the
// text goes on past it.
//
// Pinned by tests/unit/test_analytics_identity.cpp; the end-to-end heal by
// tests/integration/tests/analytics_identity_test.cpp.
// ============================================================================
#pragma once

#include <cctype>
#include <cstdint>
#include <string>

namespace AnalyticsIdentity {

// A UUID as the plugin writes it: 8-4-4-4-12 lowercase or uppercase hex.
inline bool looksLikeUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// The value of the first "<key>": "<value>" pair in `text`, or "" when the key
// is absent or its value is not a complete quoted string. Tolerates any
// whitespace around the colon; does not interpret escapes (a UUID has none).
inline std::string quotedValue(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] != ':') return "";
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] != '"') return "";
    const size_t end = text.find('"', pos + 1);
    if (end == std::string::npos) return "";   // the truncation fell inside the value
    return text.substr(pos + 1, end - pos - 1);
}

// The install id a damaged file still carries, or "" when it holds none that
// is a whole UUID.
inline std::string recoverInstallId(const std::string& text) {
    const std::string id = quotedValue(text, "installId");
    return looksLikeUuid(id) ? id : "";
}

// The value of the first "<key>": <digits> pair, or 0 when absent, not a plain
// non-negative integer, or cut short by the end of the text (see the header).
inline unsigned long long recoverUnsigned(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] != ':') return 0;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    unsigned long long v = 0;
    size_t digits = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        if (v > (UINT64_MAX - 9) / 10) return 0;   // absurd: not a value this file wrote
        v = v * 10 + static_cast<unsigned long long>(text[pos] - '0');
        ++pos;
        ++digits;
    }
    if (pos >= text.size()) return 0;   // the digits ran into the cut: a prefix, not the number
    return digits ? v : 0;
}

}  // namespace AnalyticsIdentity
