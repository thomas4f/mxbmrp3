// ============================================================================
// tests/integration/tests/now_playing_test.cpp
// NOW PLAYING: the widget, the Dash's page and the track-change notice, all fed
// by MediaManager's snapshot (core/media_manager.h).
//
// The test build has no WinRT, so MediaManager never polls a player and the
// widget says N/A. A player is published through MXBMRP3_Test_MediaPublish, as
// the worker publishes one, and what the controls send is read back from the
// worker's queue with MXBMRP3_Test_MediaPending (core/test_hooks_media.cpp).
// What this cannot reach is the Windows side itself: the GSMTC calls, the
// Volume Mixer write and the CP1252 conversion run only in the MSVC build.
//
// The notice case stages the two default faces' real .fnt (StagedFonts), so
// its captions are cut by the Title font's glyph widths as in game; elsewhere
// no font is staged and text measures a grid cell a character.
//
// The icons are not staged, so each button draws its mark ("|<", ">", "||",
// ">|", "Vo"): that is where a test finds the buttons, and it pins the
// missing-icon fallback with them.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int NO_PLAYER = 2, PAUSED = 3, PLAYING = 4;   // MediaManager::State
constexpr int CMD_PREVIOUS = 1, CMD_PLAY = 2, CMD_NEXT = 3;   // MediaManager::Command

// Start with the integration on and `extra` sections laid over it; the
// first-step hint dropped so the Dash's page is live.
void start(PluginHost& host, const char* save, const std::string& extra) {
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save) >= 0);
    host.writeSettingsFile(save, "[Settings]\nversion=6\n\n[Messages]\ndashTried=1\n\n[General]\nnowPlaying=1\n\n"
                                 "[NoticesHud]\nnotice_default_setup=0\n\n" + extra);
    host.loadSettings(save);
    REQUIRE(host.hasMediaHooks());
    REQUIRE(host.hasStringRows());
    host.draw();
}

const PluginHost::StringRow* rowOf(const std::vector<PluginHost::StringRow>& rows, const std::string& text) {
    for (const auto& r : rows)
        if (r.text == text) return &r;
    return nullptr;
}

bool shows(PluginHost& host, const char* hud, const std::string& text) {
    return rowOf(host.hudStringRows(hud), text) != nullptr;
}

// Run from a tree whose plugins\\mxbmrp3_data\\fonts holds the shipped Title and
// Normal faces, so AssetManager measures them; the CWD is restored after.
struct StagedFonts {
    std::filesystem::path prev = std::filesystem::current_path();
    explicit StagedFonts(const char* dir) {
        const std::filesystem::path fonts = std::filesystem::path(dir) / "plugins" / "mxbmrp3_data" / "fonts";
        std::filesystem::create_directories(fonts);
        for (const char* face : { "EnterSansman-Italic", "RobotoMono-Regular" }) {
            const std::string name = std::string(face) + ".fnt";
            std::filesystem::copy_file(std::filesystem::path(MXB_REPO_DATA_DIR) / "fonts" / name, fonts / name,
                                       std::filesystem::copy_options::overwrite_existing);
        }
        std::filesystem::current_path(dir);
    }
    ~StagedFonts() { std::filesystem::current_path(prev); }
};

// A left click at x, y: hover (which also wakes the pointer), press, release.
void clickAt(PluginHost& host, float x, float y) {
    host.injectMouse(true, x - 0.01f, y, 0); host.draw();
    host.injectMouse(true, x, y, 0); host.draw();
    host.injectMouse(true, x, y, 1); host.draw();
    host.injectMouse(true, x, y, 0); host.draw();
}

}  // namespace

TEST_CASE("now playing: the widget says N/A without media controls, then shows the player") {
    PluginHost host(dllPath());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\now_playing_widget\\";
    start(host, save, "[NowPlayingWidget]\nvisible=1\n");
    host.draw();
    CHECK(shows(host, "now_playing_widget", "N/A"));

    host.mediaPublish("A Song Title That Runs Well Past The Card", "The Artist", PLAYING, 60000, 240000, 40);
    host.draw();
    const auto rows = host.hudStringRows("now_playing_widget");
    // Cut at the card's width, never ellipsised.
    CHECK(rowOf(rows, "A Song Title That Runs Well") != nullptr);
    CHECK(rowOf(rows, "The Artist") != nullptr);
    CHECK(rowOf(rows, "||") != nullptr);   // playing: the button pauses
    CHECK(rowOf(rows, ">") == nullptr);
    CHECK_FALSE(shows(host, "now_playing_widget", "N/A"));

    // Paused: the button plays. Nothing rebuilds between two identical snapshots.
    host.mediaPublish("A Song Title That Runs Well Past The Card", "The Artist", PAUSED, 60000, 240000, 40);
    host.draw();
    CHECK(shows(host, "now_playing_widget", ">"));
    CHECK_FALSE(shows(host, "now_playing_widget", "||"));

    host.mediaPublish("", "", NO_PLAYER);
    host.draw();
    CHECK(shows(host, "now_playing_widget", "No player"));
    host.shutdown();
}

TEST_CASE("now playing: the buttons and the volume slider queue their commands") {
    PluginHost host(dllPath());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\now_playing_click\\";
    start(host, save, "[NowPlayingWidget]\nvisible=1\n");
    REQUIRE(host.hasInjectedMouse());
    host.mediaPublish("Song", "Artist", PLAYING, 1000, 200000, 20);
    host.draw();
    const auto rows = host.hudStringRows("now_playing_widget");
    const auto* prev = rowOf(rows, "|<");
    const auto* play = rowOf(rows, "||");
    const auto* next = rowOf(rows, ">|");
    const auto* vol = rowOf(rows, "Vo");
    REQUIRE(prev);
    REQUIRE(play);
    REQUIRE(next);
    REQUIRE(vol);
    CHECK(prev->x < play->x);
    CHECK(play->x < next->x);
    CHECK(next->x < vol->x);
    // The marks sit on the button row; a click a little below their tops lands in it.
    const float rowY = static_cast<float>(prev->y) + 0.008f;
    REQUIRE(host.mediaPending(0) == 0);

    clickAt(host, static_cast<float>(prev->x), rowY);
    CHECK(host.mediaPending(0) == CMD_PREVIOUS);
    clickAt(host, static_cast<float>(play->x), rowY);
    CHECK(host.mediaPending(0) == CMD_PLAY);
    clickAt(host, static_cast<float>(next->x), rowY);
    CHECK(host.mediaPending(0) == CMD_NEXT);

    // The slider: 9 characters from 1.5 right of its icon; the buttons are 3
    // characters apart. Its middle is about half volume.
    const double charW = (play->x - prev->x) / 3.0;
    clickAt(host, static_cast<float>(vol->x + charW * 6.0), rowY);
    const int volume = host.mediaPending(1);
    CHECK(volume >= 45);
    CHECK(volume <= 55);

    // With the settings menu open the card takes no clicks: the menu owns them.
    host.mediaPublish("Song", "Artist", PLAYING, 1000, 200000, 20);
    host.showSettings(true);
    host.draw();
    clickAt(host, static_cast<float>(prev->x), rowY);
    CHECK(host.mediaPending(0) == CMD_NEXT);
    host.shutdown();
}

TEST_CASE("now playing: the Dash's page shows the widget's rows") {
    PluginHost host(dllPath());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\now_playing_dash\\";
    start(host, save, "[DashHud]\nvisible=1\npage=nowplaying\npage_nowplaying=1\npage_names=0\nnotice_takeover=0\n");
    host.mediaPublish("Song", "Artist", PLAYING, 1000, 200000, 20);
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 14);   // Now Playing
    CHECK(shows(host, "dash_hud", "Song"));
    CHECK(shows(host, "dash_hud", "Artist"));
    CHECK(shows(host, "dash_hud", "||"));
    host.shutdown();
}

TEST_CASE("now playing: a new track is a notice, on the Notices HUD and the Dash") {
    PluginHost host(dllPath());   // loads from the CWD, so before staging
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\now_playing_notice\\";
    StagedFonts fonts(save);
    start(host, save, "[DashHud]\nvisible=1\npage=laps\n\n[NoticesHud]\nvisible=1\n");
    // The first track seen was already playing: not a change.
    host.mediaPublish("First Song", "First Artist", PLAYING, 1000, 200000, 20);
    host.draw();
    host.draw();
    CHECK_FALSE(shows(host, "notices_hud", "First Song"));
    CHECK(host.dashPage() == 2);   // Laps

    // A pause and a play of the same track are not either.
    host.mediaPublish("First Song", "First Artist", PAUSED, 1000, 200000, 20);
    host.draw();
    host.mediaPublish("First Song", "First Artist", PLAYING, 2000, 200000, 20);
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 2);

    // A new one is: its title (cut to the slab) on the Notices HUD, title and
    // artist on the Dash, which it takes over.
    host.mediaPublish("Second Song With A Long Name", "Second Artist", PLAYING, 0, 180000, 20);
    host.draw();
    host.draw();
    const std::string title = "Second Song With A Long Name";
    auto cutTitle = [&](const char* hud) {   // the longest leading part of the title shown
        std::string best;
        for (const auto& r : host.hudStringRows(hud))
            if (r.text.size() >= 8 && title.compare(0, r.text.size(), r.text) == 0 && r.text.size() > best.size())
                best = r.text;
        return best;
    };
    CHECK(host.dashPage() == 15);   // Notices
    const std::string onNotices = cutTitle("notices_hud");
    const std::string onDash = cutTitle("dash_hud");
    INFO("Notices '" << onNotices << "', Dash '" << onDash << "'");
    // Each panel cuts to its own width, by the Title font's glyphs: more than
    // the grid's cell a character gives (16 on the slab, 18 on the Dash's card),
    // the Dash more than the slab, and neither the whole of this one.
    CHECK(onNotices.size() > 16);
    CHECK(onDash.size() > 18);
    CHECK(onDash.size() > onNotices.size());
    CHECK(onDash.size() < title.size());
    CHECK(shows(host, "dash_hud", "Second Artist"));
    host.shutdown();
}

TEST_CASE("now playing: the track notice has its own switch, and the player filter is kept") {
    PluginHost host(dllPath());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\now_playing_notice_off\\";
    start(host, save, "[DashHud]\nvisible=1\npage=laps\n\n[NoticesHud]\nvisible=1\nnotice_track_changed=0\n\n"
                      "[General]\nnowPlayingApps=spotify, chrome\n");
    host.mediaPublish("First Song", "First Artist", PLAYING);
    host.draw();
    host.mediaPublish("Second Song", "Second Artist", PLAYING);
    host.draw();
    host.draw();
    CHECK_FALSE(shows(host, "notices_hud", "Second Song"));
    CHECK(host.dashPage() == 2);
    host.shutdown();   // saves from the live state

    std::string ini;
    {
        FILE* f = std::fopen("Z:\\tmp\\mxbmrp3-tests\\now_playing_notice_off\\mxbmrp3\\mxbmrp3_settings.ini", "rb");
        REQUIRE(f);
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) ini.append(buf, n);
        std::fclose(f);
    }
    CHECK(ini.find("notice_track_changed=0") != std::string::npos);
    // INI-only ([General] nowPlayingApps, core/media_app_filter.h): written back as read.
    CHECK(ini.find("nowPlayingApps=spotify, chrome ;") != std::string::npos);
}
