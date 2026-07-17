// ============================================================================
// core/http_server_internal.h
// Shared internal JSON-append helpers for the HttpServer translation units
// (http_server*.cpp). Extracted verbatim from http_server.cpp when
// buildJsonSnapshot() was split into http_server_snapshot.cpp; the logic is
// unchanged. Header-inline (was file-local `static` in the single TU) so every
// HttpServer TU sees one definition without ODR conflicts.
//
// Direct string building (rather than nlohmann::json) avoids per-frame heap
// allocations: buildJsonSnapshot() runs every time standings change.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace http_server_detail {

// Append a JSON-escaped string value (handles \, ", control chars)
inline void appendJsonString(std::string& out, const char* str) {
    out += '"';
    for (const char* p = str; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default: {
            unsigned char c = static_cast<unsigned char>(*p);
            if (c < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                out += esc;
            } else if (c < 0x80) {
                out += static_cast<char>(c);
            } else {
                // The game supplies names / track strings in a single-byte Western codepage
                // (Latin-1 / CP-1252), so a lone high byte like 0xFC ('ü') is invalid UTF-8
                // and renders as � in the browser (in-game it's fine via the CP-1252 font).
                // Promote it to 2-byte UTF-8 as a Latin-1 code point: always valid UTF-8, and
                // correct for the 0xA0-0xFF accent/umlaut range that covers Western names.
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
            break;
        }
        }
    }
    out += '"';
}

inline void appendJsonInt(std::string& out, int val) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", val);
    out += buf;
}

inline void appendJsonInt64(std::string& out, long long val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", val);
    out += buf;
}

inline void appendJsonFloat(std::string& out, float val) {
    // NaN/Inf would print as "nan"/"inf" — invalid JSON that makes the
    // client's JSON.parse throw on every snapshot until the value changes.
    if (!std::isfinite(val)) {
        out += '0';
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", val);
    out += buf;
}

}  // namespace http_server_detail
