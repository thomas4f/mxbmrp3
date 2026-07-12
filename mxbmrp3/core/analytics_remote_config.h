// ============================================================================
// core/analytics_remote_config.h
// Pure parsing/decision logic for the remote analytics sampling config — the
// developer-controlled cost lever for Aptabase (which bills per event).
//
// If analytics is enabled, the client fetches a small JSON file from the public
// release repo and reads `aptabase_full_sample` ∈ [0,1]: the fraction of launches
// that send the FULL event set (session_end + custom events) ON TOP of the events
// every launch always sends (app_started + crash). So the lever lets the developer
// dial Aptabase volume down WITHOUT shipping a release when per-event cost spikes.
//
//   1.0  → every launch is full  (the default; == current behavior)
//   0.0  → app_started + crash only  (the cheapest tier)
//   0.25 → ~1 in 4 launches send the full set
//
// REDUCE-ONLY + FAIL-OPEN: the value can only ever LOWER what's sent below the
// documented/consented maximum, and any empty/missing/garbage/out-of-range input
// (a GitHub outage, a 404, a typo) resolves to 1.0 = full — so the lever can never
// silently blind analytics, and a tampered file can at worst force everyone to
// minimal (the developer loses their own data) or back to the consented full. No
// signing needed. See AnalyticsManager::applyRemoteSampling().
//
// Header-only + no platform deps (just the vendored JSON lib) so the real logic is
// unit-tested headlessly on any host — see tests/unit/test_analytics_remote_config.cpp.
// ============================================================================
#pragma once

#include <string>
#include "../vendor/nlohmann/json.hpp"

namespace AnalyticsRemoteConfig {

// Parse the remote config body and return the full-event sample fraction in [0,1].
// Fail-open to 1.0 (full) on anything unexpected. Never throws.
inline double parseFullSample(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.is_object()) return 1.0;
        auto it = j.find("aptabase_full_sample");
        if (it == j.end() || !it->is_number()) return 1.0;
        double v = it->get<double>();
        if (!(v == v)) return 1.0;   // NaN -> full
        if (v < 0.0) return 0.0;     // clamp
        if (v > 1.0) return 1.0;     // clamp (also catches +Inf)
        return v;
    } catch (...) {
        return 1.0;   // malformed JSON -> full
    }
}

// Decide whether THIS launch sends the full event set, given the sample fraction and
// a single random draw in [0,1). The endpoints are deterministic (no draw consulted),
// so sample=1.0 is always full and sample=0.0 is always minimal — which makes the
// binary switch behavior exact and lets a test assert it without an RNG.
inline bool shouldSendFull(double sample, double roll01) {
    if (sample >= 1.0) return true;
    if (sample <= 0.0) return false;
    return roll01 < sample;
}

}  // namespace AnalyticsRemoteConfig
