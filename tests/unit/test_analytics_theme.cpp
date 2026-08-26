// ============================================================================
// tests/unit/test_analytics_theme.cpp
// The panel-theme analytics label, and the one thing about it that can rot.
//
// THE CLASSIFIER is four rules over two inputs, and each rule exists because
// the obvious one-liner (send the theme name) gets one of them wrong: an empty
// name is a CHOICE ("none") rather than missing data; a name we didn't ship is
// somebody's folder name and must not leave the machine; and a name whose
// folder is gone renders unthemed no matter what the INI says, so counting it
// as a custom theme would report adoption that isn't happening.
//
// THE DRIFT GUARD is the reason this file is worth its length. kShippedThemes
// is a hardcoded list, and the failure mode of a hardcoded list of shipped
// assets is silent: add mxbmrp3_data/themes/<new>/ without touching it and
// every user of that theme is filed under "custom" forever, in a dataset with
// no way to notice. So the second case walks the shipped directory itself and
// requires each theme found there to be in the list — the omission fails the
// build the moment the folder lands, not a release later.
// ============================================================================
#include "doctest.h"

#include "core/analytics_theme.h"

#include <filesystem>
#include <string>
#include <vector>

#ifndef THEMES_DIR
#error "THEMES_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

TEST_CASE("analytics theme label: none / shipped / custom / missing") {
    // No theme selected. "none" rather than "" — running unthemed is a reading,
    // and an empty string in the payload reads as a failure to collect one.
    CHECK(AnalyticsTheme::label("", true) == "none");
    // The installed flag cannot make an unselected theme into anything else:
    // nothing is selected, so nothing can be missing.
    CHECK(AnalyticsTheme::label("", false) == "none");

    // Shipped themes report themselves — they are ours, so the name is safe and
    // the per-theme split is the whole point of the property.
    CHECK(AnalyticsTheme::label("carbon-dark", true) == "carbon-dark");
    CHECK(AnalyticsTheme::label("carbon-light", true) == "carbon-light");
    // "debug" is NOT on the list: its slices are not built, so a player running
    // one cut it themselves from the master in assets/themes/ - which makes it
    // theirs, and "custom" the honest answer.
    CHECK(AnalyticsTheme::label("debug", true) == "custom");

    // Anything else installed is a third-party or hand-rolled theme: counted,
    // never named.
    CHECK(AnalyticsTheme::label("my-cool-theme", true) == "custom");
    CHECK(AnalyticsTheme::label("thomas-personal", true) == "custom");
    // Including a case variant of a shipped name: AssetManager matches names
    // exactly, so this is a DIFFERENT folder that happens to look familiar.
    CHECK(AnalyticsTheme::label("Carbon-Dark", true) == "custom");

    // Named but not installed: the setting survives the folder (an unknown name
    // degrades without rewriting the value), so the player is looking at flat
    // panels. Neither "custom" nor "none" — both would lose that.
    CHECK(AnalyticsTheme::label("carbon-dark", false) == "missing");
    CHECK(AnalyticsTheme::label("deleted-theme", false) == "missing");

    // The label set is closed: these four values are the entire vocabulary, so a
    // dashboard can enumerate them.
    for (const char* name : { "", "carbon-dark", "debug", "whatever" }) {
        for (bool installed : { true, false }) {
            const std::string v = AnalyticsTheme::label(name, installed);
            CHECK((v == "none" || v == "custom" || v == "missing"
                   || AnalyticsTheme::isShipped(v)));
        }
    }
}

TEST_CASE("every shipped theme folder is in kShippedThemes") {
    namespace fs = std::filesystem;
    const fs::path dir(THEMES_DIR);
    REQUIRE_MESSAGE(fs::is_directory(dir), "shipped themes dir missing: " << THEMES_DIR);

    int seen = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        ++seen;
        // The message is the fix, not just the failure: whoever added the folder
        // is the one who has to read this.
        CHECK_MESSAGE(AnalyticsTheme::isShipped(name),
                      "shipped theme '" << name << "' is missing from "
                      "AnalyticsTheme::kShippedThemes (mxbmrp3/core/analytics_theme.h) "
                      "— without it, everyone using it reports as \"custom\"");
    }
    // A themes dir that scanned clean because it was EMPTY would pass the loop
    // above without checking anything.
    CHECK(seen > 0);

    // ...and the converse: a name in the list with no folder means a theme was
    // removed or renamed and the list kept a ghost, which quietly reserves a
    // label nothing can ever report.
    for (const char* shipped : AnalyticsTheme::kShippedThemes) {
        // std::string, not the bare const char*: doctest's message builder
        // converts a pointer to bool, so the raw literal reports the theme name
        // as "1" and the message names nothing. Found by deleting a theme folder
        // to check this case can actually fail.
        const std::string name(shipped);
        CHECK_MESSAGE(fs::is_directory(dir / name),
                      "kShippedThemes names '" << name << "' but "
                      << std::string(THEMES_DIR) << "/" << name << " does not exist");
    }
}
