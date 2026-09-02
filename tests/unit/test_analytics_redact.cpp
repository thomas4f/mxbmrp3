// ============================================================================
// tests/unit/test_analytics_redact.cpp
// Unit tests for core/analytics_redact.h — keeping the install id out of the
// player's log file.
//
// The bug this pins is a privacy one rather than a crash: the launch beacon's
// body was logged verbatim, and it carries a STABLE per-install UUID. Anyone
// collecting log files from testers could therefore tell which two logs came
// from the same person, and join either to a row in the analytics backend. The
// body still gets logged — it is how an ingest rejection is diagnosed — with the
// id and only the id taken out of it.
//
// Header-only, no game engine, no network. See tests/unit/README.md.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
#include "doctest.h"

#include "core/analytics_redact.h"

using AnalyticsRedact::redactInstallId;

namespace {
// A real UUIDv4, the shape generateUuidV4() produces.
const std::string kId = "3f2b9c14-7d6a-4e51-9b08-2ac5d7e04f13";
}  // namespace

TEST_CASE("redactInstallId: the id is gone and the rest of the body is untouched") {
    const std::string body =
        "{\"events\":[{\"eventName\":\"app_started\",\"props\":{\"install_id\":\"" + kId +
        "\",\"game\":\"MX Bikes\",\"launch_count\":42}}]}";
    const std::string out = redactInstallId(body, kId);

    CHECK(out.find(kId) == std::string::npos);
    CHECK(out.find("<install-id>") != std::string::npos);
    // Everything the log line is actually FOR must survive, or the redaction has
    // bought privacy by destroying the diagnostic.
    CHECK(out.find("app_started") != std::string::npos);
    CHECK(out.find("MX Bikes") != std::string::npos);
    CHECK(out.find("launch_count\":42") != std::string::npos);
    // The placeholder replaces the value INSIDE its quotes, so the body a
    // maintainer reads out of the log is still JSON.
    CHECK(out.find("\"install_id\":\"<install-id>\"") != std::string::npos);
}

TEST_CASE("redactInstallId: EVERY occurrence goes, whatever key carries it") {
    // Matching on the value rather than on a key name is the point: the id
    // already appears under two different names (props.install_id for Aptabase,
    // hit.session for GoatCounter), and a key-name filter would need extending
    // for each new one. This is the case that would fail if someone "simplified"
    // it to a lookup for "install_id".
    const std::string body =
        "{\"hits\":[{\"session\":\"" + kId + "\"}],\"props\":{\"install_id\":\"" + kId +
        "\"},\"note\":\"" + kId + "\"}";
    const std::string out = redactInstallId(body, kId);

    CHECK(out.find(kId) == std::string::npos);
    // Three replacements, not one: a loop that stopped at the first hit would
    // pass every other assertion in this file.
    int n = 0;
    for (std::string::size_type p = out.find("<install-id>"); p != std::string::npos;
         p = out.find("<install-id>", p + 1)) ++n;
    CHECK(n == 3);
}

TEST_CASE("redactInstallId: an unusable id leaves the body alone rather than shredding it") {
    // m_installId is empty when the RNG or the identity file failed. An empty
    // needle matches at every position, so a naive replace would splice the
    // placeholder between every character and produce an unreadable log line out
    // of a body that had no id in it to begin with.
    const std::string body = "{\"props\":{\"game\":\"MX Bikes\"}}";
    CHECK(redactInstallId(body, "") == body);
    CHECK(redactInstallId(body, "abc") == body);
    // Short ids are refused for a second reason: they collide. "MX" would
    // otherwise gut the payload.
    CHECK(redactInstallId(body, "MX") == body);
}

TEST_CASE("redactInstallId: a body that never carried the id is returned unchanged") {
    // The GoatCounter no-identity path and every future body that simply has no
    // id in it must survive byte-for-byte.
    const std::string body = "{\"no_sessions\":true,\"hits\":[{\"path\":\"/launch/mxb/1.29.4\"}]}";
    CHECK(redactInstallId(body, kId) == body);
}
