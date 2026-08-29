// ============================================================================
// tests/unit/test_install_prefs.cpp
// The installer's analytics opt-out marker (core/install_prefs.h).
//
// WHY THIS EXISTS. Every branch here is a privacy outcome, and two of them are
// the kind of bug nobody notices because it fails QUIETLY in the direction of
// sending more data:
//
//   - honouring analyticsOptOut=0 would let an UPGRADE switch analytics back on
//     for someone who turned it off in-game. Setup cannot read the per-game
//     settings file, so it cannot know that setting and must never overrule it.
//   - re-applying a marker whose stamp was already honoured would pin analytics
//     off forever and silently beat the in-game toggle -- the failure the stamp
//     exists to prevent (see the header for why the file cannot just be
//     deleted on consume).
// ============================================================================
#include <string>

#include "doctest.h"

#include "core/install_prefs.h"

TEST_CASE("install_prefs: a real Setup-written file opts out") {
    const std::string text =
        "; MXBMRP3 install preferences - written by Setup\n"
        "[Install]\n"
        "analyticsOptOut=1\n"
        "stamp=1.29.2\n";
    const InstallPrefs::Prefs p = InstallPrefs::parse(text);
    CHECK(p.optOut);
    CHECK(p.stamp == "1.29.2");
    CHECK(InstallPrefs::shouldApply(p, ""));            // never honoured before
    CHECK_FALSE(InstallPrefs::shouldApply(p, "1.29.2")); // already honoured
    CHECK(InstallPrefs::shouldApply(p, "1.29.1"));       // a newer Setup ran
}

TEST_CASE("install_prefs: the marker can never switch analytics back ON") {
    // The file expresses opt-out or nothing. analyticsOptOut=0 is not an
    // instruction to enable -- an upgrade must not overrule an in-game opt-out.
    const InstallPrefs::Prefs p =
        InstallPrefs::parse("[Install]\nanalyticsOptOut=0\nstamp=1.29.2\n");
    CHECK_FALSE(p.optOut);
    CHECK_FALSE(InstallPrefs::shouldApply(p, ""));
    CHECK_FALSE(InstallPrefs::shouldApply(p, "1.29.1"));
}

TEST_CASE("install_prefs: absent or empty file changes nothing") {
    CHECK_FALSE(InstallPrefs::shouldApply(InstallPrefs::parse(""), ""));
    CHECK_FALSE(InstallPrefs::shouldApply(InstallPrefs::parse("\n\n"), ""));
    CHECK_FALSE(InstallPrefs::shouldApply(InstallPrefs::parse("[Install]\n"), ""));
}

TEST_CASE("install_prefs: hand-edited junk is skipped, not fatal") {
    // This file is plain text next to the plugin, so somebody will edit it. A
    // throw here would abort the whole settings load (the naked-std::stoul
    // class of bug the project's INI rule exists for).
    const InstallPrefs::Prefs p = InstallPrefs::parse(
        "garbage line with no equals\n"
        "  analyticsOptOut = 1   ; trailing comment\n"
        "unknownKey=whatever\n"
        "=novalue\n"
        "stamp = 1.30.0 \n");
    CHECK(p.optOut);
    CHECK(p.stamp == "1.30.0");
}

TEST_CASE("install_prefs: a file with no trailing newline still parses") {
    const InstallPrefs::Prefs p =
        InstallPrefs::parse("analyticsOptOut=1\nstamp=1.29.2");
    CHECK(p.optOut);
    CHECK(p.stamp == "1.29.2");
}

TEST_CASE("install_prefs: an unstamped opt-out is honoured exactly once") {
    // Only reachable by hand-editing. Honour it, but record a sentinel so it
    // does not re-apply on every launch and override the in-game toggle.
    const InstallPrefs::Prefs p = InstallPrefs::parse("analyticsOptOut=1\n");
    CHECK(p.stamp.empty());
    CHECK(InstallPrefs::shouldApply(p, ""));
    const std::string recorded = InstallPrefs::stampToRecord(p);
    CHECK(recorded == "unstamped");
    CHECK_FALSE(InstallPrefs::shouldApply(p, recorded));
}

TEST_CASE("install_prefs: recorded stamp is never empty") {
    // "" has to keep meaning "nothing has ever been applied".
    CHECK_FALSE(InstallPrefs::stampToRecord(InstallPrefs::parse(
        "analyticsOptOut=1\nstamp=1.29.2\n")).empty());
    CHECK_FALSE(InstallPrefs::stampToRecord(
        InstallPrefs::parse("analyticsOptOut=1\n")).empty());
}
