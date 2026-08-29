// ============================================================================
// tests/unit/pack_by_name_test.cpp
// Every pack type stores its selection the same way, and every one of those
// keys is exempt from inline-comment stripping.
//
// THE BUG THIS PINS, found by auditing the five pack types against each other
// rather than by anything failing. `;` is legal in a Windows directory name and
// the settings loader's comment strip was unconditional, so a theme in
// `retro;90s` loaded as `retro`, degraded to unthemed, and the next save wrote
// the TRUNCATED name back -- destroying the choice permanently, which is the one
// thing by-name storage exists to prevent. Settings::isFolderNameValue() is the
// exemption, and it names its keys through the key SYMBOLS so a rename cannot
// leave it pointing at a key that no longer exists.
//
// Symbols protect a rename. They do nothing about an ADDITION, which is how the
// gauges pack shipped with `gaugesPack` missing from that list: four pack types
// were exempt and the fifth silently was not. Nothing failed, because every
// shipped and plausible folder name is semicolon-free -- the bug only appears in
// somebody's own folder, months later, and takes their choice with it.
//
// So this walks the pack types as DATA. A sixth type fails here on the day its
// key is added, which is the only moment anybody is thinking about this.
// ============================================================================
#include "doctest.h"

#include "core/settings_keys.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("every pack type's selection key is exempt from comment stripping") {
    // One row per pack type, named the way a person would say it, so a failure
    // reads as "the gauges pack is not exempt" rather than as a string literal.
    struct PackKey { const char* what; const char* key; };
    const std::vector<PackKey> kPackKeys = {
        { "panel theme",  Settings::Keys::Global::PANEL_THEME },
        { "spotter pack", Settings::Keys::Global::SPOTTER_PACK },
        { "gamepad pad",  Settings::Keys::Gamepad::PACK },
        { "pit board",    Settings::Keys::Pitboard::PACK },
        { "gauges set",   Settings::IniOnly::GAUGES_PACK },
    };

    for (const PackKey& p : kPackKeys) {
        CHECK_MESSAGE(Settings::isFolderNameValue(p.key),
                      "the " << p.what << " selection ('" << p.key << "') is stored as a FOLDER "
                      "NAME, so a ';' in it is data; without the exemption the name is "
                      "truncated on load and the truncation is written back on save");
    }
}

TEST_CASE("the exemption is not a blanket one") {
    // It buys correctness for folder names at the cost of the inline comments
    // those lines can no longer carry, so it has to stay a short list. tts_voice
    // is the documented counter-example: its line IS written with a comment, so
    // it must keep stripping, and a Windows voice name containing a semicolon
    // truncating is the accepted trade.
    CHECK_FALSE(Settings::isFolderNameValue("tts_voice"));
    CHECK_FALSE(Settings::isFolderNameValue("visible"));
    CHECK_FALSE(Settings::isFolderNameValue(""));
}

TEST_CASE("every shipped pack opens with the same [pack] section") {
    // THE "LEARN ONE, KNOW THE REST" RULE, and the one place it used to fail.
    // Each type spelled its identity section differently -- [pad], [board],
    // [gauges], [theme], [Pack] -- singular, plural, and one capitalised, so a
    // modder who had written two packs still had to open a shipped example to
    // write the third. On line one of the file.
    //
    // Walks the shipped tree rather than a list written here: a sixth pack type
    // that invents its own section fails on the day its first pack ships.
    namespace fs = std::filesystem;
    int checked = 0;
    for (const char* root : { THEMES_DIR, GAMEPADS_DIR, PITBOARDS_DIR, GAUGES_DIR, SPOTTERS_DIR }) {
        if (!fs::exists(root)) continue;
        for (const fs::directory_entry& pack : fs::directory_iterator(root)) {
            if (!pack.is_directory()) continue;
            for (const fs::directory_entry& f : fs::directory_iterator(pack.path())) {
                if (f.path().extension() != ".ini") continue;
                std::ifstream in(f.path());
                REQUIRE(in.is_open());
                std::string line;
                while (std::getline(in, line)) {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    // Section headers only, and only real ones -- the shipped inis
                    // carry commented-out examples too, which check_docs.py owns.
                    if (line.size() < 3 || line.front() != '[' || line.back() != ']') continue;
                    const std::string section = line.substr(1, line.size() - 2);
                    // Computed first: doctest expands the expression it is given,
                    // and a chain of && is more than it can decompose.
                    const bool perTypeIdentity =
                        section == "pad" || section == "board" || section == "gauges" ||
                        section == "theme" || section == "Pack";
                    CHECK_MESSAGE(!perTypeIdentity,
                                  f.path().string() << " uses the old per-type identity section ["
                                  << section << "]; every pack type opens with [pack]");
                    ++checked;
                }
            }
        }
    }
    CHECK_MESSAGE(checked > 0, "no shipped pack inis were scanned - the roots are wrong");
}

TEST_CASE("the two gauges use ONE selection key, not one each") {
    // The tacho and the speedo each store their own value, but under the same
    // key name in their own sections -- the same string, so the two rows above
    // cannot disagree about what a gauges selection is called.
    CHECK(std::string(Settings::IniOnly::Speedo::PACK.key) ==
          std::string(Settings::IniOnly::Tacho::PACK.key));
    CHECK(std::string(Settings::IniOnly::Speedo::PACK.key) ==
          std::string(Settings::IniOnly::GAUGES_PACK));
}
