// ============================================================================
// tests/integration/tests/sprite_order_test.cpp
// THE SPRITE-ORDER INVARIANT (CLAUDE.md): discovery (AssetManager) hands out
// absolute 1-based sprite indices in one order; registration
// (HudManager::setupDefaultResources) pushes file names in what must be the
// same order. The two walks live in different files and mirror each other's
// block arithmetic (textures, icons, themes, pads, boards, gauges), so a skew
// is silent: everything still draws, just with another asset's art.
//
// What pins it is the SELF-CHECK, verifySpriteRegistrationOrder(), which runs
// at the end of setupDefaultResources and re-derives every recorded index
// against the table actually registered. This test drives it two ways:
//
//   - GREEN on the trees where the index arithmetic has historically been at
//     risk: a rejected (incomplete) theme sitting alphabetically BETWEEN two
//     accepted ones (the rewind-on-rejection path -- a rewind bug shifts every
//     later theme's indices by the dropped file count), a theme skin with its
//     own art (registered after all standalone themes, out of directory
//     order), and gamepad packs following the theme block.
//
//   - NON-ZERO when the table is wrong: the must-catch probe re-runs the
//     checker with two table entries swapped. Without this half, a checker
//     that compared nothing would pass the green half forever.
//
// Same real-tree staging discipline as pack_skin_test.cpp (whose header has
// the rationale): minimal TGAs in an own temp directory, CWD pointed at it,
// the REAL discovery run via Startup. The small helpers are per-file on
// purpose -- each test stages its own process, root and tree.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <windows.h>
#include <cstdio>
#include <string>

namespace {

const char* kRoot = "Z:\\tmp\\mxbmrp3-tests\\sprite_order";

void makeDirs(const std::string& path) {
    std::string acc;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '\\') {
            if (!acc.empty() && acc.back() != ':') CreateDirectoryA(acc.c_str(), nullptr);
        }
        if (i < path.size()) acc += path[i];
    }
}

// The smallest file readTgaDimensions accepts (header only; discovery reads no
// pixels).
void writeTga(const std::string& path, int w, int h) {
    unsigned char header[18] = {};
    header[2] = 2;
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

// Stage under an own leaf and chdir into it; restore the original CWD on
// destruction (integration_main's status sentinel is CWD-relative).
struct StagedTree {
    std::string cwd;
    char prev[MAX_PATH] = {};
    explicit StagedTree(const char* leaf) {
        GetCurrentDirectoryA(MAX_PATH, prev);
        cwd = std::string(kRoot) + "\\" + leaf;
        makeDirs(cwd + "\\plugins\\mxbmrp3_data\\gamepads");
        makeDirs(cwd + "\\plugins\\mxbmrp3_data\\themes");
        REQUIRE(SetCurrentDirectoryA(cwd.c_str()));
    }
    ~StagedTree() { SetCurrentDirectoryA(prev); }
    std::string pads() const { return cwd + "\\plugins\\mxbmrp3_data\\gamepads"; }
    std::string themes() const { return cwd + "\\plugins\\mxbmrp3_data\\themes"; }
};

// The nine frame slices discovery requires (the slice names ARE the format --
// see pack_skin_test.cpp's stageBaseTheme).
void stageFullTheme(const std::string& themeDir, const char* name) {
    const std::string dir = themeDir + "\\" + name;
    makeDirs(dir);
    static const char* kSlices[9] = {
        "frame_center",
        "frame_corner_tl", "frame_corner_tr", "frame_corner_bl", "frame_corner_br",
        "frame_edge_top", "frame_edge_bottom", "frame_edge_left", "frame_edge_right",
    };
    for (const char* slice : kSlices) writeTga(dir + "\\" + slice + ".tga", 8, 8);
    writeText(dir + "\\theme.ini", "");
}

// A complete standalone gamepad pack, stems read from the DLL's own table.
void stageBasePad(PluginHost& host, const std::string& padDir, const char* name) {
    const std::string dir = padDir + "\\" + name;
    makeDirs(dir);
    const int stems = host.gamepadStemCount();
    REQUIRE(stems > 0);
    for (int i = 0; i < stems; ++i) {
        const std::string stem = host.gamepadStemName(i);
        REQUIRE_FALSE(stem.empty());
        writeTga(dir + "\\" + stem + ".tga", 8, 8);
    }
    writeText(dir + "\\" + std::string(name) + ".ini", "");
}

}  // namespace

TEST_CASE("sprite order self-check is green across rejects, skins and packs") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    StagedTree tree("green");

    // Two accepted themes with a REJECTED one alphabetically between them: the
    // rewind path. "alpha" also brings a card set, so its file count is not
    // the minimal nine (the arithmetic must track counts, not assume them).
    stageFullTheme(tree.themes(), "alpha");
    writeTga(tree.themes() + "\\alpha\\card_center.tga", 8, 8);
    const std::string bad = tree.themes() + "\\midbad";
    makeDirs(bad);
    writeTga(bad + "\\frame_center.tga", 8, 8);   // incomplete: skipped + rewound
    stageFullTheme(tree.themes(), "zzz");

    // A theme skin with its OWN art: registered after all standalone themes,
    // out of directory order ("aaaskin" sorts first, registers last).
    const std::string skin = tree.themes() + "\\aaaskin";
    makeDirs(skin);
    writeTga(skin + "\\frame_center.tga", 8, 8);
    writeText(skin + "\\theme.ini", "[pack]\nbase = alpha\n");

    // Gamepad block after the themes: a base pack and an ini-only skin.
    stageBasePad(host, tree.pads(), "basepad");
    const std::string padSkin = tree.pads() + "\\zskin";
    makeDirs(padSkin);
    writeText(padSkin + "\\zskin.ini", "[pack]\nbase = basepad\n");

    host.startup((tree.cwd + "\\save\\").c_str());

    REQUIRE_MESSAGE(host.spriteOrderMismatches() != -1,
                    "MXBMRP3_Test_SpriteOrderMismatches hook missing");
    CHECK(host.spriteOrderMismatches() == 0);

    host.shutdown();
}

TEST_CASE("sprite order self-check catches a skewed table (must-catch)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    StagedTree tree("mustcatch");
    stageFullTheme(tree.themes(), "onlytheme");

    host.startup((tree.cwd + "\\save\\").c_str());

    REQUIRE(host.spriteOrderMismatches() == 0);
    // Swap the theme's first two registered entries: both indices are recorded
    // at discovery, so the checker must flag both. Zero here would mean the
    // check compares nothing and the green case above is vacuous.
    CHECK(host.spriteOrderWithSwap(1, 2) > 0);
    // ...and the probe restored the table.
    CHECK(host.spriteOrderMismatches() == 0);

    host.shutdown();
}
