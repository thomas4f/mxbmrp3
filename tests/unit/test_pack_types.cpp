// ============================================================================
// tests/unit/test_pack_types.cpp
// Censuses AssetManager::PACK_TYPES against the SHIPPED data directory.
//
// WHY THIS EXISTS. A pack type has to be listed there to be copied out of the
// user's Documents folder — at startup and again on RELOAD_CONFIG. Both copies
// walk that one table now, but nothing stops a FIFTH pack type shipping without
// a row, and the failure is silent in the worst way: everything works for the
// developer (whose packs are already in the plugins tree) and the type is simply
// unauthorable for everyone else, in a folder an uninstall deletes. That is not
// hypothetical — it is exactly what shipped for spotter voices, and it is what
// this file would have caught.
//
// So the census runs BOTH ways: every row must name a real shipped folder, and
// every shipped folder that looks like a pack root must have a row. "Looks like
// a pack root" is the format itself — a directory whose children are folders
// containing that type's <type>.ini — so a new type is recognised without being
// taught here.
//
// The media pattern is checked against the real packs too. It is the payload a
// pack may carry across the copy, so a wrong one is not cosmetic: it either
// carries nothing (a voice pack with no audio) or widens a trust boundary.
// ============================================================================
#include <cmath>
#include <fstream>
#include <sstream>
#include "doctest.h"

#include "core/asset_manager.h"
#include "core/pack_ini_path.h"

#include <filesystem>
#include <string>
#include <vector>

#ifndef MXB_DATA_DIR
#error "MXB_DATA_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

// "*.tga" -> ".tga". The table stores FindFirstFile patterns because that is
// what the Win32 walk consumes; here only the extension is meaningful.
std::string extensionOf(const std::string& pattern) {
    const size_t star = pattern.find('*');
    return star == std::string::npos ? pattern : pattern.substr(star + 1);
}

// A pack folder is <name>\ containing its type's <type>.ini -- theme.ini,
// gamepad.ini, pitboard.ini, spotter.ini. That IS the format, so it is also how a
// pack root is recognised without a second list to maintain.
//
// The pre-1.29.2 <name>.ini counts too, and not only for symmetry with the
// permanent read fallback (core/pack_ini_path.h): the reverse census below walks
// folders whose TYPE it has not established yet -- that is the whole point of it --
// so it cannot ask for one specific stem. Accepting any of the five spellings
// keeps a fifth pack type that ships in the old shape visible to the census that
// exists to catch it.
bool isPackFolder(const fs::path& dir) {
    if (fs::exists(dir / (dir.filename().string() + ".ini"))) return true;
    for (const auto& pt : AssetManager::PACK_TYPES) {
        if (fs::exists(dir / (std::string(pt.label) + ".ini"))) return true;
    }
    return false;
}

std::vector<fs::path> packFoldersIn(const fs::path& root) {
    std::vector<fs::path> out;
    if (!fs::exists(root) || !fs::is_directory(root)) return out;
    for (const auto& e : fs::directory_iterator(root)) {
        if (e.is_directory() && isPackFolder(e.path())) out.push_back(e.path());
    }
    return out;
}

}  // namespace

// The rename is only worth having if it holds. A shipped pack is the thing users
// copy to start their own, so one left on the old name teaches the old shape --
// and the read fallback means it would work perfectly and silently for the dev.
TEST_CASE("PACK_TYPES: every shipped pack uses its type's canonical ini name") {
    for (const auto& pt : AssetManager::PACK_TYPES) {
        const fs::path root = fs::path(MXB_DATA_DIR) / pt.subdir;
        for (const fs::path& pack : packFoldersIn(root)) {
            CAPTURE(pack.string());
            const std::string name = pack.filename().string();
            const std::string stem = pt.label;
            CHECK_MESSAGE(fs::exists(pack / (stem + ".ini")),
                          "shipped pack has no " << stem << ".ini");
            // A pack named after its own type resolves to one file by both rules.
            if (name != stem) {
                CHECK_MESSAGE(!fs::exists(pack / (name + ".ini")),
                              "shipped pack still carries the pre-1.29.2 "
                                  << name << ".ini");
            }
        }
    }
}

TEST_CASE("PACK_TYPES: every row names a real shipped pack root") {
    for (const auto& pt : AssetManager::PACK_TYPES) {
        CAPTURE(pt.subdir);
        const fs::path root = fs::path(MXB_DATA_DIR) / pt.subdir;
        REQUIRE_MESSAGE(fs::exists(root),
                        "PACK_TYPES names a folder that does not ship");
        // A row for an empty folder is not wrong, but it has never been what
        // any of these mean — every type ships at least one example for a user
        // to copy, which is how the format is documented.
        CHECK(!packFoldersIn(root).empty());
    }
}

// Stated as "nothing is left behind" rather than "something is carried": a
// pack with NO payload is legitimate (the shipped `default` spotter voice is
// an ini of phrases, spoken by Windows TTS), but a pack carrying a file the
// sync would silently drop is not — it reaches the user's plugins folder as
// an ini with nothing to play or draw, and nothing says why.
TEST_CASE("PACK_TYPES: no pack carries payload its row's pattern would drop") {
    for (const auto& pt : AssetManager::PACK_TYPES) {
        CAPTURE(pt.subdir);
        const std::string ext = extensionOf(pt.media);
        for (const fs::path& pack : packFoldersIn(fs::path(MXB_DATA_DIR) / pt.subdir)) {
            CAPTURE(pack.filename().string());
            for (const auto& f : fs::directory_iterator(pack)) {
                if (!f.is_regular_file()) continue;   // themes' icons\ subdir
                const std::string fext = f.path().extension().string();
                if (fext == ".ini" || fext == ".md") continue;
                CAPTURE(f.path().filename().string());
                CHECK_MESSAGE(fext == ext,
                              "pack file the user-asset sync would not copy");
            }
        }
    }
}

TEST_CASE("PACK_TYPES: no shipped pack root is missing a row") {
    for (const auto& e : fs::directory_iterator(fs::path(MXB_DATA_DIR))) {
        if (!e.is_directory()) continue;
        if (packFoldersIn(e.path()).empty()) continue;   // flat asset folder
        const std::string name = e.path().filename().string();
        CAPTURE(name);
        bool listed = false;
        for (const auto& pt : AssetManager::PACK_TYPES) {
            if (name == pt.subdir) { listed = true; break; }
        }
        // The whole point: a pack type nobody added to PACK_TYPES is one users
        // cannot author from their own Documents folder.
        CHECK_MESSAGE(listed, "shipped pack type is missing from PACK_TYPES");
    }
}

// ============================================================================
// emphasisBaseOf — which faces are a heavier CUT of another rather than a
// typeface of their own. The rule decides two things at once: what the font
// cycler hides, and what Title/Strong silently upgrade to.
// ============================================================================
TEST_CASE("emphasisBaseOf: Medium and SemiBold pair with their Regular") {
    // IBM Plex is the pair we actually ship. The other three are NOT shipped -
    // they came with themes that were cut before release - and they stay here on
    // purpose: the rule is a SUFFIX rule, not a table of known families, and a
    // case list containing only shipped names could not tell those two apart.
    CHECK(emphasisBaseOf("IBMPlexSans-SemiBold") == "IBMPlexSans-Regular");
    CHECK(emphasisBaseOf("Roboto-Medium") == "Roboto-Regular");
    CHECK(emphasisBaseOf("MonaSans-Medium") == "MonaSans-Regular");
    CHECK(emphasisBaseOf("OpenSans-SemiBold") == "OpenSans-Regular");
}

// BOLD IS DELIBERATELY NOT A COMPANION. RobotoMono-Bold has shipped as a
// user-selectable face since long before this rule; folding it in would take it
// out of the cycler, and anyone who had picked it would find their choice
// unreachable without hand-editing the ini. The rule covers only the suffixes
// added to BE companions.
TEST_CASE("emphasisBaseOf: Bold stays a face in its own right") {
    CHECK(emphasisBaseOf("RobotoMono-Bold").empty());
}

TEST_CASE("emphasisBaseOf: a plain face is not a companion") {
    CHECK(emphasisBaseOf("IBMPlexSans-Regular").empty());
    CHECK(emphasisBaseOf("Tiny5-Regular").empty());
    CHECK(emphasisBaseOf("").empty());
    // The suffix must be a suffix, not the whole name.
    CHECK(emphasisBaseOf("-Medium").empty());
}

// ============================================================================
// Theme box terms: a border must be whole cells, EXCEPT on [button].
//
// The parser enforces this by rejecting the row and KEEPING THE PREVIOUS VALUE
// (AssetManager::readThemeIniPairs), which is silent from the outside — a theme
// that writes `border = 1.5` under [content] renders with the built-in default
// and looks merely wrong rather than broken. Nothing else reads these files, so
// this walks them.
//
// The button exemption exists because the whole-cells rule protects a box's
// CONTENT ROWS staying on the grid lattice, and a button has none: it is one
// centred label in a box of its own. Without it a bordered button could not be
// shorter than 4 cells, which is above what Fluent, Primer and HIG specify --
// design systems the rule was calibrated against, not themes we ship.
// ============================================================================
TEST_CASE("shipped themes: only [button] uses a fractional border") {
    int borders = 0, fractional = 0, themes = 0;
    for (const fs::path& theme : packFoldersIn(fs::path(MXB_DATA_DIR) / "themes")) {
        // Resolved the way the plugin resolves it, so this walk keeps seeing a
        // theme whichever of the two names it carries.
        const fs::path ini(PackIni::resolve(theme.string() + "/",
                                            theme.filename().string(),
                                            PackIni::kTheme).path);
        if (!fs::exists(ini)) continue;
        ++themes;
        std::ifstream in(ini);
        std::string line, section;
        while (std::getline(in, line)) {
            const size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos || line[b] == ';') continue;
            if (line[b] == '[') {
                const size_t e = line.find(']', b);
                section = (e == std::string::npos) ? "" : line.substr(b + 1, e - b - 1);
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(b, eq - b);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            if (key != "border") continue;
            ++borders;
            std::istringstream vals(line.substr(eq + 1));
            double v = 0.0;
            while (vals >> v) {
                const bool whole = std::fabs(v - std::floor(v + 0.5)) < 0.001;
                CAPTURE(theme.filename().string());
                CAPTURE(section);
                CAPTURE(v);
                if (!whole) ++fractional;
                const bool allowed = whole || section == "button";
                CHECK_MESSAGE(allowed,
                              "a fractional border outside [button] is silently "
                              "dropped by the parser and the box falls back to "
                              "its built-in size");
                CHECK(v >= 0.0);
                CHECK(v <= 12.0);
            }
        }
    }
    // A walk that found no borders would pass every check vacuously. Expressed
    // PER THEME rather than as a total: the floor here was `>= 10`, calibrated
    // when eleven themes shipped, and it went red the moment the set was cut to
    // two - failing for a reason that had nothing to do with what it guards. How
    // many themes ship is a product decision that will move again; that each one
    // states borders is the invariant.
    CHECK(themes >= 1);
    CHECK(borders >= themes);
    // NO SHIPPED-USAGE FLOOR on the fractional exemption, and the count is kept
    // only so a reader can see it. There was a `fractional >= 3` here, on the
    // reasoning that an unexercised exemption means the themes needing it have
    // silently regressed — true while three themes set a hairline [button]
    // border, and false the moment the shipped set took ONE geometry, whose
    // button border is a whole cell. The exemption is still real (the parser
    // accepts it, and check_docs.py pins the kBoxKeys flag that carries it); it
    // is simply not a thing our themes do any more, and asserting that they must
    // would be this test dictating the look rather than checking the parser
    // contract it is named for.
    INFO("fractional [button] borders across the shipped themes: " << fractional);
}
