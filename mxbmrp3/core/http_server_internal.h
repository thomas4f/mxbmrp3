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

#include <cctype>
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

// Whether a request's Host header names this PC by an address: "localhost",
// an IPv4 address or a bracketed IPv6 address, each with an optional port.
// OBS, a local browser and a LAN client typing the PC's IP all pass; any
// other name is refused. An empty Host (an HTTP/1.0 tool) is allowed.
// Pinned by http_test.cpp's Host case.
inline bool isAllowedHost(const std::string& host) {
    if (host.empty()) return true;
    std::string name;
    size_t portAt = std::string::npos;
    if (host[0] == '[') {
        const size_t close = host.find(']');
        if (close == std::string::npos || close == 1) return false;
        for (size_t i = 1; i < close; ++i) {
            const char c = host[i];
            if (!std::isxdigit(static_cast<unsigned char>(c)) && c != ':' && c != '.') return false;
        }
        if (close + 1 < host.size()) {
            if (host[close + 1] != ':') return false;
            portAt = close + 2;
        }
    } else {
        const size_t colon = host.find(':');
        name = host.substr(0, colon);
        if (colon != std::string::npos) portAt = colon + 1;
    }
    if (portAt != std::string::npos) {
        if (portAt >= host.size() || host.size() - portAt > 5) return false;
        for (size_t i = portAt; i < host.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(host[i]))) return false;
        }
    }
    if (host[0] == '[') return true;

    std::string lower(name);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "localhost") return true;

    // Dotted IPv4: exactly four parts, each 0-255.
    int parts = 0;
    size_t i = 0;
    while (i <= name.size()) {
        size_t end = name.find('.', i);
        if (end == std::string::npos) end = name.size();
        const size_t len = end - i;
        if (len == 0 || len > 3) return false;
        int v = 0;
        for (size_t k = i; k < end; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(name[k]))) return false;
            v = v * 10 + (name[k] - '0');
        }
        if (v > 255) return false;
        ++parts;
        i = end + 1;
    }
    return parts == 4;
}

// Whether a request's Origin header, when present, is this server itself
// (http://<Host>). An absent Origin (a same-origin GET, OBS, a tool)
// passes; "null" (a file:// page) does not. Pinned by http_test.cpp's
// Origin case.
inline bool isSameOrigin(const std::string& origin, const std::string& host) {
    if (origin.empty()) return true;
    if (host.empty()) return false;
    return origin == "http://" + host;
}

}  // namespace http_server_detail
