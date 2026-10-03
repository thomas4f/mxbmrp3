// ============================================================================
// tests/unit/test_analytics_identity.cpp
// Recovering the install id from a damaged analytics file
// (core/analytics_identity.h). Pins:
//   - a truncated file that still holds the whole "installId" value gives it
//     back; one cut inside the value gives nothing (never half an id)
//   - only a whole UUID counts: garbage, a short string or the wrong shape is
//     refused rather than adopted as an identity
//   - the counters come back from the text too, and a missing or non-numeric
//     one reads as 0 (a restart), never as a parse of adjacent digits
//   - a number the cut fell INSIDE reads as 0 too, never as the smaller number
//     its surviving digits spell: firstSeen is the file's first key, and a
//     prefix of it persisted as the install's epoch put its age at some twenty
//     thousand days for the life of the install (analytics_identity_test)
//   - a well-formed file's values are the same through this path as through
//     the JSON parser, so a recovered install looks like itself
// ============================================================================
#include "doctest.h"
#include "core/analytics_identity.h"

#include <string>

using namespace AnalyticsIdentity;

namespace {
const std::string kId = "3f2504e0-4f89-11d3-9a0c-0305e82c3301";
const std::string kWhole =
    "{\n  \"installId\": \"" + kId + "\",\n  \"lastVersion\": \"1.30.3.57\",\n"
    "  \"firstSeen\": 1751068800,\n  \"launchCount\": 42,\n  \"sessionStart\": 1758900000\n}\n";
}

TEST_CASE("looksLikeUuid: the shape the plugin writes, and nothing else") {
    CHECK(looksLikeUuid(kId));
    CHECK(looksLikeUuid("3F2504E0-4F89-11D3-9A0C-0305E82C3301"));
    CHECK_FALSE(looksLikeUuid(""));
    CHECK_FALSE(looksLikeUuid("3f2504e0-4f89-11d3-9a0c-0305e82c330"));     // 35 chars
    CHECK_FALSE(looksLikeUuid("3f2504e04f89-11d3-9a0c-0305e82c33011"));    // dash misplaced
    CHECK_FALSE(looksLikeUuid("zf2504e0-4f89-11d3-9a0c-0305e82c3301"));    // not hex
}

TEST_CASE("a whole file reads the same through recovery as through the parser") {
    CHECK(recoverInstallId(kWhole) == kId);
    CHECK(quotedValue(kWhole, "lastVersion") == "1.30.3.57");
    CHECK(recoverUnsigned(kWhole, "firstSeen") == 1751068800ULL);
    CHECK(recoverUnsigned(kWhole, "launchCount") == 42ULL);
    CHECK(recoverUnsigned(kWhole, "sessionStart") == 1758900000ULL);
}

TEST_CASE("a truncated file gives back what it still holds, whole values only") {
    // Cut after the version: the id and version survive, the counters are gone.
    const std::string cut = kWhole.substr(0, kWhole.find("\"firstSeen\""));
    CHECK(recoverInstallId(cut) == kId);
    CHECK(quotedValue(cut, "lastVersion") == "1.30.3.57");
    CHECK(recoverUnsigned(cut, "launchCount") == 0ULL);
    // Cut INSIDE the id: nothing, never a prefix of it.
    const std::string inside = kWhole.substr(0, kWhole.find(kId) + 20);
    CHECK(recoverInstallId(inside) == "");
    // Cut inside a number: the surviving digits spell a SMALLER number, not the
    // one the file wrote, so it reads as absent -- 4 is not a launch count of
    // 42, and 17 is not an epoch (the case the integration test drives).
    const std::string num = kWhole.substr(0, kWhole.find("42") + 1);
    CHECK(recoverUnsigned(num, "launchCount") == 0ULL);
    const std::string epoch = kWhole.substr(0, kWhole.find("1751068800") + 2);
    CHECK(recoverUnsigned(epoch, "firstSeen") == 0ULL);
    // A number the cut fell right AFTER is whole: the text goes on past it.
    const std::string after = kWhole.substr(0, kWhole.find("42") + 3);   // "42,"
    CHECK(recoverUnsigned(after, "launchCount") == 42ULL);
    CHECK(recoverUnsigned("{\"launchCount\": 7}", "launchCount") == 7ULL);
    CHECK(recoverUnsigned("{\"launchCount\": 7\n", "launchCount") == 7ULL);
}

TEST_CASE("garbage and near-misses are refused") {
    CHECK(recoverInstallId("") == "");
    CHECK(recoverInstallId("not json at all") == "");
    CHECK(recoverInstallId("{\"installId\": \"short\"}") == "");
    CHECK(recoverInstallId("{\"installId\": 12345}") == "");            // not a string
    CHECK(recoverInstallId("{\"installIdentity\": \"" + kId + "\"}") == "");   // key must match
    CHECK(recoverUnsigned("{\"launchCount\": \"42\"}", "launchCount") == 0ULL);   // quoted: not ours
    CHECK(recoverUnsigned("{\"launchCount\": -3}", "launchCount") == 0ULL);
    CHECK(recoverUnsigned("{\"launchCount\":99999999999999999999999}", "launchCount") == 0ULL);   // absurd
}

TEST_CASE("whitespace around the colon does not matter, and the first key wins") {
    CHECK(recoverInstallId("{\"installId\"  :\n\t\"" + kId + "\"}") == kId);
    CHECK(recoverUnsigned("{\"launchCount\"\t: 7}", "launchCount") == 7ULL);
    const std::string twice = "{\"installId\": \"" + kId + "\", \"installId\": \"ffffffff-ffff-ffff-ffff-ffffffffffff\"}";
    CHECK(recoverInstallId(twice) == kId);
}
