// ============================================================================
// core/analytics_endpoint.h
// Pure routing helper for Aptabase: which ingest host an App Key points at.
//
// Aptabase encodes the region as the MIDDLE segment of the key (A-US-… / A-EU-…).
// Getting this wrong sends every event to the wrong region (or nowhere), silently,
// so it's split out header-only and unit-tested (tests/unit/test_analytics_endpoint.cpp)
// — no platform deps, so it verifies on any host. A self-hosted ("SH") or otherwise
// unrecognized key returns "" (we don't carry a custom host → no send).
// ============================================================================
#pragma once

#include <string>

namespace AnalyticsEndpoint {

inline std::wstring aptabaseHostForKey(const std::string& key) {
    if (key.find("-EU-") != std::string::npos) return L"eu.aptabase.com";
    if (key.find("-US-") != std::string::npos) return L"us.aptabase.com";
    return L"";   // self-hosted / unrecognized region -> no send
}

}  // namespace AnalyticsEndpoint
