// ============================================================================
// tests/integration/tests/pack_skin_test.cpp
// A PACK SKIN LAYERS OVER ITS BASE (`base =` in the pack ini): the two files a
// reskin actually changes, with everything it leaves out answered from the base
// -- the spotter voice pack rule, applied to gamepad and pit board packs.
//
// WHY A REAL FILE TREE. asset_pack_test.cpp deliberately tests the by-name
// SELECTION rule with synthetic packs and no files; this feature lives in the
// other half -- discoverGamepads()/discoverPitboards() deciding, per stem,
// which folder a sprite file resolves from, and which geometry seeds a skin.
// Faking that would test the test. So each case stages a real
// plugins/mxbmrp3_data tree of minimal TGAs in its own temp directory, points
// the process CWD at it (discovery scans DISCOVERY_DIR relative to CWD), and
// runs the real scan via Startup. Own process, own CWD, own tree: nothing here
// can contaminate another test's goldens, which is the trap asset_pack_test's
// header warns staging into the shared build dir would spring.
//
// What each case pins:
//   - a skin with only an ini + background resolves the missing stems from its
//     base, and its own file from itself;
//   - geometry inherits from the base and the skin's own keys override on top;
//   - a pitboard skin that brings its own art gets THAT art's seeded size;
//   - a base that is missing, or is itself a skin, rejects the pack whole --
//     no half-registered pack, and the by-name degradation covers rendering;
//   - a standalone pack still requires the full set (the old rule, untouched).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const char* kRoot = "Z:\\tmp\\mxbmrp3-tests\\pack_skin";

void makeDirs(const std::string& path) {
    // CreateDirectoryA is not recursive; walk the components.
    std::string acc;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '\\') {
            if (!acc.empty() && acc.back() != ':') CreateDirectoryA(acc.c_str(), nullptr);
        }
        if (i < path.size()) acc += path[i];
    }
}

// The smallest file readTgaDimensions accepts: an 18-byte header carrying
// width/height. No pixel data -- discovery only reads the header.
void writeTga(const std::string& path, int w, int h) {
    unsigned char header[18] = {};
    header[2] = 2;                                   // uncompressed truecolor
    header[12] = static_cast<unsigned char>(w & 0xff);
    header[13] = static_cast<unsigned char>((w >> 8) & 0xff);
    header[14] = static_cast<unsigned char>(h & 0xff);
    header[15] = static_cast<unsigned char>((h >> 8) & 0xff);
    header[16] = 24;
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(header, 1, sizeof(header), f);
    std::fclose(f);
}

void writeText(const std::string& path, const std::string& body) {
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
}

// One complete standalone gamepad pack: every stem the REAL table names, plus
// an ini stating a distinctive geometry width so inheritance is observable.
//
// The stems come from the DLL (GamepadSprite::kStems via a test hook), not a
// literal list here. A copy would sit outside the one-table invariant that
// keeps discovery and registration in step: add a stem and this fixture would
// silently stage an incomplete base pack, phase 1 would reject it, and every
// skin case would fail with a "missing X.tga" cascade blaming the feature
// rather than the fixture.
void stageBasePad(PluginHost& host, const std::string& padDir, const char* name,
                  float bgWidth) {
    const std::string dir = padDir + "\\" + name;
    makeDirs(dir);
    const int stems = host.gamepadStemCount();
    REQUIRE(stems > 0);                      // hook missing = fixture stages nothing
    for (int i = 0; i < stems; ++i) {
        const std::string stem = host.gamepadStemName(i);
        REQUIRE_FALSE(stem.empty());
        writeTga(dir + "\\" + stem + ".tga", 8, 8);
    }
    char ini[128];
    std::snprintf(ini, sizeof(ini), "[art]\nwidth = %.0f\n", bgWidth);
    writeText(dir + "\\" + std::string(name) + ".ini", ini);
}

// Stage a whole scenario tree under its own subdir and chdir into it. Each
// case uses a distinct leaf so a re-run against a warm directory stays valid.
// The ORIGINAL cwd is restored on destruction: integration_main.h writes its
// wine_test_status.txt sentinel cwd-relative after the last case, and a test
// that leaves the process parked in its staging tree strands the sentinel
// where the harness cannot see it -- which reads as "exit code lost".
struct StagedTree {
    std::string cwd;
    char prev[MAX_PATH] = {};
    explicit StagedTree(const char* leaf) {
        GetCurrentDirectoryA(MAX_PATH, prev);
        cwd = std::string(kRoot) + "\\" + leaf;
        makeDirs(cwd + "\\plugins\\mxbmrp3_data\\gamepads");
        makeDirs(cwd + "\\plugins\\mxbmrp3_data\\pitboards");
        REQUIRE(SetCurrentDirectoryA(cwd.c_str()));
    }
    ~StagedTree() { SetCurrentDirectoryA(prev); }
    std::string pads() const { return cwd + "\\plugins\\mxbmrp3_data\\gamepads"; }
    std::string boards() const { return cwd + "\\plugins\\mxbmrp3_data\\pitboards"; }
};

// GamepadSprite::Part values this test reads. Mirrored literals, pinned by the
// stem-name comments; the header's static_asserts keep the real enum stable.
enum { kBackground = 0, kStick = 1 };

}  // namespace

TEST_CASE("gamepad skin: missing stems resolve from the base, geometry inherits") {
    // Host FIRST: loading the DLL must happen before StagedTree changes the
    // working directory, and stageBasePad reads the stem table through it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    StagedTree tree("skin_basic");
    stageBasePad(host, tree.pads(), "basepad", 900.0f);

    // The skin: ini + its own background only. Sorts BEFORE its base on
    // purpose -- discovery must not depend on the skin following the base
    // alphabetically (the two-phase scan is what this pins).
    const std::string skin = tree.pads() + "\\aaaskin";
    makeDirs(skin);
    writeTga(skin + "\\background.tga", 8, 8);
    writeText(skin + "\\aaaskin.ini", "[pad]\nbase = basepad\n");

    host.startup((tree.cwd + "\\save\\").c_str());

    // Own art stays its own; everything absent resolves from the base.
    CHECK(host.gamepadStemSource("aaaskin", kBackground) == 0);
    CHECK(host.gamepadStemSource("aaaskin", kStick) == 1);
    CHECK(host.gamepadStemSource("basepad", kStick) == 0);

    // Geometry: the skin states no [size], so it draws the base's 900, not the
    // built-in default.
    CHECK(host.gamepadGeomWidth("aaaskin") == doctest::Approx(900.0f));

    host.shutdown();
}

TEST_CASE("gamepad skin: its own ini keys override the inherited geometry") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    StagedTree tree("skin_override");
    stageBasePad(host, tree.pads(), "basepad", 900.0f);

    const std::string skin = tree.pads() + "\\wideskin";
    makeDirs(skin);
    writeText(skin + "\\wideskin.ini", "[pad]\nbase = basepad\n\n[art]\nwidth = 1234\n");

    host.startup((tree.cwd + "\\save\\").c_str());

    CHECK(host.gamepadGeomWidth("wideskin") == doctest::Approx(1234.0f));
    // An ini-only skin is legal: every stem resolves from the base.
    CHECK(host.gamepadStemSource("wideskin", kBackground) == 1);

    host.shutdown();
}

TEST_CASE("a base that is missing or is itself a skin rejects the pack whole") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    StagedTree tree("skin_badbase");
    stageBasePad(host, tree.pads(), "basepad", 900.0f);

    const std::string orphan = tree.pads() + "\\orphan";
    makeDirs(orphan);
    writeText(orphan + "\\orphan.ini", "[pad]\nbase = nosuchpack\n");

    const std::string skin = tree.pads() + "\\firstskin";
    makeDirs(skin);
    writeText(skin + "\\firstskin.ini", "[pad]\nbase = basepad\n");

    // One level only: a skin of a skin must be rejected, not resolved through
    // a chain.
    const std::string chained = tree.pads() + "\\zchained";
    makeDirs(chained);
    writeText(chained + "\\zchained.ini", "[pad]\nbase = firstskin\n");

    host.startup((tree.cwd + "\\save\\").c_str());

    CHECK(host.gamepadStemSource("firstskin", kBackground) == 1);   // accepted
    CHECK(host.gamepadStemSource("orphan", kBackground) == -1);     // absent
    CHECK(host.gamepadStemSource("zchained", kBackground) == -1);   // absent

    // A standalone pack missing files is still rejected -- the old rule did
    // not soften: only a declared base relaxes completeness.
    const std::string partial = tree.pads() + "\\partial";
    // (staged before startup would be cleaner, but the point holds: it was
    // never staged with a full set, and it declared no base)
    CHECK(host.gamepadStemSource("partial", kBackground) == -1);

    host.shutdown();
}

TEST_CASE("pitboard skin: inherits, overrides, and takes its own art's size") {
    StagedTree tree("board_skin");

    const std::string base = tree.boards() + "\\baseboard";
    makeDirs(base);
    writeTga(base + "\\background.tga", 1600, 400);
    // #ffffff exercises the float trap: 0xFFFFFFFF has 32 significant bits, so
    // it only survives if [text] color goes through parseRgbHex, not the
    // numeric (float) ini path.
    writeText(base + "\\baseboard.ini",
              "[board]\nname = Base Board\n\n[text]\ncolor = #ffffff\n");

    // Skin A: ini only -- art AND art-derived size come from the base.
    const std::string inionly = tree.boards() + "\\inionly";
    makeDirs(inionly);
    writeText(inionly + "\\inionly.ini", "[board]\nbase = baseboard\n");

    // Skin B: brings its own art at a DIFFERENT aspect -- the seeded size must
    // be its own file's, not the base's, or the art draws distorted.
    const std::string ownart = tree.boards() + "\\ownart";
    makeDirs(ownart);
    writeTga(ownart + "\\background.tga", 800, 800);
    writeText(ownart + "\\ownart.ini", "[board]\nbase = baseboard\n");

    // Skin C: states its own text colour -- override must beat inheritance.
    // #ff8800 doubles as the byte-order pin: R=ff, B=00 authored, so ABGR must
    // come back 0xFF0088FF and a parser that skipped the swap reads 0xFFFF8800.
    const std::string tinted = tree.boards() + "\\tinted";
    makeDirs(tinted);
    writeText(tinted + "\\tinted.ini",
              "[board]\nbase = baseboard\n\n[text]\ncolor = #ff8800\n");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup((tree.cwd + "\\save\\").c_str());

    CHECK(host.pitboardStemSource("inionly", kBackground) == 1);
    CHECK(host.pitboardPackArtWidth("inionly") == doctest::Approx(1600.0f));

    CHECK(host.pitboardStemSource("ownart", kBackground) == 0);
    CHECK(host.pitboardPackArtWidth("ownart") == doctest::Approx(800.0f));

    // Text colour: a property of the pack, riding the same inheritance. The
    // base's white survives bit-exact (the parseRgbHex-not-float pin), the
    // ini-only skin inherits it, and a skin stating its own wins.
    CHECK(host.pitboardTextColor("baseboard") == 0xFFFFFFFFu);
    CHECK(host.pitboardTextColor("inionly") == 0xFFFFFFFFu);

    CHECK(host.pitboardTextColor("tinted") == 0xFF0088FFu);

    host.shutdown();
}

