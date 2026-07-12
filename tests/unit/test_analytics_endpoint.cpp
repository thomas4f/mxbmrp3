// ============================================================================
// tests/unit/test_analytics_endpoint.cpp
// Unit tests for core/analytics_endpoint.h — the App-Key -> Aptabase ingest-region
// routing. A wrong mapping here sends every event to the wrong region (or nowhere),
// silently, so pin the region parse: US/EU map to their hosts, and anything else
// (self-hosted "SH", empty, malformed, or a region substring in the wrong place)
// returns "" so the manager sends nothing rather than guessing. Header-only, no game
// engine, no network. See tests/unit/run_tests.sh.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
#include "doctest.h"

#include "core/analytics_endpoint.h"

using AnalyticsEndpoint::aptabaseHostForKey;

TEST_CASE("aptabaseHostForKey: US/EU keys route to their ingest hosts") {
    CHECK(aptabaseHostForKey("A-US-1234567890") == L"us.aptabase.com");
    CHECK(aptabaseHostForKey("A-EU-1234567890") == L"eu.aptabase.com");
}

TEST_CASE("aptabaseHostForKey: unrecognized / self-hosted / empty -> no host (no send)") {
    CHECK(aptabaseHostForKey("A-SH-1234567890").empty());   // self-hosted: no carried host
    CHECK(aptabaseHostForKey("").empty());                  // empty
    CHECK(aptabaseHostForKey("garbage").empty());           // malformed
    CHECK(aptabaseHostForKey("A-US").empty());              // truncated (no trailing '-')
    CHECK(aptabaseHostForKey("A-XX-000").empty());          // unknown region
    // The region must be the delimited middle segment, not just any occurrence of the
    // letters — a key whose random suffix happens to contain "US"/"EU" must NOT match.
    CHECK(aptabaseHostForKey("A-SH-00US00").empty());
}
