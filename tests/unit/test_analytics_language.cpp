// ============================================================================
// tests/unit/test_analytics_language.cpp
// The language analytics label, and the list it checks against.
//
// THE CLASSIFIER reports the language ON SCREEN: English when no pack is in
// effect, a shipped pack by its folder, anything else as "custom" so a user's
// folder name never leaves the machine.
//
// THE DRIFT GUARD walks the shipped languages directory both ways: a new pack
// folder missing from kShippedPacks would file every reader of it under
// "custom" with no way to notice, and a listed pack whose folder is gone keeps
// a label nothing can report.
// ============================================================================
#include "doctest.h"

#include "core/analytics_language.h"

#include <filesystem>
#include <string>

#ifndef LANGUAGES_DIR
#error "LANGUAGES_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

TEST_CASE("analytics language label: en / shipped / custom") {
    // No pack in effect (English chosen, System with no matching pack, or a
    // stored pack that is not installed): the player reads English.
    CHECK(AnalyticsLanguage::label("") == "en");

    // Shipped packs report their folder.
    CHECK(AnalyticsLanguage::label("fr") == "fr");
    CHECK(AnalyticsLanguage::label("sv") == "sv");
    CHECK(AnalyticsLanguage::label("pseudo") == "pseudo");

    // Anything else is a user's folder: never sent verbatim. Exact match, so a
    // case variant of a shipped folder is a different folder.
    CHECK(AnalyticsLanguage::label("my-turkish") == "custom");
    CHECK(AnalyticsLanguage::label("FR") == "custom");
    CHECK(AnalyticsLanguage::label("pt-PT") == "custom");
}

TEST_CASE("analytics language list covers every shipped pack folder") {
    namespace fs = std::filesystem;
    const fs::path root(LANGUAGES_DIR);
    REQUIRE(fs::is_directory(root));
    int packs = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(root)) {
        if (!e.is_directory()) continue;
        const std::string folder = e.path().filename().string();
        if (folder.empty() || folder[0] == '.') continue;
        ++packs;
        INFO("shipped language pack not in AnalyticsLanguage::kShippedPacks: " << folder);
        CHECK(AnalyticsLanguage::isShipped(folder));
    }
    // A wrong path would pass the loop vacuously.
    CHECK(packs >= 5);

    // ...and the converse: a listed pack with no folder is a ghost left by a
    // removed or renamed pack. std::string, not the raw pointer: doctest's
    // message builder prints a const char* as "1".
    for (const char* listed : AnalyticsLanguage::kShippedPacks) {
        const std::string folder(listed);
        CHECK_MESSAGE(fs::is_directory(root / folder),
                      "kShippedPacks names '" << folder << "' but "
                      << std::string(LANGUAGES_DIR) << "/" << folder << " does not exist");
    }
}
