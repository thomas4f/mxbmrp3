// ============================================================================
// tests/unit/test_analytics_spotter.cpp
// The spotter analytics label, and the drift that would rot it.
//
// Same two jobs as test_analytics_theme.cpp: pin the classifier's rules (each
// exists because the obvious one-liner gets one of them wrong), and walk the
// shipped directory so adding mxbmrp3_data/spotters/<new>/ without listing it
// fails the build rather than filing that pack's users under "custom" forever.
//
// "none" is reserved for the spotter being OFF, which is what makes this a
// single property instead of a flag plus a name.
//
// THE VALUE IS THE PACK, NOT THE BACKEND: a text-only pack spoken by the OS
// voice and a recorded one are both just their pack's name here. There was a
// "tts" value for the empty-pack case; it was unreachable (reloadCuePack fills
// an empty name in before analytics samples it) and it asked a question this
// property is not for. Its cases below became `default`'s.
// ============================================================================
#include "doctest.h"

#include "core/analytics_spotter.h"

#include <filesystem>
#include <string>

#ifndef SPOTTERS_DIR
#error "SPOTTERS_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

TEST_CASE("analytics spotter label: none / shipped / custom / missing") {
    // Off is a VALUE, not a missing reading — and it outranks everything: a
    // pack the player configured but disabled is not something they hear.
    CHECK(AnalyticsSpotter::label(false, "", true) == "none");
    CHECK(AnalyticsSpotter::label(false, "am_michael", true) == "none");
    CHECK(AnalyticsSpotter::label(false, "whatever", false) == "none");

    // Shipped packs report themselves — they are ours, and which voice players
    // pick is the whole point of the property. `default` is one of them: it
    // ships text only and is spoken by the OS voice, and that is not a separate
    // category, it is what choosing `default` sounds like.
    CHECK(AnalyticsSpotter::label(true, "default", true) == "default");
    CHECK(AnalyticsSpotter::label(true, "am_michael", true) == "am_michael");
    CHECK(AnalyticsSpotter::label(true, "bm_george", true) == "bm_george");
    CHECK(AnalyticsSpotter::label(true, "af_heart", true) == "af_heart");

    // Anything else installed is third-party or hand-recorded: counted, never
    // named — a pack folder is a string off someone's disk.
    CHECK(AnalyticsSpotter::label(true, "my-voice", true) == "custom");
    // A case variant is a DIFFERENT folder (name matching is exact).
    CHECK(AnalyticsSpotter::label(true, "AM_Michael", true) == "custom");

    // Named but not installed: the setting outlives the folder (the
    // stored-by-name invariant), so the base pack speaks while their INI claims
    // another. "missing" is a diagnosis rather than a voice — reporting the
    // name would claim adoption that is not there, and "custom" would invent
    // it. It is the one value here that is not somebody's choice.
    CHECK(AnalyticsSpotter::label(true, "am_michael", false) == "missing");
    CHECK(AnalyticsSpotter::label(true, "deleted-pack", false) == "missing");

    // The vocabulary is closed, so a dashboard can enumerate it.
    for (const char* name : { "", "default", "am_michael", "bm_george", "whatever" }) {
        for (bool installed : { true, false }) {
            for (bool enabled : { true, false }) {
                const std::string v =
                    AnalyticsSpotter::label(enabled, name, installed);
                CHECK((v == "none" || v == "custom"
                       || v == "missing" || AnalyticsSpotter::isPublished(v)));
            }
        }
    }
}

// One-directional on purpose. kPublishedPacks names every pack WE publish, and
// most of those are a separate download rather than files in this tree — so
// "every name has a folder" is not true and must not be asserted. What must
// hold is the direction that costs data quality: a pack we BUNDLE and forgot
// to list would report as "custom" forever, silently.
TEST_CASE("every bundled spotter pack folder is in kPublishedPacks") {
    namespace fs = std::filesystem;
    const fs::path dir(SPOTTERS_DIR);
    REQUIRE_MESSAGE(fs::is_directory(dir), "shipped packs dir missing: " << SPOTTERS_DIR);

    int seen = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        ++seen;
        CHECK_MESSAGE(AnalyticsSpotter::isPublished(name),
                      "shipped pack '" << name << "' is missing from "
                      "kPublishedPacks — its users would report as 'custom'");
    }
    // A directory that walks to nothing would pass the loop vacuously.
    CHECK(seen > 0);
}
