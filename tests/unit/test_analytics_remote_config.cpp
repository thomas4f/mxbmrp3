// ============================================================================
// tests/unit/test_analytics_remote_config.cpp
// Unit tests for core/analytics_remote_config.h — the pure parse/decision logic
// behind the remote Aptabase cost lever. The whole point is that it FAILS OPEN
// (to full) on anything unexpected and can only ever REDUCE what's sent, so these
// pin exactly that: garbage/missing/out-of-range input resolves to 1.0 (full),
// and the 0.0/1.0 endpoints are deterministic (no RNG) so the binary switch is
// exact. Header-only, no game engine, no network. See tests/unit/run_tests.sh.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
#include "doctest.h"

#include "core/analytics_remote_config.h"

using AnalyticsRemoteConfig::parseFullSample;
using AnalyticsRemoteConfig::shouldSendFull;

TEST_CASE("parseFullSample: valid values pass through, clamped to [0,1]") {
    CHECK(parseFullSample(R"({"aptabase_full_sample": 1.0})") == doctest::Approx(1.0));
    CHECK(parseFullSample(R"({"aptabase_full_sample": 0.0})") == doctest::Approx(0.0));
    CHECK(parseFullSample(R"({"aptabase_full_sample": 0.25})") == doctest::Approx(0.25));
    CHECK(parseFullSample(R"({"aptabase_full_sample": 1})") == doctest::Approx(1.0));   // int is a number
    // Out of range clamps rather than disabling.
    CHECK(parseFullSample(R"({"aptabase_full_sample": 2.0})") == doctest::Approx(1.0));
    CHECK(parseFullSample(R"({"aptabase_full_sample": -0.5})") == doctest::Approx(0.0));
}

TEST_CASE("parseFullSample: anything unexpected FAILS OPEN to 1.0 (full)") {
    CHECK(parseFullSample("") == doctest::Approx(1.0));                       // empty (a 404/outage)
    CHECK(parseFullSample("not json at all") == doctest::Approx(1.0));        // garbage
    CHECK(parseFullSample("{ broken json") == doctest::Approx(1.0));          // malformed
    CHECK(parseFullSample("{}") == doctest::Approx(1.0));                     // object, field missing
    CHECK(parseFullSample(R"({"other_key": 0.0})") == doctest::Approx(1.0));  // wrong field
    CHECK(parseFullSample(R"({"aptabase_full_sample": "0.0"})") == doctest::Approx(1.0)); // wrong type (string)
    CHECK(parseFullSample(R"({"aptabase_full_sample": null})") == doctest::Approx(1.0));  // null
    CHECK(parseFullSample("[1,2,3]") == doctest::Approx(1.0));               // not an object
    CHECK(parseFullSample("0.0") == doctest::Approx(1.0));                    // bare number, not an object
}

TEST_CASE("shouldSendFull: endpoints are deterministic, the middle uses the roll") {
    // 1.0 = every launch full (roll ignored); 0.0 = never full (roll ignored).
    CHECK(shouldSendFull(1.0, 0.99));
    CHECK(shouldSendFull(1.0, 0.0));
    CHECK_FALSE(shouldSendFull(0.0, 0.0));
    CHECK_FALSE(shouldSendFull(0.0, 0.99));
    // A fraction: full only when the draw lands under the sample.
    CHECK(shouldSendFull(0.5, 0.3));
    CHECK_FALSE(shouldSendFull(0.5, 0.7));
    CHECK_FALSE(shouldSendFull(0.5, 0.5));   // boundary: roll < sample is strict
    CHECK(shouldSendFull(0.25, 0.10));
    CHECK_FALSE(shouldSendFull(0.25, 0.40));
}
