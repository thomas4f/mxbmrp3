// ============================================================================
// tests/integration/tests/gauges_migration_test.cpp
// Custom gauge art drawn before gauges/<name>/ packs survives the upgrade.
//
// THE BREAK THIS PREVENTS is one that already happened once, with screenshots:
// when the pit board and the gamepad pad became pack HUDs they stopped
// consulting the flat textures directory, and a user upgrading with custom art
// got grey boxes. The tacho and speedo are the same change, and a wider blast
// radius -- custom dial faces are the oldest and easiest thing to redraw in this
// plugin, because a dial is one square picture with no geometry to match.
//
// So the plugin carries the art across itself: AssetManager::migrateLegacyGaugeArt
// turns a pre-pack tacho_widget_N.tga / speedo_widget_N.tga in the user's own
// Documents tree into a real gauges pack in that same tree, once.
//
// WHAT THIS CAN AND CANNOT ASSERT. The migration is file I/O against the user's
// save path, which the harness DOES stage, so every clause below is checked on
// disk exactly as the plugin wrote it. What it deliberately does not check is
// that the resulting pack then REGISTERS. The plugin builds its own
// plugins\mxbmrp3_data tree at startup by syncing whatever a test staged, so the
// migrated folder really does arrive there -- but the SHIPPED packs never do,
// and this pack states `base = classic`, so discovery correctly refuses it for a
// base that is not installed here. A pack count would therefore read 0 for "the
// shipped set is not staged in this harness" identically to "the migration
// produced nothing" -- the same false-green shape pack_texture_variant_test's
// header describes paying for once already.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <direct.h>
#include <fstream>
#include <string>

namespace {

// The harness talks to the plugin in Windows paths and to the filesystem in the
// same ones -- Z: is the Wine root -- so one string serves both.
const std::string kRoot = "Z:\\tmp\\mxbmrp3-tests\\gaugesmigrate";

std::string userDir()    { return kRoot + "\\mxbmrp3"; }
std::string texturesDir(){ return userDir() + "\\textures"; }
std::string gaugesDir()  { return userDir() + "\\gauges"; }
std::string legacyDir()  { return gaugesDir() + "\\legacy"; }

bool exists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.is_open();
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::string();
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

void writeFile(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.is_open(), "could not write " << path);
    f << body;
}

// A REAL 18-byte TGA header (discovery reads dimensions from it) followed by a
// distinctive marker, so the same file serves both jobs: the byte-for-byte copy
// assertion, and being a face the discovery pass will actually accept.
std::string fakeArt() {
    std::string a(18, '\0');
    a[2] = 2;                       // uncompressed truecolor
    a[12] = 8; a[14] = 8;           // 8x8
    a[16] = 24;                     // bpp
    a += "these-exact-bytes-must-arrive";
    return a;
}

void resetTree() {
    // Fresh every case: the marker file is the whole point of one of them, so a
    // leftover from an earlier case would make the others pass for free.
    _mkdir(kRoot.c_str());
    _mkdir(userDir().c_str());
    _mkdir(texturesDir().c_str());
    ::DeleteFileA((legacyDir() + "\\tacho.tga").c_str());
    ::DeleteFileA((legacyDir() + "\\speedo.tga").c_str());
    ::DeleteFileA((legacyDir() + "\\gauge.ini").c_str());
    ::RemoveDirectoryA(legacyDir().c_str());
    ::DeleteFileA((gaugesDir() + "\\classic\\tacho.tga").c_str());
    ::DeleteFileA((gaugesDir() + "\\classic\\speedo.tga").c_str());
    ::DeleteFileA((gaugesDir() + "\\classic\\gauge.ini").c_str());
    ::RemoveDirectoryA((gaugesDir() + "\\classic").c_str());
    ::DeleteFileA((gaugesDir() + "\\.legacy-migrated").c_str());
    ::RemoveDirectoryA(gaugesDir().c_str());
    ::DeleteFileA((texturesDir() + "\\tacho_widget_1.tga").c_str());
    ::DeleteFileA((texturesDir() + "\\speedo_widget_1.tga").c_str());

    // ...and the copies the plugin's own asset sync makes of all of the above.
    // Startup copies the user's tree into plugins\mxbmrp3_data (relative to the
    // CWD, which is the build directory), and that tree PERSISTS between runs --
    // so without this the fake art staged by one case is still sitting in the
    // shared discovery folder for every later test in the suite, where it trips
    // the stranded-texture warning and offers a half-resolved `legacy` pack that
    // nothing here put there.
    ::DeleteFileA("plugins\\mxbmrp3_data\\textures\\tacho_widget_1.tga");
    ::DeleteFileA("plugins\\mxbmrp3_data\\textures\\speedo_widget_1.tga");
    ::DeleteFileA("plugins\\mxbmrp3_data\\gauges\\legacy\\tacho.tga");
    ::DeleteFileA("plugins\\mxbmrp3_data\\gauges\\legacy\\speedo.tga");
    ::DeleteFileA("plugins\\mxbmrp3_data\\gauges\\legacy\\gauge.ini");
    ::RemoveDirectoryA("plugins\\mxbmrp3_data\\gauges\\legacy");
    ::DeleteFileA("plugins\\mxbmrp3_data\\gauges\\classic\\tacho.tga");
    ::DeleteFileA("plugins\\mxbmrp3_data\\gauges\\classic\\speedo.tga");
    ::DeleteFileA("plugins\\mxbmrp3_data\\gauges\\classic\\gauge.ini");
    ::RemoveDirectoryA("plugins\\mxbmrp3_data\\gauges\\classic");
}

}  // namespace

TEST_CASE("gauges migration: pre-pack art becomes a pack in the user's own folder") {
    resetTree();
    writeFile(texturesDir() + "\\tacho_widget_1.tga", fakeArt());
    writeFile(texturesDir() + "\\speedo_widget_1.tga", fakeArt());

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup((kRoot + "\\").c_str());

    CHECK_MESSAGE(exists(legacyDir() + "\\tacho.tga"),
                  "custom tacho face was not carried into a pack");
    CHECK_MESSAGE(exists(legacyDir() + "\\speedo.tga"),
                  "custom speedo face was not carried into a pack");

    // The BYTES, not just the name: a migration that created empty placeholders
    // would satisfy every other check here and still lose the user's art.
    CHECK(readAll(legacyDir() + "\\tacho.tga") == fakeArt());
    CHECK(readAll(legacyDir() + "\\speedo.tga") == fakeArt());

    const std::string ini = readAll(legacyDir() + "\\gauge.ini");
    CHECK_MESSAGE(!ini.empty(), "no gauge.ini written beside the migrated art");
    // `base = classic` is load-bearing twice over -- see migrateLegacyGaugeArt.
    CHECK(ini.find("base = classic") != std::string::npos);
    // The CANONICAL section, and specifically not the per-type one. This is the
    // assertion that used to read `[gauges]` and therefore passed while the
    // generated pack was broken: the readers had been renamed to `pack.*` and the
    // writer had not, so `base` was silently dropped. See the case below for why
    // checking the emitted TEXT is not enough on its own.
    CHECK(ini.find("[pack]") != std::string::npos);
    CHECK(ini.find("[gauges]") == std::string::npos);

    // The source is LEFT ALONE. It is the user's file; the migration copies.
    CHECK(exists(texturesDir() + "\\tacho_widget_1.tga"));
}

TEST_CASE("gauges migration: the generated pack is one the reader understands") {
    // THE ASSERTION THE STRING CHECKS CANNOT MAKE. The migration writes config for
    // the plugin to read back, and the only question worth asking about generated
    // config is whether the reader understood it. Checking the emitted text answers
    // "did we write what we meant to", which stayed true through a release where
    // the section had been renamed under the writer's feet and every `base` it
    // produced was being thrown away.
    //
    // So this stages a real `classic` alongside, lets discovery run for real, and
    // reads the RESOLVED pack back: base bound, and the base's dial ranges
    // inherited rather than the built-in defaults.
    resetTree();
    writeFile(texturesDir() + "\\tacho_widget_1.tga", fakeArt());

    // A shipped-style base with a deliberately non-default rev limit, so
    // "inherited from classic" and "fell back to the built-in 15000" cannot be
    // confused for each other.
    const std::string classic = gaugesDir() + "\\classic";
    _mkdir(gaugesDir().c_str());
    _mkdir(classic.c_str());
    writeFile(classic + "\\tacho.tga", fakeArt());
    writeFile(classic + "\\speedo.tga", fakeArt());
    writeFile(classic + "\\gauge.ini",
              "[pack]\nname = Classic\n\n[tacho]\nmax = 12345\n\n[speedo]\nmax = 199\n");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup((kRoot + "\\").c_str());

    const std::string base = host.gaugesInfo("classic");
    REQUIRE_MESSAGE(!base.empty(), "the staged base pack was not discovered - fixture problem");
    CHECK(base.find("tachoMax=12345") != std::string::npos);

    const std::string legacy = host.gaugesInfo("legacy");
    REQUIRE_MESSAGE(!legacy.empty(),
                    "the migrated pack was not discovered at all - with `base` dropped it "
                    "fails the all-or-nothing completeness check, which is the shape this "
                    "whole migration exists to avoid");
    CHECK_MESSAGE(legacy.find("base=classic") != std::string::npos,
                  "the generated ini's `base` never reached the reader");
    CHECK_MESSAGE(legacy.find("tachoMax=12345") != std::string::npos,
                  "the migrated pack did not inherit its base's dial range, so a needle "
                  "would be placed against figures painted for another scale");
}

TEST_CASE("gauges migration: one redrawn face is enough for a valid pack") {
    // Redrawing only the rev-counter is the common case -- it is the gauge people
    // stare at. The pack must still be complete, which is what `base = classic`
    // buys: the speedo stem resolves from the shipped set instead of the pack
    // being skipped for an incomplete sprite set.
    resetTree();
    writeFile(texturesDir() + "\\tacho_widget_1.tga", fakeArt());

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup((kRoot + "\\").c_str());

    CHECK(exists(legacyDir() + "\\tacho.tga"));
    CHECK_FALSE_MESSAGE(exists(legacyDir() + "\\speedo.tga"),
                        "a face the user never drew must not be invented");
    CHECK(readAll(legacyDir() + "\\gauge.ini").find("base = classic") != std::string::npos);
}

TEST_CASE("gauges migration: it happens once, and a deleted pack stays deleted") {
    resetTree();
    writeFile(texturesDir() + "\\tacho_widget_1.tga", fakeArt());

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup((kRoot + "\\").c_str());
    }
    REQUIRE(exists(legacyDir() + "\\tacho.tga"));
    CHECK_MESSAGE(exists(gaugesDir() + "\\.legacy-migrated"),
                  "no marker written; the migration would re-run every launch");

    // Somebody looks at the generated pack and decides they do not want it.
    ::DeleteFileA((legacyDir() + "\\tacho.tga").c_str());
    ::DeleteFileA((legacyDir() + "\\gauge.ini").c_str());
    ::RemoveDirectoryA(legacyDir().c_str());

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup((kRoot + "\\").c_str());
    }

    CHECK_MESSAGE(!exists(legacyDir() + "\\gauge.ini"),
                  "the migration came back after the user deleted it");
}

TEST_CASE("gauges migration: no pre-pack art means no pack and no marker") {
    // And specifically NO MARKER: somebody who drops custom art into their
    // textures folder next month must still be migrated, so the absence of art
    // today cannot be recorded as "nothing to do, ever".
    resetTree();

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup((kRoot + "\\").c_str());

    CHECK_FALSE(exists(legacyDir() + "\\gauge.ini"));
    CHECK_FALSE_MESSAGE(exists(gaugesDir() + "\\.legacy-migrated"),
                        "an empty install armed the one-shot marker");
}
