// ============================================================================
// tests/unit/palette_test.cpp
// The Appearance palette, and its contract with the shipped asset packs.
//
// WHAT THE PACK COLOURS PROMISE. Nine pit board and gamepad skins ship in brand
// hues, and the palette carries eight of those hues under the SAME NAMES the
// packs are shown by, so a player running the Crimson board can set their
// primary text to the same #de1c21 rather than eyeballing a nearby red. That
// promise is a pair of strings in two unrelated files -- a label in
// getColorName's switch and a `name` in a pack's ini -- and nothing about
// renaming one makes the other fail to build. Hence the census below, which is
// the only thing standing between "exact match" and "two things that used to
// agree".
//
// Eight of the nine were ADDED; the ninth, Graphite, was already in the palette
// at #646464 under the name "Dark Gray" -- pickable, but not findable by anyone
// trying to match a board, which is the same failure as not having it. So it was
// renamed rather than added: a second constant with that value would be a
// duplicate `case` label in getColorName, a build error reached only after
// somebody had spent the time. The census below is what turned that up.
//
// The rest of the cases guard the palette's own invariants, none of which had
// any coverage before: every entry nameable, every entry unique, every entry
// findable by getColorIndex. Uniqueness is the load-bearing one -- getColorName
// is a switch on the VALUE, so two entries sharing a value cannot both be
// labelled, and getColorIndex would silently answer with whichever comes first.
// ============================================================================
#include "doctest.h"

#include "core/color_config.h"
#include "core/plugin_constants.h"

#include <fstream>
#include <set>
#include <string>
#include <vector>

#ifndef PITBOARDS_DIR
#error "PITBOARDS_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

namespace {

// The `name` a pack ini gives itself, or empty. Deliberately a tiny reader
// rather than the production ini walk: this case is about what a HUMAN reading
// the two files would see, so it should not share a parser with either side.
std::string packDisplayName(const std::string& dir) {
    std::ifstream f(dir + "/pitboard.ini");
    if (!f.is_open()) return std::string();
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            const size_t b = s.find_first_not_of(ws);
            if (b == std::string::npos) { s.clear(); return; }
            s = s.substr(b, s.find_last_not_of(ws) - b + 1);
        };
        trim(key);
        trim(value);
        if (key == "name") return value;
    }
    return std::string();
}

// The palette's labels, as a set, so a case can ask "is this name offered".
std::set<std::string> paletteNames() {
    std::set<std::string> names;
    for (unsigned long c : ColorPalette::ALL_COLORS) names.insert(ColorPalette::getColorName(c));
    return names;
}

}  // namespace

TEST_CASE("palette: every entry is nameable, unique and findable") {
    std::set<unsigned long> seen;
    for (size_t i = 0; i < ColorPalette::ALL_COLORS.size(); ++i) {
        const unsigned long c = ColorPalette::ALL_COLORS[i];

        // "Custom" is getColorName's fallback for a colour it does not know, so a
        // palette entry answering it means the switch and the array disagree.
        CHECK_MESSAGE(std::string(ColorPalette::getColorName(c)) != "Custom",
                      "ALL_COLORS[" << i << "] has no label in getColorName");

        CHECK_MESSAGE(seen.insert(c).second,
                      "ALL_COLORS[" << i << "] repeats an earlier value; getColorName is a "
                      "switch on the value, so only one of them could ever be labelled");

        CHECK(ColorPalette::getColorIndex(c) == static_cast<int>(i));
    }
    // An off-palette colour must report as absent, not as index 0 -- cycleColor
    // reads that answer to decide whether it can step from here.
    CHECK(ColorPalette::getColorIndex(0xFF123456) == -1);
    CHECK(std::string(ColorPalette::getColorName(0xFF123456)) == "Custom");
}

TEST_CASE("palette: every shipped board skin's name is a colour a player can pick") {
    // THE CONTRACT. Walk the shipped boards rather than a list written here: a
    // tenth skin added without a matching palette entry is exactly the regression
    // this case exists to catch, and a hardcoded list would not see it.
    static const char* kSkins[] = {
        "crimson", "cyan", "graphite", "lime", "navy", "orange", "royal", "silver", "yellow",
    };
    const std::set<std::string> names = paletteNames();

    for (const char* skin : kSkins) {
        const std::string display = packDisplayName(std::string(PITBOARDS_DIR) + "/" + skin);
        REQUIRE_MESSAGE(!display.empty(), "shipped board '" << skin << "' has no [pack] name");
        CHECK_MESSAGE(names.count(display) == 1,
                      "board skin '" << display << "' has no palette colour of the same name, so a "
                      "player cannot match their text to it");
    }
}

TEST_CASE("palette: the pack colours are the brand hues the skins were tinted from") {
    // The values, not just the names. color_config.cpp static_asserts the same
    // equalities at compile time; these are here so the expectation is READABLE
    // next to the census above, and so a failure names the skin rather than a
    // constant.
    using namespace PluginConstants::BrandColors;
    CHECK(ColorPalette::CRIMSON == HONDA);
    CHECK(ColorPalette::ORANGE  == KTM);
    CHECK(ColorPalette::YELLOW  == SUZUKI);
    CHECK(ColorPalette::LIME    == KAWASAKI);
    CHECK(ColorPalette::CYAN    == TM);
    CHECK(ColorPalette::NAVY    == HUSQVARNA);
    CHECK(ColorPalette::ROYAL   == YAMAHA);
    CHECK(ColorPalette::SILVER  == ALTA);

    CHECK(ColorPalette::GRAPHITE == STARK);
    CHECK(std::string(ColorPalette::getColorName(STARK)) == "Graphite");
}

TEST_CASE("palette: the renamed generics kept their values") {
    // The rename was the whole reason the pack colours could take those names,
    // and it was only safe because a name is display-only. These three pin that
    // nothing moved underneath: WARNING, the yellow-flag icon, the fuel bar and
    // the gear readout all still resolve to the exact colours they always did.
    CHECK(ColorPalette::AMBER == PluginUtils::makeColor(255, 165, 0));          // was ORANGE
    CHECK(ColorPalette::BRIGHT_YELLOW == PluginUtils::makeColor(255, 255, 0));  // was YELLOW
    CHECK(ColorPalette::AQUA == PluginUtils::makeColor(0, 255, 255));           // was CYAN

    // ...and that they did not merely swap labels with the pack colours.
    CHECK(ColorPalette::AMBER != ColorPalette::ORANGE);
    CHECK(ColorPalette::BRIGHT_YELLOW != ColorPalette::YELLOW);
    CHECK(ColorPalette::AQUA != ColorPalette::CYAN);
}
