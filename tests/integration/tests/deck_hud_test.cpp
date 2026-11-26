// ============================================================================
// tests/integration/tests/deck_hud_test.cpp
// The decks (hud/deck_hud.h), on the Decks tab, drawn by a theme's deck
// layout (themes/<name>/decks.ini or decks-slanted.ini, hud/deck_layout.h).
//
// WHY THIS FILE EXISTS. A deck reads the same state the widgets do, but
// through its own gathering code, its own INI section and a pack that places
// every string -- and each of those fails silently: the snapshot and the
// standings stay right while a deck draws a stale value, loses its style on
// a restart, or a pack drops a string to a misread key. So the cases below
// assert what a deck DRAWS, from settings and packs written to disk the way a
// player's are:
//   * every deck is off by default (a new panel is opt-in);
//   * the shipped Cabinet pack loads whole: the Speed deck's gear, speed and
//     unit, the Race deck's place of the field, session and lap of a lap
//     race, and an unknown style name falls back to Cabinet rather than
//     blanking the deck;
//   * a deck sits on the theme's panel; Title, Opacity and Scale are every
//     deck's ([Decks]), and no deck has a theme of its own;
//   * Slant draws its lean as pack images
//     (`art = image`), registered in sprite order; an unknown image draws solid;
//   * a style layered over Cabinet (`base = cabinet`) is picked by name and
//     its own keys win: the shape of a pack someone authors in Documents;
//   * a text's `fit` holds a long string (a translation) inside its width:
//     it shrinks, then is cut, and never runs over the next element;
//   * the riding gauges (Wheelie, Slide, Spin) draw in every shipped style;
//     the Slide gauge times a slide past grip and keeps the session's longest
//     as MAX (deck_coach.h's rules, here through real telemetry and the
//     deck's own clock);
//   * the Combo deck follows a chain through FmxManager: the trick turning
//     as a chip, the points and multiplier once it lands, LANDED +points
//     when the chain banks (into SCORE and BEST), BAILED and no points when
//     a crash in the landing grace loses it.
//   * every shipped style loads with no warning in the log and draws every
//     deck (a misspelt key only warns, and falls back to the base's);
//     the Rival deck tags the gap GAINED or LOST at each line, shows the
//     rival's last lap in a race, and defends against a closer rider behind (with a margin both ways), the
//     Timing deck holds a split with its top speed against the sector's best,
//     the Gap deck tags a sector beating its best (an invalid lap sets neither
//     best), the Speed deck stamps the reaction from the gate drop for
//     REACT_US (back-dated when the classification reports it late), and the
//     Spin gauge reads the rear wheel's slip
//     live and times it (only the rear on the ground counts, never while
//     crashed).
//   * the Session deck shows the server, the track and the format (the
//     Session HUD's own helpers).
//   * the Notice deck carries the toasts through one queue with the race
//     notices: the toast card stands down while it is on (one toast, one
//     place), a notice that calls for action goes ahead of a toast and the
//     stamp counts the one held, its clock stopped until the notice has
//     gone, and with nothing up it draws nothing except
//     a preview on the Decks tab.
//   * the Fuel deck reads the Fuel widget's figures with the widget off:
//     the laps left, the fuel and the average a lap, the tank as blocks,
//     and the laps left warm under two; its history is the player's own,
//     on track, for one session.
//   * the Now Playing deck shows the player's track and times, its buttons,
//     volume and track strip take clicks and drags (NowPlayingControls, the
//     widget's own), the track strip carries its knob only while the player
//     seeks, and a new track raises the stamp.
//   * a skin's own keys reach the drawing (corners, tone colours, tint,
//     hug-to, a needle's colour, glow, its [fonts]); Slanted is saved
//     once; the Decks and Widgets switches survive a restart, each
//     under its own key.
// The game rules themselves are unit-tested (test_deck_coach.cpp,
// test_deck_meter.cpp, test_deck_score.cpp); this pins that the decks read them.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "deck_ids.h"
#include "doctest.h"
#include "ini.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

// What these cases stage under their save dirs, the plugin's Startup syncs into
// the SHARED discovery tree (CWD-relative plugins\mxbmrp3_data), which outlives
// this binary: every later test then had these themes and decks installed --
// Appearance grew a Panel theme row and Gap Bar a Theme cycle, and
// i18n_limits_test and settings_click_test failed in the full run only
// (`run_tests.sh deck_hud_test i18n_limits_test settings_click_test`
// reproduces it). So the synced themes, decks and gauges go when the binary
// ends, as gauges_migration_test clears its own.
// Every face a shipped deck style names (Cabinet's three, then the theme
// styles'), then VT323: the tall face a player picks instead
const char* const kStagedFonts[] = { "Tomorrow-BoldItalic", "Tomorrow-SemiBoldItalic", "Audiowide-Regular",
                                     "PressStart2P-Regular", "IBMPlexSans-SemiBold", "IBMPlexSans-Regular",
                                     "IBMPlexMono-Regular", "Sora-SemiBold", "Sora-Regular", "VT323-Regular" };

// The staged faces leave the shared tree too: installed, they change how a
// later case's text fits (the Notice deck's line break)
void removeSyncedFonts() {
    std::error_code ec;
    for (const char* font : kStagedFonts)
        std::filesystem::remove(std::filesystem::path("plugins/mxbmrp3_data/fonts") / (std::string(font) + ".fnt"), ec);
}
struct SyncedFontsCleanup {
    ~SyncedFontsCleanup() { removeSyncedFonts(); }
};

struct SyncedPacksCleanup {
    ~SyncedPacksCleanup() {
        std::error_code ec;
        for (const char* type : { "themes", "gauges" })
            std::filesystem::remove_all(std::filesystem::path("plugins/mxbmrp3_data") / type, ec);
        removeSyncedFonts();
        for (int n = 1; n <= 4; ++n)   // the slant-edge ramps the smoothing case stages
            std::filesystem::remove("plugins/mxbmrp3_data/textures/slant_edge_" + std::to_string(n) + ".tga", ec);
        std::filesystem::remove("plugins/mxbmrp3_data/textures/glow_1.tga", ec);   // the Notice case's
    }
} g_syncedPacksCleanup;

constexpr int RACE = 6;     // PiBoSo Race1 session enum
constexpr int PLAYER = 10;  // first active RaceAddEntry after EventInit = the player

// The ink of the first string reading `text` on a HUD; 0 if none
unsigned long colourOf(PluginHost& host, const char* hud, const char* text) {
    const auto rows = host.hudStringRows(hud);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text == text) return host.stringColor(hud, static_cast<int>(i));
    }
    return 0;
}

// Every deck on at once: the carousel stays on its first deck (Timing, put
// back in its turn), with no notice taking it over and no Auto step
std::string quietCarousel(const char* section) {
    return std::strcmp(section, "CarouselDeck") == 0 ? "auto=0\nnoticeFocus=0\ndeckTiming=1\n" : "";
}

bool has(const std::vector<PluginHost::StringRow>& rows, const char* text) {
    for (const auto& r : rows) {
        if (r.text == text) return true;
    }
    return false;
}

bool hasPrefix(const std::vector<PluginHost::StringRow>& rows, char c) {
    for (const auto& r : rows) {
        if (r.text.size() > 1 && r.text[0] == c) return true;
    }
    return false;
}

// `head` drawn with `tail` right after it: a value and its smaller unit, or
// a place and its "/field". A one-character head alone matches anything.
bool hasPair(const std::vector<PluginHost::StringRow>& rows, const char* head, const char* tail) {
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        if (rows[i].text == head && rows[i + 1].text == tail) return true;
    }
    return false;
}

// Cabinet's meter band (speed-, race-, fuel-meter-1: under the panel,
// design px) as drawn: each quad whose middle sits in it, and its colour.
// The panel (box-1, 11.88 down to 118.8) is the deck's largest quad.
struct MeterPart { PluginHost::QuadRect rect; unsigned long colour = 0; };
std::vector<MeterPart> meterBand(PluginHost& host, const char* deck) {
    const auto rects = host.hudQuadRects(deck);
    PluginHost::QuadRect panel;
    double area = 0.0;
    for (const auto& q : rects) {
        const double a = (q.r - q.l) * (q.b - q.t);
        if (a > area) { area = a; panel = q; }
    }
    std::vector<MeterPart> band;
    if (area <= 0.0) return band;
    const double perPx = (panel.b - panel.t) / 106.92;
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& q = rects[i];
        const double mid = 11.88 + ((q.t + q.b) * 0.5 - panel.t) / perPx;
        const double h = (q.b - q.t) / perPx;
        if (mid > 103.53 && mid < 103.53 + 7.64 && h < 7.7) band.push_back({ q, host.quadColor(deck, static_cast<int>(i)) });
    }
    std::sort(band.begin(), band.end(), [](const MeterPart& a, const MeterPart& b) { return a.rect.l < b.rect.l; });
    return band;
}
int meterBlocks(PluginHost& host, const char* deck) { return static_cast<int>(meterBand(host, deck).size()); }
// The share of a meter's drawn length lit: every part not in the unlit
// colour (the rightmost part's, the meter not being full)
double meterLit(const std::vector<MeterPart>& band) {
    if (band.empty()) return -1.0;
    double lit = 0.0, all = 0.0;
    for (const auto& p : band) {
        all += p.rect.r - p.rect.l;
        if (p.colour != band.back().colour) lit += p.rect.r - p.rect.l;
    }
    return all > 0.0 ? lit / all : -1.0;
}

// The text drawn right after `label` (a deck line's value), "" without one
std::string after(const std::vector<PluginHost::StringRow>& rows, const char* label) {
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        if (rows[i].text == label) return rows[i + 1].text;
    }
    return "";
}

std::string joined(const std::vector<PluginHost::StringRow>& rows) {
    std::string out;
    for (const auto& r : rows) out += "[" + r.text + "]";
    return out;
}

// A shipped pack folder (ini and images) of one pack type, staged through the
// user folder (under another name with `as`).
void stagePack(const std::string& save, const char* type, const char* folder, const char* as = nullptr) {
    CreateDirectoryA((save + "mxbmrp3\\" + type).c_str(), nullptr);
    const std::string dir = save + "mxbmrp3\\" + type + "\\" + (as ? as : folder) + "\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    const std::string from = std::string(MXB_REPO_DATA_DIR "/") + type + "/" + folder + "/";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((from + "*").c_str(), &fd);
    REQUIRE_MESSAGE(h != INVALID_HANDLE_VALUE, "shipped pack missing: " << from);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        REQUIRE(CopyFileA((from + fd.cFileName).c_str(), (dir + fd.cFileName).c_str(), FALSE) != 0);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// The shipped Cabinet Dark theme, which holds the shipped deck layouts (Cabinet
// in decks.ini, Slant in decks-slanted.ini), staged through the user-asset
// folder the plugin syncs at Startup (a test run discovers no repo data of its
// own). The global theme stays None, whose decks draw Cabinet Dark's layouts.
void stageCabinet(const std::string& save) {
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    stagePack(save, "themes", "cabinet-dark");
}

// The test layouts written so far, by theme name: true when slanted
std::map<std::string, bool>& testLayouts() {
    static std::map<std::string, bool> layouts;
    return layouts;
}

// The Decks switch ships off (a new install draws none): every case here
// starts with it on, as a player who switched the decks on has it. The
// default case below starts the plain way.
int startDecks(PluginHost& host, const char* save) {
    const int r = host.startup(save);
    host.setMasterEnabled(PluginHost::Master::Decks, true);
    return r;
}

void writeFile(const std::string& path, const char* text);

// A test deck layout: `dir` (themes\<name>\) as a skin of Cabinet Dark with no
// art of its own, holding `ini` as its decks.ini -- or as decks-slanted.ini
// when it builds on a slanted layout. `base = cabinet` and `base = slant` in
// `ini` read as Cabinet Dark's Regular and Slanted. deckIni(name) selects it.
void writeLayout(const std::string& dir, std::string ini) {
    std::string name = dir.substr(0, dir.size() - 1);
    name = name.substr(name.find_last_of('\\') + 1);
    bool slanted = false;
    const size_t at = ini.find("base = ");
    if (at != std::string::npos) {
        const size_t from = at + 7, end = ini.find('\n', from);
        std::string base = ini.substr(from, end - from);
        if (base == "slant") slanted = true;
        else if (const auto it = testLayouts().find(base); it != testLayouts().end()) slanted = it->second;
        if (base == "cabinet" || base == "slant") base = "cabinet-dark";
        ini.replace(from, end - from, base);
    }
    testLayouts()[name] = slanted;
    writeFile(dir + "theme.ini", "[pack]\nbase = cabinet-dark\n");
    writeFile(dir + (slanted ? "decks-slanted.ini" : "decks.ini"), ini.c_str());
}

// The settings that draw the decks in a layout: "cabinet" and "slant" are
// Cabinet Dark's two under Theme None, "carbon", "modern" and "retro" their
// dark theme's Regular, a test layout its own theme's; anything else is
// written as the Slanted value, as a hand-edited file would hold it.
std::string deckIni(const std::string& style) {
    if (style == "cabinet") return "[Decks]\nslanted=0\n";
    if (style == "slant") return "[Decks]\nslanted=1\n";
    if (style == "carbon" || style == "modern" || style == "retro")
        return "[Display]\npanelTheme=" + style + "-dark\n[Decks]\nslanted=0\n";
    if (const auto it = testLayouts().find(style); it != testLayouts().end())
        return "[Display]\npanelTheme=" + style + "\n[Decks]\nslanted=" + (it->second ? "1" : "0") + "\n";
    return "[Decks]\nslanted=" + style + "\n";
}

// The faces the shipped styles name, staged as stageCabinet stages its pack
void stageFonts(const std::string& save) {
    CreateDirectoryA((save + "mxbmrp3\\fonts").c_str(), nullptr);
    for (const char* font : kStagedFonts) {
        const std::string name = std::string(font) + ".fnt";
        REQUIRE_MESSAGE(CopyFileA((std::string(MXB_REPO_DATA_DIR "/fonts/") + name).c_str(),
                                  (save + "mxbmrp3\\fonts\\" + name).c_str(), FALSE) != 0,
                        "shipped font missing: " << name);
    }
}

void writeFile(const std::string& path, const char* text) {
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    fputs(text, f);
    fclose(f);
}

// A bike with a rev range: red from 11,700, the limiter at 13,000. The
// Speed deck's meter draws only what the game says the range is.
constexpr int SHIFT_RPM = 11700, LIMITER_RPM = 13000;

// A five-lap race, the player second of two on lap 2, riding in third gear.
void openRace(PluginHost& host, bool revRange = true) {
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 0.0f,
                   revRange ? SHIFT_RPM : 0, revRange ? LIMITER_RPM : 0, revRange ? LIMITER_RPM : 0);
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.runInit(RACE);
    host.classify(RACE, 100000, {
        { .num = 22, .best = 90000, .laps = 1, .gap = 0 },
        { .num = PLAYER, .best = 91000, .laps = 1, .gap = 1500 },
    });
    TelemetryRow t;
    t.time = 1.0f;
    t.gear = 3;
    t.speed = 20.0f;   // m/s: 72 km/h, 45 mph
    t.rpm = 6000;
    host.telemetryFrame(t);
    host.draw();
    host.draw();
}

// fmx_test's 100 Hz ride on the injected FMX clock, each frame drawn; with
// `motionClock` the Motion clock steps with it (the Combo chips rise on it)
struct FmxRide {
    PluginHost& host;
    bool motionClock = false;
    long long t = 1'000'000;
    float x = 0.0f;
    void tick(TelemetryRow r) {
        t += 10'000;
        x += r.speed * 0.01f;
        r.posX = x;
        r.time = static_cast<float>(t) / 1.0e6f;
        host.fmxSetNowUs(t);
        if (motionClock) host.setMotionNowUs(t);
        host.telemetryFrame(r);
        host.draw();
    }
    void ground(int n) { for (int i = 0; i < n; ++i) tick(TelemetryRow{}); }
};

// One 10 ms telemetry sample heading north and travelling `pathDeg`
// clockwise of it, the deck's clock moved on with it. The speed wobbles by a
// hair: the deck steps only on a FRESH sample, as the game's are.
struct SlideRide {
    PluginHost& host;
    long long now = 1'000'000;
    int frame = 0;
    void operator()(float pathDeg) {
        TelemetryRow row;
        row.speed = 15.0f + 0.001f * static_cast<float>(frame++ % 7);
        const float rad = pathDeg * 3.14159265f / 180.0f;
        row.velX = row.speed * std::sin(rad);
        row.velZ = row.speed * std::cos(rad);
        host.telemetryFrame(row);
        now += 10000;
        host.setMotionNowUs(now);
        host.drawWithState(0);
    }
};

// Two telemetry samples at about `speed` m/s, 0.1 s apart (they differ: the
// first only sets the clock), for the Timing deck's speed trap
struct TrapRide {
    PluginHost& host;
    float clock = 2.0f;
    void operator()(float speed) {
        for (int i = 0; i < 2; ++i) {
            TelemetryRow t;
            t.speed = speed - 1.0f + static_cast<float>(i);
            t.time = clock += 0.1f;
            host.telemetryFrame(t);
            host.drawWithState(0);
        }
    }
};

// Both riders still on the grid, classified in `state` (the gate's states)
void grid(PluginHost& host, int state, int sessionMs = 0) {
    host.classify(RACE, sessionMs, {
        { .num = 22, .laps = 0, .gap = 0 },
        { .num = PLAYER, .laps = 0, .gap = 0 },
    }, state);
}

// A quad's signed area (twice it): its sign is its winding, which the game
// needs to be the panel's
float quadArea(PluginHost& host, const char* id, int i) {
    float x[4] = {}, y[4] = {};
    for (int v = 0; v < 4; ++v) REQUIRE(host.quadVertex(id, i, v, &x[v], &y[v]));
    float a = 0.0f;
    for (int v = 0; v < 4; ++v) a += x[v] * y[(v + 1) % 4] - x[(v + 1) % 4] * y[v];
    return a;
}

}  // namespace

// The starter set is on, the rest off, and the Decks switch, off, holds them
// all back (default_positions_test has where they open).
TEST_CASE("decks: only the starter set is on by default, under the Decks switch") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_off\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(save.c_str());
    openRace(host);
    for (const char* deck : kDeckIds) {
        const std::string id = deck;
        const bool starter = id == "timing_deck" || id == "race_deck" || id == "speed_deck" ||
                             id == "fuel_deck" || id == "carousel_deck";
        CHECK_MESSAGE(host.hudVisible(deck) == (starter ? 1 : 0), deck << " has the wrong default");
        CHECK(host.hudHeldBack(deck) == 1);
    }
    host.shutdown();
}

// The Carousel is a starter deck, and its turn skips Timing, a starter deck
// already on screen beside it: one click on Decks shows each readout once
// (Thomas, option A). The marker is the only key set, so the turn is the default.
TEST_CASE("decks: the carousel's default turn leaves out Timing") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_default\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[CarouselDeck]\nauto=0\nmarker=count\nnoticeFocus=0\n" +
                                         deckIni("cabinet"));
    startDecks(host, save.c_str());
    openRace(host);
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    REQUIRE_MESSAGE(has(rows(), "GAP 1/6"), joined(rows()));
    CHECK(host.hudVisible("timing_deck") == 1);
    // A whole turn round never comes to Timing
    for (int i = 0; i < 6; ++i) {
        REQUIRE(host.injectHotkey("deck_next"));
        host.draw();
        CHECK_FALSE_MESSAGE(has(rows(), "TIMING 1/6"), joined(rows()));
    }
    CHECK_MESSAGE(has(rows(), "GAP 1/6"), joined(rows()));
    host.shutdown();
}

TEST_CASE("decks: the Cabinet style draws what the Speed and Race decks gather") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_on\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // A hand-edited Slanted value that is not 1: the decks must still draw,
    // in the Regular layout.
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n"
        "[SpeedDeck]\nvisible=1\n"
        "[RaceDeck]\nvisible=1\n" + deckIni("Nope"));
    startDecks(host, save.c_str());
    openRace(host);

    const auto deck = host.hudStringRows("speed_deck");
    CHECK_MESSAGE(has(deck, "SPEED"), joined(deck));
    // The gear, then the speed over its unit
    const bool kmh = hasPair(deck, "3", "72") && hasPair(deck, "72", "KM/H");
    const bool mph = hasPair(deck, "3", "45") && hasPair(deck, "45", "MPH");
    CHECK_MESSAGE((kmh || mph), "gear, speed over its unit: " << joined(deck));
    CHECK_MESSAGE(meterBlocks(host, "speed_deck") == 16, "the rev meter's blocks: " << meterBlocks(host, "speed_deck"));

    const auto race = host.hudStringRows("race_deck");
    CHECK_MESSAGE(has(race, "RACE"), "an unknown style must fall back to Cabinet: " << joined(race));
    // Cabinet draws "2/2" and "2/5" with a smaller tail: head and tail are two strings
    CHECK_MESSAGE(hasPair(race, "2", "/2"), "second of two: " << joined(race));
    CHECK_MESSAGE(hasPair(race, "2", "/5"), "on lap two of a five-lap race: " << joined(race));
    CHECK_MESSAGE(has(race, "RACE 1"), "the session under the place: " << joined(race));
    host.shutdown();
}

// Cabinet's deck background is `art = panel`: the theme's own frame, so a deck
// sits in the same chrome as every HUD, and without a panel theme it draws one
// plain fill. A pack painting its own box instead would pass every string
// check and simply ignore the theme.
TEST_CASE("decks: a deck draws on the theme's panel") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_theme\\";
    stageCabinet(save);
    // The Speed deck's quad count under a nine-slice panel theme, or none
    const auto quadsUnder = [&](bool themed) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasThemeGeometry());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.installTheme("deckframe", 2.0f, 1.0f, /*titleBand=*/0, /*card=*/0);
        if (!themed) host.clearTheme();
        openRace(host);
        const size_t n = host.hudQuadRects("speed_deck").size();
        host.shutdown();
        return n;
    };
    const size_t themed = quadsUnder(true);
    const size_t plain = quadsUnder(false);
    // Nine slices against one plain fill: the rest of the deck is the same
    CHECK_MESSAGE(themed == plain + 8, "themed " << themed << " vs no theme " << plain);
}

// Every shipped deck layout (deckIni): Cabinet Dark's two, then the dark
// themes holding their own Regular
const char* const kStyles[] = { "cabinet", "slant", "carbon", "modern", "retro" };

void stageStyles(const std::string& save) {
    stageCabinet(save);
    for (const char* theme : { "carbon-dark", "modern-dark", "retro-dark" }) stagePack(save, "themes", theme);
}

// The decks line up with the classic HUDs: Cabinet sizes every box in whole
// grid cells, a cell under the deck's top (the name tab hangs in it), so a deck
// on a grid line has all four box edges on grid lines -- and keeps them when
// the font size moves the lattice (DeckHud::lx/ly scale with the live cell).
// Slant keeps Cabinet's heights and left edge; its lean's tip overhangs the
// right (slant/deck.ini), so only its width is not whole cells. Its lean over
// one deck's height IS whole cells (two), so a deck snapped one down and two
// columns left carries the diagonal straight on: at 12 degrees it was 2.56
// cells, and a snapped stack showed a 5 px notch at every seam.
// Fails as it did before: a 208 x 112 px box is 19.7 x 9.4 cells.
TEST_CASE("decks: deck_grid_test, every box is whole grid cells at any font size") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_grid\\";
    stageCabinet(save);
    std::vector<double> cabinetCells;   // each deck's width in cells on Cabinet, by font, in kDeckIds order
    for (const std::string style : { "cabinet", "slant" }) {   // Cabinet first: Slant leans from it
        INFO("style " << style);
        const bool lean = style == "slant";
        size_t at = 0;
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasUiFontSize());
        std::string ini = "[Settings]\nversion=11\n";
        for (const char* section : kDeckSections)
            ini += std::string("[") + section + "]\nvisible=1\n" + quietCarousel(section);
        ini += deckIni(style);
        host.writeSettingsFile(save.c_str(), ini.c_str());
        startDecks(host, save.c_str());
        openRace(host);
        for (const float font : { 0.02f, 0.026f }) {
            host.setUiFontSize(font);
            host.draw();
            const double cellW = font * 0.275, cellH = font * 1.1 / 2.0;
            const auto cells = [](double v, double cell) { return v / cell; };
            const auto whole = [](double c) { return std::abs(c - std::round(c)) < 0.02; };
            for (const char* id : kDeckIds) {
                if (host.hudVisible(id) < 0) continue;   // Combo: not on this game
                const std::string deck = id;
                const auto e = host.hudScreenEdges(id);
                const double l = e.l / 1e6, w = (e.r - e.l) / 1e6, h = (e.b - e.t) / 1e6;
                // Where a deck sits is the player's (a font change does not move
                // it); only its default place is the deck's, on the default lattice
                if (font == 0.02f)
                    CHECK_MESSAGE(whole(cells(l, cellW)), deck << ": left at " << cells(l, cellW) << " cells");
                if (!lean) {
                    CHECK_MESSAGE(whole(cells(w, cellW)), deck << " @" << font << ": " << cells(w, cellW) << " cells wide");
                    cabinetCells.push_back(cells(w, cellW));
                } else if (std::round(cells(h, cellH)) == 10.0) {   // a card deck: its card is h less the tab cell
                    REQUIRE(at < cabinetCells.size());
                    const double over = cells(w, cellW) - cabinetCells[at];   // the lean's run over the card
                    const double perDeck = over * cells(h, cellH) / (cells(h, cellH) - 1.0);
                    CHECK_MESSAGE(whole(perDeck), deck << " @" << font << ": leans " << perDeck << " cells per deck");
                }
                ++at;
                CHECK_MESSAGE(whole(cells(h, cellH)), deck << " @" << font << ": " << cells(h, cellH) << " cells tall");
            }
        }
        host.shutdown();
    }
}

TEST_CASE("decks: a style over Cabinet is picked by name and its keys win") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_skin\\";
    stageCabinet(save);
    const std::string dir = save + "mxbmrp3\\themes\\wide\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    writeLayout(dir, "[pack]\nbase = cabinet\n\n[speed]\nsize = 40, 10\n");

    // The Speed deck on the skin is twice as wide as on Cabinet.
    PluginHost cab(dllPath());
    REQUIRE(cab.loaded());
    cab.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n");
    cab.startup(save.c_str());
    openRace(cab);
    const auto narrow = cab.hudScreenEdges("speed_deck");
    cab.shutdown();

    PluginHost wide(dllPath());
    REQUIRE(wide.loaded());
    wide.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n" + deckIni("wide"));
    wide.startup(save.c_str());
    openRace(wide);
    const auto broad = wide.hudScreenEdges("speed_deck");
    const auto rows = wide.hudStringRows("speed_deck");
    wide.shutdown();

    REQUIRE(narrow.r > narrow.l);
    REQUIRE(broad.r > broad.l);
    CHECK(static_cast<double>(broad.r - broad.l) ==
          doctest::Approx(2.0 * static_cast<double>(narrow.r - narrow.l)).epsilon(0.02));
    CHECK(broad.b - broad.t == narrow.b - narrow.t);
    // The skin states only a size: everything else is still Cabinet's
    CHECK_MESSAGE(has(rows, "SPEED"), joined(rows));
}

// A section can be NAMED ([speed-box-back]) instead of numbered, and the
// file's order is the drawing order either way, so a box goes in between two
// others by being written between them. A skin's section of a name its base
// has changes that box in its place; a new name comes after the base's. The
// colours mark each box among the quads the deck draws. A field or meter of
// another deck is ignored (deckField / deckMeter hold each deck to its own),
// and so is a name too long for a section.
TEST_CASE("decks: named sections draw in the file's order, and a skin changes or adds by name") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_named\\";
    stageCabinet(save);
    const auto pack = [&](const char* name, const char* ini) {
        const std::string dir = save + "mxbmrp3\\themes\\" + name + "\\";
        CreateDirectoryA(dir.c_str(), nullptr);
        writeLayout(dir, ini);
    };
    pack("numbered", "[speed]\nsize = 20, 10\n"
                     "[speed-box-1]\nrect = 0 0 20 10\ncolor = #ff0000\n"
                     "[speed-box-2]\nrect = 1 1 5 5\ncolor = #00ff00\n");
    pack("named", "[speed]\nsize = 20, 10\n"
                  "[speed-box-back]\nrect = 0 0 20 10\ncolor = #ff0000\n"
                  "[speed-box-mid]\nrect = 2 2 4 4\ncolor = #0000ff\n"
                  "[speed-box-front]\nrect = 1 1 5 5\ncolor = #00ff00\n");
    pack("skin", "[pack]\nbase = named\n\n"
                 "[speed-box-back]\ncolor = #ffff00\n"
                 "[speed-box-top]\nrect = 3 3 2 2\ncolor = #ff00ff\n");
    // Another deck's field and meter, and a name longer than a section takes:
    // each ignored. The box keeps drawing (`when = row-name` would hide it,
    // the Speed deck never filling a row), the cyan meter stays the revs
    // (`show = progress` would take the Race deck's, which the Speed deck
    // never fills, and draw nothing), and the long name places nothing.
    pack("foreign", "[speed]\nsize = 20, 10\n"
                    "[speed-box-1]\nrect = 0 0 20 10\ncolor = #ff0000\nwhen = row-name\n"
                    "[speed-box-abcdefghijklmnopqrstuvwxyz]\nrect = 1 1 5 5\ncolor = #0000ff\n"
                    "[speed-meter-1]\nshow = rpm\nshow = progress\nkind = bar\nrect = 1 8 17 0.5\n"
                    "color = #00ffff\ndim-color = #00ffff\n");

    // The deck's boxes as their colours (#rrggbb), in drawing order
    const auto boxes = [&](const char* style) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasInkHooks());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n") + deckIni(style));
        startDecks(host, save.c_str());
        openRace(host);
        std::vector<std::string> out;
        const int count = static_cast<int>(host.hudQuadRects("speed_deck").size());
        for (int i = 0; i < count; ++i) {
            const unsigned long abgr = host.quadColor("speed_deck", i);
            char rgb[8];
            snprintf(rgb, sizeof(rgb), "#%02lx%02lx%02lx", abgr & 0xff, (abgr >> 8) & 0xff, (abgr >> 16) & 0xff);
            out.push_back(rgb);
        }
        host.shutdown();
        return out;
    };
    using V = std::vector<std::string>;
    CHECK(boxes("numbered") == V{ "#ff0000", "#00ff00" });
    CHECK(boxes("named") == V{ "#ff0000", "#0000ff", "#00ff00" });
    CHECK(boxes("skin") == V{ "#ffff00", "#0000ff", "#00ff00", "#ff00ff" });
    const V foreign = boxes("foreign");
    INFO("foreign: " << foreign.size() << " quads");
    CHECK((!foreign.empty() && foreign.front() == "#ff0000"));
    CHECK(std::count(foreign.begin(), foreign.end(), "#0000ff") == 0);
    CHECK(std::count(foreign.begin(), foreign.end(), "#00ffff") > 0);
}

// How many of a deck's quads draw a texture (art = image, or theme art).
int texturedQuads(PluginHost& host, const char* deck) {
    int n = 0;
    const int count = static_cast<int>(host.hudQuadRects(deck).size());
    for (int i = 0; i < count; ++i) n += host.quadSprite(deck, i) > 0 ? 1 : 0;
    return n;
}

// The lean is painted into IMAGES (`art = image`): the game draws a quad's
// edges hard, so a slanted solid box steps along its diagonal. Each image is
// registered as a sprite after the gauges; a registration that skews the
// order draws another asset's art in its place, silently, so the order is
// checked too, with a gauges pack staged so every block registered before the
// deck images is non-empty (a block left out of the count would otherwise
// shift nothing). An image the layout's theme does not have draws a plain box
// instead of nothing: "typo" is "uncapped" (Slant with the card's ends turned
// off, so the card is its picture under any theme) naming a picture no theme
// has, drawn in the same theme art, so it draws as many quads, one fewer textured.
TEST_CASE("decks: Slant draws its images") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_image\\";
    stageCabinet(save);
    stagePack(save, "gauges", "classic");
    for (const auto& [name, ini] : { std::pair<const char*, const char*>{ "uncapped", "[pack]\nbase = slant\n\n[speed-box-1]\ncaps = 0\n" },
                                     { "typo", "[pack]\nbase = uncapped\n\n[speed-box-1]\nimage = nope\n" } }) {
        const std::string dir = save + "mxbmrp3\\themes\\" + name + "\\";
        CreateDirectoryA(dir.c_str(), nullptr);
        writeLayout(dir, ini);
    }

    struct Frame { int textured = 0; size_t quads = 0; };
    const auto frame = [&](const char* style) {
        INFO("style " << style);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasQuadSprite());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n") + deckIni(style));
        startDecks(host, save.c_str());
        openRace(host);
        CHECK(host.spriteOrderMismatches() == 0);
        const auto rows = host.hudStringRows("speed_deck");
        CHECK_MESSAGE(has(rows, "SPEED"), joined(rows));
        Frame f;
        f.textured = texturedQuads(host, "speed_deck");
        f.quads = host.hudQuadRects("speed_deck").size();
        host.shutdown();
        return f;
    };
    CHECK(frame("slant").textured >= 4);   // card, frame or rim, divider, tab
    const Frame uncapped = frame("uncapped"), typo = frame("typo");
    CHECK(uncapped.textured >= 4);
    CHECK(typo.quads == uncapped.quads);
    CHECK(typo.textured == uncapped.textured - 1);
}

// A theme draws the decks' pictures: deck_<stem>.tga in the theme's folder
// (or its base theme's) replaces the pack's <stem>.tga while that theme is on,
// so one Slant pack carries every theme's look. Each theme's copy is its own
// sprite: the Speed deck's quads then name a different sprite under a theme
// that has one, the same under a skin theme as under its base, and the pack's
// own under a theme that has none. Before, every theme drew the pack's picture.
TEST_CASE("decks: a theme's deck_ image replaces the pack's") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_themed\\";
    stageCabinet(save);
    const std::string themes = save + "mxbmrp3\\themes\\";
    CreateDirectoryA(themes.c_str(), nullptr);
    const auto tga = [](const std::string& path, int w, int h) {
        unsigned char header[18] = {};
        header[2] = 2;
        header[12] = static_cast<unsigned char>(w & 0xff);
        header[13] = static_cast<unsigned char>(w >> 8);
        header[14] = static_cast<unsigned char>(h & 0xff);
        header[15] = static_cast<unsigned char>(h >> 8);
        header[16] = 24;
        FILE* f = fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        fwrite(header, 1, sizeof(header), f);
        fclose(f);
    };
    stagePack(save, "themes", "carbon-dark");
    stagePack(save, "themes", "carbon-light");
    // carbon-dark gains a Slant card; skinned is carbon-dark with no files of its own
    tga(themes + "carbon-dark\\deck_slant_230x112.tga", 230, 112);
    CreateDirectoryA((themes + "skinned").c_str(), nullptr);
    writeFile(themes + "skinned\\theme.ini", "[pack]\nbase = carbon-dark\n");
    // hidden is skinned with a 1x1 name tab: a 1x1 theme picture hides the pack's
    CreateDirectoryA((themes + "hidden").c_str(), nullptr);
    writeFile(themes + "hidden\\theme.ini", "[pack]\nbase = carbon-dark\n");
    tga(themes + "hidden\\deck_tab.tga", 1, 1);
    // welled paints Slant's 1x1 Speed well, which draws only where a theme paints it
    CreateDirectoryA((themes + "welled").c_str(), nullptr);
    writeFile(themes + "welled\\theme.ini", "[pack]\nbase = carbon-dark\n");
    tga(themes + "welled\\deck_well_198x10.tga", 198, 10);

    const auto sprites = [&](const char* theme) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasQuadSprite());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[Display]\npanelTheme=") + theme +
                                             "\n[SpeedDeck]\nvisible=1\n" + deckIni("slant"));
        startDecks(host, save.c_str());
        openRace(host);
        CHECK(host.spriteOrderMismatches() == 0);
        std::vector<int> out;
        const int count = static_cast<int>(host.hudQuadRects("speed_deck").size());
        for (int i = 0; i < count; ++i) out.push_back(host.quadSprite("speed_deck", i));
        host.shutdown();
        return out;
    };
    const std::vector<int> own = sprites("carbon-dark");
    const std::vector<int> plain = sprites("carbon-light");
    const std::vector<int> skinned = sprites("skinned");
    REQUIRE(!own.empty());
    CHECK_MESSAGE(own.size() == plain.size(), "the theme's picture changes no geometry");
    CHECK_MESSAGE(own != plain, "carbon-dark's deck_slant_230x112 was not drawn");
    CHECK_MESSAGE(skinned == own, "a skin theme draws its base theme's deck picture");
    CHECK_MESSAGE(sprites("hidden").size() < skinned.size(), "a 1x1 deck_tab hides the pack's tab");
    CHECK_MESSAGE(sprites("welled").size() > skinned.size(), "a theme's deck_well_198x10 shows the pack's 1x1 well");
}

// Each theme draws its decks in its own layout: its folder's decks.ini, else
// its base theme's, else Cabinet Dark's, and decks-slanted.ini the same way
// for the Slanted Style. Each theme here is set beside two skins of it that
// draw its art: "-skin" holds no layout (so draws the theme's own) and "-cab"
// holds one built on Cabinet Dark's (so draws Cabinet in the theme's art).
// Before, every theme drew Cabinet's boxes in its colours. The Retro layout
// shows the two pieces of theme art it adds: the name tab in the theme's
// title bar (art = title, its nine slices) and each meter sunk in the theme's
// card (a well), so it draws more themed quads than Cabinet in the same art.
TEST_CASE("decks: a deck follows its theme's own layout, Regular or Slanted") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_theme_style\\";
    stageStyles(save);
    const char* const themes[] = { "carbon-dark", "modern-light", "retro-dark", "retro-light" };
    for (const char* theme : themes) {
        stagePack(save, "themes", theme);
        for (const char* kind : { "-skin", "-cab" }) {
            const std::string dir = save + "mxbmrp3\\themes\\" + theme + kind + "\\";
            CreateDirectoryA(dir.c_str(), nullptr);
            writeFile(dir + "theme.ini", (std::string("[pack]\nbase = ") + theme + "\n").c_str());
            if (std::string(kind) == "-cab") writeFile(dir + "decks.ini", "[pack]\nbase = cabinet-dark\n");
        }
    }

    struct Frame { std::vector<int> sprites; size_t strings = 0; int textured = 0; };
    const auto frame = [&](const std::string& theme, const char* style) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasQuadSprite());
        std::string ini = "[Settings]\nversion=11\n";
        if (!theme.empty()) ini += "[Display]\npanelTheme=" + theme + "\n";
        ini += "[SpeedDeck]\nvisible=1\n";
        if (style) ini += deckIni(style);
        host.writeSettingsFile(save.c_str(), ini);
        startDecks(host, save.c_str());
        openRace(host);
        CHECK(host.spriteOrderMismatches() == 0);
        Frame f;
        const int count = static_cast<int>(host.hudQuadRects("speed_deck").size());
        for (int i = 0; i < count; ++i) f.sprites.push_back(host.quadSprite("speed_deck", i));
        f.textured = texturedQuads(host, "speed_deck");
        f.strings = host.hudStringRows("speed_deck").size();
        host.shutdown();
        return f;
    };
    const auto same = [](const Frame& a, const Frame& b) { return a.sprites == b.sprites && a.strings == b.strings; };

    // No theme: Regular is the default Style, and Slanted draws otherwise
    CHECK(same(frame("", nullptr), frame("", "cabinet")));
    CHECK_FALSE(same(frame("", nullptr), frame("", "slant")));
    for (const char* theme : themes) {
        INFO("theme " << theme);
        const std::string skin = std::string(theme) + "-skin", cab = std::string(theme) + "-cab";
        const Frame own = frame(theme, nullptr);
        CHECK(same(own, frame(skin, nullptr)));
        CHECK_FALSE(same(own, frame(cab, nullptr)));
        // None of them has a Slanted layout: each draws Cabinet Dark's
        const Frame slanted = frame(theme, "slant");
        CHECK(same(slanted, frame(skin, "slant")));
        CHECK(same(slanted, frame(cab, "slant")));
    }
    // Retro's title-bar tab and meter well are the theme's art
    CHECK(frame("retro-dark", nullptr).textured > frame("retro-dark-cab", nullptr).textured);
}

// Where a layout finds its pictures and where a theme without one finds its
// layout, each pinned by a case that fails without its own line:
// - a Slanted layout on `base = regular` looks for its pictures down the
//   theme's Regular chain too: "chained" names one of Cabinet Dark's, and
//   draws one textured quad more than "missing", which names none;
// - a theme's own copy of a picture its layout already draws reuses that
//   sprite (no picture is registered twice in a layout);
// - a theme with no layout, itself or its base (Cabinet Light), draws Cabinet
//   Dark's, not whichever layout is discovered first ("aaa", a standalone
//   theme with a whole layout of its own whose Speed deck is twice as wide).
TEST_CASE("decks: a layout's pictures and a layoutless theme's layout follow the theme chain") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_chain\\";
    stageCabinet(save);
    stagePack(save, "themes", "cabinet-light");
    const std::string themes = save + "mxbmrp3\\themes\\";
    for (const auto& [name, image] : { std::pair<const char*, const char*>{ "chained", "slant_230x112" }, { "missing", "nope" } }) {
        const std::string dir = themes + name + "\\";
        CreateDirectoryA(dir.c_str(), nullptr);
        writeFile(dir + "theme.ini", "[pack]\nbase = cabinet-dark\n");
        writeFile(dir + "decks.ini", "[pack]\nbase = cabinet-dark\n");
        writeFile(dir + "decks-slanted.ini",
                  (std::string("[pack]\nbase = regular\n\n[speed-box-1]\nart = image\nimage = ") + image + "\n").c_str());
    }
    {
        // aaa: Cabinet Dark's Regular, whole (no base), with a 40-column Speed deck
        FILE* in = fopen((std::string(MXB_REPO_DATA_DIR) + "/themes/cabinet-dark/decks.ini").c_str(), "rb");
        REQUIRE(in != nullptr);
        std::string ini;
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), in)) > 0;) ini.append(buf, n);
        fclose(in);
        const size_t at = ini.find("[speed]\nsize = 20, 10");
        REQUIRE(at != std::string::npos);
        ini.replace(at, 22, "[speed]\nsize = 40, 10");
        // a standalone theme (Cabinet Light's art), so it is discovered first
        stagePack(save, "themes", "cabinet-light", "aaa");
        writeFile(themes + "aaa\\decks.ini", ini.c_str());
    }
    struct Frame { int textured = 0; double width = 0; size_t quads = 0; int dups = -1; };
    const auto frame = [&](const char* theme, bool slanted) {
        INFO("theme " << theme << (slanted ? " slanted" : ""));
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[Display]\npanelTheme=") + theme +
                                             "\n[SpeedDeck]\nvisible=1\n[Decks]\nslanted=" + (slanted ? "1" : "0") + "\n");
        startDecks(host, save.c_str());
        openRace(host);
        Frame f;
        f.dups = host.deckImageDuplicates();
        f.textured = texturedQuads(host, "speed_deck");
        const auto rects = host.hudQuadRects("speed_deck");
        f.quads = rects.size();
        const auto edges = host.hudScreenEdges("speed_deck");
        f.width = static_cast<double>(edges.r - edges.l);
        host.shutdown();
        return f;
    };
    const Frame chained = frame("chained", true), missing = frame("missing", true);
    CHECK(chained.dups == 0);
    CHECK(chained.quads == missing.quads);
    CHECK(chained.textured == missing.textured + 1);
    const Frame cabinet = frame("cabinet-dark", false);
    REQUIRE(cabinet.width > 0);
    CHECK(frame("aaa", false).width == doctest::Approx(2.0 * cabinet.width).epsilon(0.02));
    CHECK(frame("cabinet-light", false).width == doctest::Approx(cabinet.width));
}

// The game draws a quad's edges hard, so a leaning solid quad (a meter's block,
// bar, column) stepped along its slant: on the Speed deck's rpm blocks most of
// all. Each one now draws as an upright core plus a wedge per slanted side,
// textured with a slant_edge ramp (tools/edge_gen.py), so its lean comes out
// smooth on every renderer. Checked: with the ramps installed every leaning
// part gains exactly its two wedges (16 rpm blocks: 32 quads more, all
// textured) and every quad -- each wedge a TURNED copy of one picture -- keeps
// the panel's winding, which the game requires; without them it draws as
// before, one quad a part.
TEST_CASE("decks: a leaning meter part draws smooth: a core and a textured wedge per side") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_edges\\";
    stageCabinet(save);

    struct Frame { int quads = 0, textured = 0, against = 0; };
    const auto frame = [&](const char* id, const char* section) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasQuadSprite());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[") + section +
                                             "]\nvisible=1\n" + deckIni("slant"));
        startDecks(host, save.c_str());
        openRace(host);
        Frame f;
        f.quads = static_cast<int>(host.hudQuadRects(id).size());
        f.textured = texturedQuads(host, id);
        const float panel = quadArea(host, id, 0);
        REQUIRE(panel != 0.0f);
        for (int i = 1; i < f.quads; ++i) {
            const float a = quadArea(host, id, i);
            if (a != 0.0f && (a > 0.0f) != (panel > 0.0f)) ++f.against;
        }
        host.shutdown();
        return f;
    };

    const Frame hard = frame("speed_deck", "SpeedDeck");
    const Frame inputsHard = frame("inputs_deck", "InputsDeck");
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    for (int n = 1; n <= 4; ++n) {
        const std::string name = "slant_edge_" + std::to_string(n) + ".tga";
        REQUIRE_MESSAGE(CopyFileA((std::string(MXB_REPO_DATA_DIR "/textures/") + name).c_str(),
                                  (save + "mxbmrp3\\textures\\" + name).c_str(), FALSE) != 0,
                        "could not stage " << name << " - the case would be vacuous");
    }
    const Frame smooth = frame("speed_deck", "SpeedDeck");
    const Frame inputsSmooth = frame("inputs_deck", "InputsDeck");

    CHECK(smooth.quads - hard.quads == 32);
    CHECK(smooth.textured - hard.textured == 32);
    // The Inputs columns (a wider lean, another ramp): each part its two wedges
    CHECK(inputsSmooth.quads > inputsHard.quads);
    CHECK((inputsSmooth.quads - inputsHard.quads) % 2 == 0);
    CHECK(inputsSmooth.textured - inputsHard.textured == inputsSmooth.quads - inputsHard.quads);
    for (const Frame& f : { hard, smooth, inputsHard, inputsSmooth })
        CHECK_MESSAGE(f.against == 0, f.against << " of " << f.quads << " quads wound against the panel");
}

// Which way round each wedge is, which counting cannot see: a wedge drawn
// turned the wrong way has its ramp on the outside of the slant, a notch where
// the lean should be, with the same quads and textures. Each wedge's corners
// are listed from its picture's TL, so corner 0 is the ramp's clear end and
// corner 2 its solid one, and the ramp's edge runs from corner 1 to 3. For a
// part leaning either way (the Slant pack, and a skin leaning it back), every
// wedge must have its solid end toward the part's core and its edge leaning
// with the part. And blocks with no gap between them (a skin closing them up)
// share their sides: only the strip's two ends are smoothed, so exactly two
// wedges, where smoothing every side would draw two a block.
TEST_CASE("decks: each wedge faces its core and leans with its part") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_wedges\\";
    stageCabinet(save);
    for (const auto& [name, ini] : { std::pair<const char*, const char*>{ "back", "[speed-meter-1]\nskew = -0.129\n" },
                                     { "closed", "[speed-meter-1]\ngap = 0\n" } }) {
        const std::string dir = save + "mxbmrp3\\themes\\" + name + "\\";
        CreateDirectoryA(dir.c_str(), nullptr);
        writeLayout(dir, (std::string("[pack]\nbase = slant\n\n") + ini).c_str());
    }
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    for (int n = 1; n <= 4; ++n) {
        const std::string name = "slant_edge_" + std::to_string(n) + ".tga";
        REQUIRE(CopyFileA((std::string(MXB_REPO_DATA_DIR "/textures/") + name).c_str(),
                          (save + "mxbmrp3\\textures\\" + name).c_str(), FALSE) != 0);
    }

    struct Wedge { float x[4], y[4]; float coreMidX; };
    // The Speed deck's wedges (quads drawn in a ramp, a sprite no other quad
    // uses) with the middle of the solid quad drawn beside each.
    const auto wedges = [&](const char* pack) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasQuadSprite());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n") + deckIni(pack));
        startDecks(host, save.c_str());
        openRace(host);
        const char* id = "speed_deck";
        const int count = static_cast<int>(host.hudQuadRects(id).size());
        std::vector<int> sprite(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) sprite[static_cast<size_t>(i)] = host.quadSprite(id, i);
        const auto corners = [&](int i, float* x, float* y) {
            for (int v = 0; v < 4; ++v) REQUIRE(host.quadVertex(id, i, v, &x[v], &y[v]));
        };
        std::vector<Wedge> out;
        for (int i = 0; i < count; ++i) {
            if (sprite[static_cast<size_t>(i)] <= 0) continue;
            // A wedge sits beside an untextured core of the same height.
            for (int j : { i + 1, i - 1 }) {
                if (j < 0 || j >= count || sprite[static_cast<size_t>(j)] > 0) continue;
                Wedge w{};
                float cx[4], cy[4];
                corners(i, w.x, w.y);
                corners(j, cx, cy);
                const auto [wTop, wBot] = std::minmax({ w.y[0], w.y[1], w.y[2], w.y[3] });
                const auto [cTop, cBot] = std::minmax({ cy[0], cy[1], cy[2], cy[3] });
                if (std::fabs(wTop - cTop) > 1e-6f || std::fabs(wBot - cBot) > 1e-6f) continue;
                w.coreMidX = (cx[0] + cx[1] + cx[2] + cx[3]) / 4.0f;
                out.push_back(w);
                break;
            }
        }
        host.shutdown();
        return out;
    };
    // +1 when a wedge's ramp edge leans right going up (y grows downward)
    const auto leanOf = [](const Wedge& w) {
        const int top = w.y[1] < w.y[3] ? 1 : 3, bottom = 4 - top;
        return w.x[top] > w.x[bottom] ? 1 : -1;
    };
    for (const auto& [pack, lean] : { std::pair<const char*, int>{ "slant", 1 }, { "back", -1 } }) {
        CAPTURE(pack);
        const auto found = wedges(pack);
        REQUIRE(found.size() >= 32);   // the 16 rpm blocks, two a block
        int facing = 0, leaning = 0;
        for (const Wedge& w : found) {
            facing += std::fabs(w.x[2] - w.coreMidX) < std::fabs(w.x[0] - w.coreMidX) ? 1 : 0;
            leaning += leanOf(w) == lean ? 1 : 0;
        }
        CHECK(facing == static_cast<int>(found.size()));
        CHECK(leaning == static_cast<int>(found.size()));
    }
    // Closed up, the rpm strip's blocks share their sides: two wedges for the
    // strip where the gapped Slant draws 32.
    CHECK(wedges("slant").size() - wedges("closed").size() == 30);
}

TEST_CASE("decks: a text longer than its fit is cut to it") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_fit\\";
    stageCabinet(save);
    const std::string dir = save + "mxbmrp3\\themes\\narrow\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    // Cabinet's first Speed text is the title, SPEED: allow it a sliver
    writeLayout(dir, "[pack]\nbase = cabinet\n\n[speed-text-1]\nfit = 1\n");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n" + deckIni("narrow"));
    startDecks(host, save.c_str());
    openRace(host);
    const auto rows = host.hudStringRows("speed_deck");
    host.shutdown();
    CHECK_MESSAGE(!has(rows, "SPEED"), "the title runs past its fit: " << joined(rows));
    bool cut = false;
    for (const auto& r : rows) {
        if (!r.text.empty() && r.text.size() < 5 && std::string("SPEED").compare(0, r.text.size(), r.text) == 0) cut = true;
    }
    CHECK_MESSAGE(cut, "the title is cut, not dropped: " << joined(rows));
}

TEST_CASE("decks: a pack's before key ends a text just short of a later field") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_before\\";
    stageCabinet(save);
    const std::string dir = save + "mxbmrp3\\themes\\hugging\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    // Slide's MAX label (text 3) is listed before its value (text 4): the
    // key places it once the value is laid, wherever that is
    const std::string dir24 = save + "mxbmrp3\\themes\\hugging24\\";
    CreateDirectoryA(dir24.c_str(), nullptr);
    writeLayout(dir, "[pack]\nbase = cabinet\n\n[slide-text-3]\nbefore = best\nx = 0.5\n");
    writeLayout(dir24, "[pack]\nbase = cabinet\n\n[slide-text-3]\nbefore = best\nx = 2.5\n");
    const char* const packs[] = { "cabinet", "hugging", "hugging24" };
    double maxX[3] = {}, valueX[3] = {};
    for (int k = 0; k < 3; ++k) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[SlideDeck]\nvisible=1\n") + deckIni(packs[k]));
        startDecks(host, save.c_str());
        openRace(host);
        const auto rows = host.hudStringRows("slide_deck");
        host.shutdown();
        for (size_t i = 0; i + 1 < rows.size(); ++i) {
            if (rows[i].text == "MAX") { maxX[k] = rows[i].x; valueX[k] = rows[i + 1].x; }
        }
        INFO(packs[k] << ": " << joined(rows));
        REQUIRE(maxX[k] > 0.0);
    }
    // The value stays put (to the pixel, at 1080p)
    CHECK(std::abs(valueX[1] - valueX[0]) * 1920.0 < 0.5);
    CHECK(std::abs(valueX[2] - valueX[0]) * 1920.0 < 0.5);
    // Shipped, MAX is right-aligned at its own x, well short of the value; with
    // the key it ends x columns short of the value's start: right of where it
    // was, and two columns (21.12 px) further left for x = 2.5 than for x = 0.5
    CHECK(maxX[1] > maxX[0]);
    CHECK(maxX[1] < valueX[1]);
    const double moved = (maxX[1] - maxX[2]) * 1920.0;
    CHECK_MESSAGE(std::abs(moved - 21.12) < 0.5, "x 0.5 -> 2.5 moved MAX " << moved << " px");
}

TEST_CASE("decks: the riding gauges draw in every shipped style") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_gauges\\";
    stageStyles(save);
    for (const char* style : kStyles) {
        INFO("style " << style);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        std::string ini = "[Settings]\nversion=11\n";
        for (const char* section : { "WheelieDeck", "SlideDeck", "SpinDeck" })
            ini += std::string("[") + section + "]\nvisible=1\n";
        ini += deckIni(style);
        host.writeSettingsFile(save.c_str(), ini);
        startDecks(host, save.c_str());
        openRace(host);
        CHECK(host.spriteOrderMismatches() == 0);
        for (const auto& [id, title] : { std::pair<const char*, const char*>{ "wheelie_deck", "WHEELIE" },
                                         { "slide_deck", "SLIDE" }, { "spin_deck", "SPIN" } }) {
            const auto rows = host.hudStringRows(id);
            CHECK_MESSAGE(has(rows, title), id << ": " << joined(rows));
            CHECK_MESSAGE(has(rows, "MAX"), id << ": " << joined(rows));
            CHECK_MESSAGE(has(rows, "0.0"), id << ": the timer before anything happened (its \" s\" a smaller tail): " << joined(rows));
            // The gauge: a band per scale part (arcs: in slices) or 15 blocks
            CHECK_MESSAGE(host.hudQuadRects(id).size() >= 15, id << " drew " << host.hudQuadRects(id).size() << " quads");
        }
        host.shutdown();
    }
}

// An arc's centre, radius and thickness are cells like every other length
// (deck_layout.h's kIniUnits): the table pins the units, this pins that the
// arc goes through it. A dial alone on its deck, radius 6 rows (71.3 px at
// 1080p) over a half turn through the top, spans at least its radius across;
// read as pixels it would be 6 px and every dial would shrink about 11x.
TEST_CASE("decks: a dial's radius is in rows, drawn at its size") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_dial\\";
    stageCabinet(save);
    const std::string dir = save + "mxbmrp3\\themes\\dial\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    writeLayout(dir, "[wheelie]\nsize = 30, 20\n\n[wheelie-meter-1]\nshow = tilt\nkind = arc\n"
                                "arc = 15 10 6 1\nfrom = -90\nsweep = 180\n");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[WheelieDeck]\nvisible=1\n" + deckIni("dial"));
    startDecks(host, save.c_str());
    openRace(host);
    const auto quads = host.hudQuadRects("wheelie_deck");
    host.shutdown();
    REQUIRE(!quads.empty());
    double l = 1e9, r = -1e9;
    for (const auto& q : quads) { l = std::min(l, q.l); r = std::max(r, q.r); }
    const double width = (r - l) * 1920.0;
    CHECK_MESSAGE(width >= 6 * 11.88, "the dial spans " << width << " px across");
    CHECK_MESSAGE(width <= 2 * 7 * 11.88, "the dial spans " << width << " px across");
}

TEST_CASE("decks: the Slide gauge times a slide past grip and keeps the longest as MAX") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_slide\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SlideDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("SlideTrack", "Player");
    host.addEntry(PLAYER, "Player");
    host.setMotion(0);

    SlideRide sample{ host };

    for (int i = 0; i < 5; ++i) sample(0.0f);
    for (int i = 0; i < 3; ++i) sample(4.0f);       // under GRIP_BELOW: no slide
    auto rows = host.hudStringRows("slide_deck");
    CHECK_MESSAGE(after(rows, "MAX") == "-", "grip times nothing: " << joined(rows));
    for (int i = 0; i < 100; ++i) sample(20.0f);    // a second sideways
    rows = host.hudStringRows("slide_deck");
    CHECK_MESSAGE(has(rows, "20\xB0"), "the angle under the needle: " << joined(rows));
    // The slip angle settles over a sample or two (core/slip_angle.h), so a
    // timer a tenth short of the samples' time is right
    const std::string first = after(rows, "SLIDE");
    CHECK_MESSAGE((first == "1.0" || first == "0.9"), "the slide's timer: " << joined(rows));
    CHECK_MESSAGE(after(rows, "MAX") == first, "the longest so far is this one: " << joined(rows));
    for (int i = 0; i < 5; ++i) sample(0.0f);       // straight: the slide ends
    for (int i = 0; i < 50; ++i) sample(-25.0f);    // half a second the other way
    rows = host.hudStringRows("slide_deck");
    const std::string second = after(rows, "SLIDE");
    CHECK_MESSAGE((second == "0.5" || second == "0.4"), "a new slide times from zero: " << joined(rows));
    CHECK_MESSAGE(after(rows, "MAX") == first, "the longest stays: " << joined(rows));
    // Down, the bike sideways is a crash, not a slide: no timer, no MAX
    host.raceTrackPosition({ { .num = PLAYER, .trackPos = 0.1f, .crashed = 1 } });
    for (int i = 0; i < 5; ++i) sample(0.0f);
    const std::string last = after(host.hudStringRows("slide_deck"), "SLIDE");
    for (int i = 0; i < 200; ++i) sample(40.0f);
    rows = host.hudStringRows("slide_deck");
    CHECK_MESSAGE(after(rows, "MAX") == first, "a crash times nothing: " << joined(rows));
    CHECK_MESSAGE(after(rows, "SLIDE") == last, "the last slide stays up: " << joined(rows));
    host.shutdown();
}

// THE GAME DROPS A QUAD WOUND THE OTHER WAY (base_hud.h), silently: the Slide
// gauge's arc sweeps backward (`sweep = -120` in every shipped pack), so its
// blocks and needle swap their radii to keep the winding. Lose that swap and
// every string and count here still passes while the gauge vanishes in game,
// so each quad's signed area is held to the panel's sign. The needle is then
// read as an angle: the real slide angle, either side of straight down.
TEST_CASE("decks: every riding gauge quad keeps the game's winding, and the needle stands at the slide angle") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_winding\\";
    stageCabinet(save);
    for (const char* style : { "cabinet", "slant" }) {
        INFO("style " << style);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasMotion());
        std::string ini = "[Settings]\nversion=11\n";
        for (const char* section : { "WheelieDeck", "SlideDeck", "SpinDeck" })
            ini += std::string("[") + section + "]\nvisible=1\n";
        ini += deckIni(style);
        host.writeSettingsFile(save.c_str(), ini);
        startDecks(host, save.c_str());
        host.eventInit("SlideTrack", "Player");
        host.setMotion(0);
        SlideRide sample{ host };
        // The needle's direction from its inner end out, in degrees off straight
        // down (+ toward screen right), in 16:9 screen units
        const auto needleDeg = [&]() {
            const int last = static_cast<int>(host.hudQuadRects("slide_deck").size()) - 1;
            float x[4] = {}, y[4] = {};
            for (int v = 0; v < 4; ++v) REQUIRE(host.quadVertex("slide_deck", last, v, &x[v], &y[v]));
            const float dx = ((x[0] + x[3]) - (x[1] + x[2])) * 16.0f / 9.0f, dy = (y[0] + y[3]) - (y[1] + y[2]);
            return std::atan2(dx, dy) * 180.0f / 3.14159265f;
        };

        for (int i = 0; i < 5; ++i) sample(0.0f);
        CHECK_MESSAGE(std::fabs(needleDeg()) < 2.0f, "straight: the needle hangs down, " << needleDeg());
        for (int i = 0; i < 30; ++i) sample(20.0f);
        const float one = needleDeg();
        CHECK_MESSAGE(std::fabs(std::fabs(one) - 20.0f) < 3.0f, "20 degrees sideways: " << one);
        for (const char* id : { "wheelie_deck", "slide_deck", "spin_deck" }) {
            INFO(id);
            const int n = static_cast<int>(host.hudQuadRects(id).size());
            REQUIRE(n > 15);
            const float panel = quadArea(host, id, 0);
            REQUIRE(panel != 0.0f);
            int against = 0;
            for (int i = 1; i < n; ++i) {
                const float a = quadArea(host, id, i);
                if (a != 0.0f && (a > 0.0f) != (panel > 0.0f)) ++against;
            }
            CHECK_MESSAGE(against == 0, against << " of " << n << " quads wound against the panel");
        }
        for (int i = 0; i < 30; ++i) sample(-20.0f);
        const float other = needleDeg();
        CHECK_MESSAGE(std::fabs(std::fabs(other) - 20.0f) < 3.0f, "20 degrees the other way: " << other);
        CHECK_MESSAGE((one > 0.0f) != (other > 0.0f), "the other slide swings the other side: " << one << " / " << other);
        host.shutdown();
    }
}

TEST_CASE("decks: the Combo deck follows a chain from the trick to LANDED, and a bail") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_combo\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasFmx());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[ComboDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1 /*practice*/, 0, 480000);
    host.addEntry(PLAYER, "Alice");
    host.runInit(1);
    host.runStart();

    FmxRide bike{ host };
    const auto backflip = [&]() {   // 1.5 s at -300 deg/s: past a full turn
        for (int i = 0; i < 150; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0;
            r.rearMaterial = 0;
            r.pitchVel = -300.0f;
            bike.tick(r);
        }
    };
    const auto startsWith = [](const std::vector<PluginHost::StringRow>& rows, const char* prefix) {
        for (const auto& r : rows) if (r.text.rfind(prefix, 0) == 0) return true;
        return false;
    };

    bike.ground(20);
    auto rows = host.hudStringRows("combo_deck");
    for (const char* label : { "COMBO", "POINTS", "SCORE", "BEST", "0" })
        CHECK_MESSAGE(has(rows, label), label << ": " << joined(rows));
    CHECK_MESSAGE(!has(rows, "BACKFLIP"), "no trick yet: " << joined(rows));

    // In the air: the trick turning is a chip already
    backflip();
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(has(rows, "BACKFLIP"), "the trick's chip: " << joined(rows));

    // Landed, through the grace: the chain's points (counted once, not again
    // as the trick's own) and its multiplier
    bike.ground(80);
    const int points = host.fmxState().chainScore;
    REQUIRE(points > 0);
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(has(rows, std::to_string(points).c_str()), "the chain's points " << points << ": " << joined(rows));
    CHECK_MESSAGE(has(rows, "x1.0"), "one trick: " << joined(rows));
    CHECK_MESSAGE(has(rows, "BACKFLIP"), joined(rows));

    // The chain runs out and banks: LANDED +points, into SCORE (rolled up)
    // and BEST; the multiplier goes with the chain
    bike.ground(210);
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(has(rows, ("LANDED +" + std::to_string(points)).c_str()), "x1.0 banks the points: " << joined(rows));
    CHECK_MESSAGE(!has(rows, "x1.0"), "no multiplier once banked: " << joined(rows));
    bike.ground(80);
    rows = host.hudStringRows("combo_deck");
    int shown = 0;
    for (const auto& r : rows) shown += r.text == std::to_string(points) ? 1 : 0;
    CHECK_MESSAGE(shown >= 2, "the score and the best combo both read the points: " << joined(rows));

    // A crash in the landing grace loses the chain: BAILED, no points
    backflip();
    bike.ground(10);
    {
        TelemetryRow r;
        r.crashed = 1;
        bike.tick(r);
    }
    bike.ground(5);
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(has(rows, "BAILED"), joined(rows));
    CHECK_MESSAGE(!startsWith(rows, "LANDED"), joined(rows));
    CHECK_MESSAGE(host.fmxState().sessionScore == points, "nothing banked");

    // Still down past the chain's linger, the lost chain stays up, as the FMX
    // HUD holds it (Fmx::ChainHold); back up, it goes
    host.raceTrackPosition({ { PLAYER, 0.5f, 1 } });
    bike.ground(400);
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(has(rows, "BACKFLIP"), "held while down: " << joined(rows));
    host.raceTrackPosition({ { PLAYER, 0.5f, 0 } });
    bike.ground(5);
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(!has(rows, "BACKFLIP"), "let go once up: " << joined(rows));

    host.fmxSetNowUs(-1);
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("decks: the Combo deck's LANDED counts a chain's points times its multiplier") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_combo_mult\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasFmx());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[ComboDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1 /*practice*/, 0, 480000);
    host.addEntry(PLAYER, "Alice");
    host.runInit(1);
    host.runStart();
    FmxRide bike{ host };
    const auto backflip = [&]() {
        for (int i = 0; i < 150; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0;
            r.rearMaterial = 0;
            r.pitchVel = -300.0f;
            bike.tick(r);
        }
    };
    // Two tricks in one chain: a multiplier past x1.0
    bike.ground(20);
    backflip();
    bike.ground(80);
    backflip();
    bike.ground(80);
    const int chain = host.fmxState().chainScore;
    REQUIRE(chain > 0);
    auto rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(!has(rows, "x1.0"), "two tricks multiply: " << joined(rows));
    // Banked: LANDED shows what the session gained, more than the raw points
    bike.ground(210);
    const int banked = host.fmxState().sessionScore;
    CHECK(banked > chain);
    rows = host.hudStringRows("combo_deck");
    CHECK_MESSAGE(has(rows, ("LANDED +" + std::to_string(banked)).c_str()),
                  "chain " << chain << ", banked " << banked << ": " << joined(rows));
    host.fmxSetNowUs(-1);
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("decks: the Combo deck shows the trick's stats, and stacks its chips beside the deck") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_combo_stack\\";
    stageCabinet(save);
    for (const int motion : { 0, 1 }) {
        INFO("motion " << motion);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasFmx());
        REQUIRE(host.hasMotion());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[ComboDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.setMotion(motion);
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(1 /*practice*/, 0, 480000);
        host.addEntry(PLAYER, "Alice");
        host.runInit(1);
        host.runStart();

        FmxRide bike{ host, true };
        // Every chip drawn, top to bottom: a jump shows as AIR until the
        // turn names it BACKFLIP
        const auto chipYs = [&]() {
            std::vector<double> ys;
            for (const auto& r : host.hudStringRows("combo_deck"))
                if (r.text == "BACKFLIP" || r.text == "AIR") ys.push_back(r.y);
            std::sort(ys.begin(), ys.end());
            return ys;
        };
        // A backflip; the newest chip's height on the first frame it shows
        const auto backflip = [&]() {
            const size_t before = chipYs().size();
            double first = -1.0;
            for (int i = 0; i < 150; ++i) {
                TelemetryRow r;
                r.frontMaterial = 0;
                r.rearMaterial = 0;
                r.pitchVel = -300.0f;
                bike.tick(r);
                const std::vector<double> ys = chipYs();
                if (first < 0.0 && ys.size() > before) first = ys.back();
            }
            return first;
        };

        // Before a trick: the four labels, each value empty
        bike.ground(20);
        auto rows = host.hudStringRows("combo_deck");
        for (const char* label : { "TIME", "DIST", "HIGH", "ROT" })
            CHECK_MESSAGE(has(rows, label), label << ": " << joined(rows));
        int empty = 0;
        for (const auto& r : rows) empty += r.text == "-" ? 1 : 0;
        CHECK_MESSAGE(empty >= 4, "no trick, no numbers: " << joined(rows));

        // The first chip: it rises in from below with Motion on, stands at
        // once with it off, and settles on the same place either way
        const double firstAt = backflip();
        REQUIRE(firstAt >= 0.0);
        bike.ground(80);
        std::vector<double> ys = chipYs();
        REQUIRE(ys.size() == 1);
        const double restAt = ys.back();
        if (motion) CHECK_MESSAGE(firstAt > restAt + 0.5 / 1080.0, "rose in from below: " << firstAt << " -> " << restAt);
        else CHECK_MESSAGE(std::abs((firstAt - restAt) * 1080.0) < 0.5, "stood at once: " << firstAt << " vs " << restAt);
        CHECK_MESSAGE(restAt * 1080.0 > 90.0, "the newest stands at the deck's bottom");

        // The trick's numbers, held after it lands: its time (1.5 s at
        // 100 Hz; the unit is the pack's smaller tail) and its turn
        rows = host.hudStringRows("combo_deck");
        bool turn = false;
        for (const auto& r : rows) {
            const size_t n = r.text.size();
            if (n >= 2 && std::isdigit(static_cast<unsigned char>(r.text[0])) && r.text[n - 1] == '\xB0') turn = true;
        }
        CHECK_MESSAGE(hasPair(rows, "1.5", "s"), "the trick's time over its unit: " << joined(rows));
        CHECK_MESSAGE(turn, "the turn: " << joined(rows));

        // Five more in the chain: each new one at the bottom pushes the rest
        // up a pitch; five stand, the sixth has faded out past the top
        for (int k = 0; k < 5; ++k) {
            backflip();
            bike.ground(80);
            ys = chipYs();
            INFO("tricks " << k + 2);
            REQUIRE(!ys.empty());
            CHECK(std::abs((ys.back() - restAt) * 1080.0) < 0.5);
            CHECK(ys.size() == static_cast<size_t>(std::min(k + 2, 5)));
            for (size_t i = 1; i < ys.size(); ++i)
                CHECK_MESSAGE(std::abs((ys[i] - ys[i - 1]) - (ys[1] - ys[0])) * 1080.0 < 0.5, "one pitch apart");
        }

        // The chain lands: its chips stay where they stood (no second rise)
        bike.ground(210);
        const std::vector<double> landed = chipYs();
        // ...and the stamp shows the banked points whole, four digits and all
        // (PluginUtils::formatScore's "1,665")
        std::string banked = std::to_string(host.fmxState().sessionScore);
        for (int at = static_cast<int>(banked.size()) - 3; at > 0; at -= 3) banked.insert(static_cast<size_t>(at), ",");
        rows = host.hudStringRows("combo_deck");
        CHECK_MESSAGE(has(rows, ("LANDED +" + banked).c_str()), "whole: " << joined(rows));
        REQUIRE(landed.size() == 5);
        CHECK(std::abs((landed.back() - restAt) * 1080.0) < 0.5);
        // A new chain counts from zero: its first chip rises in again with
        // Motion on (stands at once with it off), alone in the stack
        double againAt = -1.0;
        for (int i = 0; i < 150; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0;
            r.rearMaterial = 0;
            r.pitchVel = -300.0f;
            bike.tick(r);
            const std::vector<double> now = chipYs();
            if (againAt < 0.0 && now.size() == 1) againAt = now[0];
        }
        REQUIRE(againAt >= 0.0);
        if (motion) CHECK_MESSAGE(againAt > restAt + 0.5 / 1080.0, "the new chain's chip rose in: " << againAt << " vs " << restAt);
        else CHECK_MESSAGE(std::abs((againAt - restAt) * 1080.0) < 0.5, "stood at once: " << againAt);
        bike.ground(60);
        ys = chipYs();
        CHECK(ys.size() == 1);

        host.fmxSetNowUs(-1);
        host.setMotionNowUs(-1);
        host.runDeinit();
        host.shutdown();
    }
}

// ---- The later decks ---------------------------------------------------------

namespace {
// A ten-lap race, the player P2 behind #22: the gap at the line closes by
// `close` ms a lap from `first`, one classification and one line crossing
// per lap up to `laps`.
void chaseLaps(PluginHost& host, int laps, int first, int close) {
    for (int lap = 1; lap <= laps; ++lap) {
        host.classify(RACE, lap * 90000, {
            { .num = 22, .best = 90000, .laps = lap, .gap = 0 },
            { .num = PLAYER, .best = 91000, .laps = lap, .gap = first - close * (lap - 1) },
        });
        host.raceLap(RACE, PLAYER, lap, 91000);
        host.draw();
    }
}
}  // namespace

// Every shipped style loads clean and draws every deck. A misspelt key or an
// image a skin names but lacks only logs a warning and falls back to the
// base's, so a skin with a typo passes every string check while drawing
// Cabinet's text: the log is the one place it shows.
TEST_CASE("decks: every shipped style loads with no warning and draws every deck") {
    // In kDeckIds order; the Notice deck draws nothing with nothing up
    const char* const kTitles[] = { "SPEED", "RACE", "TIMING", "WHEELIE", "SLIDE", "COMBO", "GAP", "RIVAL",
                                    "SPIN", "INPUTS", "STANDINGS", "SESSION", nullptr, "FUEL", "NOW PLAYING",
                                    "LAP LOG", "TIMING" };
    static_assert(std::size(kTitles) == std::size(kDeckIds), "one title per deck");
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_every_style\\";
    const std::string logPath = save + "mxbmrp3\\mxbmrp3_log.txt";
    stageStyles(save);
    stageFonts(save);
    const SyncedFontsCleanup fontsGo;
    for (const char* style : kStyles) {
        INFO("style " << std::string(style));
        DeleteFileA(logPath.c_str());
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        std::string ini = "[Settings]\nversion=11\n";
        for (const char* section : kDeckSections) ini += std::string("[") + section + "]\nvisible=1\n" + quietCarousel(section);
        ini += deckIni(style);
        host.writeSettingsFile(save.c_str(), ini);
        startDecks(host, save.c_str());
        openRace(host);
        CHECK(host.spriteOrderMismatches() == 0);
        for (size_t i = 0; i < std::size(kDeckIds); ++i) {
            if (!kTitles[i] || host.hudVisible(kDeckIds[i]) < 0) continue;   // Combo: FMX games only
            const auto rows = host.hudStringRows(kDeckIds[i]);
            CHECK_MESSAGE(has(rows, kTitles[i]), kDeckIds[i] << ": " << joined(rows));
        }
        host.shutdown();   // flushes the log
        const std::string log = ini::readFile(logPath);
        REQUIRE_FALSE(log.empty());
        // Only the shipped layouts: other cases stage layouts of their own
        // (a typo, on purpose) into the same discovery tree
        for (const char* shipped : { "cabinet-dark", "carbon-dark", "modern-dark", "retro-dark" }) {
            const std::string tag = std::string("Deck '") + shipped + "'";
            for (size_t at = log.find(tag); at != std::string::npos; at = log.find(tag, at + 1)) {
                const size_t eol = log.find('\n', at);
                FAIL_CHECK(log.substr(at, eol == std::string::npos ? std::string::npos : eol - at));
            }
        }
    }
}

// The Standings deck is the first list deck: the pack's row template drawn once
// per row (Deck::rows), the places the classic table's window with the leader
// pinned. Each check is a way it fails quietly: the window off by one, the
// break after the leader missing (or drawn when the places run on), the gap
// unsigned or measured to the leader, your row given a gap, "no gap yet" read
// as a 1 ms one (the grid: equal official gaps, which gapTo calls none), a
// place-change tag (no box behind it on this deck: the Race deck carries it),
// a time penalty dropped or drawn inside the row instead of hung past its end
// (or its chip drawn on a row without one), the title still drawn with Title off.
TEST_CASE("decks: the Standings deck shows the leader, then the riders around you, gaps to you") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_standings\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[StandingsDeck]\nvisible=1\n[RivalDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    // Fifteen riders a second apart; the player `at` their place
    static const char* const kNames[] = { "Ann", "Ben", "Cal", "Dan", "Eve", "Fay", "Gus",
                                          "Hal", "Ivy", "Jon", "Kim", "Lea", "Max", "Ned" };
    host.addEntry(PLAYER, "Alice");
    for (int i = 0; i < 14; ++i) host.addEntry(101 + i, kNames[i]);
    host.runInit(RACE);
    const auto classifyAt = [&](int at, int spacingMs = 1000, int penaltyAt = 0) {
        std::vector<ClassRow> order;
        for (int p = 1, other = 0; p <= 15; ++p) {
            const int num = p == at ? PLAYER : 101 + other++;
            order.push_back({ .num = num, .best = 90000, .laps = 2, .gap = (p - 1) * spacingMs,
                              .penalty = p == penaltyAt ? 5000 : 0 });
        }
        host.classify(RACE, 200000, order);
        host.draw();
        host.draw();
    };
    const auto yOf = [](const std::vector<PluginHost::StringRow>& rows, const char* text) {
        for (const auto& r : rows) if (r.text == text) return r.y;
        return -1.0;
    };

    // The grid: every official gap equal, so no gap yet -- "-", never +/-0.00,
    // on this deck and on the Rival deck that shares the gap
    classifyAt(2, 0);
    auto rows = host.hudStringRows("standings_deck");
    for (const char* zero : { "+0.00", "-0.00" })
        CHECK_MESSAGE(!has(rows, zero), "no gap yet on the grid: " << joined(rows));
    CHECK_MESSAGE(has(rows, "-"), "the grid's gaps are the placeholder: " << joined(rows));
    const auto rival = host.hudStringRows("rival_deck");
    for (const auto& r : rival)
        CHECK_MESSAGE(r.text.find("0.00") == std::string::npos, "Rival: no gap yet on the grid, " << joined(rival));

    // P11: the leader, then four ahead (P7..P10), you, three behind
    classifyAt(11);
    rows = host.hudStringRows("standings_deck");
    CHECK_MESSAGE(has(rows, "STANDINGS"), joined(rows));
    for (const char* shown : { "1", "7", "8", "9", "10", "11", "12", "13", "14" })
        CHECK_MESSAGE(has(rows, shown), "P" << shown << ": " << joined(rows));
    for (const char* hidden : { "2", "3", "4", "5", "6", "15" })
        CHECK_MESSAGE(!has(rows, hidden), "P" << hidden << " is outside the window: " << joined(rows));
    CHECK_MESSAGE(has(rows, "ANN"), "the leader's name: " << joined(rows));
    CHECK_MESSAGE(has(rows, "ALICE"), joined(rows));
    CHECK_MESSAGE(has(rows, "-10.00"), "the leader's gap is to you, ahead: " << joined(rows));
    CHECK_MESSAGE(has(rows, "-4.00"), "P7, four places ahead: " << joined(rows));
    CHECK_MESSAGE(has(rows, "+3.00"), "P14, three behind: " << joined(rows));
    CHECK_MESSAGE(!has(rows, "+0.00"), "your own row has no gap: " << joined(rows));
    // The rows after the leader sit a break further down than the pitch
    const double pitch = yOf(rows, "8") - yOf(rows, "7");
    const double afterLeader = yOf(rows, "7") - yOf(rows, "1");
    CHECK_MESSAGE(pitch > 0.0, joined(rows));
    // In pixels at 1080p: a normalized Approx would allow ~22 px either way
    CHECK_MESSAGE(std::abs((afterLeader - pitch) * 1080.0 - 6.0) < 1.0,
                  "a 6 px break after the leader: " << (afterLeader - pitch) * 1080.0 << " px");

    // P3: the places run on from the leader, P1..P9, no break; eight places
    // up in a running race, and no tag for them here
    classifyAt(3);
    rows = host.hudStringRows("standings_deck");
    CHECK_MESSAGE(!has(rows, "+8"), "no place-change tag on this deck: " << joined(rows));
    for (const char* shown : { "1", "2", "3", "9" }) CHECK_MESSAGE(has(rows, shown), "P" << shown << ": " << joined(rows));
    CHECK_MESSAGE(!has(rows, "10"), joined(rows));
    CHECK_MESSAGE(std::abs((yOf(rows, "2") - yOf(rows, "1")) - (yOf(rows, "3") - yOf(rows, "2"))) * 1080.0 < 1.0,
                  "no break when the places run on");
    CHECK_MESSAGE(has(rows, "-2.00"), "the leader, two ahead: " << joined(rows));

    // A time penalty: "+5s" on P2's row, hung past the deck's right edge (the
    // gaps end inside it), on a chip of its own; no chip on the other rows
    const size_t quadsClean = host.hudQuadRects("standings_deck").size();
    classifyAt(3, 1000, 2);
    rows = host.hudStringRows("standings_deck");
    CHECK_MESSAGE(has(rows, "+5s"), joined(rows));
    const auto xOf = [](const std::vector<PluginHost::StringRow>& list, const char* text) {
        for (const auto& r : list) if (r.text == text) return r.x;
        return -1.0;
    };
    CHECK_MESSAGE(xOf(rows, "+5s") > xOf(rows, "-2.00"), "the chip hangs right of the gaps: " << joined(rows));
    CHECK_MESSAGE(std::abs(yOf(rows, "+5s") - yOf(rows, "2")) * 1080.0 < 3.0, "on P2's row: " << joined(rows));
    double deckRight = 0.0, chipRight = 0.0;
    for (const auto& q : host.hudQuadRects("standings_deck")) chipRight = std::max(chipRight, q.r);
    classifyAt(3);
    for (const auto& q : host.hudQuadRects("standings_deck")) deckRight = std::max(deckRight, q.r);
    CHECK_MESSAGE(chipRight > deckRight, "the chip's quads lie past the deck: " << chipRight << " vs " << deckRight);
    CHECK(host.hudQuadRects("standings_deck").size() == quadsClean);
    CHECK_MESSAGE(!has(host.hudStringRows("standings_deck"), "+5s"), "cleared with the penalty");

    // Title off: the name goes, and the box hugging it with it; the rows stay
    const size_t quadsWithTitle = host.hudQuadRects("standings_deck").size();
    REQUIRE(host.setHudTitle("standings_deck", false));
    host.draw();
    rows = host.hudStringRows("standings_deck");
    CHECK_MESSAGE(!has(rows, "STANDINGS"), joined(rows));
    CHECK_MESSAGE(has(rows, "ANN"), joined(rows));
    CHECK(host.hudQuadRects("standings_deck").size() < quadsWithTitle);
    host.shutdown();
}

// The Standings deck's gaps are the splits' unless the Standings HUD's Live
// updates is on, so the deck and the table never disagree: a live gap of
// 1.00 s against an official 0.50 s shows "+0.50" with it off and "+1.00"
// with it on. Fails if the deck goes back to live gaps regardless.
// Outside a race the places are by best lap, so the deck shows each row's
// best lap (yours too) where a race shows the gap to you (his pick A, 15 Nov).
TEST_CASE("decks: the Standings deck shows each row's best lap outside a race") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_standings_practice\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[StandingsDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    constexpr int PRACTICE = 1;   // PiBoSo practice session enum
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(101, "Ann");
    host.addEntry(102, "Ben");
    host.runInit(PRACTICE);
    // Ann 1:30.000, you 1:30.512, Ben no lap yet
    host.classify(PRACTICE, 200000, {
        { .num = 101, .best = 90000, .laps = 3, .gap = 0 },
        { .num = PLAYER, .best = 90512, .laps = 3, .gap = 512 },
        { .num = 102, .best = 0, .laps = 0, .gap = 0 },
    });
    host.draw();
    host.draw();
    const auto rows = host.hudStringRows("standings_deck");
    CHECK_MESSAGE(has(rows, "1:30.000"), "the leader's best lap: " << joined(rows));
    CHECK_MESSAGE(has(rows, "1:30.512"), "your own best lap: " << joined(rows));
    CHECK_MESSAGE(!has(rows, "+0.51"), "no gap outside a race: " << joined(rows));
    CHECK_MESSAGE(has(rows, "-"), "no lap yet is the placeholder: " << joined(rows));
    host.shutdown();
}

TEST_CASE("decks: the Standings deck shows live gaps only with Live updates on") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_standings_live\\";
    stageCabinet(save);
    for (const int live : { 0, 1 }) {
        INFO("liveUpdates " << live);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[StandingsDeck]\nvisible=1\n"
                                             "[General]\nliveUpdates=" + std::to_string(live) + "\n");
        startDecks(host, save.c_str());
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(PLAYER, "Alice");
        host.addEntry(22, "Bob");
        // Officially Bob is 0.50 s behind; on track he reaches Alice's 0.20
        // a second after her (livegaps_test has the engine's side)
        const std::vector<ClassRow> lap1 = {
            { .num = PLAYER, .laps = 1, .gap = 0 },
            { .num = 22, .laps = 1, .gap = 500 },
        };
        host.classify(RACE, 1000, lap1);
        host.raceTrackPosition({ { PLAYER, 0.20f }, { 22, 0.10f } });
        host.classify(RACE, 2000, lap1);
        host.raceTrackPosition({ { PLAYER, 0.40f }, { 22, 0.20f } });
        host.draw();
        host.draw();
        const auto rows = host.hudStringRows("standings_deck");
        CHECK_MESSAGE(has(rows, live ? "+1.00" : "+0.50"), joined(rows));
        CHECK_MESSAGE(!has(rows, live ? "+0.50" : "+1.00"), joined(rows));
        host.shutdown();
    }
}

// With Motion on, a place change slides the deck's rows to their new place
// (row_slide.h, the Standings HUD's own slide): straight after the pass the
// passer's row is still on its way, between two rows. With Motion off it is
// in place at once. The Standings HUD has no slide setting of its own and
// follows Motion the same way. Fails if either list stops sliding, or slides
// with Motion off.
TEST_CASE("decks: the Standings deck and HUD slide a pass with Motion on, snap with it off") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_standings_slide\\";
    stageCabinet(save);
    for (const int motion : { 0, 1 }) {
        INFO("motion " << motion);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasMotion());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[StandingsDeck]\nvisible=1\n"
                                             "[StandingsHud]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.setMotion(motion);
        long long now = 1'000'000;
        host.setMotionNowUs(now);
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(PLAYER, "Alice");
        host.addEntry(22, "Bob");
        host.addEntry(33, "Cat");
        host.classify(RACE, 100000, { { .num = 22, .best = 90000, .laps = 1, .gap = 0 },
                                      { .num = PLAYER, .best = 90000, .laps = 1, .gap = 500 },
                                      { .num = 33, .best = 90000, .laps = 1, .gap = 900 } });
        host.draw();
        host.draw();
        const auto yOf = [](const std::vector<PluginHost::StringRow>& rows, const char* text) {
            for (const auto& r : rows) if (r.text == text) return r.y;
            return -1.0;
        };
        // The HUD's name column: short names, in whatever case the font shows
        const auto yOfName = [](const std::vector<PluginHost::StringRow>& rows, const char* name) {
            for (const auto& r : rows) {
                if (r.text.size() != 3) continue;
                bool same = true;
                for (int i = 0; i < 3; ++i) same = same && std::toupper(static_cast<unsigned char>(r.text[i])) == name[i];
                if (same) return r.y;
            }
            return -1.0;
        };
        auto rows = host.hudStringRows("standings_deck");
        const double catAt = yOf(rows, "CAT");   // P3's row, at rest
        const double aliceAt = yOf(rows, "ALICE");   // P2's
        REQUIRE(catAt >= 0.0);
        REQUIRE(aliceAt >= 0.0);
        auto hudRows = host.hudStringRows("standings_hud");
        const double hudCatAt = yOfName(hudRows, "CAT");
        REQUIRE_MESSAGE(hudCatAt >= 0.0, joined(hudRows));
        // Cat passes Alice: P2 now, on Alice's old row
        host.classify(RACE, 101000, { { .num = 22, .best = 90000, .laps = 1, .gap = 0 },
                                      { .num = 33, .best = 90000, .laps = 1, .gap = 400 },
                                      { .num = PLAYER, .best = 90000, .laps = 1, .gap = 600 } });
        host.draw();
        // A fifth into the slide (RowSlide's 500 ms): about half the way
        // there, eased
        now += 100'000;
        host.setMotionNowUs(now);
        host.draw();
        rows = host.hudStringRows("standings_deck");
        const double aliceNow = yOf(rows, "ALICE"), catNow = yOf(rows, "CAT");
        hudRows = host.hudStringRows("standings_hud");
        const double hudAliceNow = yOfName(hudRows, "ALI"), hudCatNow = yOfName(hudRows, "CAT");
        if (motion) {
            CHECK_MESSAGE(catNow < catAt, "Cat has left P3's row: " << joined(rows));
            CHECK_MESSAGE(catNow > aliceNow, "but not reached P2's yet: " << joined(rows));
        } else {
            CHECK_MESSAGE(catNow < aliceNow, "Cat on P2's row at once: " << joined(rows));
            CHECK_MESSAGE(std::abs((aliceNow - catAt) * 1080.0) < 0.5, "Alice on P3's row: " << joined(rows));
        }
        if (motion) {
            CHECK_MESSAGE(hudCatNow < hudCatAt, "the HUD's Cat has left P3's row: " << joined(hudRows));
            CHECK_MESSAGE(hudCatNow > hudAliceNow, "but not reached P2's yet: " << joined(hudRows));
        } else {
            CHECK_MESSAGE(hudCatNow < hudAliceNow, "the HUD's Cat on P2's row at once: " << joined(hudRows));
            CHECK_MESSAGE(std::abs((hudAliceNow - hudCatAt) * 1080.0) < 0.5, "the HUD's Alice on P3's row: " << joined(hudRows));
        }
        // The slide's end: both on their new rows
        now += 500'000;
        host.setMotionNowUs(now);
        host.draw();
        host.draw();
        rows = host.hudStringRows("standings_deck");
        CHECK_MESSAGE(std::abs((yOf(rows, "CAT") - aliceAt) * 1080.0) < 0.5, "Cat on P2's row: " << joined(rows));
        CHECK_MESSAGE(std::abs((yOf(rows, "ALICE") - catAt) * 1080.0) < 0.5, "Alice on P3's row: " << joined(rows));
        host.setMotionNowUs(-1);
        host.shutdown();
    }
}

TEST_CASE("decks: the Rival deck tags the gap gained or lost at each line") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_rival\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RivalDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.runInit(RACE);
    // 1.4 s, 1.1 s, 0.8 s at the line: -0.3 s a lap, closing on the rider to chase
    chaseLaps(host, 3, 1400, 300);
    auto rows = host.hudStringRows("rival_deck");
    CHECK_MESSAGE(has(rows, "CHASING"), joined(rows));
    CHECK_MESSAGE(has(rows, "#22"), "their number as the hero: " << joined(rows));
    CHECK_MESSAGE(has(rows, "BOB"), joined(rows));
    CHECK_MESSAGE(has(rows, "0.80 s"), joined(rows));
    CHECK_MESSAGE(has(rows, "GAINED 0.30"), "1.10 s to 0.80 s on the rider to chase: " << joined(rows));
    CHECK_MESSAGE(has(rows, "LAST"), joined(rows));
    CHECK_MESSAGE(!has(rows, "TREND"), joined(rows));
    // Their lap as they cross, plain in the LAST row
    host.raceLap(RACE, 22, 3, 90250);
    host.draw();
    rows = host.hudStringRows("rival_deck");
    CHECK_MESSAGE(has(rows, "1:30.250"), joined(rows));
    // The gap growing again is LOST
    host.classify(RACE, 4 * 90000, {
        { .num = 22, .best = 90000, .laps = 4, .gap = 0 },
        { .num = PLAYER, .best = 91000, .laps = 4, .gap = 1250 },
    });
    host.raceLap(RACE, PLAYER, 4, 91000);
    host.draw();
    rows = host.hudStringRows("rival_deck");
    CHECK_MESSAGE(has(rows, "LOST 0.45"), "0.80 s to 1.25 s: " << joined(rows));
    host.shutdown();
}

// The tag's guards, each a mutation the case above survives: defending, a
// shrinking gap is LOST; the first crossing and the first after a rival change
// have nothing to compare; a change that rounds to 0.00 is no news.
TEST_CASE("decks: the Rival tag is lost defending, and silent with nothing to compare") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_rival_tag\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RivalDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.setMotion(0);
    int64_t now = 1'000'000;
    host.setMotionNowUs(now);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Cid");
    host.runInit(RACE);
    const auto tagged = [](const std::vector<PluginHost::StringRow>& rows) {
        for (const auto& r : rows)
            if (r.text.rfind("GAINED", 0) == 0 || r.text.rfind("LOST", 0) == 0) return true;
        return false;
    };
    // Each lap at the line, past the last tag's time; the player leads
    const auto lap = [&](int n, std::vector<ClassRow> rows) {
        now += 4'000'000;
        host.setMotionNowUs(now);
        host.classify(RACE, n * 90000, rows);
        host.raceLap(RACE, PLAYER, n, 91000);
        host.draw();
        return host.hudStringRows("rival_deck");
    };
    // The first crossing: a gap held, none before it
    auto rows = lap(1, { { .num = PLAYER, .best = 90000, .laps = 1, .gap = 0 },
                         { .num = 22, .best = 91000, .laps = 1, .gap = 900 } });
    CHECK_MESSAGE(has(rows, "DEFENDING"), joined(rows));
    CHECK_MESSAGE(!tagged(rows), "nothing to compare yet: " << joined(rows));
    // Defending, the gap shrinking is lost
    rows = lap(2, { { .num = PLAYER, .best = 90000, .laps = 2, .gap = 0 },
                    { .num = 22, .best = 91000, .laps = 2, .gap = 600 } });
    CHECK_MESSAGE(has(rows, "LOST 0.30"), "0.90 s to 0.60 s on the rider behind: " << joined(rows));
    // 3 ms rounds to 0.00: no tag
    rows = lap(3, { { .num = PLAYER, .best = 90000, .laps = 3, .gap = 0 },
                    { .num = 22, .best = 91000, .laps = 3, .gap = 603 } });
    CHECK_MESSAGE(!tagged(rows), joined(rows));
    // A new rival (33 now right behind): its first crossing has nothing to
    // compare, whatever the old rider's gap was
    rows = lap(4, { { .num = PLAYER, .best = 90000, .laps = 4, .gap = 0 },
                    { .num = 33, .best = 91000, .laps = 4, .gap = 300 },
                    { .num = 22, .best = 91000, .laps = 4, .gap = 700 } });
    CHECK_MESSAGE(has(rows, "#33"), joined(rows));
    CHECK_MESSAGE(!tagged(rows), "a gap to another rider is no change: " << joined(rows));
    host.shutdown();
}

TEST_CASE("decks: the Speed deck stamps the reaction from the gate dropping, for REACT_US") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_start\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.runInit(RACE);
    host.runStart();
    // The green flag, the gate held (a non-racing classification), then the
    // drop at 0.5 s and the bike past REACT_SPEED 0.18 s later
    host.raceSessionState(RACE, 256);
    grid(host, 256);
    host.raceSessionState(RACE, 16);
    grid(host, 0x20);
    host.setMotionNowUs(500'000);
    grid(host, 16);
    host.setMotionNowUs(680'000);
    host.drawWithState(0);   // on track first: the switch clears telemetry
    auto rows = host.hudStringRows("speed_deck");
    CHECK_MESSAGE(!has(rows, "REACT 0.18"), "no reaction before the bike moves: " << joined(rows));
    TelemetryRow r;
    r.speed = 4.0f;
    r.time = 0.68f;
    r.gear = 1;
    host.telemetryFrame(r);
    host.drawWithState(0);
    rows = host.hudStringRows("speed_deck");
    CHECK_MESSAGE(has(rows, "REACT 0.18"), joined(rows));
    // Gone REACT_US (10 s) later
    host.setMotionNowUs(10'600'000);
    host.drawWithState(0);
    rows = host.hudStringRows("speed_deck");
    CHECK_MESSAGE(has(rows, "REACT 0.18"), "still up just under 10 s on: " << joined(rows));
    host.setMotionNowUs(10'720'000);
    host.drawWithState(0);
    rows = host.hudStringRows("speed_deck");
    CHECK_MESSAGE(!has(rows, "REACT 0.18"), "gone after 10 s: " << joined(rows));
    host.shutdown();
}

// A spectated rider's riding is not the player's: the gauges read the
// player's own bike only, and a REACT stamp is the player's alone. Flipping to
// a spectated rider must also rebuild the Wheelie gauge (its key holds whose
// bike it reads), or it keeps the player's last angle up.
TEST_CASE("decks: a spectated rider earns no REACT stamp, and the Wheelie gauge stops reading the player") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_spectate\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n[WheelieDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.runInit(RACE);
    host.runStart();
    host.raceSessionState(RACE, 256);
    grid(host, 256);
    host.raceSessionState(RACE, 16);
    grid(host, 0x20);
    host.setMotionNowUs(500'000);
    grid(host, 16);
    host.setMotionNowUs(600'000);
    host.drawWithState(0);   // on track first: the switch clears telemetry
    // Up on the back wheel at 30 degrees, creeping under REACT_SPEED
    TelemetryRow r;
    r.speed = 0.5f;
    r.time = 0.6f;
    r.gear = 1;
    r.pitch = -30.0f;
    r.frontMaterial = 0;
    r.rearMaterial = 1;
    host.telemetryFrame(r);
    host.drawWithState(0);
    auto wheelie = host.hudStringRows("wheelie_deck");
    REQUIRE_MESSAGE(has(wheelie, "30\xB0"), "the player's wheelie: " << joined(wheelie));
    // The camera on #22: the bike data goes on arriving, but it is not the player's view
    host.drawWithState(1);
    host.spectateVehicles({ { 22, "Bob" } }, /*curSelectionIndex=*/0);
    host.drawWithState(1);
    wheelie = host.hudStringRows("wheelie_deck");
    CHECK_MESSAGE(!has(wheelie, "30\xB0"), "a spectated rider's gauge shows no player's angle: " << joined(wheelie));
    host.setMotionNowUs(680'000);
    r.speed = 4.0f;
    r.time = 0.68f;
    r.pitch = 0.0f;
    r.frontMaterial = 1;
    host.telemetryFrame(r);
    host.drawWithState(1);
    const auto deck = host.hudStringRows("speed_deck");
    for (const auto& row : deck) CHECK_MESSAGE(row.text.rfind("REACT", 0) != 0, "spectating: no stamp, " << joined(deck));
    host.shutdown();
}

TEST_CASE("decks: the Speed deck back-dates a drop the classification reports late") {
    // A lap race's session time counts up from the drop; a timed race's counts
    // down from its length, so there the lag is the length less the time
    for (const int lengthMs : { 0, 600'000 }) {
        CAPTURE(lengthMs);
        const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_start_late\\";
        stageCabinet(save);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(RACE, /*numLaps=*/lengthMs > 0 ? 0 : 5, lengthMs);
        host.addEntry(PLAYER, "Alice");
        host.addEntry(22, "Bob");
        host.runInit(RACE);
        host.runStart();
        // The gate dropped at 0.25 s, but the classification saying so arrives at
        // 0.5 s carrying 250 ms of race time (a slow server's rate): the bike
        // past REACT_SPEED at 0.68 s is 0.43 s after the real drop, not 0.18 s
        host.raceSessionState(RACE, 256, lengthMs);
        grid(host, 256, lengthMs);
        host.raceSessionState(RACE, 16, lengthMs);
        grid(host, 0x20, lengthMs);
        host.setMotionNowUs(500'000);
        grid(host, 16, lengthMs > 0 ? lengthMs - 250 : 250);
        host.setMotionNowUs(680'000);
        host.drawWithState(0);   // on track first: the switch clears telemetry
        auto rows = host.hudStringRows("speed_deck");
        CHECK_MESSAGE(!has(rows, "REACT 0.43"), "no reaction before the bike moves: " << joined(rows));
        TelemetryRow r;
        r.speed = 4.0f;
        r.time = 0.68f;
        r.gear = 1;
        host.telemetryFrame(r);
        host.drawWithState(0);
        rows = host.hudStringRows("speed_deck");
        CHECK_MESSAGE(has(rows, "REACT 0.43"), "timed from the real drop, not the classification's arrival: " << joined(rows));
        host.shutdown();
    }
}

TEST_CASE("decks: the Timing deck holds a split with its top speed, and an invalid lap sets no best") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_timing\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[TimingDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    host.drawWithState(0);
    TrapRide ride{ host };
    // From the line: a sector is whole only from a crossing seen
    host.raceLap(RACE, PLAYER, 1, 91000);
    host.drawWithState(0);
    // 40 m/s through the first sector: 89 mph (the Speed widget's default
    // unit) on the chip, the split's own time held in thousandths
    ride(40.0f);
    host.raceSplit(RACE, PLAYER, 2, 0, 30000);
    host.drawWithState(0);
    auto rows = host.hudStringRows("timing_deck");
    CHECK_MESSAGE(has(rows, "89 MPH"), joined(rows));
    CHECK_MESSAGE(has(rows, "30.000"), "the split held: " << joined(rows));
    // 50 m/s, then the lap ends invalid: its last sector sets no best
    ride(50.0f);
    host.raceLap(RACE, PLAYER, 2, 90000, 0, 30000, 60000, /*invalid=*/true);
    host.drawWithState(0);
    // 30 m/s through the next lap's first sector: 67 mph, 22 down on the best
    // through it (the 89 of the lap before)
    ride(30.0f);
    host.raceSplit(RACE, PLAYER, 3, 0, 30000);
    host.drawWithState(0);
    rows = host.hudStringRows("timing_deck");
    CHECK_MESSAGE(has(rows, "67 MPH"), joined(rows));
    CHECK_MESSAGE(has(rows, "-22"), "against the sector's best: " << joined(rows));
    host.shutdown();
}

// The Timing deck's sector parts came from the split list alone, which
// is empty on GP Bikes (its markers are switched off) -- and was on any
// MX Bikes track whose start/finish sits at 0 m, until the split list read
// the markers as the map does (gap_bar_splits_test). It now reads the Gap
// Bar's splits: the markers, else where the rider was seen crossing each.
TEST_CASE("decks: the Timing deck cuts its bar at a split learned from a crossing, with no markers sent") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_timing_learn\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[TimingDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);   // no TrackCenterline: no split markers
    host.raceLap(RACE, PLAYER, 1, 91000);
    host.drawWithState(0);
    // The meter's pieces: the deck's lowest quads (the strip along its foot)
    const auto meterPieces = [&host]() {
        const auto quads = host.hudQuadRects("timing_deck");
        double bottom = 0.0;
        for (const auto& q : quads) bottom = std::max(bottom, q.b);
        double top = bottom;
        for (const auto& q : quads) if (q.b >= bottom - 1e-4) top = std::min(top, q.t);
        int n = 0;
        for (const auto& q : quads) if (q.t >= top - 1e-4 && q.b <= bottom + 1e-4) ++n;
        return n;
    };
    host.raceTrackPosition({ { .num = PLAYER, .trackPos = 0.10f }, { .num = 22, .trackPos = 0.20f } });
    host.drawWithState(0);
    const int whole = meterPieces();
    // Split 1 crossed at 40% of the lap: the bar is cut there from now on
    host.raceTrackPosition({ { .num = PLAYER, .trackPos = 0.40f }, { .num = 22, .trackPos = 0.50f } });
    host.raceSplit(RACE, PLAYER, 2, 0, 30000);
    host.drawWithState(0);
    host.raceTrackPosition({ { .num = PLAYER, .trackPos = 0.45f }, { .num = 22, .trackPos = 0.55f } });
    host.drawWithState(0);
    const int cut = meterPieces();
    CHECK_MESSAGE(cut > whole, "the learned split cuts the bar: " << whole << " pieces before, " << cut << " after");
    host.shutdown();
}

TEST_CASE("decks: the Gap deck sets each sector against its best, tags a new one, and skips a folded run") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_gap\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[GapDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    // Struck, so it sets no best: the bests are PluginData's, valid laps only
    host.raceLap(RACE, PLAYER, 1, 91000, 0, -1, -1, /*invalid=*/true);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 2, 0, 30000);
    host.drawWithState(0);
    auto rows = host.hudStringRows("gap_deck");
    CHECK_MESSAGE(has(rows, "S1"), joined(rows));
    CHECK_MESSAGE(!has(rows, "BEST S1"), "a sector's first run has nothing to beat: " << joined(rows));
    host.raceLap(RACE, PLAYER, 2, 90000, 0, 30000, 60000);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 3, 0, 29500);
    host.drawWithState(0);
    rows = host.hudStringRows("gap_deck");
    CHECK_MESSAGE(has(rows, "-0.500"), "half a second under the sector's best: " << joined(rows));
    CHECK_MESSAGE(has(rows, "BEST S1"), "a new best sector is tagged: " << joined(rows));
    CHECK_MESSAGE(has(rows, "VS PB"), "the General tab's reference names the hero: " << joined(rows));
    // A lap with no split reported is not a 90 s S3: the next whole S3 is set
    // against lap 2's 30 s, the session's best, never against 90 s
    // (unfixed: -30.000 and a BEST S3 tag)
    host.raceLap(RACE, PLAYER, 3, 90000, 0, 29500, 59500);
    host.drawWithState(0);
    host.raceLap(RACE, PLAYER, 4, 90000);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 5, 0, 30000);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 5, 1, 60000);
    host.drawWithState(0);
    host.raceLap(RACE, PLAYER, 5, 90000, 0, 30000, 60000);
    host.drawWithState(0);
    rows = host.hudStringRows("gap_deck");
    CHECK_MESSAGE(has(rows, "S3"), joined(rows));
    CHECK_MESSAGE(!has(rows, "BEST S3"), "a lap with no splits counted as one sector: " << joined(rows));
    CHECK_MESSAGE(!has(rows, "-30.000"), joined(rows));
    CHECK_MESSAGE(has(rows, "+0.000"), "level with lap 2's S3: " << joined(rows));
    host.shutdown();
}

// The Gap deck's bests are PluginData's, the ones Timing, Lap Log and the
// Pitboard read: a best set while the deck was hidden still counts. It used to
// keep its own, from the sectors it saw run, and so disagreed after a lap away.
TEST_CASE("decks: the Gap deck sets a sector against a best it never saw") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_gap_hidden\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[GapDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    REQUIRE(host.setHudVisible("gap_deck", false));
    host.raceLap(RACE, PLAYER, 1, 91000);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 2, 0, 30000);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 2, 1, 60000);
    host.drawWithState(0);
    // Back for the line, which starts the next sector's clock: it saw none of
    // lap 2's sectors run
    REQUIRE(host.setHudVisible("gap_deck", true));
    host.drawWithState(0);
    host.raceLap(RACE, PLAYER, 2, 90000, 0, 30000, 60000);
    host.drawWithState(0);
    host.raceSplit(RACE, PLAYER, 3, 0, 29000);
    host.drawWithState(0);
    const auto rows = host.hudStringRows("gap_deck");
    CHECK_MESSAGE(has(rows, "-1.000"), "against the 30 s set while hidden: " << joined(rows));
    CHECK_MESSAGE(has(rows, "BEST S1"), joined(rows));
    host.shutdown();
}

TEST_CASE("decks: an invalid lap's last sector sets no Timing trap best and no Gap best") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_invalid_best\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[TimingDeck]\nvisible=1\n[GapDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    host.drawWithState(0);
    TrapRide ride{ host };
    // Both splits reported, then the line after `last` ms through S3 at `speed`
    const auto lap = [&](int num, float speed, int last, bool invalid) {
        host.raceSplit(RACE, PLAYER, num, 0, 30000);
        host.drawWithState(0);
        host.raceSplit(RACE, PLAYER, num, 1, 60000);
        host.drawWithState(0);
        ride(speed);
        host.raceLap(RACE, PLAYER, num, 60000 + last, 0, 30000, 60000, invalid);
        host.drawWithState(0);
    };
    host.raceLap(RACE, PLAYER, 1, 91000, 0, -1, -1, /*invalid=*/true);   // sets no best
    host.drawWithState(0);
    // The best S3: 30 s at 40 m/s (89 mph)
    lap(2, 40.0f, 30000, false);
    // Faster on both counts, but the lap is invalid: shown, never kept
    lap(3, 50.0f, 29000, true);
    auto rows = host.hudStringRows("gap_deck");
    CHECK_MESSAGE(!has(rows, "BEST S3"), "an invalid lap's sector is no best: " << joined(rows));
    // 45 m/s (101 mph) in 29.5 s, valid: measured against the valid best
    // (unfixed: against the invalid lap's, -11 and +0.500)
    lap(4, 45.0f, 29500, false);
    rows = host.hudStringRows("timing_deck");
    CHECK_MESSAGE(has(rows, "101 MPH"), joined(rows));
    CHECK_MESSAGE(has(rows, "+12"), "against the valid lap's 89: " << joined(rows));
    rows = host.hudStringRows("gap_deck");
    CHECK_MESSAGE(has(rows, "-0.500"), "against the valid lap's 30 s: " << joined(rows));
    CHECK_MESSAGE(has(rows, "BEST S3"), joined(rows));
    host.shutdown();
}

TEST_CASE("decks: the Rival deck defends against a closer rider behind, with a margin both ways") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_defend\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RivalDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Cleo");
    host.runInit(RACE);
    // The player second, 1.5 s behind Bob; Cleo `behind` ms further back
    int lapNum = 0;
    const auto at = [&](int behind) {
        ++lapNum;
        host.classify(RACE, lapNum * 90000, {
            { .num = 22, .best = 90000, .laps = lapNum, .gap = 0 },
            { .num = PLAYER, .best = 91000, .laps = lapNum, .gap = 1500 },
            { .num = 33, .best = 92000, .laps = lapNum, .gap = 1500 + behind },
        });
        host.draw();
        host.draw();
        return host.hudStringRows("rival_deck");
    };
    auto rows = at(2000);
    CHECK_MESSAGE(has(rows, "CHASING"), joined(rows));
    CHECK_MESSAGE(has(rows, "#22"), joined(rows));
    // Chasing, the gap is the one ahead (stepRival measures both)
    CHECK_MESSAGE(has(rows, "1.50 s"), "the gap to Bob: " << joined(rows));
    // Closer behind, but not by the margin: still chasing
    rows = at(1450);
    CHECK_MESSAGE(has(rows, "CHASING"), "within the margin, the choice holds: " << joined(rows));
    CHECK_MESSAGE(has(rows, "#22"), joined(rows));
    rows = at(500);
    CHECK_MESSAGE(has(rows, "DEFENDING"), joined(rows));
    CHECK_MESSAGE(has(rows, "#33"), "the rider behind as the hero: " << joined(rows));
    // Further behind than ahead, but not by the margin: still defending
    rows = at(1550);
    CHECK_MESSAGE(has(rows, "DEFENDING"), "within the margin, the choice holds: " << joined(rows));
    CHECK_MESSAGE(has(rows, "#33"), joined(rows));
    rows = at(2000);
    CHECK_MESSAGE(has(rows, "CHASING"), joined(rows));
    host.shutdown();
}

TEST_CASE("decks: the Spin gauge reads the rear wheel's slip and times it") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_spin\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpinDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.runInit(RACE);
    host.runStart();
    host.drawWithState(0);   // on track first: the switch clears telemetry
    // The bike at 5 m/s, its rear wheel turning 10% faster, except one
    // sample at 50% (a burst of wheelspin)
    const auto sample = [&](int ms, float rear, int rearMaterial = 1) {
        host.setMotionNowUs(static_cast<int64_t>(ms) * 1000);
        TelemetryRow r;
        r.time = static_cast<float>(ms) / 1000.0f;
        r.gear = 1;
        r.speed = 5.0f;
        r.wheelSpeedRear = rear;
        r.rearMaterial = rearMaterial;
        r.roll = static_cast<float>(ms) / 1000.0f;   // a FRESH sample: the decks skip a repeated one
        host.telemetryFrame(r);
        host.drawWithState(0);
    };
    // Every step is SPIN_SMOOTH_S long, so the shown share is the raw one.
    // The first sample only starts the clock: nine steps, then the burst
    for (int ms = 600; ms <= 1500; ms += 100) sample(ms, 5.5f);
    sample(1600, 7.5f);
    auto spin = host.hudStringRows("spin_deck");
    CHECK_MESSAGE(has(spin, "50%"), "the live slip: " << joined(spin));
    CHECK_MESSAGE(has(spin, "1.0"), "slipping past HOOK_FROM for a second: " << joined(spin));
    const unsigned long spinningInk = colourOf(host, "spin_deck", "50%");
    for (int ms = 1700; ms <= 2600; ms += 100) sample(ms, 5.5f);
    spin = host.hudStringRows("spin_deck");
    CHECK_MESSAGE(has(spin, "10%"), "the live slip: " << joined(spin));
    // Plain values: spinning and hooked read in the same ink, the blocks
    // carry the state
    CHECK(spinningInk != 0);
    CHECK(colourOf(host, "spin_deck", "10%") == spinningInk);
    CHECK_MESSAGE(after(spin, "MAX") == "2.0", "the longest rises with the spell: " << joined(spin));
    // Off the ground the wheel spins freely: it reads 0, as the rumble reads
    // it, and the spell is over. One sample off over a bump used to read "no
    // reading" and flick the gauge to the dash and back.
    sample(2700, 20.0f, /*rearMaterial=*/0);
    spin = host.hudStringRows("spin_deck");
    CHECK_MESSAGE(!has(spin, "-"), "a bump is not no reading: " << joined(spin));
    sample(2800, 20.0f, /*rearMaterial=*/0);
    spin = host.hudStringRows("spin_deck");
    CHECK_MESSAGE(has(spin, "0%"), joined(spin));
    CHECK_MESSAGE(!has(spin, "100%"), joined(spin));
    CHECK_MESSAGE(after(spin, "MAX") == "2.0", "the longest is kept: " << joined(spin));
    host.shutdown();
}

TEST_CASE("decks: the Decks master switch off stops the Inputs deck's history") {
    // The deck hidden by its master switch draws nothing, so it must not keep
    // PluginData filling the history for it (isTelemetryHistoryNeeded)
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_inputs_master\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasTelemetrySurfaces());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[InputsDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.setMasterEnabled(PluginHost::Master::Decks, false);
    openRace(host);
    for (int i = 0; i < 30; ++i) {
        TelemetryRow r;
        r.time = 2.0f + static_cast<float>(i) * 0.01f;
        r.gear = 3;
        r.speed = 20.0f;
        r.throttle = 0.5f;
        host.telemetryFrame(r);
        host.draw();
    }
    CHECK_MESSAGE(host.tmHistoryDepth() == 0, "history depth " << host.tmHistoryDepth());
    host.shutdown();
}

// The left edge of a column filled to `share` of its track (a fill on the
// track: the same width and bottom); -1 without one
double filledLeft(const std::vector<PluginHost::QuadRect>& quads, double share) {
    for (const auto& track : quads)
        for (const auto& fill : quads) {
            const double th = track.b - track.t, fh = fill.b - fill.t;
            if (th <= 0.0 || &track == &fill) continue;
            if (std::fabs(track.l - fill.l) < 1e-4 && std::fabs(track.r - fill.r) < 1e-4 &&
                std::fabs(track.b - fill.b) < 1e-4 && std::fabs(fh / th - share) < 0.02)
                return fill.l;
        }
    return -1.0;
}

TEST_CASE("decks: the Inputs deck fills the shared history and draws it") {
    // The deck draws the Telemetry HUD's history, which PluginData fills only
    // while something draws it (HudManager::isTelemetryHistoryNeeded): the
    // deck on its own must be enough, or its lines stay empty
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_inputs\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasTelemetrySurfaces());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[InputsDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    const size_t before = host.hudQuadRects("inputs_deck").size();
    // The throttle opening to half, the front brake off
    for (int i = 0; i < 30; ++i) {
        TelemetryRow r;
        r.time = 2.0f + static_cast<float>(i) * 0.01f;
        r.gear = 3;
        r.speed = 20.0f;
        r.throttle = std::min(static_cast<float>(i) / 20.0f, 0.5f);
        r.roll = static_cast<float>(i) * 0.1f;
        host.telemetryFrame(r);
        host.draw();
    }
    CHECK_MESSAGE(host.tmHistoryDepth() >= 30, "history depth " << host.tmHistoryDepth());
    const auto quads = host.hudQuadRects("inputs_deck");
    CHECK_MESSAGE(quads.size() > before + 1, "the throttle's line and fill: " << before << " -> " << quads.size());
    // The throttle's column: a fill on its track, the same width and bottom,
    // half its height
    const double halfAt = filledLeft(quads, 0.5);
    CHECK_MESSAGE(halfAt >= 0.0, "no column filled to half its track");
    // The lines scroll: the throttle held steady still moves them (the
    // history's revision is in the key; the shown values alone do not change)
    const auto held = [&](int from, int n) {
        for (int i = from; i < from + n; ++i) {
            TelemetryRow r;
            r.time = 2.0f + static_cast<float>(i) * 0.01f;
            r.gear = 3;
            r.speed = 20.0f;
            r.throttle = 0.5f;
            r.roll = static_cast<float>(i) * 0.1f;
            host.telemetryFrame(r);
            host.draw();
        }
    };
    held(30, 1);
    const auto steady = host.hudQuadRects("inputs_deck");
    held(31, 5);
    const auto scrolled = host.hudQuadRects("inputs_deck");
    bool moved = steady.size() != scrolled.size();
    for (size_t i = 0; !moved && i < steady.size(); ++i)
        moved = std::fabs(steady[i].l - scrolled[i].l) > 1e-6 || std::fabs(steady[i].r - scrolled[i].r) > 1e-6;
    CHECK_MESSAGE(moved, "the lines stood still while the history grew");
    // Off the throttle, the front brake at a quarter: its own column, right
    // of the throttle's (inputs-meter-4)
    for (int i = 36; i < 60; ++i) {
        TelemetryRow r;
        r.time = 2.0f + static_cast<float>(i) * 0.01f;
        r.gear = 3;
        r.speed = 20.0f;
        r.frontBrake = 0.25f;
        host.telemetryFrame(r);
        host.draw();
    }
    const auto braking = host.hudQuadRects("inputs_deck");
    CHECK_MESSAGE(filledLeft(braking, 0.5) < 0.0, "the throttle's column still half full");
    CHECK_MESSAGE(filledLeft(braking, 0.25) > halfAt, "the brake's column, filled to a quarter, right of the throttle's");
    host.shutdown();
}

TEST_CASE("decks: the Inputs deck's show edge starts the history fresh, unless another user draws it") {
    // The Telemetry HUD, the Dash's Telemetry page and the Inputs deck draw one
    // history; each clears it when it starts drawing it, but never under another
    // one still drawing it (HudManager::startTelemetryHistory)
    const auto pump = [](PluginHost& host, int n) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.time = 3.0f + static_cast<float>(i) * 0.01f;
            r.speed = 20.0f;
            r.throttle = 0.25f + 0.3f * static_cast<float>(i % 2);
            r.roll = static_cast<float>(i) * 0.1f;
            host.telemetryFrame(r);
            host.draw();
        }
    };
    SUBCASE("alone: shown again, it starts fresh") {
        const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_inputs_edge\\";
        stageCabinet(save);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasTelemetrySurfaces());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[InputsDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.tmSetVisible(false);
        openRace(host);
        pump(host, 40);
        REQUIRE(host.tmHistoryDepth() >= 40);
        REQUIRE(host.setHudVisible("inputs_deck", false));
        host.draw();
        REQUIRE(host.setHudVisible("inputs_deck", true));
        host.draw();
        CHECK(host.tmHistoryDepth() == 0);
        host.shutdown();
    }
    SUBCASE("under the Dash's Telemetry page: the page's lines are kept") {
        const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_inputs_dash\\";
        stageCabinet(save);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasTelemetrySurfaces());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[DashHud]\nvisible=1\npage=telemetry\npage_names=0\n");
        startDecks(host, save.c_str());
        host.tmSetVisible(false);
        host.session(1, 0);   // as dash_test's Telemetry page: no race, no notice over it
        host.draw();
        REQUIRE(host.dashPage() == 12);
        pump(host, 40);
        const int depth = host.tmHistoryDepth();
        REQUIRE(depth >= 40);
        REQUIRE(host.setHudVisible("inputs_deck", true));
        host.draw();
        CHECK(host.tmHistoryDepth() >= depth);
        host.shutdown();
    }
    SUBCASE("the Telemetry HUD shown over it: the deck's lines are kept") {
        const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_inputs_tm\\";
        stageCabinet(save);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasTelemetrySurfaces());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[InputsDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.tmSetVisible(false);
        openRace(host);
        pump(host, 40);
        const int depth = host.tmHistoryDepth();
        REQUIRE(depth >= 40);
        host.tmSetVisible(true);
        host.draw();
        CHECK(host.tmHistoryDepth() >= depth);
        host.shutdown();
    }
}

TEST_CASE("decks: a crash ends the Spin gauge's timer and adds no spin") {
    // The bike is down with its rear wheel flat out: crashed, that is no
    // spin, live or in the timer
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_spin_crash\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpinDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.runInit(RACE);
    host.runStart();
    host.drawWithState(0);   // on track first: the switch clears telemetry
    const auto sample = [&](int ms, float rear) {
        host.setMotionNowUs(static_cast<int64_t>(ms) * 1000);
        TelemetryRow r;
        r.time = static_cast<float>(ms) / 1000.0f;
        r.gear = 1;
        r.speed = 5.0f;
        r.wheelSpeedRear = rear;
        r.roll = static_cast<float>(ms) / 1000.0f;   // a FRESH sample
        host.telemetryFrame(r);
        host.drawWithState(0);
    };
    for (int ms = 600; ms <= 1000; ms += 100) sample(ms, 5.5f);
    host.raceTrackPosition({ { .num = PLAYER, .trackPos = 0.1f, .crashed = 1 } });
    for (int ms = 1100; ms <= 1500; ms += 100) sample(ms, 10.0f);
    auto spin = host.hudStringRows("spin_deck");
    CHECK_MESSAGE(!has(spin, "100%"), "crashed: no live share: " << joined(spin));
    CHECK_MESSAGE(after(spin, "MAX") == "0.4", "the spell before the crash, not one through it: " << joined(spin));
    host.shutdown();
}

// The Session deck: the Session HUD's rows (server, track, format), shouted,
// in every shipped style. Fails if the deck stops reading the Session HUD's
// helpers.
TEST_CASE("decks: the Session deck shows the server, track and format") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_session\\";
    stageCabinet(save);
    for (const char* style : { "cabinet", "slant" }) {
        INFO("style " << style);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        std::string ini = "[Settings]\nversion=11\n";
        ini += std::string("[SessionDeck]\nvisible=1\n") + deckIni(style);
        host.writeSettingsFile(save.c_str(), ini);
        startDecks(host, save.c_str());
        // Online, so the server's own name is the label
        host.eventInit("Southwick", "Alice", 1600.0f, 2, "Test 450", "MX1", "", /*serverType=*/1, "MXB-Club EU #2");
        host.raceEvent("Southwick");
        host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
        host.addEntry(PLAYER, "Alice");
        host.addEntry(22, "Bob");
        host.draw();

        const auto rows = host.hudStringRows("session_deck");
        CHECK_MESSAGE(has(rows, "SESSION"), joined(rows));
        CHECK_MESSAGE(has(rows, "MXB-CLUB EU #2"), "the server: " << joined(rows));
        CHECK_MESSAGE(after(rows, "TRACK") == "SOUTHWICK", joined(rows));
        // The length only: the session and its state are stamps on a change
        CHECK_MESSAGE(after(rows, "FORMAT") == "5 LAPS", "the format: " << joined(rows));
        host.shutdown();
    }
}

// The Session deck's server label counts the riders where the game names no
// server ("Unknown" alone, "Online" with company), and a rider joining changes
// no session data -- only the entry list. Fails if the deck stops rebuilding
// on RaceEntries (handlesDataType): the label would stay "UNKNOWN".
TEST_CASE("decks: the Session deck's server label follows a rider joining") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_session_join\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SessionDeck]\nvisible=1\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    // No server type and no name (GP Bikes / KRP, or spectating): the count decides
    host.eventInit("Southwick", "Alice", 1600.0f, 2, "Test 450", "MX1", "", /*serverType=*/-1, "");
    host.raceEvent("Southwick");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.draw();
    auto rows = host.hudStringRows("session_deck");
    CHECK_MESSAGE(has(rows, "UNKNOWN"), "alone: " << joined(rows));

    host.addEntry(22, "Bob");
    host.draw();
    rows = host.hudStringRows("session_deck");
    CHECK_MESSAGE(has(rows, "ONLINE"), "a rider joined: " << joined(rows));
    host.shutdown();
}

// A deck held back misses the notifications it gathers on, so the first frame
// it is drawn again gathers afresh. The Session deck's key does not hold the
// field size (a notification does), so this fails if DeckHud::update stops
// marking a deck dirty on its way back from the hidden path.
TEST_CASE("decks: a deck held back while its data changes shows the change the frame it returns") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_session_held\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SessionDeck]\nvisible=1\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.eventInit("Southwick", "Alice", 1600.0f, 2, "Test 450", "MX1", "", /*serverType=*/-1, "");
    host.raceEvent("Southwick");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.draw();
    auto rows = host.hudStringRows("session_deck");
    CHECK_MESSAGE(has(rows, "UNKNOWN"), "alone: " << joined(rows));

    // The Decks switch off while Bob joins, then on again: one frame
    host.setMasterEnabled(PluginHost::Master::Decks, false);
    host.draw();
    host.addEntry(22, "Bob");
    host.draw();
    host.setMasterEnabled(PluginHost::Master::Decks, true);
    host.draw();
    rows = host.hudStringRows("session_deck");
    CHECK_MESSAGE(has(rows, "ONLINE"), "the rider who joined while held back: " << joined(rows));
    host.shutdown();
}

// The Session deck stamps the session, or its state, when it changes -- the
// FORMAT row carries only the length. Nothing is stamped for what was there
// when the deck came up. Fails if the stamp is lost, or raised on the first
// look.
TEST_CASE("decks: the Session deck stamps a new session and a new state") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_session_stamp\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SessionDeck]\nvisible=1\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.eventInit("Southwick", "Alice");
    host.raceEvent("Southwick");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.draw();
    host.draw();
    auto rows = host.hudStringRows("session_deck");
    for (const char* stamp : { "RACE 1", "IN PROGRESS", "WAITING" })
        CHECK_MESSAGE(!has(rows, stamp), "no stamp for the first look: " << joined(rows));

    // The state changes: it is stamped, as the Session HUD names it
    host.raceSessionState(RACE, /*state=*/512);
    host.draw();
    rows = host.hudStringRows("session_deck");
    CHECK_MESSAGE(has(rows, "RACE OVER"), "the new state, stamped: " << joined(rows));
    CHECK_MESSAGE(after(rows, "FORMAT") == "5 LAPS", "the row keeps the length only: " << joined(rows));

    // A new session: the session is stamped, not its state
    host.session(RACE + 1, /*numLaps=*/5, /*lengthMs=*/0);
    host.draw();
    rows = host.hudStringRows("session_deck");
    CHECK_MESSAGE(has(rows, "RACE 2"), "the new session, stamped: " << joined(rows));
    host.shutdown();
}

// Bars and Stamps (Settings > Decks > Style), one switch each for every deck,
// saved in [Decks]. Bars off drops the Race deck's progress bar (and its well)
// but not the Speed deck's revs, which are its readout; Stamps off drops the
// Session deck's state stamp. Fails if a switch misses a deck, drops a
// readout meter, or is not saved where it is read.
TEST_CASE("decks: Bars and Stamps off drop every deck's bar and stamp, not its readout") {
    struct Frame { size_t race = 0, speed = 0; bool stamped = false; };
    const auto frame = [](const char* tag, const char* parts) {
        const std::string save = std::string("Z:\\tmp\\mxbmrp3-tests\\deck_parts_") + tag + "\\";
        stageCabinet(save);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RaceDeck]\nvisible=1\n[SpeedDeck]\nvisible=1\n"
                                             "[SessionDeck]\nvisible=1\n" + deckIni("cabinet") + parts);
        startDecks(host, save.c_str());
        openRace(host);
        host.raceSessionState(RACE, /*state=*/512);   // the Session deck stamps the new state
        host.draw();
        Frame f;
        f.race = host.hudQuadRects("race_deck").size();
        f.speed = host.hudQuadRects("speed_deck").size();
        f.stamped = has(host.hudStringRows("session_deck"), "RACE OVER");
        // Saved once, in [Decks], as read
        REQUIRE(host.save());
        const ini::Map saved = ini::parse(ini::readFile(save + "mxbmrp3\\mxbmrp3_settings.ini"));
        const bool off = std::strcmp(tag, "off") == 0;
        for (const char* key : { "bars", "stamps" }) {
            const auto it = saved.find({ "Decks", key });
            REQUIRE_MESSAGE(it != saved.end(), key);
            CHECK(it->second == (off ? "0" : "1"));
        }
        host.shutdown();
        return f;
    };
    const Frame on = frame("on", "");
    const Frame off = frame("off", "bars=0\nstamps=0\n");
    CHECK(on.stamped);
    CHECK_FALSE(off.stamped);
    CHECK_MESSAGE(off.race < on.race, "the progress bar goes: " << on.race << " -> " << off.race);
    CHECK_MESSAGE(off.speed == on.speed, "the revs stay: " << on.speed << " -> " << off.speed);
}

// Switched on the Decks tab while the decks are up, every deck's bar goes the
// next frame - not only the deck the switch names for its own redraw (Speed,
// the first deck), and not at each deck's next data change. Measured on the
// Race deck for that reason; fails if DeckHud::setBars stops dirtying
// every deck.
TEST_CASE("decks: Bars switched on the Decks tab redraws every deck at once") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_parts_live\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RaceDeck]\nvisible=1\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    openRace(host);
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Decks"));
    host.draw();
    // Style leads the tab, so its switches are the first three arrow pairs:
    // Slanted, Bars, Stamps
    const std::string sig = host.regionSignature();
    const size_t slantedAt = sig.find(":decks.slanted;"), barsAt = sig.find(":decks.bars;");
    REQUIRE(slantedAt != std::string::npos);
    REQUIRE(barsAt > slantedAt);
    REQUIRE(sig.rfind(":common.", slantedAt) == std::string::npos);
    const int bars = 1;
    const size_t on = host.hudQuadRects("race_deck").size();
    REQUIRE(host.clickCycle(bars, /*up=*/true));
    host.draw();
    CHECK_MESSAGE(host.hudQuadRects("race_deck").size() < on, "the bar gone at once: " << on);
    REQUIRE(host.clickCycle(bars, /*up=*/true));
    host.draw();
    CHECK(host.hudQuadRects("race_deck").size() == on);
    host.shutdown();
}

// The Notice deck alone: a hotkey's message on the deck, not on the toast
// card, which stands down while the deck is on and comes back with it off.
// Fails if the card and the deck each take their own toast (two places, or
// the queue split between them), or the card keeps drawing under the deck.
TEST_CASE("decks: the Notice deck carries the toasts and the toast card stands down") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_notice\\";
    stageCabinet(save);
    // The message's icon, so the deck has one to draw
    CreateDirectoryA((save + "mxbmrp3\\icons").c_str(), nullptr);
    REQUIRE(CopyFileA(MXB_REPO_DATA_DIR "/icons/eye-slash.tga", (save + "mxbmrp3\\icons\\eye-slash.tga").c_str(), FALSE) != 0);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMessages());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n[NoticeDeck]\nvisible=1\n" + deckIni("cabinet") +
        "[Hotkeys]\nstandings_key=112\nstandings_mod=0\n");
    startDecks(host, save.c_str());
    host.messagesStartup(/*fresh=*/false, "1.0", "1.0");
    // On track with a setup loaded: no notice up. Staging the pack earned an
    // achievement, which the deck shows, under its tab, not the card
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 0);
    REQUIRE(host.setHudVisibleBoth("standings_hud", true));
    host.draw();
    host.draw();
    const auto earned = host.hudStringRows("notice_deck");
    CHECK_MESSAGE(has(earned, "ACHIEVEMENT"), joined(earned));
    // Each notice draws its own icon (fails if the icon cache keeps the
    // first name's sprite)
    const auto sprites = [&] {
        std::vector<int> out;
        const int count = static_cast<int>(host.hudQuadRects("notice_deck").size());
        for (int i = 0; i < count; ++i) out.push_back(host.quadSprite("notice_deck", i));
        std::sort(out.begin(), out.end());
        return out;
    };
    const std::vector<int> achievementIcon = sprites();
    CHECK(host.hudHeldBack("achievement_widget") == 1);

    // A message takes the deck from the achievement, which waits ("+n")
    REQUIRE(host.injectHotkey("standings"));
    host.draw();
    host.draw();
    CHECK(host.messageShowing() == "Standings hidden");
    const auto rows = host.hudStringRows("notice_deck");
    CHECK_MESSAGE(has(rows, "MESSAGE"), joined(rows));
    CHECK_MESSAGE(has(rows, "STANDINGS HIDDEN"), joined(rows));
    CHECK_MESSAGE(hasPrefix(rows, '+'), joined(rows));
    // The message's own icon (every "hidden" card wears eye-slash), not the
    // achievement's left in the cache
    const int eyeSlash = host.iconSpriteForName("eye-slash");
    REQUIRE(eyeSlash > 0);
    CHECK_FALSE(std::count(achievementIcon.begin(), achievementIcon.end(), eyeSlash));
    const std::vector<int> messageIcon = sprites();
    CHECK_MESSAGE(std::count(messageIcon.begin(), messageIcon.end(), eyeSlash) == 1, "the message's own icon drawn");
    // The detail runs over two lines where the first runs out of room
    CHECK_MESSAGE(joined(rows).find("[Press F1 to show it][again]") != std::string::npos, joined(rows));
    CHECK(host.hudHeldBack("achievement_widget") == 1);
    CHECK(host.hudStringRows("achievement_widget").empty());

    // The deck off: the card takes the same toast back, nothing lost
    REQUIRE(host.setHudVisibleBoth("notice_deck", false));
    host.draw();
    host.draw();
    CHECK(host.hudHeldBack("achievement_widget") == 0);
    CHECK(host.messageShowing() == "Standings hidden");
    CHECK_MESSAGE(has(host.hudStringRows("achievement_widget"), "Standings hidden"),
                  joined(host.hudStringRows("achievement_widget")));
    // ...and so with the deck on but the Decks switch off
    REQUIRE(host.setHudVisibleBoth("notice_deck", true));
    host.draw();
    host.draw();
    CHECK(host.hudHeldBack("achievement_widget") == 1);
    host.setMasterEnabled(PluginHost::Master::Decks, false);
    host.draw();
    host.draw();
    CHECK(host.hudHeldBack("achievement_widget") == 0);
    CHECK_MESSAGE(has(host.hudStringRows("achievement_widget"), "Standings hidden"),
                  joined(host.hudStringRows("achievement_widget")));
    host.shutdown();
}

// The Fuel widget (which the Fuel deck borrows its unit from) rebuilds on
// new data only: frames with no telemetry between them redraw what it built.
// Fails if FuelWidget::update rebuilds every frame while shown, as it did
// while it also ran the fuel history (since moved into PluginData).
TEST_CASE("decks: the Fuel widget rebuilds on new data, not every frame") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\fuel_widget_dirty\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[FuelWidget]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    host.bmSetVisible(true);   // the rebuild counter moves only while the profiler runs
    REQUIRE(host.hudRebuildCount("fuel_widget") >= 0);
    host.draw();
    const int before = host.hudRebuildCount("fuel_widget");
    for (int i = 0; i < 20; ++i) host.draw();
    CHECK_MESSAGE(host.hudRebuildCount("fuel_widget") == before, "no new data, no rebuild");
    TelemetryRow t;
    t.time = 3.0f;
    t.gear = 3;
    t.rpm = 6000;
    host.telemetryFrame(t);
    host.draw();
    CHECK_MESSAGE(host.hudRebuildCount("fuel_widget") > before, "new telemetry, a rebuild");
    host.shutdown();
}

// Hidden, the Fuel widget keeps a change for when it is drawn. Shown in game,
// setVisible marks it dirty anyway; the companion surface coming up does
// not, so a widget shown only there would draw what it built before. Fails if
// FuelWidget::update drops the change while hidden.
TEST_CASE("decks: the Fuel widget hidden keeps a change for the surface that shows it") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\fuel_widget_hidden\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[FuelWidget]\nvisible=0\ncompanionConfigured=1\ncompanionVisible=1\n");
    startDecks(host, save.c_str());
    host.setDisplayTarget(2 /*BOTH*/);
    // A tank to measure against (openRace's bike has none)
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 7.0f);
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.runInit(RACE);
    host.drawWithState(0);   // on track first: the switch clears telemetry
    // New telemetry while it is on no surface (the companion is closed)
    TelemetryRow t;
    t.time = 3.0f;
    t.gear = 3;
    t.rpm = 6000;
    t.speed = 20.0f;
    t.fuel = 4.2f;
    host.telemetryFrame(t);
    host.drawWithState(0);
    const std::string stale = joined(host.hudStringRows("fuel_widget"));
    host.companionWindow(true);
    host.drawWithState(0);
    CHECK_MESSAGE(joined(host.hudStringRows("fuel_widget")) != stale, "the change kept, drawn when the companion shows it: " << stale << " / " << joined(host.hudStringRows("fuel_widget")));
    host.companionWindow(false);
    host.shutdown();
}

// The Decks master switch off stops deck work, not only its drawing: a deck
// whose own row is on gathers and rebuilds nothing while held back, and
// switched on again it gathers afresh, showing what changed meanwhile.
// Fails if DeckHud::update takes its hidden path on visibility alone.
TEST_CASE("decks: the Decks switch off stops a deck's rebuilds") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_master_off\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    openRace(host);
    host.bmSetVisible(true);   // the rebuild counter moves only while the profiler runs
    host.draw();
    REQUIRE(host.hudRebuildCount("speed_deck") >= 0);
    TelemetryRow t;
    t.gear = 3;
    t.rpm = 6000;
    float time = 2.0f;
    const auto ride = [&](float from) {
        for (int i = 0; i < 10; ++i) {
            t.time = time += 0.01f;
            t.speed = from + static_cast<float>(i);   // a new km/h each frame
            host.telemetryFrame(t);
            host.draw();
        }
    };
    int before = host.hudRebuildCount("speed_deck");
    ride(20.0f);
    CHECK_MESSAGE(host.hudRebuildCount("speed_deck") > before, "a shown deck follows the speed");

    host.setMasterEnabled(PluginHost::Master::Decks, false);
    host.draw();
    before = host.hudRebuildCount("speed_deck");
    ride(40.0f);
    CHECK_MESSAGE(host.hudRebuildCount("speed_deck") == before, "held back by the Decks switch: no rebuilds");

    // On again: the speed it missed, at once (49 m/s = 176 km/h, 110 mph)
    host.setMasterEnabled(PluginHost::Master::Decks, true);
    host.draw();
    CHECK(host.hudRebuildCount("speed_deck") > before);
    const auto rows = host.hudStringRows("speed_deck");
    CHECK_MESSAGE((has(rows, "110") || has(rows, "176")), joined(rows));
    host.shutdown();
}

// A notice that calls for action (a rider coming at you) goes ahead of a
// toast: the deck shows the notice under its source's tab and counts the
// toast it holds ("+1"); the toast is taken and kept, not dropped. With the
// Decks tab open and nothing up, the deck shows a preview to place it by.
// A colour no palette uses, so only the Notices HUD's override can produce it
constexpr const char* kNoticesHudInk = "0xFFFF00FF";
constexpr unsigned long kNoticesHudInkValue = 0xFFFF00FFul;

TEST_CASE("decks: an urgent notice goes ahead of a toast, which waits; nothing up previews on the tab") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_notice_q\\";
    stageCabinet(save);
    // Cabinet with a glow on its panel (no shipped style has one): a halo
    // while the notice up calls for action
    const std::string dir = save + "mxbmrp3\\themes\\glowing\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    writeLayout(dir, "[pack]\nbase = cabinet\n[notice-box-1]\nglow = 0.67\n");
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    REQUIRE(CopyFileA(MXB_REPO_DATA_DIR "/textures/glow_1.tga", (save + "mxbmrp3\\textures\\glow_1.tga").c_str(), FALSE) != 0);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasGlow());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n[NoticeDeck]\nvisible=1\n" + deckIni("glowing") +
        "[Hotkeys]\nstandings_key=112\nstandings_mod=0\n"
        // The Notices HUD recoloured: a deck takes no HUD's settings, so its
        // notice keeps the deck's own colour (checked under "rider oncoming")
        "[NoticesHud]\ncolor_warning=" + std::string(kNoticesHudInk) + "\n");
    startDecks(host, save.c_str());
    host.setGlow(true);
    // The toasts' clock, stepped below rather than waited out
    long long now = 1'000'000;
    host.setMotionNowUs(now);
    host.messagesStartup(/*fresh=*/false, "1.0", "1.0");

    SUBCASE("a rider oncoming holds the toast") {
        // The player (#4) at mid-lap and #7 riding backward ahead of them
        // (glow_test's scene): RIDER ONCOMING
        host.eventInit("NoticeTrack", "Player");
        host.raceEvent("NoticeTrack", 2);
        host.session(RACE, 3, 0);
        host.addEntry(4, "Player");
        host.addEntry(22, "Crashed");
        host.addEntry(7, "Backward");
        host.runInit(RACE);
        host.classify(RACE, 300000, { { .num = 4,  .laps = 2, .gap = 0 },
                                      { .num = 22, .laps = 2, .gap = 500 },
                                      { .num = 7,  .laps = 2, .gap = 3000 } });
        REQUIRE(host.setHudVisibleBoth("standings_hud", true));
        auto positions = [&](float wrongWay, int crashed) {
            host.raceTrackPosition({ { .num = 4,  .trackPos = 0.500f, .posZ = 120.0f },
                                     { .num = 22, .trackPos = 0.530f, .crashed = crashed, .posZ = 150.0f },
                                     { .num = 7,  .trackPos = wrongWay, .posZ = 250.0f } });
        };
        positions(0.640f, 0);
        positions(0.635f, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        positions(0.630f, 1);
        // On track (view state 0): the warnings are the rider's own
        host.drawWithState(0);
        host.drawWithState(0);
        REQUIRE(host.injectHotkey("standings"));
        host.drawWithState(0);
        host.drawWithState(0);
        const auto rows = host.hudStringRows("notice_deck");
        CHECK_MESSAGE(has(rows, "ALERT"), joined(rows));
        CHECK_MESSAGE(!has(rows, "STANDINGS HIDDEN"), joined(rows));
        // The notice in the deck's colours, never the recoloured Notices HUD's
        int inked = 0;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            inked += (host.stringColor("notice_deck", i) & 0x00FFFFFFul) == (kNoticesHudInkValue & 0x00FFFFFFul) ? 1 : 0;
        CHECK_MESSAGE(inked == 0, "the Notice deck took the Notices HUD's colour on " << inked << " strings");
        CHECK_MESSAGE(hasPrefix(rows, '+'), "the toasts held behind it: " << joined(rows));
        CHECK(host.messageShowing() == "Standings hidden");   // taken, and kept
        // The halo: a quad drawing the glow texture
        const int glow = host.textureSprite("glow");
        REQUIRE(glow > 0);
        int halos = 0;
        const int quads = static_cast<int>(host.hudQuadRects("notice_deck").size());
        for (int i = 0; i < quads; ++i) halos += host.quadSprite("notice_deck", i) == glow ? 1 : 0;
        CHECK_MESSAGE(halos > 0, "no glow behind the panel");

        // Held past its own four seconds, the toast's clock stopped: when the
        // rider has gone by, it shows for its time from there. Fails if the
        // feed runs the clock under the notice (HudManager's `masked`): the
        // toast would have expired unseen.
        float back = 0.625f;
        for (int i = 0; i < 9; ++i) {
            now += 500'000;
            host.setMotionNowUs(now);
            positions(back, 1);
            back -= 0.005f;
            host.drawWithState(0);
        }
        REQUIRE_MESSAGE(has(host.hudStringRows("notice_deck"), "ALERT"), "the notice outlasted the toast's time");
        // Both behind the player now: nothing ahead to warn of
        const auto clear = [&] {
            host.raceTrackPosition({ { .num = 4,  .trackPos = 0.500f, .posZ = 120.0f },
                                     { .num = 22, .trackPos = 0.300f, .posZ = -300.0f },
                                     { .num = 7,  .trackPos = 0.200f, .posZ = -400.0f } });
        };
        bool shown = false;
        for (int i = 0; i < 60 && !shown; ++i) {
            now += 100'000;
            host.setMotionNowUs(now);
            clear();
            host.drawWithState(0);
            shown = has(host.hudStringRows("notice_deck"), "STANDINGS HIDDEN");
        }
        CHECK_MESSAGE(shown, "the held toast after the notice: " << joined(host.hudStringRows("notice_deck")));
        // ...for its time on the Motion clock, not the wall's: stepped past
        // it, the toast is gone though the test took no time at all
        for (int i = 0; i < 12; ++i) {
            now += 500'000;
            host.setMotionNowUs(now);
            clear();
            host.drawWithState(0);
        }
        CHECK_MESSAGE(!has(host.hudStringRows("notice_deck"), "STANDINGS HIDDEN"),
                      "the toast outlived its time: " << joined(host.hudStringRows("notice_deck")));
    }
    SUBCASE("the Decks tab previews an empty deck") {
        host.eventInit("TestTrack", "Alice");
        host.session(1, 0, 0);
        host.draw();
        CHECK(host.hudStringRows("notice_deck").empty());
        host.showSettings(true);
        REQUIRE(host.openSettingsTab("Decks"));
        host.draw();
        const auto rows = host.hudStringRows("notice_deck");
        CHECK_MESSAGE(has(rows, "PREVIEW"), joined(rows));
        host.showSettings(false);
        host.draw();
        CHECK(host.hudStringRows("notice_deck").empty());
    }
    host.setMotionNowUs(-1);
    host.shutdown();
}

// The Fuel deck: the Fuel widget's numbers, read while the widget itself is off
// (its tracking runs regardless). Three laps of 0.6 L from an 8 L tank: 6.2 L
// left, 0.60 a lap, 10.3 laps. Then the tank near empty: 1.0 L is 1.7 laps,
// under two, so the hero turns warm. Fails if the deck reads the widget only
// while the widget is shown, rounds where the widget does not, or never warms.
TEST_CASE("decks: the Fuel deck shows the laps left, the fuel and the average a lap") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_fuel\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[FuelDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/20, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.runInit(RACE);
    TelemetryRow t;
    t.speed = 20.0f;
    // On track first (raceLap draws spectating, and the switch back clears
    // the telemetry), then the sample, then the frames that read it
    const auto ride = [&](float fuel) {
        host.drawWithState(0);
        t.time += 0.1f;
        t.fuel = fuel;
        host.telemetryFrame(t);
        host.drawWithState(0);
        host.drawWithState(0);
    };
    ride(8.0f);
    auto rows = host.hudStringRows("fuel_deck");
    CHECK_MESSAGE(has(rows, "FUEL"), joined(rows));
    CHECK_MESSAGE(has(rows, "LAPS LEFT"), joined(rows));
    // The unit is the value's tail, its own smaller string
    CHECK_MESSAGE(hasPair(rows, "8.0", "L"), "the fuel, in litres: " << joined(rows));
    CHECK_MESSAGE(!has(rows, "0.60"), "no lap yet, no average: " << joined(rows));
    CHECK_MESSAGE(meterBlocks(host, "fuel_deck") == 16, "the tank's blocks: " << meterBlocks(host, "fuel_deck"));

    for (int lap = 1; lap <= 3; ++lap) {
        ride(8.0f - 0.6f * static_cast<float>(lap));
        host.raceLap(RACE, PLAYER, lap, 91000);
        ride(8.0f - 0.6f * static_cast<float>(lap));
    }
    rows = host.hudStringRows("fuel_deck");
    CHECK_MESSAGE(has(rows, "6.2"), joined(rows));
    CHECK_MESSAGE(has(rows, "0.60"), "the average a lap: " << joined(rows));
    CHECK_MESSAGE(has(rows, "10.3"), "the laps left: " << joined(rows));
    const unsigned long plain = colourOf(host, "fuel_deck", "10.3");
    REQUIRE(plain != 0ul);

    ride(1.0f);
    rows = host.hudStringRows("fuel_deck");
    CHECK_MESSAGE(has(rows, "1.7"), "under two laps: " << joined(rows));
    const unsigned long warm = colourOf(host, "fuel_deck", "1.7");
    CHECK_MESSAGE(warm != plain, "the laps left warm under two");
    host.shutdown();
}

// The Lap Log deck: the last laps, oldest on top, with the lap running at the
// foot. Three rows: with a lap running, the two last laps and it, so the
// oldest drops off, times in hundredths. A struck lap keeps its time, muted;
// the best lap and best sectors are lit, green as in the Lap Log HUD, and the
// running lap's crossed sector reads good (green too) against the best.
TEST_CASE("decks: the Lap Log deck shows the last laps by sector, oldest on top, the lap running below") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_laplog\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[LapLogDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    host.drawWithState(0);
    auto rows = host.hudStringRows("laplog_deck");
    CHECK_MESSAGE(has(rows, "LAP LOG"), joined(rows));
    CHECK_MESSAGE(has(rows, "-"), "nothing logged yet is the placeholder: " << joined(rows));
    // Out on track and over the line: the lap timer runs. Then 1:31.000
    // (30.000 / 30.000 / 31.000), 1:30.000 (29.500 / 31.000 / 29.500), and
    // 1:32.000 struck
    host.runStart();
    host.raceTrackPosition({ { PLAYER, 0.95f } });
    host.raceTrackPosition({ { PLAYER, 0.05f } });
    host.raceLap(RACE, PLAYER, 1, 91000, /*best=*/1, 30000, 60000);
    host.raceLap(RACE, PLAYER, 2, 90000, /*best=*/1, 29500, 60500);
    host.raceLap(RACE, PLAYER, 3, 92000, 0, 31000, 62000, /*invalid=*/true);
    // The next lap's first sector crossed
    host.raceSplit(RACE, PLAYER, 4, 0, 29000);
    TrapRide ride{ host };
    ride(40.0f);
    rows = host.hudStringRows("laplog_deck");
    const auto yOf = [](const std::vector<PluginHost::StringRow>& rows, const char* text) {
        for (const auto& r : rows) if (r.text == text) return r.y;
        return -1.0;
    };
    CHECK_MESSAGE(!has(rows, "1:31.00"), "the oldest lap drops off: " << joined(rows));
    CHECK_MESSAGE(has(rows, "1:30.00"), joined(rows));
    CHECK_MESSAGE(has(rows, "1:32.00"), "the struck lap keeps its time: " << joined(rows));
    CHECK_MESSAGE(has(rows, "29.50"), "a sector, in seconds to the hundredth: " << joined(rows));
    CHECK_MESSAGE(has(rows, "29.00"), "the running lap's first sector: " << joined(rows));
    REQUIRE(yOf(rows, "1:30.00") >= 0.0);
    CHECK_MESSAGE(yOf(rows, "1:30.00") < yOf(rows, "1:32.00"), "oldest on top: " << joined(rows));
    CHECK_MESSAGE(yOf(rows, "1:32.00") < yOf(rows, "29.00"), "the lap running at the foot: " << joined(rows));
    // The best lap lit, the struck lap's time muted, the crossed sector good
    const unsigned long best = colourOf(host, "laplog_deck", "1:30.00");
    const unsigned long struck = colourOf(host, "laplog_deck", "1:32.00");
    const unsigned long good = colourOf(host, "laplog_deck", "29.00");
    CHECK(best != struck);
    CHECK(good != struck);
    CHECK_MESSAGE(good == best, "a best reads green, as a good sector does (it was the grey tertiary)");
    host.shutdown();
}

// The Lap Log deck says what the Lap Log HUD says, row for row: the same lap
// numbers (L<n>, logged and running alike), INVALID for a lap struck without
// a time, PIT for a lap through the pits, logged or still running. The
// running lap's sectors read good or bad against the best, and it redraws on
// its own: the time ticking, a split crossed with the clock still. In the
// Slanted layout each row leans, the lower one further left. The sequence is
// lap_log_status_test's, in practice (a race cut keeps its time).
TEST_CASE("decks: the Lap Log deck reads as the Lap Log HUD, the running lap live") {
    constexpr int PRACTICE = 1;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_laplog_status\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[LapLogDeck]\nvisible=1\n" + deckIni("slant"));
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.runInit(PRACTICE);
    host.runStart();
    REQUIRE(host.setHudVisible("lap_log_hud", true));
    TrapRide ride{ host };
    host.raceTrackPosition({ { PLAYER, 0.95f } });
    host.raceTrackPosition({ { PLAYER, 0.05f } });
    // L1 clean (20.000 / 20.000 / 20.000), L2 struck with no time, L3 into
    // the pits mid-lap, back out and over the line
    host.raceLap(PRACTICE, PLAYER, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { PLAYER, 0.02f } });
    host.raceLap(PRACTICE, PLAYER, 2, 0, /*best=*/0, 0, 0, /*invalid=*/true);
    host.raceTrackPosition({ { PLAYER, 0.02f } });
    host.runStop();
    host.runDeinit();
    host.runInit(PRACTICE);
    host.runStart();
    host.raceTrackPosition({ { PLAYER, 0.88f } });
    host.raceLap(PRACTICE, PLAYER, 3, 0, /*best=*/0, 0, 0, /*invalid=*/true);
    host.raceTrackPosition({ { PLAYER, 0.02f } });
    ride(20.0f);

    // A row's cells: every string on the line of its lap label
    const auto rowOf = [&](const char* label) {
        const auto rows = host.hudStringRows("laplog_deck");
        double y = -1.0;
        for (const auto& r : rows) if (r.text == label) y = r.y;
        std::vector<PluginHost::StringRow> cells;
        if (y < 0.0) return cells;
        // A row's cells sit a hair off its label's baseline, a row pitch apart
        for (const auto& r : rows) if (std::fabs(r.y - y) < 0.008) cells.push_back(r);
        return cells;
    };
    const auto rowHas = [&](const char* label, const char* text) { return has(rowOf(label), text); };
    const auto xOf = [&](const char* label) {
        for (const auto& r : host.hudStringRows("laplog_deck")) if (r.text == label) return r.x;
        return -1.0;
    };
    const auto hudHas = [&](const char* label) { return has(host.hudStringRows("lap_log_hud"), label); };
    // Three rows: L2 and L3 logged, L4 running; the HUD names the same laps
    auto rows = host.hudStringRows("laplog_deck");
    for (const char* label : { "L2", "L3", "L4" }) {
        CHECK_MESSAGE(has(rows, label), label << ": " << joined(rows));
        CHECK_MESSAGE(hudHas(label), label << " in the HUD: " << joined(host.hudStringRows("lap_log_hud")));
    }
    CHECK_MESSAGE(!has(rows, "L1"), "the oldest off: " << joined(rows));
    CHECK_MESSAGE(rowHas("L2", "INVALID"), joined(rowOf("L2")));
    CHECK_MESSAGE(rowHas("L3", "PIT"), joined(rowOf("L3")));
    // Slanted: the lower row leans further left
    REQUIRE(xOf("L2") > 0.0);
    CHECK_MESSAGE(xOf("L4") < xOf("L3"), xOf("L3") << " -> " << xOf("L4"));
    CHECK_MESSAGE(xOf("L3") < xOf("L2"), xOf("L2") << " -> " << xOf("L3"));

    // The running time ticks on the wall clock, with nothing else changed:
    // the time cell is the row's right-most
    const auto timeOf = [&](const char* label) {
        PluginHost::StringRow last;
        for (const auto& c : rowOf(label)) if (c.x > last.x) last = c;
        return last.text;
    };
    const std::string before = timeOf("L4");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    host.drawWithState(0);
    const std::string after = timeOf("L4");
    CHECK_MESSAGE(after != before, "the time ticks: " << before);
    CHECK_MESSAGE(after.find('.') != std::string::npos, "a time, in tenths: " << after);

    // S1 at 19.00, under the best 20.00: shown the frame it's crossed,
    // the clock still
    host.raceSplit(PRACTICE, PLAYER, 3, 0, 19000);
    host.drawWithState(0);
    CHECK_MESSAGE(rowHas("L4", "19.00"), joined(rowOf("L4")));
    // S2 22.00, over the best 20.00: good and bad read apart
    host.raceSplit(PRACTICE, PLAYER, 3, 1, 41000);
    host.drawWithState(0);
    REQUIRE_MESSAGE(rowHas("L4", "22.00"), joined(rowOf("L4")));
    CHECK(colourOf(host, "laplog_deck", "19.00") != colourOf(host, "laplog_deck", "22.00"));

    // Into the pits mid-lap: the running row says PIT, its sectors kept
    host.runStop();
    host.runDeinit();
    host.drawWithState(0);
    CHECK_MESSAGE(rowHas("L4", "PIT"), joined(rowOf("L4")));
    CHECK_MESSAGE(rowHas("L4", "19.00"), joined(rowOf("L4")));
    host.shutdown();
}

// The Lap Log deck in a monospaced digit face (Carbon's, Retro's): every
// figure at the one size. In thousandths a lap ("1:48.000") and a sector over
// ten seconds ("36.000") overflowed their columns, so `fit` shrank the
// logged rows while the running time stayed full size. And the running time
// reads green ahead of the reference lap at that point, red behind: the Lap
// Log HUD's gap row, which the deck has no room for.
TEST_CASE("decks: the Lap Log deck draws every figure at one size, the running time toned by the gap") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_laplog_mono\\";
    stageCabinet(save);
    stageFonts(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringSize());
    auto setNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
    REQUIRE(setNowUs != nullptr);
    host.writeSettingsFile(save.c_str(), ("[Settings]\nversion=11\n" + deckIni("cabinet") +
                                          "[LapLogDeck]\nvisible=1\nfont_digits=IBMPlexMono-Regular\n"
                                          // No hold at the line: the live gap, not the lap's official one
                                          "[General]\ndefaultFreeze=0\n").c_str());
    startDecks(host, save.c_str());
    openRace(host);
    host.runStart();
    // Laps on the lap clock, one track sample per 0.5% of a lap, so the live
    // gap has a lap to measure against
    long long lapStart = 1'000'000'000LL;
    const auto ride = [&](int lapMs, int to, int from) {
        for (int p = from; p <= to; ++p) {
            setNowUs(lapStart + static_cast<long long>(lapMs) * 1000LL * p / 200);
            host.raceTrackPosition({ { PLAYER, p / 200.0f } });
        }
    };
    setNowUs(lapStart - 20000);
    host.raceTrackPosition({ { PLAYER, 0.99f } });
    setNowUs(lapStart);
    host.raceTrackPosition({ { PLAYER, 0.0f } });
    // 1:48.000 (35.759 / 36.000 / 36.241), then 1:49.000
    const int laps[2] = { 108000, 109000 };
    const int split1[2] = { 35759, 109000 / 3 };
    for (int l = 0; l < 2; ++l) {
        ride(laps[l], 199, 1);
        lapStart += static_cast<long long>(laps[l]) * 1000LL;
        setNowUs(lapStart);
        host.raceTrackPosition({ { PLAYER, 0.0f } });
        host.raceLap(RACE, PLAYER, l + 1, laps[l], /*best=*/l == 0 ? 1 : 0, split1[l], split1[l] + 36000);
        host.drawWithState(0);
    }
    // Halfway round in 50 s: 4 s up on the best lap there
    ride(100000, 100, 1);
    host.drawWithState(0);
    host.drawWithState(0);

    const auto rows = host.hudStringRows("laplog_deck");
    REQUIRE_MESSAGE(has(rows, "1:48.00"), joined(rows));
    REQUIRE_MESSAGE(has(rows, "36.00"), joined(rows));
    // Hundredths cut, as the game's thousandths are: 35.759 reads 35.75
    CHECK_MESSAGE(has(rows, "35.75"), "cut, not rounded: " << joined(rows));
    CHECK_MESSAGE(!has(rows, "35.76"), joined(rows));
    // Every figure (a lap time, a sector, the running time) at one size
    int size = 0;
    std::string running;
    size_t runningAt = 0;
    double bottom = -1.0, right = -1.0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text.find('.') == std::string::npos) continue;
        const int sz = host.stringSize("laplog_deck", static_cast<int>(i));
        if (size == 0) size = sz;
        CHECK_MESSAGE(sz == size, rows[i].text << " is set at " << sz << ", the rest at " << size);
        if (rows[i].y > bottom + 0.008 || (std::fabs(rows[i].y - bottom) < 0.008 && rows[i].x > right)) {
            bottom = rows[i].y;
            right = rows[i].x;
            running = rows[i].text;
            runningAt = i;
        }
    }
    // The running time (the bottom row's right-most) green, as the best lap is
    const unsigned long best = colourOf(host, "laplog_deck", "1:48.00");
    const unsigned long ahead = host.stringColor("laplog_deck", static_cast<int>(runningAt));
    CHECK_MESSAGE(ahead == best, "ahead of the best lap: " << running);
    // Then slow: at 95% in 123.5 s, behind the best lap's 102.6 s there
    ride(130000, 190, 101);
    host.drawWithState(0);
    host.drawWithState(0);
    const auto later = host.hudStringRows("laplog_deck");
    unsigned long behind = 0;
    bottom = right = -1.0;
    for (size_t i = 0; i < later.size(); ++i) {
        if (later[i].text.find('.') == std::string::npos) continue;
        if (later[i].y > bottom + 0.008 || (std::fabs(later[i].y - bottom) < 0.008 && later[i].x > right)) {
            bottom = later[i].y;
            right = later[i].x;
            behind = host.stringColor("laplog_deck", static_cast<int>(i));
        }
    }
    CHECK_MESSAGE(behind != ahead, "behind reads apart from ahead");
    CHECK(behind != best);
    host.shutdown();
}

// The Lap Log deck's running time holds the official gap at a crossing, as
// the Timing deck's delta does (General's freeze, on by default): just over
// the line after a lap a second slower than the best, it reads behind even
// while the live gap, a moment into a quick lap, is ahead. Fails if the Lap
// Log deck stops stepping the freeze (the live gap would show).
TEST_CASE("decks: the Lap Log deck's running time holds the lap's official gap at the line") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_laplog_hold\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    auto setNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
    REQUIRE(setNowUs != nullptr);
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[LapLogDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    openRace(host);
    host.runStart();
    long long lapStart = 1'000'000'000LL;
    const auto ride = [&](int lapMs, int to, int from) {
        for (int p = from; p <= to; ++p) {
            setNowUs(lapStart + static_cast<long long>(lapMs) * 1000LL * p / 200);
            host.raceTrackPosition({ { PLAYER, p / 200.0f } });
        }
    };
    setNowUs(lapStart - 20000);
    host.raceTrackPosition({ { PLAYER, 0.99f } });
    setNowUs(lapStart);
    host.raceTrackPosition({ { PLAYER, 0.0f } });
    // 1:48.000, the best, then 1:49.000: +1.000 at the line
    const int laps[2] = { 108000, 109000 };
    for (int l = 0; l < 2; ++l) {
        ride(laps[l], 199, 1);
        lapStart += static_cast<long long>(laps[l]) * 1000LL;
        setNowUs(lapStart);
        host.raceTrackPosition({ { PLAYER, 0.0f } });
        host.raceLap(RACE, PLAYER, l + 1, laps[l], /*best=*/l == 0 ? 1 : 0, laps[l] / 3, laps[l] * 2 / 3);
        host.drawWithState(0);
    }
    // A moment into a quick lap: 1% in 0.9 s, the live gap ahead
    ride(90000, 2, 1);
    host.drawWithState(0);
    host.drawWithState(0);
    const auto rows = host.hudStringRows("laplog_deck");
    unsigned long running = 0;
    std::string text;
    double bottom = -1.0, right = -1.0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text.find('.') == std::string::npos) continue;
        if (rows[i].y > bottom + 0.008 || (std::fabs(rows[i].y - bottom) < 0.008 && rows[i].x > right)) {
            bottom = rows[i].y;
            right = rows[i].x;
            running = host.stringColor("laplog_deck", static_cast<int>(i));
            text = rows[i].text;
        }
    }
    const unsigned long best = colourOf(host, "laplog_deck", "1:48.00");
    REQUIRE_MESSAGE(best != 0ul, joined(rows));
    const unsigned long slower = colourOf(host, "laplog_deck", "1:49.00");
    REQUIRE(running != 0ul);
    CHECK_MESSAGE(running != best, "held +1.000, not the live gap ahead: " << text);
    CHECK_MESSAGE(running != slower, "toned, not plain: " << text);
    host.shutdown();
}

// PluginData's fuel history is the player's own, on track, for one session.
// Each check is a guard that had no test when the tracking moved out of the
// Fuel widget: a laps-left off track (the replay of your own bike: no
// spectated rider, so the display rider is you), a spectated rider's
// telemetry taken for a refuel, and a new session keeping the last one's
// laps.
TEST_CASE("decks: the Fuel deck's history is your own, on track, this session") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_fuel_guards\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[FuelDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/20, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(11, "Bob");
    host.runInit(RACE);
    TelemetryRow t;
    t.speed = 20.0f;
    const auto sample = [&](int state, float fuel) {
        host.drawWithState(state);
        t.time += 0.1f;
        t.fuel = fuel;
        host.telemetryFrame(t);
        host.drawWithState(state);
        host.drawWithState(state);
    };
    const auto ride = [&](float fuel) { sample(0, fuel); };
    ride(8.0f);
    for (int lap = 1; lap <= 3; ++lap) {
        ride(8.0f - 0.6f * static_cast<float>(lap));
        host.raceLap(RACE, PLAYER, lap, 91000);
        ride(8.0f - 0.6f * static_cast<float>(lap));
    }
    auto rows = host.hudStringRows("fuel_deck");
    REQUIRE_MESSAGE(has(rows, "10.3"), joined(rows));

    // Off track on your own bike: no laps left to show
    sample(2, 6.2f);
    rows = host.hudStringRows("fuel_deck");
    CHECK_MESSAGE(!has(rows, "10.3"), "no laps left off track: " << joined(rows));

    // Spectating Bob mid-lap: his fuel is no refuel of yours. Taken for one,
    // your next lap would measure from his 9.0 and the average jump.
    host.spectateVehicles({ { PLAYER, "Alice" }, { 11, "Bob" } }, 1);
    sample(1, 9.0f);
    ride(6.2f);
    ride(5.6f);
    host.raceLap(RACE, PLAYER, 4, 91000);
    ride(5.6f);
    rows = host.hudStringRows("fuel_deck");
    CHECK_MESSAGE(has(rows, "0.60"), "the average a lap, unmoved: " << joined(rows));

    // A new session: its laps start over
    host.session(RACE, /*numLaps=*/20, /*lengthMs=*/0);
    ride(5.6f);
    rows = host.hudStringRows("fuel_deck");
    CHECK_MESSAGE(!has(rows, "0.60"), "no average carried into a new session: " << joined(rows));
    host.shutdown();
}

// RELOAD_CONFIG re-reads every layout on top of its base's geometry. A skin of
// Slant is a skin of a skin (Slant is one of Cabinet); discovery adds skins a
// pass at a time, so the base search must look only at layouts ahead of this
// one, which reload had wrong: it searched only packs BEHIND the skin, so
// a skin of a skin found no base, fell back to empty geometry and drew every
// box at the origin after the hotkey until the game restarted.
TEST_CASE("decks: reloading the layouts keeps a skin of a skin's geometry") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_reload\\";
    stageStyles(save);
    const std::string skin = save + "mxbmrp3\\themes\\skin-slant\\";
    CreateDirectoryA(skin.c_str(), nullptr);
    writeLayout(skin, "[pack]\nbase = slant\n");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    std::string ini = "[Settings]\nversion=11\n";
    for (const char* section : kDeckSections) ini += std::string("[") + section + "]\nvisible=1\n" + quietCarousel(section);
    ini += deckIni("skin-slant");
    host.writeSettingsFile(save.c_str(), ini.c_str());
    startDecks(host, save.c_str());
    openRace(host);
    const auto capture = [&] {
        for (int i = 0; i < 30; ++i) host.draw();   // let the meters' eases settle
        std::vector<std::vector<PluginHost::QuadRect>> all;
        for (const char* id : kDeckIds) all.push_back(host.hudQuadRects(id));
        return all;
    };
    const auto before = capture();
    const auto control = capture();   // a meter fed by the clock moves between captures with no reload
    size_t drawn = 0;
    for (const auto& quads : before) drawn += quads.size() > 4 ? 1 : 0;
    REQUIRE(drawn >= 10);   // most decks draw; an empty capture would compare equal
    REQUIRE(host.injectHotkey("reload_config"));   // the real RELOAD_CONFIG path, HUD rebuilds included
    const auto after = capture();
    const auto same = [](const PluginHost::QuadRect& a, const PluginHost::QuadRect& b) {
        return std::abs(a.l - b.l) < 1e-6 && std::abs(a.t - b.t) < 1e-6 &&
               std::abs(a.r - b.r) < 1e-6 && std::abs(a.b - b.b) < 1e-6;
    };
    REQUIRE(before.size() == after.size());
    size_t compared = 0;
    for (size_t i = 0; i < before.size(); ++i) {
        // The reload posts its own "Settings reloaded" notice, which the Notice deck shows
        if (std::strcmp(kDeckIds[i], "notice_deck") == 0) continue;
        INFO("deck " << kDeckIds[i]);
        REQUIRE(before[i].size() == after[i].size());
        for (size_t q = 0; q < before[i].size(); ++q) {
            if (control[i].size() != before[i].size() || !same(control[i][q], before[i][q])) continue;
            INFO("quad " << q);
            CHECK(same(after[i][q], before[i][q]));
            ++compared;
        }
    }
    CHECK(compared > 100);
    host.shutdown();
}

// Slant leans everything the same way: 0.2 columns right per row up (0.1778 px
// per px; decks-slanted.ini's header). A skew is that lean over its own box's
// height, so every skewed box must carry it; the Inputs dividers were left at
// 0.43 over 1.91 px, the old 12.8 degrees, with the top one 2.6 px off the lean,
// and every slanted theme style inherits Slant's numbers. Fails as it did: 0.225
// px per px, 0.253 columns per row.
TEST_CASE("decks: every Slant skew carries the one lean") {
    const std::string path = MXB_REPO_DATA_DIR "/themes/cabinet-dark/decks-slanted.ini";
    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f != nullptr, "shipped deck layout missing: " << path);
    char line[256];
    std::string section;
    double rectH = 0.0;
    int checked = 0;
    while (fgets(line, sizeof(line), f)) {
        double x = 0, y = 0, w = 0, h = 0, skew = 0;
        if (line[0] == '[') { section = line; rectH = 0.0; continue; }
        if (sscanf(line, " rect = %lf %lf %lf %lf", &x, &y, &w, &h) == 4) rectH = h;
        if (sscanf(line, " skew = %lf", &skew) == 1) {
            INFO(section);
            REQUIRE(rectH > 0.0);
            CHECK(std::abs(skew) / rectH == doctest::Approx(0.2).epsilon(0.02));   // columns per row
            ++checked;
        }
    }
    fclose(f);
    CHECK(checked >= 10);
}

// Live updates on Both moves the places with the live order, and the decks
// follow it as the Position widget and the Dash do (livegaps_test): the
// running order PluginData keeps (core/running_order.h), the Standings HUD hidden.
// They read the official classification, so with Both on the Race deck said
// P1 while the Position widget beside it said P2. Fails as it did: still P1 of 3.
TEST_CASE("decks: Live updates on Both moves the decks' places with the live order") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_live_order\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[General]\nliveUpdates=2\n"
                                         "[StandingsHud]\nvisible=0\n[RaceDeck]\nvisible=1\n"
                                         "[StandingsDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Carl");
    host.classify(RACE, 1000, {
        { .num = PLAYER, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 400 },
        { .num = 33, .laps = 1, .gap = 900 },
    });
    host.raceTrackPosition({ { PLAYER, 0.30f }, { 22, 0.29f }, { 33, 0.20f } });
    host.draw();
    host.draw();
    REQUIRE_MESSAGE(joined(host.hudStringRows("race_deck")).find("[1][/3]") != std::string::npos,
                    joined(host.hudStringRows("race_deck")));
    // Bob goes by and stays ahead, past the live order's half-second hold
    float pos = 0.40f;
    for (int i = 0; i < 25; ++i) {
        host.raceTrackPosition({ { PLAYER, pos }, { 22, pos + 0.01f }, { 33, pos - 0.1f } });
        host.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pos += 0.002f;
    }
    host.draw();
    const auto race = host.hudStringRows("race_deck");
    CHECK_MESSAGE(joined(race).find("[2][/3]") != std::string::npos, joined(race));
    // The Standings deck: Bob's row above yours
    double bobY = -1, aliceY = -1;
    for (const auto& r : host.hudStringRows("standings_deck")) {
        if (r.text == "BOB") bobY = r.y;
        if (r.text == "ALICE") aliceY = r.y;
    }
    REQUIRE(bobY >= 0);
    REQUIRE(aliceY >= 0);
    CHECK(bobY < aliceY);
    host.shutdown();
}

// The decks' gaps follow the Standings HUD's rule (StandingsGap::planGap,
// measured from the player), not one of their own: the Rival deck read live
// gaps whatever Live updates said, so with it off the Rival deck and the
// Standings HUD showed the same rider different gaps. Fails as it did: live
// off still showed "1.00 s".
TEST_CASE("decks: the Rival deck's gap follows Live updates, as the Standings HUD's does") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_rival_live\\";
    stageCabinet(save);
    for (const int live : { 0, 1 }) {
        INFO("liveUpdates " << live);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RivalDeck]\nvisible=1\n"
                                             "[General]\nliveUpdates=" + std::to_string(live) + "\n");
        startDecks(host, save.c_str());
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(PLAYER, "Alice");
        host.addEntry(22, "Bob");
        // Officially Bob is 0.50 s behind; on track 1.00 s (as the Standings
        // deck's live case above)
        const std::vector<ClassRow> lap1 = {
            { .num = PLAYER, .laps = 1, .gap = 0 },
            { .num = 22, .laps = 1, .gap = 500 },
        };
        host.classify(RACE, 1000, lap1);
        host.raceTrackPosition({ { PLAYER, 0.20f }, { 22, 0.10f } });
        host.classify(RACE, 2000, lap1);
        host.raceTrackPosition({ { PLAYER, 0.40f }, { 22, 0.20f } });
        host.draw();
        host.draw();
        const auto rows = host.hudStringRows("rival_deck");
        CHECK_MESSAGE(has(rows, "BOB"), joined(rows));
        CHECK_MESSAGE(has(rows, live ? "1.00 s" : "0.50 s"), joined(rows));
        CHECK_MESSAGE(!has(rows, live ? "0.50 s" : "1.00 s"), joined(rows));
        host.shutdown();
    }
}

// The decks follow Appearance as the HUDs do. Colours are palette slots, so a
// colour the player picks reaches them; fonts are categories, the pack's
// [fonts] face used only while the player keeps the default. Cabinet named its
// faces on each text, so a font picked in Appearance never reached its
// figures. Fails as it did: the speed figures keep Tomorrow with Digits picked.
TEST_CASE("decks: a colour or font picked in Appearance reaches the decks") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_appearance\\";
    stageCabinet(save);
    stageFonts(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringFont());
    REQUIRE(host.hasPaletteHooks());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    REQUIRE(host.fontCount() >= 4);
    openRace(host);
    enum { CAT_DIGITS = 3, SLOT_PRIMARY = 0 };
    // The speed figures ("45"): digits, primary
    const auto speed = [&](int* font, unsigned long* colour) {
        host.draw();
        host.draw();
        const auto rows = host.hudStringRows("speed_deck");
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].text != "45") continue;
            *font = host.stringFont("speed_deck", static_cast<int>(i));
            *colour = host.stringColor("speed_deck", static_cast<int>(i));
            return true;
        }
        return false;
    };
    int packFont = 0, font = 0;
    unsigned long packColour = 0, colour = 0;
    REQUIRE_MESSAGE(speed(&packFont, &packColour), joined(host.hudStringRows("speed_deck")));
    // The player's Digits font wins over Cabinet's Tomorrow
    host.setFont(CAT_DIGITS, "VT323-Regular");
    REQUIRE(speed(&font, &colour));
    CHECK(font != packFont);
    // Back to the default: Cabinet's own again
    host.clearFontOverride(CAT_DIGITS);
    REQUIRE(speed(&font, &colour));
    CHECK(font == packFont);
    // And the player's Primary colour
    host.cycleColor(SLOT_PRIMARY, false);   // the palette's last: its first is the default white
    REQUIRE(speed(&font, &colour));
    CHECK(colour != packColour);
    host.shutdown();
}

// A style has no palette: the 1.36 review dropped decks.ini's [colors] layer,
// so a slot is the theme's colour, or the player's pick. A layout still
// stating [colors] draws as if it did not. Fails if the layer comes back.
TEST_CASE("decks: a style's [colors] is ignored; the theme's palette and the player's pick colour the deck") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_palette\\";
    stageCabinet(save);
    const std::string dir = save + "mxbmrp3\\themes\\palette\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    writeLayout(dir, "[pack]\nbase = cabinet\n[colors]\nprimary = #ff2200\nbackground = #003366\n");
    const auto rgb = [](unsigned long c) { return c & 0x00FFFFFFul; };
    struct Seen { unsigned long speed = 0; bool panel = false; };
    // The speed figures' ink (primary), and whether a quad is tinted the
    // style's background
    const auto draw = [&](const char* extra) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasInkHooks());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n" + deckIni("palette") +
                                                         "[SpeedDeck]\nvisible=1\n") + extra);
        startDecks(host, save.c_str());
        openRace(host);
        host.draw();
        host.draw();
        Seen seen;
        const auto rows = host.hudStringRows("speed_deck");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == "45") seen.speed = host.stringColor("speed_deck", static_cast<int>(i));
        const size_t quads = host.hudQuadRects("speed_deck").size();
        for (size_t i = 0; i < quads; ++i)
            seen.panel = seen.panel || rgb(host.quadColor("speed_deck", static_cast<int>(i))) == 0x663300ul;
        host.shutdown();
        return seen;
    };
    const Seen style = draw("");
    CHECK_MESSAGE(rgb(style.speed) != 0x0022FFul, "the speed in the theme's primary: " << std::hex << style.speed);
    CHECK_MESSAGE(!style.panel, "the panel in the theme's background");
    // The player's Primary wins
    const Seen picked = draw("[Colors]\nprimary=0xFF00FF00\n");
    CHECK_MESSAGE(rgb(picked.speed) == 0x00FF00ul, "the player's primary: " << std::hex << picked.speed);
    // ...and so does this deck's own
    const Seen own = draw("color_primary=0xFFFF0000\n");
    CHECK_MESSAGE(rgb(own.speed) == 0xFF0000ul, "the deck's own primary: " << std::hex << own.speed);
}

TEST_CASE("decks: a deck's own font override beats its style's face") {
    // The per-HUD font_<category> key (an INI power-user override) on one deck
    // wins over the pack's [fonts] suite, as a global pick does (DeckHud::getFont)
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_hud_font\\";
    stageCabinet(save);
    stageFonts(save);
    // The speed figures' ("45") font, with the deck section's extra lines
    const auto speedFont = [&](const char* extra) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasStringFont());
        host.writeSettingsFile(save.c_str(), (std::string("[Settings]\nversion=11\n" + deckIni("cabinet") + "[SpeedDeck]\nvisible=1\n") +
                                              extra).c_str());
        startDecks(host, save.c_str());
        openRace(host);
        host.draw();
        host.draw();
        int font = -1;
        const auto rows = host.hudStringRows("speed_deck");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == "45") font = host.stringFont("speed_deck", static_cast<int>(i));
        host.shutdown();
        return font;
    };
    const int packFont = speedFont("");
    REQUIRE(packFont >= 0);
    const int own = speedFont("font_digits=VT323-Regular\n");
    REQUIRE(own >= 0);
    CHECK(own != packFont);
}

TEST_CASE("decks: Slanted is saved once, in [Decks], and every deck draws it") {
    // One choice, one key: saved per deck, a hand edit or an old profile left
    // the decks disagreeing with nothing to show it
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_style_once\\";
    const std::string iniPath = save + "mxbmrp3\\mxbmrp3_settings.ini";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n" + deckIni("slant") +
                                         "[SpeedDeck]\nvisible=1\n[RaceDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    REQUIRE(host.save());
    const ini::Map saved = ini::parse(ini::readFile(iniPath));
    const auto style = saved.find({ "Decks", "slanted" });
    REQUIRE(style != saved.end());
    CHECK(style->second == "1");
    for (const auto& entry : saved) {
        if (entry.first.second == "slanted")
            CHECK_MESSAGE(entry.first.first == "Decks", "Slanted in [" << entry.first.first << "]");
    }
    host.shutdown();

    // Any other value (an older build's pack name) draws Regular
    PluginHost again(dllPath());
    REQUIRE(again.loaded());
    again.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n" + deckIni("gone") + "[SpeedDeck]\nvisible=1\n");
    again.startup(save.c_str());
    openRace(again);
    CHECK_MESSAGE(has(again.hudStringRows("speed_deck"), "SPEED"), joined(again.hudStringRows("speed_deck")));
    again.shutdown();
}

TEST_CASE("decks: no rev range from the game, no rev meter on the Speed deck") {
    // As the RPM and Bars widgets: the meter shows the game's range or none,
    // never an invented one
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_no_range\\";
    stageCabinet(save);
    const auto quads = [&](bool range) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        openRace(host, range);
        const size_t n = host.hudQuadRects("speed_deck").size();
        host.shutdown();
        return n;
    };
    const size_t with = quads(true), without = quads(false);
    CHECK_MESSAGE(without < with, "meter quads: " << with << " with a range, " << without << " without");
}

// Every deck's Title, Opacity and Scale (the case below) are saved once, in
// [Decks], and into no deck's section. A key a player wrote into a deck's
// section is kept as written, like any hand edit, and overruled at every load.
TEST_CASE("decks: Title, Opacity and Scale are saved in [Decks] alone") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_shared_save\\";
    stageStyles(save);
    const std::string shared = "[Decks]\nslanted=0\ntitle=0\nopacity=0.6\nscale=1.5\n";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n"
                                          "[SpeedDeck]\nvisible=1\n[RaceDeck]\nvisible=1\n" + shared);
    startDecks(host, save.c_str());
    REQUIRE(host.save());
    const ini::Map saved = ini::parse(ini::readFile(save + "mxbmrp3\\mxbmrp3_settings.ini"));
    REQUIRE_FALSE(saved.empty());
    bool decks = false;
    for (const auto& kv : saved) {
        const std::string& section = kv.first.first;
        const std::string& key = kv.first.second;
        if (section == "Decks") {
            decks = true;
            if (key == "title") CHECK(kv.second == "0");
            if (key == "opacity") CHECK(std::stof(kv.second) == doctest::Approx(0.6f));
            if (key == "scale") CHECK(std::stof(kv.second) == doctest::Approx(1.5f));
            continue;
        }
        if (section.size() < 4 || section.compare(section.size() - 4, 4, "Deck") != 0) continue;
        INFO("[" << section << "] " << key);
        CHECK(key != "showTitle");
        CHECK(key != "theme");
        CHECK(key != "backgroundOpacity");
        CHECK(key != "scale");
    }
    CHECK(decks);
    host.shutdown();
}

// Title, Opacity and Scale are one each for every deck, saved once in [Decks],
// and a deck draws in the panel theme, never one of its own (his pick B, 24
// Nov: a set reads as one graphic, and lines up on its cell grid at any size).
// A [SpeedDeck] holding its own -- an INI from before they were shared, or
// one edited by hand -- cannot set that deck apart: [Decks] is applied after
// the deck sections. Fails if a deck keeps a value of its own, or if a load
// leaves a deck's own value standing.
TEST_CASE("decks: Title, Opacity and Scale are every deck's, in the panel theme") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_shared_look\\";
    stageStyles(save);
    // The deck's layout: every quad's place and picture
    const auto layout = [](PluginHost& host) {
        std::vector<long long> out;
        const auto rects = host.hudQuadRects("speed_deck");
        for (size_t i = 0; i < rects.size(); ++i) {
            for (const double v : { rects[i].l, rects[i].t, rects[i].r, rects[i].b }) out.push_back(std::llround(v * 1e5));
            out.push_back(host.quadSprite("speed_deck", static_cast<int>(i)));
        }
        return out;
    };
    const std::string shared = "[Decks]\nslanted=0\ntitle=0\nopacity=0.6\nscale=1.5\n";

    // The Speed deck drawn from `ini`, in a race
    const auto drawnFrom = [&](const std::string& ini) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n" + ini);
        startDecks(host, save.c_str());
        openRace(host);
        const std::vector<long long> out = layout(host);
        host.shutdown();
        return out;
    };
    const std::vector<long long> ref = drawnFrom("[SpeedDeck]\nvisible=1\n" + shared);
    // Its own scale draws otherwise, or the comparison below proves nothing
    REQUIRE(drawnFrom("[SpeedDeck]\nvisible=1\n[Decks]\nslanted=0\ntitle=0\nopacity=0.6\nscale=2.5\n") != ref);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n"
        "[SpeedDeck]\nvisible=1\ntheme=retro-dark\nshowTitle=1\nbackgroundOpacity=0.2\nscale=2.5\n"
        "[RaceDeck]\nvisible=1\n" + shared);
    startDecks(host, save.c_str());
    openRace(host);
    CHECK_MESSAGE(layout(host) == ref, "the Speed deck kept a theme, title or scale of its own");
    CHECK(host.hudScales("speed_deck").own == 1500);
    CHECK(host.hudScales("race_deck").own == 1500);

    // One deck set apart live -- its own theme, which draws otherwise, and its
    // own scale -- comes back with the rest on a load
    REQUIRE(host.setHudTheme("speed_deck", "retro-dark"));
    REQUIRE(host.setHudScale("speed_deck", 2.5f));
    host.draw();
    REQUIRE(layout(host) != ref);
    REQUIRE(host.hudScales("speed_deck").own == 2500);
    host.loadSettings(save.c_str());
    host.draw();
    CHECK(layout(host) == ref);
    CHECK(host.hudScales("speed_deck").own == 1500);

    // A [Decks] without the three keys still overrules a deck's own: the
    // shared values held (1.5 here) are pushed after every deck's section
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\nscale=2.5\n[Decks]\nslanted=0\n");
    host.loadSettings(save.c_str());
    host.draw();
    CHECK_MESSAGE(host.hudScales("speed_deck").own == 1500, "a [Decks] without scale left the deck's own");
    host.shutdown();
}


// A theme's slanted frame ends (frame_slant_left/right.tga) replace a Slant
// card: its `caps` box draws the theme's frame leaning -- the two ends as tall
// as the card and as wide as their pictures' shape (90 x 324), the frame's
// top, centre and bottom between -- and its `uncapped` outline is dropped. A
// theme without them (Modern's ends removed here) keeps the pack's own two
// pictures, card and outline. Between the ends, three bands: the frame's top
// edge and bottom edge, each the theme's frame border thick (Carbon: one
// column), and its centre. Carbon is `tint = 0`, so all five draw WHITE at the
// panel's opacity: tinted by the background slot, its baked art would darken.
TEST_CASE("decks: a theme's slanted frame ends draw a Slant card") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_slant_caps\\";
    stageStyles(save);
    stagePack(save, "themes", "carbon-dark");
    stagePack(save, "themes", "modern-dark");
    // From the shared tree too: the sync copies files across, and never deletes
    // one another case's staging left there
    for (const char* end : { "left", "right" }) {
        DeleteFileA((save + "mxbmrp3\\themes\\modern-dark\\frame_slant_" + end + ".tga").c_str());
        std::error_code ec;
        std::filesystem::remove(std::string("plugins/mxbmrp3_data/themes/modern-dark/frame_slant_") + end + ".tga", ec);
    }
    constexpr double kCardH = 9 * 11.88, kCardW = 21.8 * 10.56, kCapW = kCardH * 90.0 / 324.0;
    struct Count { int whole = 0, ends = 0, sprites = 0, white = 0, panelAlpha = 0; bool bands = false, flush = false; };
    const auto count = [&](const char* theme) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[Display]\npanelTheme=") + theme +
                                             "\n[SpeedDeck]\nvisible=1\n" + deckIni("slant") + "opacity=0.6\n");
        startDecks(host, save.c_str());
        openRace(host);
        Count c;
        int endSprite[2] = { -1, -1 };
        const auto rects = host.hudQuadRects("speed_deck");
        for (size_t i = 0; i < rects.size(); ++i) {
            const double w = (rects[i].r - rects[i].l) * 1920.0, h = (rects[i].b - rects[i].t) * 1080.0;
            if (std::abs(h - kCardH) > 0.5) continue;
            if (std::abs(w - kCardW) < 0.5) ++c.whole;
            if (std::abs(w - kCapW) >= 0.5 || c.ends >= 2) continue;
            endSprite[c.ends++] = host.quadSprite("speed_deck", static_cast<int>(i));
            if (c.ends != 1 || i + 4 >= rects.size()) continue;
            // The left end, then top band, centre, bottom band, right end
            int sprite[3];
            double bandH[3];
            for (int k = 0; k < 3; ++k) {
                sprite[k] = host.quadSprite("speed_deck", static_cast<int>(i) + 1 + k);
                bandH[k] = (rects[i + 1 + k].b - rects[i + 1 + k].t) * 1080.0;
            }
            // Registration order: frame_center, then corner/edge pairs, edge_top
            // second and edge_bottom fourth
            c.bands = sprite[0] == sprite[1] + 2 && sprite[2] == sprite[1] + 4 && std::abs(bandH[0] - 10.56) < 0.05 &&
                      std::abs(bandH[2] - 10.56) < 0.05 && std::abs(bandH[1] - (kCardH - 2 * 10.56)) < 0.05;
            // The bands meet each end edge to edge: running them under the ends
            // (#802) drew the overlap twice below full opacity, a dark stripe
            // down each seam on every renderer
            c.flush = true;
            for (size_t k = i + 1; k <= i + 3; ++k)
                c.flush = c.flush && std::abs((rects[i].r - rects[k].l) * 1920.0) < 0.05 &&
                          std::abs((rects[k].r - rects[i + 4].l) * 1920.0) < 0.05;
            for (size_t k = i; k <= i + 4; ++k) {
                const unsigned long col = host.quadColor("speed_deck", static_cast<int>(k));
                if ((col & 0x00FFFFFFul) == 0x00FFFFFFul && (col >> 24) != 0) ++c.white;
                if (std::abs(static_cast<int>(col >> 24) - static_cast<int>(0.6 * 255.0)) <= 1) ++c.panelAlpha;
            }
        }
        c.sprites = endSprite[0] > 0 && endSprite[1] > 0 && endSprite[0] != endSprite[1] ? 2 : 0;
        host.shutdown();
        return c;
    };
    const Count carbon = count("carbon-dark");
    CHECK_MESSAGE(carbon.ends == 2, "Carbon's two leaning ends, " << kCapW << " px wide");
    CHECK_MESSAGE(carbon.sprites == 2, "each end its own picture");
    CHECK_MESSAGE(carbon.whole == 0, "neither the pack's card nor its outline under Carbon's frame");
    CHECK_MESSAGE(carbon.bands, "top edge, centre, bottom edge between the ends, a border thick");
    CHECK_MESSAGE(carbon.white == 5, "Carbon's baked art draws white, not tinted by the background");
    CHECK_MESSAGE(carbon.flush, "the bands meet each end edge to edge, never under it");
    // The frame stands in for the panel: at the panel's opacity, not the
    // fallback card's own 0.92 on top of it (it read paler than Regular)
    CHECK_MESSAGE(carbon.panelAlpha == 5, "the frame at the deck's background opacity");
    const Count modern = count("modern-dark");
    CHECK_MESSAGE(modern.ends == 0, "a theme with no slanted ends");
    CHECK_MESSAGE(modern.whole == 2, "the pack's card and its outline, as before");
}

// The fonts are normalised for digit width and centring, not height, and the
// Speed deck's gear (5.23 text sizes in a 5.73-row band) was sized in Tomorrow,
// whose digits are half the font size. Slant draws in the theme's faces, and
// VT323 (Retro's face then) stands 0.68: its gear stood 77 px in the 68 px
// band and spilled out of the card. A text whose figures stand taller than its band is now drawn
// smaller to fit it; one in a face that fits keeps its size. The band was
// then still 5.73 rows, ending 0.07 rows above the revs bar, so IBM Plex
// (0.585) filled it and Carbon's gear stood on the bar: the band is now
// Tomorrow's own digits' height, so every face shrinks to the same figures
// with Cabinet's clearance (Retro's size override went with it). Retro now
// draws in Press Start 2P, whose figures fit; VT323 stays the tall face, as a
// player's own pick for the deck.
TEST_CASE("decks: a tall font's gear shrinks into its band") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_tall_font\\";
    stageStyles(save);
    stagePack(save, "themes", "retro-dark");
    stagePack(save, "themes", "carbon-dark");
    stageFonts(save);
    constexpr double kBand = 4.75 * 11.88 / 1080.0, kDesigned = 5.2315 * 21.6 / 1080.0;
    const auto gearSize = [&](const char* theme, const char* speedDeck) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        std::string ini = "[Settings]\nversion=11\n";
        if (theme) ini += std::string("[Display]\npanelTheme=") + theme + "\n";
        ini += std::string("[SpeedDeck]\nvisible=1\n") + speedDeck + deckIni("slant");
        host.writeSettingsFile(save.c_str(), ini);
        startDecks(host, save.c_str());
        openRace(host);
        host.draw();
        // The deck's own strings (the Gear widget draws a "3" too), matched to
        // the frame's by position; the gear is the biggest of them
        float biggest = 0.0f;
        const auto mine = host.hudStringRows("speed_deck");
        for (const auto& s : host.lastStrings())
            for (const auto& m : mine)
                if (m.text == s.text && std::abs(m.x - s.x) < 1e-4 && std::abs(m.y - s.y) < 1e-4) biggest = std::max(biggest, s.size);
        host.shutdown();
        REQUIRE(biggest > 0.0f);
        return static_cast<double>(biggest);
    };
    const double plain = gearSize(nullptr, ""), tall = gearSize(nullptr, "font_digits=VT323-Regular\n"),
                 retro = gearSize("retro-dark", ""), carbon = gearSize("carbon-dark", "");
    CHECK_MESSAGE(std::abs(plain - kDesigned) < 1e-4, "Tomorrow's figures fit: the gear keeps its size");
    CHECK_MESSAGE(tall * 0.68 <= kBand + 1e-3, "VT323's figures fit the band: size " << tall * 1080.0 << " px");
    CHECK_MESSAGE(tall < plain * 0.95, "the tall face is drawn smaller");
    CHECK_MESSAGE(std::abs(retro - kDesigned) < 1e-4, "Press Start 2P's figures fit: Retro's gear keeps its size");
    // IBM Plex Mono stands 0.585: its figures fit the band, clear of the bar
    CHECK_MESSAGE(carbon * 0.585 <= kBand + 1e-3, "Plex's figures fit the band: size " << carbon * 1080.0 << " px");
    CHECK_MESSAGE(carbon < plain * 0.95, "Carbon's gear is drawn smaller than designed");
}

// The decks draw in the theme's faces, as every HUD does. A shipped layout
// that names its own ([fonts] in a decks*.ini) makes its decks read
// unlike the HUDs beside them, and Regular unlike Slanted: Carbon's and
// Modern's did. The key stays for a player's own layout.
TEST_CASE("decks: no shipped layout names its own fonts") {
    int layouts = 0;
    for (const auto& theme : std::filesystem::directory_iterator(MXB_REPO_DATA_DIR "/themes")) {
        if (!theme.is_directory()) continue;
        for (const auto& file : std::filesystem::directory_iterator(theme.path())) {
            const std::string name = file.path().filename().string();
            if (name.rfind("decks", 0) != 0 || file.path().extension() != ".ini") continue;
            ++layouts;
            std::ifstream in(file.path());
            std::string line;
            while (std::getline(in, line))
                CHECK_MESSAGE(line.rfind("[fonts]", 0) != 0, file.path().string() << " names its own fonts");
        }
    }
    CHECK(layouts >= 5);
}

// The shipped Regular layout, section -> key -> value (comments dropped):
// every other layout is a skin of it, so it holds every text's size and place.
static std::map<std::string, std::map<std::string, std::string>> shippedLayout() {
    std::map<std::string, std::map<std::string, std::string>> ini;
    std::ifstream in(MXB_REPO_DATA_DIR "/themes/cabinet-dark/decks.ini");
    std::string line, section;
    const auto trim = [](std::string t) {
        const auto a = t.find_first_not_of(" \t\r"), b = t.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    };
    while (std::getline(in, line)) {
        line = trim(line.substr(0, line.find(';')));
        if (line.size() > 2 && line.front() == '[') section = line.substr(1, line.size() - 2);
        else if (const auto eq = line.find('='); eq != std::string::npos && !section.empty())
            ini[section][trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return ini;
}

// A deck's stamp (NEW BEST, BEST S2, GAINED 0.20, BAILED...) is one size on
// every deck: Normal, the mock's stamp beside its Small name tab. It was
// Large, twice the tab, and its fit shrank the longer ones, so GAINED 0.20
// drew smaller than BEST S2 on the same screen. The Notice deck's stamp is
// its queue count, drawn as a second name tab, and stays the tab's size.
TEST_CASE("decks: every shipped deck's stamp draws at Normal") {
    const auto ini = shippedLayout();
    REQUIRE(!ini.empty());
    int stamps = 0;
    for (const auto& [name, keys] : ini) {
        if (name.find("-text-") == std::string::npos || !keys.count("show") || keys.at("show") != "stamp") continue;
        const double size = keys.count("size") ? std::atof(keys.at("size").c_str()) : 1.0;
        const double want = name.rfind("notice-", 0) == 0 ? 0.75 : 1.0;
        CHECK_MESSAGE(std::abs(size - want) < 1e-6, name << " stamp is size " << size);
        ++stamps;
    }
    CHECK(stamps >= 10);
}

// Every deck keeps one grid: content 1.5 columns in from each side (a
// value's right edge, a caption's left), the bottom meter 0.75 in. The small
// decks once sat at 1.347 and 18.847, a pixel and a half off the wide ones,
// and three dividers on three columns. A row's own columns (Standings, Lap
// Log), a label beside a value and a text placed after/before another are
// their deck's own.
TEST_CASE("decks: every shipped deck keeps the content margins") {
    const auto ini = shippedLayout();
    REQUIRE(!ini.empty());
    int checked = 0;
    for (const auto& [name, keys] : ini) {
        const auto dash = name.find('-');
        if (dash == std::string::npos) continue;
        const auto deck = ini.find(name.substr(0, dash));
        if (deck == ini.end() || !deck->second.count("size")) continue;
        const double cols = std::atof(deck->second.at("size").c_str());
        const std::string show = keys.count("show") ? keys.at("show") : "";
        if (name.find("-meter-") != std::string::npos && keys.count("rect")) {
            double x = 0, y = 0, w = 0, h = 0;
            if (std::sscanf(keys.at("rect").c_str(), "%lf %lf %lf %lf", &x, &y, &w, &h) == 4 && std::abs(y - 8.715) < 1e-6) {
                CHECK_MESSAGE(std::abs(x - 0.75) < 1e-6, name << " starts at " << x);
                CHECK_MESSAGE(std::abs(x + w - (cols - 0.75)) < 1e-6, name << " ends at " << x + w);
                ++checked;
            }
            continue;
        }
        // A divider is any thin, tall box: one width, one bottom row, and on
        // a small deck one column, so the right-hand column starts alike.
        if (name.find("-box-") != std::string::npos && keys.count("rect")) {
            double x = 0, y = 0, w = 0, h = 0;
            if (std::sscanf(keys.at("rect").c_str(), "%lf %lf %lf %lf", &x, &y, &w, &h) == 4 && w < 0.3 && h > 3.0) {
                CHECK_MESSAGE(std::abs(w - 0.188) < 1e-6, name << " divider is " << w << " wide");
                CHECK_MESSAGE(std::abs(y + h - 8.232) < 1e-6, name << " divider ends at " << y + h);
                if (cols < 25.0) CHECK_MESSAGE(std::abs(x - 9.375) < 1e-6, name << " divider sits at " << x);
                ++checked;
            }
            continue;
        }
        if (name.find("-text-") == std::string::npos || !keys.count("x") || keys.count("after") || keys.count("before") ||
            show == "stamp" || show.rfind("row-", 0) == 0 || show.size() > 6 && show.compare(show.size() - 6, 6, "-label") == 0)
            continue;
        const double x = std::atof(keys.at("x").c_str());
        const std::string align = keys.count("align") ? keys.at("align") : "left";
        if (align == "right") {
            CHECK_MESSAGE(std::abs(x - (cols - 1.5)) < 1e-6, name << " (" << show << ") ends at " << x);
            ++checked;
        } else if (align == "left" && x < 2.0) {
            CHECK_MESSAGE(std::abs(x - 1.5) < 1e-6, name << " (" << show << ") starts at " << x);
            ++checked;
        }
    }
    CHECK(checked >= 40);
}

// A deck's `art = title` box is the theme's title band, and like a HUD's band
// it is a card COVER: the panel's fill is cut around it, so the band does not
// composite over a second layer of fill and read a shade darker than the
// panel. Modern draws the Speed deck's name on one, under a theme with title
// art. The cover alone was not enough: a deck's fill pool was one strip, so
// the cut overflowed to the whole centre.
TEST_CASE("decks: a deck's title box cuts the panel's fill under it") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_title_cut\\";
    stageStyles(save);
    stagePack(save, "themes", "modern-dark");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasFillCut());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n" + deckIni("modern"));
    startDecks(host, save.c_str());
    openRace(host);
    host.draw();
    const PluginHost::FillCut cut = host.hudFillCut("speed_deck");
    REQUIRE_MESSAGE(cut.themed, "the deck drew no themed fill");
    REQUIRE_MESSAGE(!cut.covers.empty(), "the title box recorded no cover");
    constexpr double kIn = 1e-6;
    for (const auto& c : cut.covers) {
        for (const auto& s : cut.strips) {
            const bool overlaps = s.l < c.r - kIn && s.r > c.l + kIn && s.t < c.b - kIn && s.b > c.t + kIn;
            CHECK_MESSAGE(!overlaps, "a fill strip " << s.l << "," << s.t << "-" << s.r << "," << s.b
                          << " runs under the title box " << c.l << "," << c.t << "-" << c.r << "," << c.b);
        }
    }
    host.shutdown();
}

// A chip's word keeps its colour's own alpha, scaled by the pill's fade: a
// translucent palette colour stays translucent on a pill at rest. Fading by
// replacing the alpha drew it opaque.
TEST_CASE("decks: a chip's word keeps its colour's own alpha") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_chip_alpha\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasFmx());
    REQUIRE(host.hasInkHooks());
    // Every slot at half alpha, so whichever tone the chip takes is translucent
    std::string ini = "[Settings]\nversion=11\n[ComboDeck]\nvisible=1\n[Colors]\n";
    for (const char* slot : { "primary", "secondary", "tertiary", "muted", "background", "positive", "warning",
                              "neutral", "negative", "accent" })
        ini += std::string(slot) + "=0x80FFFFFF\n";
    host.writeSettingsFile(save.c_str(), ini);
    startDecks(host, save.c_str());
    host.setMotion(0);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1 /*practice*/, 0, 480000);
    host.addEntry(PLAYER, "Alice");
    host.runInit(1);
    host.runStart();
    FmxRide bike{ host, true };
    for (int i = 0; i < 150; ++i) {
        TelemetryRow r;
        r.frontMaterial = 0;
        r.rearMaterial = 0;
        r.pitchVel = -300.0f;
        bike.tick(r);
    }
    bike.ground(80);
    const auto rows = host.hudStringRows("combo_deck");
    int chips = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text != "BACKFLIP" && rows[i].text != "AIR") continue;
        ++chips;
        const unsigned long a = (host.stringColor("combo_deck", static_cast<int>(i)) >> 24) & 0xFFu;
        CHECK_MESSAGE(a >= 0x7Eu, "word alpha " << a);
        CHECK_MESSAGE(a <= 0x82u, "word alpha " << a << ": the colour's own alpha was replaced");
    }
    CHECK_MESSAGE(chips == 1, "one chip at rest: " << joined(rows));
    host.shutdown();
}

// The Race deck's foot: a lap race's progress one part a lap, run on with
// the lap's way round; a timed race's clock draining in the hot colour. And
// the clock line under the lap: the time run in a lap race, the countdown in
// a timed one.
TEST_CASE("decks: the Race deck runs its progress by the lap and its clock by the session") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_race_progress\\";
    stageCabinet(save);
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RaceDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        openRace(host);   // five laps, one done, the session 100 s in
        const auto at = [&](float trackPos) {
            host.raceTrackPosition({ { .num = PLAYER, .trackPos = trackPos }, { .num = 22, .trackPos = 0.9f } });
            host.draw();
            host.draw();
            return meterBand(host, "race_deck");
        };
        const auto start = at(0.0f);
        CHECK(start.size() == 5);   // a part a lap
        CHECK(meterLit(start) == doctest::Approx(0.2).epsilon(0.03));
        CHECK(meterLit(at(0.5f)) == doctest::Approx(0.3).epsilon(0.03));   // half way round lap two
        const auto rows = host.hudStringRows("race_deck");
        CHECK_MESSAGE(has(rows, "01:40"), "the time run: " << joined(rows));
        host.shutdown();
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasPaletteHooks());
        host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[RaceDeck]\nvisible=1\n");
        startDecks(host, save.c_str());
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(RACE, /*numLaps=*/0, /*lengthMs=*/600000);
        host.addEntry(PLAYER, "Alice");
        host.addEntry(22, "Bob");
        host.runInit(RACE);
        // Ten minutes, four left
        host.classify(RACE, 240000, {
            { .num = 22, .best = 90000, .laps = 1, .gap = 0 },
            { .num = PLAYER, .best = 91000, .laps = 1, .gap = 1500 },
        });
        host.draw();
        host.draw();
        const auto rows = host.hudStringRows("race_deck");
        CHECK_MESSAGE(has(rows, "04:00"), "the countdown: " << joined(rows));
        const auto band = meterBand(host, "race_deck");
        REQUIRE(!band.empty());
        CHECK(meterLit(band) == doctest::Approx(0.4).epsilon(0.03));
        CHECK(band.front().colour == host.effectiveColor(6 /*WARNING: the pack's hot-color*/));
        host.shutdown();
    }
}

// The Wheelie deck: a hold timed from the front wheel lifting, kept as MAX
// once down; the band holding the needle lit, in the zone colour inside the
// balance zone (25-45 degrees until the rider's own is learned), hot past it.
TEST_CASE("decks: the Wheelie deck times a hold and lights the balance zone") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_wheelie\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    REQUIRE(host.hasPaletteHooks());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[WheelieDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.setMotion(0);
    host.eventInit("TestTrack", "Player");
    host.addEntry(PLAYER, "Player");
    long long now = 1'000'000;
    int frame = 0;
    // One 10 ms sample, the front wheel up at `deg` (0: both down)
    const auto sample = [&](float deg) {
        TelemetryRow r;
        r.speed = 15.0f + 0.001f * static_cast<float>(frame++ % 7);   // fresh
        r.time = static_cast<float>(now) / 1.0e6f;
        r.pitch = -deg;
        r.frontMaterial = deg > 0.0f ? 0 : 1;
        r.rearMaterial = 1;
        host.telemetryFrame(r);
        now += 10'000;
        host.setMotionNowUs(now);
        host.drawWithState(0);
    };
    // The lit band: the meter's one quad in a full-strength colour of the three
    const auto lit = [&](unsigned long colour) {
        const auto rects = host.hudQuadRects("wheelie_deck");
        int n = 0;
        for (size_t i = 0; i < rects.size(); ++i) n += host.quadColor("wheelie_deck", static_cast<int>(i)) == colour ? 1 : 0;
        return n;
    };
    const unsigned long zone = host.effectiveColor(5 /*POSITIVE*/), hot = host.effectiveColor(6 /*WARNING*/);
    for (int i = 0; i < 5; ++i) sample(0.0f);
    for (int i = 0; i < 151; ++i) sample(35.0f);   // 1.5 s in the zone (the first sample starts it)
    auto rows = host.hudStringRows("wheelie_deck");
    CHECK_MESSAGE(hasPair(rows, "1.5", "s"), "the hold: " << joined(rows));
    CHECK_MESSAGE(has(rows, "35\xB0"), joined(rows));
    CHECK_MESSAGE(lit(zone) >= 1, "the needle's band in the zone colour");
    for (int i = 0; i < 50; ++i) sample(55.0f);    // past the zone
    CHECK_MESSAGE(lit(hot) >= 1, "past the zone: hot");
    CHECK(lit(zone) == 0);
    for (int i = 0; i < 20; ++i) sample(0.0f);     // down: the hold is MAX
    rows = host.hudStringRows("wheelie_deck");
    CHECK_MESSAGE(after(rows, "MAX") == "2.0", "the longest hold: " << joined(rows));
    host.setMotionNowUs(-1);
    host.shutdown();
}

// The Timing deck after a sector 22 slower through the trap than the best:
// "67 MPH" held with "-22", a bad tone (the Timing test's scene, after openRace)
void slowTrap(PluginHost& host) {
    host.drawWithState(0);
    TrapRide ride{ host };
    host.raceLap(RACE, PLAYER, 1, 91000);
    host.drawWithState(0);
    ride(40.0f);
    host.raceSplit(RACE, PLAYER, 2, 0, 30000);
    host.drawWithState(0);
    ride(50.0f);
    host.raceLap(RACE, PLAYER, 2, 90000, 0, 30000, 60000, /*invalid=*/true);
    host.drawWithState(0);
    ride(30.0f);
    host.raceSplit(RACE, PLAYER, 3, 0, 30000);
    host.drawWithState(0);
}

// Keys no shipped style sets apart from Cabinet's own values pass every other
// check misread: each is set by a skin here and its effect looked for, against
// Cabinet drawing the same scene.
TEST_CASE("decks: a skin's corners, tone colours, tint, needle colour and faces reach the drawing") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_keys\\";
    stageCabinet(save);
    stageFonts(save);
    const SyncedFontsCleanup fontsGo;
    const std::string dir = save + "mxbmrp3\\themes\\keys\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    writeLayout(dir,
        "[pack]\nbase = cabinet\n"
        "[fonts]\ndigits = VT323-Regular\n"
        // The divider leans 10 px over its height
        "[speed-box-2]\ncorners = 9.231,3.089 8.284,8.232 8.476,8.232 9.423,3.089\n"
        "[timing-text-10]\nbad-color = accent\n"
        // The trap's box takes the difference's tone
        "[timing-box-4]\nwhen = trap-diff\ntint = 1\n"
        "[wheelie-meter-1]\nneedle-color = accent\n");
    const auto rgb = [](unsigned long c) { return c & 0x00FFFFFFul; };

    struct Seen {
        int digitsFont = -1;          // the speed figures' face
        bool leaning = false;         // a quad whose left edge leans 10 px
        unsigned long trapDiff = 0;   // "-22"'s ink
        bool hugsToDiff = false;      // a box from the trap's start past the difference's
        int negativeQuads = 0;        // Timing quads in the palette's negative
        int accentQuads = 0;          // Wheelie quads in accent, a wheelie under the zone
        unsigned long negative = 0;   // the palette's
    };
    const auto draw = [&](const char* style) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasStringFont());
        REQUIRE(host.hasPaletteHooks());
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n"
                                             "[TimingDeck]\nvisible=1\n[WheelieDeck]\nvisible=1\n") + deckIni(style));
        startDecks(host, save.c_str());
        Seen seen;
        openRace(host);
        auto rows = host.hudStringRows("speed_deck");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == "45") seen.digitsFont = host.stringFont("speed_deck", static_cast<int>(i));
        // The divider (61.1 px tall) leaning 10 px: its top's middle that far
        // right of its foot's, for its height (screen px, 1920 x 1080)
        const int speedQuads = static_cast<int>(host.hudQuadRects("speed_deck").size());
        for (int i = 0; i < speedQuads; ++i) {
            float x[4] = {}, y[4] = {};
            bool all = true;
            for (int v = 0; v < 4; ++v) all = all && host.quadVertex("speed_deck", i, v, &x[v], &y[v]);
            if (!all) continue;
            const float midY = (y[0] + y[1] + y[2] + y[3]) * 0.25f;
            double topX = 0.0, footX = 0.0, topY = 0.0, footY = 0.0;
            for (int v = 0; v < 4; ++v) {
                if (y[v] < midY) { topX += x[v] * 0.5; topY += y[v] * 0.5; }
                else { footX += x[v] * 0.5; footY += y[v] * 0.5; }
            }
            const double height = (footY - topY) * 1080.0;
            if (height > 1.0 && std::fabs((topX - footX) * 1920.0 / height - 10.0 / 61.1) < 0.02) seen.leaning = true;
        }
        slowTrap(host);
        rows = host.hudStringRows("timing_deck");
        double trapX = -1.0, diffX = -1.0;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].text == "-22") { seen.trapDiff = host.stringColor("timing_deck", static_cast<int>(i)); diffX = rows[i].x; }
            if (rows[i].text == "67 MPH") trapX = rows[i].x;
        }
        REQUIRE_MESSAGE((trapX >= 0.0 && diffX > trapX), joined(rows));
        const auto timing = host.hudQuadRects("timing_deck");
        seen.negative = host.effectiveColor(8 /*NEGATIVE*/);
        const unsigned long negative = rgb(seen.negative);
        for (size_t i = 0; i < timing.size(); ++i) {
            if (timing[i].l < trapX && timing[i].r > diffX && timing[i].r - timing[i].l < 0.25) seen.hugsToDiff = true;
            seen.negativeQuads += rgb(host.quadColor("timing_deck", static_cast<int>(i))) == negative ? 1 : 0;
        }
        // Up at 20 degrees: active, under the 25-degree zone (the plain run)
        for (int i = 0; i < 30; ++i) {
            TelemetryRow r;
            r.speed = 15.0f + 0.001f * static_cast<float>(i % 7);
            r.time = 5.0f + static_cast<float>(i) * 0.01f;
            r.pitch = -20.0f;
            r.frontMaterial = 0;
            r.rearMaterial = 1;
            host.telemetryFrame(r);
            host.drawWithState(0);
        }
        const unsigned long accent = rgb(host.effectiveColor(9 /*ACCENT*/));
        const int wheelieQuads = static_cast<int>(host.hudQuadRects("wheelie_deck").size());
        for (int i = 0; i < wheelieQuads; ++i) seen.accentQuads += rgb(host.quadColor("wheelie_deck", i)) == accent ? 1 : 0;
        host.shutdown();
        return seen;
    };
    const Seen cabinet = draw("cabinet"), keys = draw("keys");
    // [fonts]
    REQUIRE(cabinet.digitsFont >= 0);
    CHECK(keys.digitsFont != cabinet.digitsFont);
    // corners
    CHECK_FALSE(cabinet.leaning);
    CHECK(keys.leaning);
    // bad-color: Cabinet's names the palette's negative
    CHECK(rgb(cabinet.trapDiff) == rgb(cabinet.negative));
    CHECK(keys.trapDiff != cabinet.trapDiff);
    // hug-to: the trap's box runs on past the difference, in both
    CHECK(cabinet.hugsToDiff);
    CHECK(keys.hugsToDiff);
    // tint: the trap's box in the difference's bad tone
    CHECK(keys.negativeQuads > cabinet.negativeQuads);
    // needle-color: the needle in accent in the plain run
    CHECK(keys.accentQuads > cabinet.accentQuads);
}

TEST_CASE("decks: the Decks and Widgets switches each come back from a restart as they were") {
    // One table names both keys (HudManager::masterGroupInfo): a row naming the
    // other's key passed every case that only flips a switch in one session
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_masters\\";
    stageCabinet(save);
    for (const bool decks : { false, true }) {
        INFO("decks " << decks);
        {
            PluginHost host(dllPath());
            REQUIRE(host.loaded());
            host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[SpeedDeck]\nvisible=1\n[SpeedWidget]\nvisible=1\n");
            startDecks(host, save.c_str());
            host.setMasterEnabled(PluginHost::Master::Decks, decks);
            host.setMasterEnabled(PluginHost::Master::Widgets, !decks);
            REQUIRE(host.save());
            host.shutdown();
        }
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(save.c_str());   // the file's switch, not startDecks'
        openRace(host);
        CHECK(host.hudHeldBack("speed_deck") == (decks ? 0 : 1));
        CHECK(host.hudHeldBack("speed_widget") == (decks ? 1 : 0));
        host.shutdown();
    }
}

// The Now Playing deck: MediaManager's snapshot, published as its worker
// would (the test build has none), drawn by Cabinet's [media]. The icons are
// not staged, so the buttons draw nothing; their boxes still take the click,
// found from the pack's design px over the panel's drawn rect. Fails if the
// deck shows another track's times, a button or strip sends nothing (or
// sends with a player that refuses it), the seek goes out before the press
// ends, the knob shows on a track that cannot seek, a new track raises no
// stamp, a volume drag outlives the player, or a press on the menu over the
// deck reaches its button.
TEST_CASE("decks: the Now Playing deck plays, seeks and sets the volume") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_media\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMediaHooks());
    REQUIRE(host.hasInjectedMouse());
    REQUIRE(host.hasQuadRects());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[General]\nnowPlaying=1\n[MediaDeck]\nvisible=1\n");
    startDecks(host, save.c_str());
    host.draw();
    auto rows = host.hudStringRows("media_deck");
    CHECK_MESSAGE(has(rows, "NOW PLAYING"), joined(rows));
    CHECK_MESSAGE(has(rows, "N/A"), "no media controls in this build: " << joined(rows));

    constexpr int PAUSED = 3, PLAYING = 4;   // MediaManager::State
    constexpr int CMD_PREVIOUS = 1, CMD_PLAY = 2, CMD_NEXT = 3, CMD_SHUFFLE = 4, CMD_LOOP = 5;
    constexpr int TAKES_ALL = 1 | 2 | 4;     // seek, shuffle, loop (test_hooks_media.cpp)
    host.mediaPublish("Song", "Artist", PLAYING, 61000, 200000, 20, TAKES_ALL);
    host.draw();
    rows = host.hudStringRows("media_deck");
    CHECK_MESSAGE(has(rows, "Song"), joined(rows));
    CHECK_MESSAGE(has(rows, "Artist"), joined(rows));
    CHECK_MESSAGE(has(rows, "1:01"), "played: " << joined(rows));
    CHECK_MESSAGE(has(rows, "-2:19"), "left: " << joined(rows));
    {
        // The track is one continuous bar, not blocks: no part starts past
        // where the ones before it end (the knob sits over the bar)
        const auto band = meterBand(host, "media_deck");
        REQUIRE(!band.empty());
        double reach = band.front().rect.r;
        for (size_t i = 1; i < band.size(); ++i) {
            CHECK_MESSAGE(band[i].rect.l <= reach + 0.5, "a gap in the track's bar before part " << i);
            reach = std::max(reach, static_cast<double>(band[i].rect.r));
        }
        CHECK_MESSAGE(band.size() <= 3, "the track's parts: " << band.size());
    }

    // Design px to the screen, over the panel (box-1: 0, 11.88, 316.8 wide)
    PluginHost::QuadRect panel;
    double area = 0.0;
    for (const auto& q : host.hudQuadRects("media_deck")) {
        const double a = (q.r - q.l) * (q.b - q.t);
        if (a > area) { area = a; panel = q; }
    }
    REQUIRE(area > 0.0);
    const double sx = (panel.r - panel.l) / 316.8, sy = (panel.b - panel.t) / 106.92;
    const auto at = [&](double px, double py) {
        return std::pair<float, float>(static_cast<float>(panel.l + px * sx), static_cast<float>(panel.t + (py - 11.88) * sy));
    };
    const auto click = [&](double px, double py) {
        const auto p = at(px, py);
        host.injectMouse(true, p.first - 0.01f, p.second, 0); host.draw();
        host.injectMouse(true, p.first, p.second, 0); host.draw();
        host.injectMouse(true, p.first, p.second, 1); host.draw();
        host.injectMouse(true, p.first, p.second, 0); host.draw();
    };
    // The knob: a quad standing past the track strip (103.53, 7.64 tall)
    const auto knobs = [&]() {
        int n = 0;
        for (const auto& q : host.hudQuadRects("media_deck")) {
            const double t = 11.88 + (q.t - panel.t) / sy, b = 11.88 + (q.b - panel.t) / sy;
            if (t < 102.0 && t > 98.0 && b > 112.0 && b < 116.0) ++n;
        }
        return n;
    };
    CHECK_MESSAGE(knobs() == 1, "the track's knob: " << knobs());

    const double row = 85.36;   // the buttons' middle
    click(25.74, row);
    CHECK(host.mediaPending(0) == CMD_SHUFFLE);
    click(55.44, row);
    CHECK(host.mediaPending(0) == CMD_PREVIOUS);
    click(87.12, row);
    CHECK(host.mediaPending(0) == CMD_PLAY);
    click(118.8, row);
    CHECK(host.mediaPending(0) == CMD_NEXT);
    click(148.5, row);
    CHECK(host.mediaPending(0) == CMD_LOOP);

    // The volume: its bar's middle (218.79 + 82.17 / 2)
    click(218.79 + 82.17 * 0.5, 85.38);
    const int volume = host.mediaPending(1);
    CHECK_MESSAGE((volume >= 45 && volume <= 55), "the volume: " << volume);

    // The track strip: pressed at a quarter, let go at the middle: the middle
    // of the track, once, on release
    const double y = 103.53 + 3.82;
    const auto q = at(7.92 + 300.96 * 0.25, y), m = at(7.92 + 300.96 * 0.5, y);
    REQUIRE(host.mediaPending(2) == -1);
    host.injectMouse(true, q.first - 0.01f, q.second, 0); host.draw();
    host.injectMouse(true, q.first, q.second, 0); host.draw();
    host.injectMouse(true, q.first, q.second, 1); host.draw();
    host.injectMouse(true, m.first, m.second, 1); host.draw();
    CHECK(host.mediaPending(2) == -1);   // not while the drag lasts
    rows = host.hudStringRows("media_deck");
    CHECK_MESSAGE(has(rows, "1:40"), "the drag's position, shown: " << joined(rows));
    host.injectMouse(true, m.first, m.second, 0); host.draw();
    const int seek = host.mediaPending(2);
    CHECK_MESSAGE((seek >= 96000 && seek <= 104000), "the seek: " << seek);

    // A volume drag ends when its bar goes (the player gone): the player
    // back, the pointer still down, it moves nothing
    const auto v0 = at(218.79 + 82.17 * 0.5, 85.38), v1 = at(218.79 + 82.17 * 0.9, 85.38);
    host.injectMouse(true, v0.first - 0.01f, v0.second, 0); host.draw();
    host.injectMouse(true, v0.first, v0.second, 0); host.draw();
    host.injectMouse(true, v0.first, v0.second, 1); host.draw();
    REQUIRE(host.mediaPending(1) == volume);
    host.mediaPublish("", "", 2);   // MediaManager::State::NO_PLAYER
    host.draw();
    host.mediaPublish("Song", "Artist", PLAYING, 61000, 200000, 20, TAKES_ALL);
    host.draw();
    host.injectMouse(true, v1.first, v1.second, 1); host.draw();
    CHECK_MESSAGE(host.mediaPending(1) == volume, "the drag ended with its bar: " << host.mediaPending(1));
    host.injectMouse(true, v1.first, v1.second, 0); host.draw();
    host.injectMouse(false); host.draw();

    // A player that takes none: no knob, and the toggles send nothing
    host.mediaPublish("Other", "Artist", PAUSED, 1000, 200000, 20, 0);
    host.draw();
    rows = host.hudStringRows("media_deck");
    CHECK_MESSAGE(knobs() == 0, "no seek, no knob: " << knobs());
    CHECK_MESSAGE(!has(rows, "NEW TRACK"), "a new track raises no stamp: " << joined(rows));
    click(55.44, row);
    REQUIRE(host.mediaPending(0) == CMD_PREVIOUS);
    click(25.74, row);
    click(148.5, row);
    CHECK(host.mediaPending(0) == CMD_PREVIOUS);
    // ... and the track strip, which it cannot seek, does not drag (let go
    // at three quarters, not where the last seek went)
    const auto e = at(7.92 + 300.96 * 0.75, y);
    host.injectMouse(true, q.first - 0.01f, q.second, 0); host.draw();
    host.injectMouse(true, q.first, q.second, 0); host.draw();
    host.injectMouse(true, q.first, q.second, 1); host.draw();
    host.injectMouse(true, e.first, e.second, 1); host.draw();
    host.injectMouse(true, e.first, e.second, 0); host.draw();
    CHECK_MESSAGE(host.mediaPending(2) == seek, "no seek for a player that refuses one: " << host.mediaPending(2));

    // The click is the topmost HUD's (BaseHud::ownsClick): moved so Next
    // sits in the middle of the open menu, the press is the menu's; the menu
    // shut, the same spot is Next again
    REQUIRE(host.hasScreenEdges());
    host.showSettings(true);
    host.draw();
    const PluginHost::ScreenEdges menu = host.hudScreenEdges("settings_hud");
    const PluginHost::ScreenEdges deckAt = host.hudScreenEdges("media_deck");
    REQUIRE(menu.r > menu.l);
    const auto next = at(118.8, row);
    const float mx = static_cast<float>(menu.l + menu.r) * 0.5e-6f;
    const float my = static_cast<float>(menu.t + menu.b) * 0.5e-6f;
    const float dx = next.first - static_cast<float>(deckAt.l) * 1e-6f;
    const float dy = next.second - static_cast<float>(deckAt.t) * 1e-6f;
    REQUIRE(host.setHudOffset("media_deck", 0.0f, 0.0f));
    host.draw();
    const PluginHost::ScreenEdges home = host.hudScreenEdges("media_deck");
    REQUIRE(host.setHudOffset("media_deck", mx - dx - static_cast<float>(home.l) * 1e-6f,
                              my - dy - static_cast<float>(home.t) * 1e-6f));
    host.draw();
    const auto press = [&](float x, float y) {
        host.injectMouse(true, x - 0.01f, y, 0); host.draw();
        host.injectMouse(true, x, y, 0); host.draw();
        host.injectMouse(true, x, y, 1); host.draw();
        host.injectMouse(true, x, y, 0); host.draw();
    };
    press(mx, my);
    CHECK_MESSAGE(host.mediaPending(0) == CMD_PREVIOUS, "the menu's click, not Next: " << host.mediaPending(0));
    host.showSettings(false);
    host.draw();
    press(mx, my);
    CHECK_MESSAGE(host.mediaPending(0) == CMD_NEXT, "Next, the menu shut: " << host.mediaPending(0));
    host.shutdown();
}

// The carousel (DeckHud, THE CAROUSEL) steps as the Dash pages do, on the
// shared PageFlip: a rider's step puts the deck's name up, at full while it
// holds with the readouts under it not shown, then the readouts come back. The
// count marker rides in the title, counting only the decks in the turn.
TEST_CASE("decks: the carousel steps through its decks, each named as it comes, its place in the title") {
    constexpr long long T0 = 1000000000ll;   // Motion's clock, in microseconds
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    const std::string base = "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=0\nmarker=count\nnoticeFocus=0\n";
    host.writeSettingsFile(save.c_str(), base + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotionNowUs(T0);
    host.setMotion(/*Normal: 240 ms*/ 2);
    openRace(host);
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    const auto alphaOf = [&](const char* text) {
        const auto r = rows();
        for (size_t i = 0; i < r.size(); ++i)
            if (r[i].text == text) return static_cast<int>(host.stringColor("carousel_deck", static_cast<int>(i)) >> 24);
        return -1;
    };
    // Every string but the title and the name at no alpha: the readouts held back
    const auto readoutsHidden = [&](const char* title, const char* name) {
        const auto r = rows();
        int readouts = 0;
        for (size_t i = 0; i < r.size(); ++i) {
            if (r[i].text == title || r[i].text == name) continue;
            ++readouts;
            if ((host.stringColor("carousel_deck", static_cast<int>(i)) >> 24) != 0) return -1;
        }
        return readouts;
    };
    const auto at = [&](long long us) { host.setMotionNowUs(us); host.draw(); };
    at(T0);
    REQUIRE_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));
    CHECK(alphaOf("TIMING") == -1);   // no name before a step

    // Deck +: Gap, its name at full while it holds, its readouts laid out but not shown
    const long long t1 = T0 + 1000000;
    host.setMotionNowUs(t1);
    REQUIRE(host.injectHotkey("deck_next"));
    at(t1);
    CHECK_MESSAGE(has(rows(), "GAP 2/7"), joined(rows()));
    at(t1 + 300000);
    CHECK(alphaOf("GAP") == 0xFF);
    CHECK(readoutsHidden("GAP 2/7", "GAP") > 0);
    at(t1 + 600000 + 120000);   // halfway from the name to the readouts
    const int half = alphaOf("GAP");
    CHECK(half > 0x10);
    CHECK(half < 0xF0);
    at(t1 + 600000 + 300000);   // over: the readouts, no name
    CHECK(alphaOf("GAP") == -1);
    CHECK(readoutsHidden("GAP 2/7", "GAP") == -1);

    // Deck -: back to Timing, round the other way
    const long long t2 = T0 + 2000000;
    host.setMotionNowUs(t2);
    REQUIRE(host.injectHotkey("deck_prev"));
    at(t2);
    CHECK_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));
    // and once more round, from the first to the last
    host.setMotionNowUs(t2 + 1000000);
    REQUIRE(host.injectHotkey("deck_prev"));
    at(t2 + 1000000);
    CHECK_MESSAGE(has(rows(), "COMBO 7/7"), joined(rows()));

    // Gap out of the turn: Combo on to Timing, then Rival, counted of six
    host.writeSettingsFile(save.c_str(), base + "deckGap=0\n" + deckIni("cabinet"));
    host.loadSettings(save.c_str());
    const long long t3 = T0 + 4000000;
    host.setMotionNowUs(t3);
    REQUIRE(host.injectHotkey("deck_next"));
    at(t3);
    CHECK_MESSAGE(has(rows(), "TIMING 1/6"), joined(rows()));
    REQUIRE(host.injectHotkey("deck_next"));
    at(t3);
    CHECK_MESSAGE(has(rows(), "RIVAL 2/6"), joined(rows()));

    // Motion off: the name for its hold, then a cut to the readouts
    host.setMotion(/*Off*/ 0);
    const long long t4 = T0 + 6000000;
    host.setMotionNowUs(t4);
    REQUIRE(host.injectHotkey("deck_next"));   // Rival -> Session
    at(t4);
    CHECK(alphaOf("SESSION") == 0xFF);
    at(t4 + 590000);
    CHECK(alphaOf("SESSION") == 0xFF);
    at(t4 + 610000);
    CHECK(alphaOf("SESSION") == -1);

    // Deck names off: a step goes straight to the readouts
    host.writeSettingsFile(save.c_str(), base + "deckGap=1\nnames=0\n" + deckIni("cabinet"));
    host.loadSettings(save.c_str());
    const long long t5 = T0 + 8000000;
    host.setMotionNowUs(t5);
    REQUIRE(host.injectHotkey("deck_next"));
    at(t5);
    CHECK(alphaOf("INPUTS") == -1);
    CHECK_MESSAGE(has(rows(), "INPUTS 5/7"), joined(rows()));

    // Names on: a left click on the title steps on, named like Deck +. The
    // deck sits at its default place, away from the screen's corner, so a
    // hit test that forgets the deck's offset misses (it did, until this)
    host.writeSettingsFile(save.c_str(), base + "deckGap=1\nnames=1\n" + deckIni("cabinet"));
    host.loadSettings(save.c_str());
    const long long t6 = T0 + 10000000;
    at(t6);
    double tx = -1.0, ty = -1.0;
    for (const auto& r : rows()) if (r.text == "INPUTS 5/7") { tx = r.x; ty = r.y; }
    REQUIRE(tx >= 0.0);
    const float cx = static_cast<float>(tx) + 0.01f, cy = static_cast<float>(ty) + 0.005f;
    host.injectMouse(true, cx - 0.01f, cy, 0); at(t6);
    host.injectMouse(true, cx, cy, 0); at(t6);
    host.injectMouse(true, cx, cy, 1); at(t6);
    host.injectMouse(true, cx, cy, 0); at(t6);
    CHECK_MESSAGE(has(rows(), "LAP LOG 6/7"), joined(rows()));
    CHECK_MESSAGE(alphaOf("LAP LOG") == 0xFF, "named: " << joined(rows()));
    host.shutdown();
}

// Auto (off by default) steps on its own clock, which waits while a notice
// holds the carousel; neither it nor a new race notice, which takes it over
// for 4 s, puts a name up: only the rider's step does. A step while a notice
// holds gives the rider's deck back for good.
TEST_CASE("decks: the carousel's Auto clock steps it, and a notice takes it over until it ends or a step") {
    constexpr long long T0 = 1000000000ll;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_auto\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=5\nmarker=count\n" +
                                         deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotion(/*Off: cuts*/ 0);
    host.setMotionNowUs(T0);
    host.draw();
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    const auto at = [&](long long us) { host.setMotionNowUs(us); host.draw(); };
    // The race opens with no setup named: the DEFAULT SETUP notice takes it over
    openRace(host);
    at(T0 + 1000);
    REQUIRE_MESSAGE(has(rows(), "DEFAULT SETUP"), joined(rows()));
    CHECK_FALSE(has(rows(), "TIMING 1/7"));
    // Past 4 s the rider's deck is back, its Auto clock having waited
    at(T0 + 4100000);
    CHECK_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));
    at(T0 + 5200000);   // 5 s after the start, but only ~1 s of it shown
    CHECK_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));
    at(T0 + 9200000);   // 5 s shown: on to Gap, unnamed (Motion off holds a name 600 ms)
    CHECK_MESSAGE(has(rows(), "GAP 2/7"), joined(rows()));
    CHECK_FALSE_MESSAGE(has(rows(), "GAP"), joined(rows()));
    at(T0 + 14300000);
    CHECK_MESSAGE(has(rows(), "RIVAL 3/7"), joined(rows()));
    // Its gap measured the frame Auto steps in (the clock runs after the
    // decks' own steps), not the none left from before its turn
    CHECK_MESSAGE(has(rows(), "1.50 s"), "the gap to Bob, the first frame: " << joined(rows()));
    host.shutdown();

    // A step while the notice holds gives the deck back at once, and that
    // notice does not take it over again
    PluginHost again(dllPath());
    REQUIRE(again.loaded());
    again.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nmarker=count\n" +
                                          deckIni("cabinet"));
    again.startup(save.c_str());
    again.setMotion(0);
    again.setMotionNowUs(T0);
    openRace(again);
    again.setMotionNowUs(T0 + 1000);
    again.draw();
    REQUIRE_MESSAGE(has(again.hudStringRows("carousel_deck"), "DEFAULT SETUP"), joined(again.hudStringRows("carousel_deck")));
    REQUIRE(again.injectHotkey("deck_next"));
    again.draw();
    CHECK_MESSAGE(has(again.hudStringRows("carousel_deck"), "TIMING 1/7"), joined(again.hudStringRows("carousel_deck")));
    again.setMotionNowUs(T0 + 2000000);
    again.draw();
    CHECK_MESSAGE(has(again.hudStringRows("carousel_deck"), "TIMING 1/7"), joined(again.hudStringRows("carousel_deck")));
    // No auto key: Auto is off, so it stays put
    again.setMotionNowUs(T0 + 20000000);
    again.draw();
    CHECK_MESSAGE(has(again.hudStringRows("carousel_deck"), "TIMING 1/7"), joined(again.hudStringRows("carousel_deck")));
    again.shutdown();
}

// What leaves fades: a step fades the deck stepped away from out from where
// it stood, and a step while a name is up fades that name rather than
// dropping it. Fails if finishCarousel stops drawing the deck it left (a pop
// to the name) or carouselShow stops gathering the name it leaves.
TEST_CASE("decks: the carousel fades out the deck it leaves, and a name it steps past") {
    constexpr long long T0 = 1000000000ll;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_fade\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=0\nmarker=count\nnoticeFocus=0\n" +
                                         deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotionNowUs(T0);
    host.setMotion(/*Normal: 240 ms*/ 2);
    openRace(host);
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    const auto at = [&](long long us) { host.setMotionNowUs(us); host.draw(); };
    // Every alpha a text is drawn at (a fading copy can stand beside the new one)
    const auto alphasOf = [&](const std::string& text) {
        std::vector<int> out;
        const auto r = rows();
        for (size_t i = 0; i < r.size(); ++i)
            if (r[i].text == text) out.push_back(static_cast<int>(host.stringColor("carousel_deck", static_cast<int>(i)) >> 24));
        return out;
    };
    at(T0);
    REQUIRE_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));
    std::vector<std::string> timing;
    for (const auto& r : rows())
        if (!r.text.empty() && r.text != "TIMING 1/7") timing.push_back(r.text);
    REQUIRE_FALSE(timing.empty());

    // Deck +: Timing -> Gap. Halfway into the step GAP's name is up, Gap's
    // readouts not yet shown, and Timing's, as they stood, fading out
    const auto fading = [&] {
        int n = 0;
        const auto r = rows();
        for (size_t i = 0; i < r.size(); ++i) {
            const int a = static_cast<int>(host.stringColor("carousel_deck", static_cast<int>(i)) >> 24);
            if (a > 0 && a < 0xFF && r[i].text != "GAP") ++n;
        }
        return n;
    };
    const long long t3 = T0 + 1000000;
    host.setMotionNowUs(t3);
    REQUIRE(host.injectHotkey("deck_next"));
    at(t3);
    at(t3 + 120000);
    REQUIRE_MESSAGE(has(rows(), "GAP 2/7"), joined(rows()));
    CHECK_MESSAGE(fading() == static_cast<int>(timing.size()), "Timing's texts fading: " << fading() << " of " << timing.size());
    at(t3 + 300000);
    CHECK_MESSAGE(fading() == 0, "the fade over: " << joined(rows()));

    // A step while GAP's name holds at full: the name fades, not cut (Rival
    // has a GAP label of its own, held back at no alpha under RIVAL)
    at(t3 + 400000);
    REQUIRE(alphasOf("GAP") == std::vector<int>{ 0xFF });
    REQUIRE(host.injectHotkey("deck_next"));   // Gap -> Rival
    at(t3 + 400000);
    at(t3 + 400000 + 120000);
    REQUIRE_MESSAGE(has(rows(), "RIVAL 3/7"), joined(rows()));
    int nameFading = 0;
    for (const int a : alphasOf("GAP"))
        if (a > 0 && a < 0xFF) ++nameFading;
    CHECK_MESSAGE(nameFading == 1, "the name it left, fading: " << joined(rows()));
    host.shutdown();
}

// The turn changing under it, and being hidden: switched off, the deck
// showing gives way to the next one on; hidden while a notice holds it, it
// comes back on the rider's deck. Fails if followCarousel keeps a deck that
// was switched off, or the hidden path keeps the hold.
TEST_CASE("decks: the carousel follows its turn, and comes back from hidden on the rider's deck") {
    constexpr long long T0 = 1000000000ll;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_follow\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    const std::string base = "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nauto=0\nmarker=count\nnames=0\n";
    host.writeSettingsFile(save.c_str(), base + "visible=1\nnoticeFocus=0\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotion(/*Off: cuts*/ 0);
    host.setMotionNowUs(T0);
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    const auto at = [&](long long us) { host.setMotionNowUs(us); host.draw(); };
    openRace(host);
    at(T0 + 1000);
    REQUIRE_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));

    // Timing switched off while it shows: on to Gap, first of six
    host.writeSettingsFile(save.c_str(), base + "visible=1\nnoticeFocus=0\ndeckTiming=0\n" + deckIni("cabinet"));
    host.loadSettings(save.c_str());
    at(T0 + 2000);
    CHECK_MESSAGE(has(rows(), "GAP 1/6"), joined(rows()));
    host.shutdown();

    // Hidden while a notice holds it, shown again: the rider's deck
    PluginHost again(dllPath());
    REQUIRE(again.loaded());
    again.writeSettingsFile(save.c_str(), base + "visible=1\n" + deckIni("cabinet"));
    again.startup(save.c_str());
    again.setMotion(0);
    again.setMotionNowUs(T0);
    openRace(again);
    const auto rows2 = [&] { return again.hudStringRows("carousel_deck"); };
    again.setMotionNowUs(T0 + 1000);
    again.draw();
    REQUIRE_MESSAGE(has(rows2(), "DEFAULT SETUP"), joined(rows2()));
    REQUIRE(again.setHudVisible("carousel_deck", false));
    again.setMotionNowUs(T0 + 2000);
    again.draw();
    REQUIRE(again.setHudVisible("carousel_deck", true));
    again.setMotionNowUs(T0 + 3000);
    again.draw();
    again.draw();
    CHECK_MESSAGE(has(rows2(), "TIMING 1/7"), joined(rows2()));
    CHECK_FALSE(has(rows2(), "DEFAULT SETUP"));
    again.shutdown();
}

// What the carousel's deck saw before it stepped away is not news when it
// comes back: carouselShow forgets it, as a deck shown again does. Here the
// Session deck: its state changes while Inputs shows, and stepping back
// raises no stamp for it. Fails if carouselShow stops calling forgetUnshown.
TEST_CASE("decks: a carousel deck shown again stamps nothing that changed while it was away") {
    constexpr long long T0 = 1000000000ll;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_unshown\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=0\nmarker=count\nnames=0\nnoticeFocus=0\n"
                                         "deckTiming=0\ndeckGap=0\ndeckRival=0\ndeckLapLog=0\ndeckCombo=0\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotion(/*Off: cuts*/ 0);
    host.setMotionNowUs(T0);
    openRace(host);
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    host.draw();
    REQUIRE_MESSAGE(has(rows(), "SESSION 1/2"), joined(rows()));
    REQUIRE(host.injectHotkey("deck_next"));
    host.draw();
    REQUIRE_MESSAGE(has(rows(), "INPUTS 2/2"), joined(rows()));
    host.raceSessionState(RACE, /*state=*/512);
    host.draw();
    REQUIRE(host.injectHotkey("deck_next"));
    host.draw();
    host.draw();
    REQUIRE_MESSAGE(has(rows(), "SESSION 1/2"), joined(rows()));
    CHECK_MESSAGE(!has(rows(), "RACE OVER"), "no stamp for what changed away: " << joined(rows()));
    // ...while one that changes as it shows is stamped
    host.session(RACE + 1, /*numLaps=*/5, /*lengthMs=*/0);
    host.draw();
    CHECK_MESSAGE(has(rows(), "RACE 2"), joined(rows()));
    host.shutdown();
}

// The dots marker: one dot a deck in the turn beside the title, inside the
// tab hugging it, the deck showing's lit
TEST_CASE("decks: the carousel's dots stand beside its title, one a deck in the turn") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_dots\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const auto quadsWith = [&](const char* ini) {
        host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=0\nnoticeFocus=0\n") +
                                             ini + deckIni("cabinet"));
        host.loadSettings(save.c_str());
        host.draw();
        host.draw();
        return host.hudQuadRects("carousel_deck").size();
    };
    // Started as the cases load it: Timing in the turn, so no load moves the
    // carousel off a deck that left it mid-count, and no notice holding it
    // (the carousel is a starter deck, so it is up from the start)
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nauto=0\nnoticeFocus=0\n" +
                                         deckIni("cabinet"));
    startDecks(host, save.c_str());
    openRace(host);
    const size_t off = quadsWith("marker=off\n");
    const size_t dots = quadsWith("marker=dots\n");
    const size_t fewer = quadsWith("marker=dots\ndeckGap=0\ndeckRival=0\n");
    CHECK(dots == off + 7);
    CHECK(fewer == off + 5);
    CHECK(has(host.hudStringRows("carousel_deck"), "TIMING"));
    host.shutdown();
}

// The dots keep one place as the carousel steps: the title holds the widest
// of the turn's names (DeckHud::widestName), so a short name (GAP) leaves
// them where a long one (TIMING) put them. Fails if the title's run goes
// back to hugging its own text: the dots then jumped sideways on each step.
TEST_CASE("decks: the carousel's dots stand still as it steps between names of different lengths") {
    constexpr long long T0 = 1000000000ll;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_dots_still\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    const std::string base = "[Settings]\nversion=11\n[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=0\nnames=0\nnoticeFocus=0\n";
    host.writeSettingsFile(save.c_str(), base + "marker=dots\n" + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotion(/*Off*/ 0);
    openRace(host);
    long long now = T0;
    const auto settle = [&] { now += 2000000; host.setMotionNowUs(now); host.draw(); host.draw(); };
    // The dots: seven quads of one size on one line, the highest such run (the
    // title band is the deck's top)
    const auto dotsNow = [&] {
        const auto rects = host.hudQuadRects("carousel_deck");
        const auto same = [](double a, double b) { return std::fabs(a - b) < 3e-6; };   // rects come rounded to 1e-6
        std::vector<PluginHost::QuadRect> best;
        for (const auto& q : rects) {
            std::vector<PluginHost::QuadRect> run;
            for (const auto& o : rects)
                if (same(o.t, q.t) && same(o.b, q.b) && same(o.r - o.l, q.r - q.l)) run.push_back(o);
            if (run.size() == 7 && (best.empty() || q.t < best[0].t)) best = run;
        }
        std::sort(best.begin(), best.end(), [](const auto& x, const auto& y) { return x.l < y.l; });
        return best;
    };
    settle();
    REQUIRE_MESSAGE(has(host.hudStringRows("carousel_deck"), "TIMING"), joined(host.hudStringRows("carousel_deck")));
    const auto timing = dotsNow();
    REQUIRE(timing.size() == 7);
    REQUIRE(host.injectHotkey("deck_next"));
    settle();
    REQUIRE_MESSAGE(has(host.hudStringRows("carousel_deck"), "GAP"), joined(host.hudStringRows("carousel_deck")));
    const auto gap = dotsNow();
    REQUIRE(gap.size() == 7);
    for (size_t i = 0; i < timing.size(); ++i) {
        CHECK_MESSAGE(std::fabs(gap[i].l - timing[i].l) < 1e-5, "dot " << i << ": " << timing[i].l << " -> " << gap[i].l);
        CHECK(std::fabs(gap[i].t - timing[i].t) < 1e-5);
    }
    host.shutdown();
}

// Now Playing joins the carousel's turn while a player plays or pauses, and
// leaves it with the player: shown then, it gives way to the next deck on.
// Only Now Playing switched on, it stays, with its line for no player. Fails
// if carouselOn stops leaving Now Playing out with nothing playing, or reads
// the player once rather than as MediaManager's revision moves.
TEST_CASE("decks: the carousel turns through Now Playing only while something plays") {
    constexpr long long T0 = 1000000000ll;
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\deck_carousel_media\\";
    stageCabinet(save);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    REQUIRE(host.hasMediaHooks());
    const std::string base = "[Settings]\nversion=11\n[General]\nnowPlaying=1\n"
                             "[CarouselDeck]\ndeckTiming=1\nvisible=1\nauto=0\nmarker=count\nnames=0\nnoticeFocus=0\n";
    host.writeSettingsFile(save.c_str(), base + deckIni("cabinet"));
    startDecks(host, save.c_str());
    host.setMotion(/*Off: cuts*/ 0);
    openRace(host);
    const auto rows = [&] { return host.hudStringRows("carousel_deck"); };
    long long now = T0;
    const auto settle = [&] { now += 1000000; host.setMotionNowUs(now); host.draw(); host.draw(); };
    settle();
    REQUIRE_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));
    // Nothing plays: round the other way is Combo, the last of seven
    REQUIRE(host.injectHotkey("deck_prev"));
    settle();
    CHECK_MESSAGE(has(rows(), "COMBO 7/7"), joined(rows()));

    constexpr int NO_PLAYER = 2, PAUSED = 3;   // MediaManager::State
    host.mediaPublish("Song", "Artist", PAUSED, 1000, 200000, 20, 0);
    settle();
    CHECK_MESSAGE(has(rows(), "COMBO 7/8"), "a paused player joins the turn: " << joined(rows()));
    REQUIRE(host.injectHotkey("deck_next"));
    settle();
    CHECK_MESSAGE(has(rows(), "NOW PLAYING 8/8"), joined(rows()));
    CHECK_MESSAGE(has(rows(), "Song"), joined(rows()));

    // The player closes while it shows: on to the next one on
    host.mediaPublish("", "", NO_PLAYER);
    settle();
    CHECK_MESSAGE(has(rows(), "TIMING 1/7"), joined(rows()));

    // Only Now Playing on: it stays, saying there is no player
    host.writeSettingsFile(save.c_str(), base + "deckTiming=0\ndeckGap=0\ndeckRival=0\ndeckSession=0\ndeckInputs=0\n"
                                                "deckLapLog=0\ndeckCombo=0\n" + deckIni("cabinet"));
    host.loadSettings(save.c_str());
    settle();
    CHECK_MESSAGE(has(rows(), "NOW PLAYING"), joined(rows()));
    CHECK_MESSAGE(has(rows(), "No player"), joined(rows()));
    host.shutdown();
}
