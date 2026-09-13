// ============================================================================
// tests/unit/test_update_version_match.cpp
// Guards UpdateChecker::isSameRelease — the "did the DLL that is running come
// from the update we just installed?" test.
//
// Regression test for a shipped bug: the post-update donation nudge, and with
// it the Fresh Coat achievement it feeds, were unreachable for every user from
// the day they landed. The sentinel the installer writes holds the raw release
// tag (vX.Y.Z, three components, so it parses as build 0) while the running
// PLUGIN_VERSION carries VER_BUILD = the git commit count and is never 0. The
// check was compareVersions(...) == 0, which compares all four components, so
// it could not once return equal. Nobody ever saw the nudge.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/update_checker.h"

#include <string>

TEST_CASE("update version match: a release tag matches the build that shipped in it") {
    // The exact shapes in play: release.yml writes the tag, resource.h +
    // stamp_version.cmake produce the running string. This is the case that
    // was broken, so it is the must-catch one.
    CHECK(UpdateChecker::isSameRelease("v1.30.2", "1.30.2.5417"));
    CHECK(UpdateChecker::isSameRelease("1.30.2", "1.30.2.5417"));
    CHECK(UpdateChecker::isSameRelease("v1.30.2", "1.30.2.0"));
    CHECK(UpdateChecker::isSameRelease("V1.30.2", "1.30.2.5417"));   // capital V
    CHECK(UpdateChecker::isSameRelease("v1.30.2-beta1", "1.30.2.5417"));

    // Two builds of the same release are the same release, whichever way round.
    CHECK(UpdateChecker::isSameRelease("1.30.2.10", "1.30.2.9999"));
    CHECK(UpdateChecker::isSameRelease("1.30.2.9999", "1.30.2.10"));
}

TEST_CASE("update version match: a different release never matches") {
    CHECK_FALSE(UpdateChecker::isSameRelease("v1.30.3", "1.30.2.5417"));   // patch
    CHECK_FALSE(UpdateChecker::isSameRelease("v1.31.2", "1.30.2.5417"));   // minor
    CHECK_FALSE(UpdateChecker::isSameRelease("v2.30.2", "1.30.2.5417"));   // major
    CHECK_FALSE(UpdateChecker::isSameRelease("v1.30", "1.30.2.5417"));     // patch 0 vs 2
}

TEST_CASE("update version match: a garbled sentinel is not a match") {
    // The sentinel is a file on disk: it can be truncated, empty or junk. It
    // must never alias to "the update installed", which is the trap the old
    // isValidVersion() pre-check existed to close (compareVersions returns 0
    // for unparseable as well as for equal).
    CHECK_FALSE(UpdateChecker::isSameRelease("", "1.30.2.5417"));
    CHECK_FALSE(UpdateChecker::isSameRelease("v", "1.30.2.5417"));
    CHECK_FALSE(UpdateChecker::isSameRelease("not-a-version", "1.30.2.5417"));
    CHECK_FALSE(UpdateChecker::isSameRelease("\x01\x02", "1.30.2.5417"));
}
