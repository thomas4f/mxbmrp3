// ============================================================================
// tests/integration/tests/helmet_lens_test.cpp
// The Goggles/Visor tint through the lens profile (helmet_lens_N.tga from
// tools/lens_gen.py, 1 = Goggles, 2 = Visor): drawn as that texture at the
// tint opacity times HelmetOverlayHud::LENS_EDGE_GAIN, so the lens's clear part
// shows the opacity the player set and its deep part up to the gain times it.
//   - past 1 / the gain, a flat floor under the lens keeps the clear part at
//     the set opacity (it used to stop at 40% whatever was set)
//   - Visor mode draws its tint first, behind the helmet art
//   - without the art installed, the flat tint at the set opacity, as before
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <string>
#include <vector>

namespace {
unsigned alpha(unsigned long c) { return static_cast<unsigned>((c >> 24) & 0xFF); }

std::string saveDir(const char* name, bool stageLens) {
    const std::string save = std::string("Z:\\tmp\\mxbmrp3-tests\\") + name + "\\";
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests", nullptr);
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    // The harness ships no assets: the lens is staged through the user-override sync.
    for (const char* file : { "helmet_lens_1.tga", "helmet_lens_2.tga" }) {
        const std::string dst = save + "mxbmrp3\\textures\\" + file;
        if (stageLens) REQUIRE(CopyFileA((std::string(MXB_REPO_DATA_DIR "/textures/") + file).c_str(),
                                         dst.c_str(), FALSE) != 0);
        else {
            // And from the discovery folder the sync copies into, which outlives
            // a host: an earlier case's staged copy would still be found there.
            DeleteFileA(dst.c_str());
            DeleteFileA((std::string("plugins\\mxbmrp3_data\\textures\\") + file).c_str());
        }
    }
    return save;
}

// The overlay's quads' alphas, with the given visor mode and tint opacity.
std::vector<unsigned> tint(PluginHost& host, const std::string& save, int mode, const char* opacity) {
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=10\n\n[HelmetOverlay]\nvisorMode=" +
                                         std::to_string(mode) + "\nvisorTintOpacity=" + opacity + "\n");
    host.loadSettings(save.c_str());
    REQUIRE(host.setHudVisible("helmet_overlay", true));
    TelemetryRow row;
    host.telemetryFrame(row);
    host.drawWithState(0);
    host.drawWithState(0);
    std::vector<unsigned> out;
    const size_t n = host.hudQuadRects("helmet_overlay").size();
    for (size_t i = 0; i < n; ++i) out.push_back(alpha(host.quadColor("helmet_overlay", static_cast<int>(i))));
    return out;
}
}  // namespace

TEST_CASE("helmet lens: the tint draws through the lens, its clear part at the set opacity") {
    const std::string save = saveDir("helmet_lens", true);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE_MESSAGE(host.textureSprite("helmet_lens") > 0, "no lens texture - the case would be vacuous");

    // Goggles: drawn last, over the helmet art. 10%: the lens alone, at 2.5 x.
    const std::vector<unsigned> low = tint(host, save, 1, "0.1");
    REQUIRE(!low.empty());
    CHECK(low.back() == 63);
    // 60%: the lens opaque, over a floor of 1 - 0.4 / 0.6 = 1/3, so the clear
    // part shows 1 - (2/3)(0.6) = 60%.
    const std::vector<unsigned> high = tint(host, save, 1, "0.6");
    REQUIRE(high.size() == low.size() + 1);
    CHECK(high.back() == 255);
    CHECK(high[high.size() - 2] == 85);

    // Visor: the tint first, behind the helmet art.
    const std::vector<unsigned> visor = tint(host, save, 2, "0.1");
    REQUIRE(visor.size() == low.size());
    CHECK(visor.front() == 63);
    host.shutdown();
}

TEST_CASE("helmet lens: without the lens art, the flat tint at the set opacity") {
    const std::string save = saveDir("helmet_lens_missing", false);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.textureSprite("helmet_lens") <= 0);
    const std::vector<unsigned> flat = tint(host, save, 1, "0.6");
    REQUIRE(!flat.empty());
    CHECK(flat.back() == 153);
    host.shutdown();
}
