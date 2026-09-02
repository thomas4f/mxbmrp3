// ============================================================================
// core/analytics_redact.h
// Strips the install id out of an analytics request body before it is written
// to the player's log.
//
// WHY THIS EXISTS. "The payload is anonymous, and this is the player's own local
// log" is true in both halves and the conclusion still does not follow:
// props["install_id"] is a STABLE per-install UUID, so a log holding the launch
// beacon's body verbatim is linkable — two logs from the same person match each
// other, and either one joins straight to a row in the analytics backend.
// Anonymous is not the same as unlinkable, and the person who ends up holding
// those logs is the one who should least be able to make that join.
//
// IT MATCHES ON THE VALUE, NOT ON A KEY NAME, and that is the whole design. A
// key-name filter ("install_id") would have to be extended by whoever adds the
// next field carrying the id — and would silently not cover the one that already
// exists under a different name, GoatCounter's hit["session"]. Matching the id
// itself covers every field that carries it, including ones not written yet.
//
// Header-only and free of platform deps so it unit-tests anywhere; the redaction
// is pinned by tests/unit/test_analytics_redact.cpp. The rule it cannot enforce
// for itself is that a body reaches the log only through
// AnalyticsManager::logOutgoing(), which is the sole caller — see its comment.
// ============================================================================
#pragma once

#include <string>

namespace AnalyticsRedact {

// What replaces the id. Stays inside the JSON string it came from, so a redacted
// body is still parseable — the log line exists to debug ingest rejections, and
// a body that no longer parses would not serve that.
inline const char* placeholder() { return "<install-id>"; }

inline std::string redactInstallId(const std::string& body, const std::string& installId) {
    // A short or empty id is not redactable, it is a needle that matches
    // everywhere: "" would splice the placeholder between every character, and a
    // handful of characters would hit unrelated hex inside a version or a hash.
    // The id is a UUIDv4 (36 chars) when it exists at all, so anything materially
    // shorter is a failure mode (RNG or file error leaves it empty), not an id.
    if (installId.size() < 16) return body;

    std::string out;
    out.reserve(body.size());
    std::string::size_type pos = 0;
    for (;;) {
        const std::string::size_type hit = body.find(installId, pos);
        if (hit == std::string::npos) { out.append(body, pos, std::string::npos); break; }
        out.append(body, pos, hit - pos);
        out += placeholder();
        pos = hit + installId.size();
    }
    return out;
}

}  // namespace AnalyticsRedact
