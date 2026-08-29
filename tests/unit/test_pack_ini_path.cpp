// ============================================================================
// tests/unit/test_pack_ini_path.cpp
// The pack-ini resolution rule (core/pack_ini_path.h), with the filesystem
// replaced by a set of paths.
//
// WHY THIS EXISTS. The rule is three lines and each branch is a shipped bug if
// it flips. Canonical-wins is what makes an upgrade take effect at all (the
// user-folder sync copies the new ini in beside the old one and never deletes,
// so both are present on every upgraded install); legacy-still-read is what
// keeps packs published before 1.29.2 alive, and there is no upgrade step that
// can reach a pack downloaded from a forum post; and resolving to the CANONICAL
// name when neither exists is what makes a "cannot read" warning name the file
// an author should create rather than the one they should not.
//
// The predicate is injected, so none of this touches a disk. That the SHIPPED
// packs are actually on the canonical name is a separate question, censused
// against the real data directory by test_pack_types.cpp.
// ============================================================================
#include <set>
#include <string>

#include "doctest.h"

#include "core/pack_ini_path.h"

namespace {

// Stands in for the filesystem: the set of paths that "exist".
struct FakeFs {
    std::set<std::string> present;
    bool operator()(const std::string& path) const {
        return present.count(path) != 0;
    }
};

const std::string kDir = "themes\\carbon-dark\\";

}  // namespace

TEST_CASE("resolve: the canonical <type>.ini is read when it is the only one") {
    FakeFs fs{{kDir + "theme.ini"}};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs);
    CHECK(r.path == kDir + "theme.ini");
    CHECK_FALSE(r.legacy);
    CHECK_FALSE(r.shadowed);
}

TEST_CASE("resolve: a pre-1.29.2 pack keeps working through <name>.ini") {
    FakeFs fs{{kDir + "carbon-dark.ini"}};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs);
    CHECK(r.path == kDir + "carbon-dark.ini");
    CHECK(r.legacy);
    CHECK_FALSE(r.shadowed);
}

TEST_CASE("resolve: canonical wins over a stale <name>.ini, and says so") {
    // The shape of every upgraded install: the sync copied theme.ini in and left
    // the old file behind. Reading the stale one would make the upgrade invisible;
    // reading the new one WITHOUT the flag makes the user's edits to the stale one
    // invisible instead. Both halves matter.
    FakeFs fs{{kDir + "theme.ini", kDir + "carbon-dark.ini"}};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs);
    CHECK(r.path == kDir + "theme.ini");
    CHECK_FALSE(r.legacy);
    CHECK(r.shadowed);
}

TEST_CASE("resolve: nothing there resolves to the canonical name") {
    FakeFs fs{};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs);
    CHECK(r.path == kDir + "theme.ini");
    CHECK_FALSE(r.legacy);
    CHECK_FALSE(r.shadowed);
}

TEST_CASE("resolve: a pack named after its own type is not self-shadowing") {
    // spotters\spotter\spotter.ini is one file that both rules name. Flagging it
    // would print a warning telling the author to delete the file being read.
    const std::string dir = "spotters\\spotter\\";
    FakeFs fs{{dir + "spotter.ini"}};
    const PackIni::Resolved r = PackIni::resolve(dir, "spotter", PackIni::kSpotter, fs);
    CHECK(r.path == dir + "spotter.ini");
    CHECK_FALSE(r.shadowed);
    CHECK_FALSE(r.legacy);
}

// ============================================================================
// WHO the shadow warning is for. `shadowed` used to mean "both files exist",
// which is true for all 33 shipped packs on every upgrade -- Setup writes the
// canonical ini and nothing removes the old one -- so the warning told people to
// delete files they never created, twice per launch per pack. Setup pruned them
// instead, from an NSIS macro that had to re-derive the rules above and got the
// same-name case wrong, deleting the canonical ini of a pack named after its own
// type. Passing the user's own copy of the pack decides it here instead: their
// tree is the only place a PERSON puts files.
// ============================================================================

TEST_CASE("resolve: a shadow in the plugin folder alone is a leftover, not a warning") {
    // The upgrade shape, and the common one: canonical + stale side by side in the
    // game's plugins tree, with nothing of this pack in the user's own folder.
    // Setup wrote both; the user has never touched it.
    FakeFs fs{{kDir + "theme.ini", kDir + "carbon-dark.ini"}};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs,
                                                 "Documents\\mxbmrp3\\themes\\carbon-dark\\");
    CHECK(r.path == kDir + "theme.ini");
    CHECK_FALSE(r.shadowed);
}

TEST_CASE("resolve: a shadow the user also has a copy of IS the warning's case") {
    // Same two files in the plugins tree, but this time the stale one is in the
    // user's folder too -- so it is theirs, they may well be editing it, and it is
    // going nowhere. This is the only case worth a line in the log.
    const std::string userDir = "Documents\\mxbmrp3\\themes\\carbon-dark\\";
    FakeFs fs{{kDir + "theme.ini", kDir + "carbon-dark.ini",
               userDir + "carbon-dark.ini"}};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs, userDir);
    CHECK(r.path == kDir + "theme.ini");
    CHECK(r.shadowed);
}

TEST_CASE("resolve: not knowing where the user's files are takes the noisy side") {
    // An empty userDir means "cannot tell" -- before discovery has run, or a
    // caller building paths some other way. A real shadow going unmentioned is the
    // bug the flag exists for, so the unknown case reports rather than hides.
    FakeFs fs{{kDir + "theme.ini", kDir + "carbon-dark.ini"}};
    const PackIni::Resolved r = PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs, "");
    CHECK(r.shadowed);
}

TEST_CASE("resolve: userDir never changes WHICH file is read") {
    // The targeting is about the warning only. A pack with just the legacy ini
    // still reads it, whoever owns it -- packs published before the rename are
    // forum downloads no upgrade step can reach.
    for (const char* userDir : {"", "Documents\\mxbmrp3\\themes\\carbon-dark\\"}) {
        FakeFs fs{{kDir + "carbon-dark.ini"}};
        const PackIni::Resolved r =
            PackIni::resolve(kDir, "carbon-dark", PackIni::kTheme, fs, userDir);
        CHECK(r.path == kDir + "carbon-dark.ini");
        CHECK(r.legacy);
        CHECK_FALSE(r.shadowed);
    }
}

TEST_CASE("resolve: each pack type has its own stem") {
    // They are distinct strings, so a copy-paste that reuses the wrong constant
    // fails here rather than at a user's install. Written out rather than
    // compared pairwise in a loop: the VALUE of each stem is also the filename
    // shipped in 30-odd pack folders and named in the modding guide, so a change
    // to one is a change to those, and this is where it should be noticed.
    CHECK(std::string(PackIni::kTheme) == "theme");
    CHECK(std::string(PackIni::kGamepad) == "gamepad");
    CHECK(std::string(PackIni::kPitboard) == "pitboard");
    CHECK(std::string(PackIni::kSpotter) == "spotter");
    CHECK(std::string(PackIni::kGauges) == "gauge");
}
