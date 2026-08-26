// ============================================================================
// core/test_hooks.cpp
// Test-only DLL exports for the headless (mingw/Wine) test harness. These let a
// test driver invoke internal actions that are normally only reachable through
// the in-game settings UI — reset-to-defaults (all / active profile / one HUD),
// copy-profile-to-all, profile switch, an explicit save, and the update-checker
// version comparison.
//
// The ENTIRE file is gated on MXBMRP3_TEST_BUILD, so it is empty in every
// shipping (MSVC) configuration and the exports never exist in a released DLL.
// It is also not referenced by mxbmrp3.vcxproj; only the cross-build's source
// glob picks it up. See tests/integration/ and DEVELOPMENT.md.
// ============================================================================
#include "steam_friends_manager.h"
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "settings_manager.h"
#include "ui_config.h"
#include "render_probe_sweep.h"
#include "hud_manager.h"
#include "../hud/settings/whats_new.h"
#include "companion_window.h"
#include "../hud/settings_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/map_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/director_widget.h"
#include "../hud/event_log_hud.h"
#include "../hud/notices_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/timing_hud.h"
#include "../hud/map_hud.h"
#include "../hud/session_hud.h"
#include "../hud/session_charts_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/gamepad_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/position_widget.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/crash_widget.h"
#include "asset_manager.h"
#include "layout_config.h"
#include "plugin_constants.h"
#include <algorithm>
#include <cstring>
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "input_manager.h"
#include "analytics_manager.h"
#include "plugin_manager.h"
#include "plugin_thread.h"
#include "plugin_data.h"
#include "xinput_reader.h"
#include "profile_manager.h"
#include "director_manager.h"
#include "stats_manager.h"
#include "spotter_manager.h"
#include "../handlers/spectate_handler.h"
#if GAME_HAS_FMX
#include "fmx_manager.h"
#endif
#include "update_checker.h"
#include "update_downloader.h"
#include "http_server.h"
#include "../game/game_config.h"
#if GAME_HAS_RECORDER
#include "event_recorder.h"
#endif
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include "crash_handler.h"

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

// PerformanceHud's element mask. It ships as CPU alone -- one section -- so the themed
// geometry gates could not exercise the section-boundary arithmetic on it and passed
// vacuously. 3 = FPS | CPU gives it two.
void MXBMRP3_Test_SetPerformanceElementsImpl(unsigned int mask) {
    PerformanceHud& hud = HudManager::getInstance().getPerformanceHud();
    hud.m_enabledElements = mask;
    hud.setDataDirty();
}

extern "C" {

#if GAME_HAS_HTTP_SERVER
// Start the web overlay server directly (default off), so an integration test can
// read /api/state without seeding a settings file and restarting the plugin.
// Start the Steam friend-scan worker without Steam. The scan itself needs the
// Steam client, but the THREADING LIFECYCLE does not: with no connection
// scanFriends() returns immediately, so the worker just loops on its condvar.
// That is exactly what a shutdown has to join, and the failure this pins is a
// hang or a use-after-free at teardown rather than anything Steam-specific.
// Returns 1 if a worker is running afterwards.
__declspec(dllexport) int MXBMRP3_Test_SteamStartWorker() {
    return SteamFriendsManager::getInstance().testStartWorker() ? 1 : 0;
}

__declspec(dllexport) int MXBMRP3_Test_SteamWorkerRunning() {
    return SteamFriendsManager::getInstance().testWorkerRunning() ? 1 : 0;
}

__declspec(dllexport) void MXBMRP3_Test_StartHttp() {
    HttpServer::getInstance().setEnabled(true);
    HttpServer::getInstance().start();
}


// Build the /api/state snapshot directly (game thread) and return it, WITHOUT
// starting the server or going through the socket + rebuild-gating. This is how
// plugin-logic tests observe computed state in isolation — no HTTP machinery.
// The returned pointer is valid until the next call (single-threaded test use).
__declspec(dllexport) const char* MXBMRP3_Test_Snapshot() {
    static std::string buf;
    buf = HttpServer::getInstance().testSnapshot();
    return buf.c_str();
}

// The SSE sequence: bumped once per actual snapshot rebuild inside
// onDataChanged. Lets a test tell "this change type rebuilt" from "this change
// type was gated away while nothing was consuming" — a distinction with no
// other observable, since a gated change simply leaves the previous snapshot
// in place. Used by http_gating_test.cpp.
__declspec(dllexport) unsigned long long MXBMRP3_Test_SnapshotSeq() {
    return HttpServer::getInstance().testSnapshotSeq();
}
#endif


// ---- Spotter -----------------------------------------------------------
// The spotter's cue DECISIONS are what tests assert — the cue log records
// every composed phrase whether or not audio playback succeeded, so a Wine
// prefix with no SAPI voice still tests detection, phrasing, and filtering.

__declspec(dllexport) void MXBMRP3_Test_SpotterEnable(int on) {
    SpotterManager::getInstance().setEnabled(on != 0);
}

__declspec(dllexport) void MXBMRP3_Test_SpotterSubtitles(int on) {
    SpotterManager::getInstance().setSubtitlesEnabled(on != 0);
}

__declspec(dllexport) void MXBMRP3_Test_SpotterCategoryMask(unsigned mask) {
    SpotterManager::getInstance().setCategoryMask(mask);
}

// Inject cue-pack CONTENT directly (the harness stages no asset tree — the
// installSyntheticTheme rationale). Empty text clears back to built-ins.
__declspec(dllexport) void MXBMRP3_Test_SpotterInstallPack(const char* iniText) {
    SpotterManager::getInstance().testInstallPack(
        iniText ? iniText : "", "Z:\\tmp\\spotter-pack");
}

// Pin which alternate a cue with variants speaks: -1 rolls (shipping
// behaviour), 0 always takes the base row. See SpotterManager::testPinVariant
// for why a test that asserts exact words wants this rather than a seed.
__declspec(dllexport) void MXBMRP3_Test_SpotterPinVariant(int idx) {
    SpotterManager::getInstance().testPinVariant(idx);
}

// Fire the settings menu's voice preview. What is worth asserting is what it
// must NOT do — reach the cue log — so this exists to let a test call it and
// then read the log back. Audio is unobservable under Wine either way.
// The Spotter Cue hotkey, which HudManager reaches through the input path a
// headless run has no way to drive. It was one of the emitters that skipped
// the category gate, so it needs to be reachable to pin that it no longer can.
__declspec(dllexport) void MXBMRP3_Test_SpotterHotkey() {
    SpotterManager::getInstance().speakHotkeyCue();
}

__declspec(dllexport) void MXBMRP3_Test_SpotterPreview(int ttsOnly) {
    SpotterManager::getInstance().previewVoice(ttsOnly != 0);
}

// Newline-joined cues, oldest first, each "sessionTimeMs<TAB>text" — the
// timestamp makes the dump a usable subtitle transcript (demo-tape renders,
// cue-timing debugging); assertions match on the text with find().
__declspec(dllexport) void MXBMRP3_Test_SpotterCueLog(char* out, int cap) {
    if (!out || cap <= 0) return;
    std::string joined;
    for (const auto& e : SpotterManager::getInstance().getCueLog()) {
        joined += std::to_string(e.sessionTimeMs);
        joined += '\t';
        joined += e.text;
        joined += '\n';
    }
    // Truncate from the FRONT, keeping the newest cues. The other way round
    // is what a fixed buffer does by default, and it is silently wrong here:
    // a test asserts on what it just provoked, so dropping the tail hands back
    // a log missing exactly the lines under test — and the suite's "must NOT
    // say X" checks then pass on text that was merely cut off. The ring holds
    // 96 entries, which outgrows any single buffer a caller picks, so the
    // choice of which end to lose is not hypothetical.
    const int size = static_cast<int>(joined.size());
    const int n = size < cap - 1 ? size : cap - 1;
    const int from = size - n;
    for (int i = 0; i < n; ++i) out[i] = joined[from + i];
    out[n] = '\0';
}

// Which audio route the LAST cue took: "<chosen key>|" plus one of
// "mix:a.wav+b.wav", "wav:x.wav", "tts", "silent" (the cue resolved to nothing at
// all) or "muted" (its category is switched off). The cue log carries only the
// words, so this is the only headless way to tell a pack's recording from the TTS
// that stands in for it — and TTS is silence on Wine/Proton, so "spoke the right
// words" is not the same question as "played". Every exit from emitCue records,
// deliberately: a seam that answers for some cues and keeps the previous cue's
// answer for the rest is read as this cue's answer, which is worse than none.
__declspec(dllexport) void MXBMRP3_Test_SpotterLastAudio(char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string& s = SpotterManager::getInstance().testLastAudioRoute();
    const int n = static_cast<int>(s.size()) < cap - 1 ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}

// Reset EVERYTHING to factory defaults (per-profile HUDs for all profiles +
// globals). Mirrors settings_hud.cpp's "Reset All". Persists.
__declspec(dllexport) void MXBMRP3_Test_ResetAll() {
    SettingsManager::getInstance().resetAllToFactoryDefaults(HudManager::getInstance());
}

// The GLOBAL half of the settings menu's "Reset Everything": replays the
// factory-default snapshot over [General]/[Advanced]/colours/fonts/hotkeys.
//
// Separate from MXBMRP3_Test_ResetAll, which is the PER-PROFILE half. The button
// calls both, and a test that calls only one is testing half a button -- which is
// how the what's-new case first "proved" that reset does not clear a dismissal
// when in fact the half that clears it had not been called.
__declspec(dllexport) void MXBMRP3_Test_ResetGlobals() {
    SettingsManager::getInstance().resetGlobalsToFactoryDefaults(HudManager::getInstance());
}

// Reset the ACTIVE profile's HUDs/widgets to defaults; globals + other profiles
// untouched. Does not persist on its own — call MXBMRP3_Test_Save().
__declspec(dllexport) void MXBMRP3_Test_ResetActiveProfile() {
    SettingsManager::getInstance().resetActiveProfileToFactoryDefaults(HudManager::getInstance());
}

// StandingsHud's per-HUD panel-theme override. Exists for theme_override_test.cpp:
// the override is captured SPARSELY (written only when the HUD has diverged), so its
// apply side has to clear authoritatively when the key is absent -- and it did not,
// which left a HUD pinned to a theme through Reset to Defaults and through entering
// a profile whose cache carries no theme key. Reading it back is the only way to see
// that from a test.
//
// StandingsHud stands in for "a per-profile HUD": the override lives on BaseHud, so
// one instance exercises the whole capture/apply path, and the hook needs no name
// parameter.
__declspec(dllexport) void MXBMRP3_Test_StandingsTheme(char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string& v = HudManager::getInstance().getStandingsHud().getThemeOverride();
    const int n = (static_cast<int>(v.size()) < cap - 1) ? static_cast<int>(v.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = v[i];
    out[n] = '\0';
}

// Install a synthetic panel theme and select it globally. No files on disk: every
// themed layout rule is arithmetic over these four numbers, and requiring the
// shipped themes to be staged next to the integration suite is what kept this
// entire surface untested. See AssetManager::installSyntheticTheme.
//
// insets are in GRID CELLS, matching a theme ini's `size` keys.
// Give the CURRENTLY INSTALLED synthetic theme an icon override: `icon` draws at
// `sprite`, and that sprite maps back to shape index `shape`. 0 if no theme is
// installed.
//
// INJECTED, not loaded: the integration harness stages no assets, so there is no
// icons/ directory to discover and no base vocabulary to key a real override to.
// The rule that a theme may only override a name the base set HAS is enforced in
// discoverThemes() against the real set and is not what this hook models -- what it
// models is the resolution surviving a theme SWITCH, which is memoised state
// (AssetManager::activeIconTheme) and therefore the part that can go stale.
__declspec(dllexport) int MXBMRP3_Test_SetThemeIconOverride(const char* icon, int sprite,
                                                            int shape) {
    if (!icon || !*icon) return 0;
    if (!AssetManager::getInstance().testSetThemeIconOverride(icon, sprite, shape)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// `[card] band-size`, injected into the last installed synthetic theme. Negative puts
// the band back on `[card] size`, which is where it sits for a theme that never names
// the key -- so a case can assert the fallback as well as the split.
__declspec(dllexport) int MXBMRP3_Test_SetThemeTitleBorder(float cells) {
    if (!AssetManager::getInstance().testSetThemeTitleBorder(cells)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// `[panel] padding-x/-y`, injected into the last installed synthetic theme. Negative
// puts an axis back on the built-in, which is where it sits for a theme that never
// names the key -- so a case can assert the fallback as well as the override.
// An ASYMMETRIC `[content] border`, injected into the last installed synthetic theme
// -- the case a uniform border cannot express. See testSetThemeContentBorder.
__declspec(dllexport) int MXBMRP3_Test_SetThemeContentBorder(float t, float r,
                                                             float b, float l) {
    if (!AssetManager::getInstance().testSetThemeContentBorder(t, r, b, l)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// ...and `[content] margin`, the term that moves the card inside the panel --
// what separates the card's centre from the panel's for the centring tests.
__declspec(dllexport) int MXBMRP3_Test_SetThemeContentMargin(float t, float r,
                                                             float b, float l) {
    if (!AssetManager::getInstance().testSetThemeContentMargin(t, r, b, l)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// ...and `[title] margin`, so the sweep can move the TITLE band the same way.
__declspec(dllexport) int MXBMRP3_Test_SetThemeTitleMargin(float t, float r,
                                                           float b, float l) {
    if (!AssetManager::getInstance().testSetThemeTitleMargin(t, r, b, l)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

__declspec(dllexport) int MXBMRP3_Test_SetThemePanelPadding(float xCells, float yCells) {
    if (!AssetManager::getInstance().testSetThemePanelPadding(xCells, yCells)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// The LIVE lattice, so a geometry test derives its expectations from the metric
// roots instead of freezing the shipped values into literals -- a frozen 11733
// or 0.046934 asserts the DEFAULT, not the design, and goes red the moment
// uiLineHeight moves. out[4]: cellW, cellH, a border cell's vertical extent
// (cellW * aspect -- the theme art lattice, font-derived, line-height-free),
// and lineHeightNormal.
__declspec(dllexport) void MXBMRP3_Test_LayoutCells(double* out) {
    const LayoutMetrics& m = layoutDefaults();
    out[0] = m.cellW;
    out[1] = m.cellH;
    out[2] = static_cast<double>(m.cellW) * PluginConstants::UI_ASPECT_RATIO;
    out[3] = m.lineHeightNormal;
}

// What the plugin would DRAW for an icon name, and for a 1-based shape index --
// the two resolution entry points, read back through the same calls every HUD makes.
__declspec(dllexport) int MXBMRP3_Test_IconSpriteForName(const char* name) {
    return (name && *name) ? AssetManager::getInstance().getIconSpriteIndex(name) : 0;
}
__declspec(dllexport) int MXBMRP3_Test_IconSpriteForShape(int shapeIndex) {
    return AssetManager::getInstance().iconSpriteForShape(shapeIndex);
}
// And the inverse, which the marker paths use to ask whether a glyph is directional.
__declspec(dllexport) int MXBMRP3_Test_ShapeForIconSprite(int sprite) {
    return AssetManager::getInstance().shapeIndexForSprite(sprite);
}

__declspec(dllexport) void MXBMRP3_Test_InstallTheme(const char* name, float frameBorder,
        float cardBorder, int titleBand, int contentCard, int cardSprites,
        int buttonSprites) {
    ThemeAsset t;
    t.name = name ? name : "synthetic";
    t.displayName = t.name;
    t.frameBorder = frameBorder;
    t.cardBorder = cardBorder;
    // hasCard() is "cardCenterSprite > 0", so the card set has to LOOK present or
    // no band and no card is ever emitted. The values are arbitrary: nothing asserts
    // which texture a slice drew, and the emitters never validate an index.
    t.centerSprite = 1;
    // cardSprites = 0 models the theme folder that ships frame slices only. hasCard()
    // is "cardCenterSprite > 0", so leaving it at zero is what makes a theme ASK for a
    // band and a card and get neither -- the case a skinner hits by forgetting the art,
    // and the one the [card]-without-inner warning was added for.
    t.cardCenterSprite = (cardSprites != 0) ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        t.cornerSprites[i] = 1; t.edgeSprites[i] = 1;
        t.cardCornerSprites[i] = t.cardCenterSprite;
        t.cardEdgeSprites[i]   = t.cardCenterSprite;
    }
    // The BUTTON set is a third, independent one: hasButton() is
    // "buttonCenterSprite > 0", and a theme folder that ships frame and card art but
    // no button_*.tga is a real shape (every button then falls back to a flat quad).
    // Off by default so the existing geometry cases keep counting exactly the quads
    // they were written against; a case about buttons asks for it.
    if (buttonSprites != 0) {
        t.buttonCenterSprite = 1;
        for (int i = 0; i < 4; ++i) {
            t.buttonCornerSprites[i] = 1;
            t.buttonEdgeSprites[i]   = 1;
        }
    }
    t.titleBand = titleBand != 0;
    t.contentCard = contentCard != 0;
    t.tintable = true;
    AssetManager::getInstance().installSyntheticTheme(t);
    UiConfig::getInstance().setThemeName(t.name);
    // Every HUD's background geometry changes (1 quad <-> 9), so this is a full
    // rebuild, not a reposition -- exactly what the Appearance tab does after the
    // same setThemeName. Without it the HUDs keep their previous quads and a test
    // silently measures the OLD theme, which is how the first run of
    // theme_geometry_test passed 16 assertions against flat panels.
    HudManager::getInstance().markAllHudsDirty();
    // A theme RESIZES every panel, so positions are re-validated. The Appearance tab
    // REQUESTS this (it runs inside a click handler, where validating re-enters the
    // update that dispatched it); this hook is not in a handler, so it validates
    // directly and a test sees the settled result without driving another frame.
    HudManager::getInstance().validateAllHudPositions();
}

// --- Appearance palette / font precedence (theme_palette_test) ---------------
// The three-step precedence is built-in default -> THEME -> user override, and it
// had no test at all: the Appearance rows build their click regions by hand, so
// MXBMRP3_Test_SettingsClickCycle cannot reach them, and ColorConfig does not link
// into the unit suite. A regression where cycleColor() wrote the slot array
// directly -- bypassing the override flag, so the theme's value kept winning and
// cycling appeared to do nothing -- shipped and was caught by a user.

// Effective colour for a slot: what actually gets drawn, after precedence.
__declspec(dllexport) unsigned long MXBMRP3_Test_EffectiveColor(int slot) {
    return ColorConfig::getInstance().getColor(static_cast<ColorSlot>(slot));
}
// Whether the USER has overridden this slot (as opposed to inheriting it).
__declspec(dllexport) int MXBMRP3_Test_ColorOverridden(int slot) {
    return ColorConfig::getInstance().isOverridden(static_cast<ColorSlot>(slot)) ? 1 : 0;
}
// What the slot resolves to with no user override: the theme's value, or the
// built-in default when the theme has no opinion.
__declspec(dllexport) unsigned long MXBMRP3_Test_ThemeOrDefaultColor(int slot) {
    return ColorConfig::getThemeOrDefaultColor(static_cast<ColorSlot>(slot));
}
__declspec(dllexport) void MXBMRP3_Test_CycleColor(int slot, int forward) {
    ColorConfig::getInstance().cycleColor(static_cast<ColorSlot>(slot), forward != 0);
}
__declspec(dllexport) void MXBMRP3_Test_ClearColorOverride(int slot) {
    ColorConfig::getInstance().clearOverride(static_cast<ColorSlot>(slot));
}
// Give the ACTIVE synthetic theme an opinion about one colour slot, so a test can
// tell "inherited from the theme" from "built-in default".
__declspec(dllexport) void MXBMRP3_Test_SetThemeColor(int slot, unsigned long abgr) {
    AssetManager::getInstance().testSetActiveThemeColor(static_cast<ColorSlot>(slot), abgr);
}

// Same three questions for fonts. `out` receives the effective font NAME.
__declspec(dllexport) void MXBMRP3_Test_EffectiveFont(int cat, char* out, int cap) {
    if (!out || cap <= 0) return;
    const char* v = FontConfig::getInstance().getFontName(static_cast<FontCategory>(cat));
    const std::string s = v ? v : "";
    const int n = (static_cast<int>(s.size()) < cap - 1) ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}
__declspec(dllexport) int MXBMRP3_Test_FontOverridden(int cat) {
    return FontConfig::getInstance().isOverridden(static_cast<FontCategory>(cat)) ? 1 : 0;
}
__declspec(dllexport) void MXBMRP3_Test_CycleFont(int cat, int forward) {
    FontConfig::getInstance().cycleFont(static_cast<FontCategory>(cat), forward != 0);
}
// Set a font by name, bypassing the asset list. cycleFont() cannot be exercised
// headlessly -- it early-returns when AssetManager has discovered no fonts, and the
// integration suite stages no .fnt files -- but the PRECEDENCE machinery it drives
// is exactly what regressed, and setFont() reaches it without any assets.
__declspec(dllexport) void MXBMRP3_Test_SetFont(int cat, const char* name) {
    FontConfig::getInstance().setFont(static_cast<FontCategory>(cat), name ? name : "");
}
// How many fonts discovery found. 0 headlessly, which is why the test above exists.
__declspec(dllexport) int MXBMRP3_Test_FontCount() {
    return static_cast<int>(AssetManager::getInstance().getFonts().size());
}

__declspec(dllexport) void MXBMRP3_Test_ClearFontOverride(int cat) {
    FontConfig::getInstance().clearOverride(static_cast<FontCategory>(cat));
}

// Defined below with the rest of the panel sweep; declared here so the name a test
// toggles by is produced by the SAME function it reads panels back with, rather than
// by a second lookup that would drift from it.
__declspec(dllexport) void MXBMRP3_Test_PanelName(int i, char* out, int cap);

// The Gap Bar's own width setting (percent) -- the second way this panel changes
// width, and the one a scale test cannot reach.
__declspec(dllexport) void MXBMRP3_Test_GapBarWidth(int percent) {
    HudManager::getInstance().getGapBarHud().setBarWidth(percent);
}

// Runtime setScale, by the registration name MXBMRP3_Test_PanelName reports -- the
// path a settings click takes, which is where the Gap Bar's centre drift lived: an
// INI load sets the field before layout, so only a RUNTIME change exercised the old
// setScaleKeepingCenter double-compensation. Returns 1 if a HUD matched.
__declspec(dllexport) int MXBMRP3_Test_SetHudScale(const char* name, float scale) {
    if (!name) return 0;
    const auto& huds = HudManager::getInstance().getHuds();
    for (int i = 0; i < static_cast<int>(huds.size()); i++) {
        char label[64];
        MXBMRP3_Test_PanelName(i, label, static_cast<int>(sizeof(label)));
        if (strcmp(label, name) != 0) continue;
        huds[static_cast<size_t>(i)]->setScale(scale);
        return 1;
    }
    return 0;
}

// Toggle ANY registered HUD's title, by the same registration name -- so a test
// reads a panel and toggles its caption through one name instead of needing a
// per-HUD hook for each. (This was SetGForceTitle, one widget wide, and the second
// HUD that needed it would have made it two.) Returns 1 if a HUD matched.
__declspec(dllexport) int MXBMRP3_Test_SetHudTitle(const char* name, int on) {
    if (!name) return 0;
    const auto& huds = HudManager::getInstance().getHuds();
    for (int i = 0; i < static_cast<int>(huds.size()); i++) {
        char label[64];
        MXBMRP3_Test_PanelName(i, label, static_cast<int>(sizeof(label)));
        if (strcmp(label, name) != 0) continue;
        huds[static_cast<size_t>(i)]->setShowTitle(on != 0);
        HudManager::getInstance().markAllHudsDirty();
        return 1;
    }
    return 0;
}

// Clear the global theme selection (back to unthemed flat panels).
__declspec(dllexport) void MXBMRP3_Test_ClearTheme() {
    UiConfig::getInstance().setThemeName(std::string());
    HudManager::getInstance().markAllHudsDirty();
}

// RESOLVE A PANEL BY ITS REGISTRATION NAME -- the one every element carries (see
// BaseHud::setHarnessId), which is also what MXBMRP3_Test_PanelName reports and what
// the benchmark report prints. Returns nullptr for a name nothing answers to; every
// caller below treats that as "no such panel" and returns its empty answer.
//
// THIS REPLACED A 21-CASE SWITCH ON AN INTEGER ID, and the switch was the last of the
// hand-maintained lookup tables in this file. Three things were wrong with it:
//
//   - Its `default:` arm returned THE G-FORCE WIDGET. An id nobody had added a case
//     for did not fail -- it silently handed back a real panel's geometry, and a test
//     asserting against it passed while measuring the wrong thing. (The G-force id
//     itself had no case; it WAS the default, so the fallback was load-bearing and
//     could not simply be deleted.)
//   - It had already diverged from its mirror once: this switch read 1 as the Gap Bar
//     while MXBMRP3_Test_HudPanelRect's own copy read 1 as the G-force widget, so one
//     PluginHost::HudId meant two different panels depending on the hook asked. Latent
//     only because no test happened to ask for that id both ways.
//   - Every entry needed a typed HudManager::getXxxHud() getter, so "make this panel
//     measurable" was a plugin-side edit. Twenty-three of the forty-four registered
//     elements had no id at all, and the sweep that centres content on its card box
//     could not see a HUD until someone added one -- which is exactly how VersionWidget
//     went unswept while its own anchor drifted.
//
// A name lookup has none of that: all 44 are addressable the moment they are
// registered, an unknown name is an error rather than someone else's panel, and a
// game-gated HUD (Records on GP Bikes / KRP) is simply absent instead of needing an
// #if. PluginHost keeps its HudId enum as readable sugar and maps it to these names on
// its own side, so the mapping table no longer exists in the plugin at all.
static const BaseHud* testHudByName(const char* name) {
    if (!name) return nullptr;
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (hud && std::strcmp(hud->getHarnessId(), name) == 0) {
            return static_cast<const BaseHud*>(hud.get());
        }
    }
    return nullptr;
}

// The placed CARD rect of a HUD's memoized plan: x, y, w, h in the same 1e6
// fixed point the string/quad hooks use, PRE-offset (the sweep compares deltas
// across two draws, so the constant offset cancels). h spans the first card's
// top to the last card's bottom. Returns 0 while the HUD has no computed plan.
__declspec(dllexport) int MXBMRP3_Test_HudCardRect(const char* name, int* out) {
    const BaseHud* hud = testHudByName(name);
    const BaseHud::PanelPlan* plan = hud ? hud->testPlacedPlan() : nullptr;
    if (!plan || plan->g.sections.empty()) return 0;
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (out) {
        out[0] = q(plan->sectionBoxX());
        out[1] = q(plan->sectionBoxY(0));
        out[2] = q(plan->sectionBoxW());
        out[3] = q(plan->Y(plan->g.sections.back().bot) - plan->sectionBoxY(0));
        // ...and the PANEL rect in the same (pre-offset) space, so a test can
        // measure the card's inset from the panel edge without mixing spaces --
        // drawn quads carry the HUD offset, this plan does not.
        out[4] = q(plan->x0);
        out[5] = q(plan->y0);
        out[6] = q(plan->width());
        out[7] = q(plan->height());
    }
    return 1;
}


// A HUD's rendered panel rect and quad count, quantised x1e6 so a headless test can
// compare exactly. `name` is the panel's registration name (testHudByName above).
// Drive a Draw first so the rect reflects the current config.
__declspec(dllexport) void MXBMRP3_Test_HudPanelRect(const char* name, int* w, int* h, int* quads) {
    const BaseHud* hud = testHudByName(name);
    // Guarded because it can now BE null: under the old id switch an unmapped id
    // returned the G-force widget, so this deref was safe by accident.
    if (!hud) { if (w) *w = 0; if (h) *h = 0; if (quads) *quads = 0; return; }
    float l = 0, t = 0, r = 0, b = 0;
    hud->panelRect(l, t, r, b);
    auto q = [](float v) { return static_cast<int>(v * 1e6f + 0.5f); };
    if (w) *w = q(r - l);
    if (h) *h = q(b - t);
    if (quads) *quads = static_cast<int>(hud->getQuads().size());
}

// Every section card a HUD emitted this rebuild, as (top, bottom) pairs quantised
// x1e6, newest rebuild only. `which`: 0 = Timing, 1 = Performance -- the two HUDs
// that stack cards without a heading between them and with one, respectively.
// Returns the number of CARDS written (so `cap` must hold 2*that ints).
//
// Exists because a section card is padded on both sides of its content: two sections
// whose content abuts overlap by twice that pad, the later card draws over the
// earlier, and the only visible symptom is the earlier card looking short. Nothing
// measurable from a screenshot distinguishes that from a deliberate layout, which is
// the whole reason the rects are read directly here.
__declspec(dllexport) int MXBMRP3_Test_SectionCards(const char* name, int* out, int cap) {
    // Was its own two-case mapping (0 = Timing, else Performance) -- a THIRD naming
    // scheme in this file, on top of the id switch and the panel-name ladder, and one
    // that answered "Performance" to every id it did not recognise. Same name lookup
    // as everything else now, so any panel with section cards can be asked.
    const BaseHud* hud = testHudByName(name);
    if (!hud) return 0;
    const auto& spans = hud->sectionCardSpans();
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    int written = 0;
    for (const auto& sp : spans) {
        if (out && (written * 2 + 1) < cap) {
            out[written * 2]     = q(sp.top);
            out[written * 2 + 1] = q(sp.bottom);
        }
        written++;
    }
    return written;
}

// EVERY registered HUD's panel size, in MILLI-CELLS (w and h interleaved), plus its
// icon name as a label. Returns the HUD count; writes min(count, cap/2) pairs.
//
// The grid sweep. A panel whose height is not a whole number of cells cannot tile
// with the one below it, and the error is invisible on the panel itself -- it only
// shows up as the NEXT HUD down sitting half a cell out. Individual ids on
// MXBMRP3_Test_HudPanelRect answer that one HUD at a time, which is how three
// sectioned panels shipped off the lattice at once: the case that existed measured a
// widget and a table HUD, and neither has a section boundary. This asks all of them.
__declspec(dllexport) int MXBMRP3_Test_PanelCells(int* outWH, int cap) {
    const auto& huds = HudManager::getInstance().getHuds();
    const LayoutMetrics& L = layoutDefaults();
    int i = 0;
    for (const auto& hud : huds) {
        if (outWH && (i * 2 + 1) < cap) {
            float l = 0, t = 0, r = 0, b = 0;
            hud->panelRect(l, t, r, b);
            outWH[i * 2]     = static_cast<int>(((r - l) / L.cellW) * 1000.0f + 0.5f);
            outWH[i * 2 + 1] = static_cast<int>(((b - t) / L.cellH) * 1000.0f + 0.5f);
        }
        i++;
    }
    return i;
}

// THE VERTICAL PADDING SWEEP: per HUD, (frameBorderY, paddingV) quantised x1e6.
//
// A themed panel always draws a frame, so its content owes that frame's edge slice a
// clearance at BOTH ends -- paddingV is spent top and bottom. Charge less than the
// margin and the first and last rows are drawn ON the frame art. That is one scalar
// comparison per HUD, which is why this is a sweep and not a per-panel hook: the
// shortfall depends on the THEME's frame size and the panel's inner geometry, not on
// anything a particular HUD does, so any single panel is a poor witness for the set.
//
// Reads BaseHud::contentPaddingY(), the one spelling of the quantity -- the same call
// getScaledDimensions() puts in ScaledDimensions::paddingV. It was recomposed here
// (the base padding widened by the theme's borders) on the argument that the risky term was
// called directly, which is true of that term and not of the composition: had paddingV
// gained or lost one, this sweep would have gone on measuring the old formula and
// passing. check_hud_helpers.sh rule 11 bans exactly these synonyms, and does not scan
// this file.
__declspec(dllexport) int MXBMRP3_Test_PanelPadY(int* outPairs, int cap) {
    const auto& huds = HudManager::getInstance().getHuds();
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    int i = 0;
    for (const auto& hud : huds) {
        if (outPairs && (i * 2 + 1) < cap) {
            outPairs[i * 2]     = q(hud->frameBorderY());
            outPairs[i * 2 + 1] = q(hud->contentPaddingY());
        }
        i++;
    }
    return i;
}

// The label for index `i` of the sweep above, so a failure names the HUD instead of
// an index the reader then has to count out in HudManager::initialize().
__declspec(dllexport) void MXBMRP3_Test_PanelName(int i, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    const auto& huds = HudManager::getInstance().getHuds();
    if (i < 0 || i >= static_cast<int>(huds.size())) return;
    // The registration name, which every element has by construction. This was a
    // ladder -- icon name, else texture base name, else "#<index>" -- and it made
    // the harness vocabulary a lottery: the same panel answered to `hud-timing`
    // in one test and `crash_widget` in another depending on which rung it landed
    // on, and Version, having neither, was addressable only by an index a reader
    // had to count out in HudManager::initialize().
    strncpy_s(out, static_cast<size_t>(cap),
              huds[static_cast<size_t>(i)]->getHarnessId(), _TRUNCATE);
}

// A HUD's quads as axis-aligned rects (l,t,r,b quantised x1e6, four ints each), in
// EMISSION order -- which is draw order, so a later quad covers an earlier one.
// Returns the quad count; writes min(count, cap/4).
//
// For asking whether a themed panel's quads TILE or STACK. Within one nine-slice they
// tile by construction (NineSlice::build cuts one rect into nine disjoint pieces), but
// a panel emits several nine-slices -- frame, title band, body card -- plus row bands
// on top, and whether those overlap is a question about the composition, not about any
// one of them. It matters because a translucent theme blends every layer: two quads
// over the same pixels is double the intended opacity, and the seam where the count
// changes reads as a band the artist never drew.

// A HUD's drawn STRINGS, one per call: y quantised x1e6 into *y, the text into out.
// Returns the total count, so a caller iterates 0..count-1. `name` is the panel's
// registration name (testHudByName above).
//
// The quad hooks cannot answer "what is on which line", and that is the question a
// layout fault actually asks. MXBMRP3_Test_HudQuadRects sees a card in the wrong place
// only as coordinates; what a user reports is two pieces of TEXT drawn on top of each
// other, and only the strings can say whether that happened. See title_band_test.
__declspec(dllexport) int MXBMRP3_Test_HudStringRows(const char* name, int index,
                                                     int* x, int* y, char* out, int cap) {
    const BaseHud* hud = testHudByName(name);
    if (!hud) return 0;
    const auto& strings = hud->getStrings();
    const int count = static_cast<int>(strings.size());
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (index >= 0 && index < count) {
        if (x) *x = q(strings[static_cast<size_t>(index)].m_afPos[0]);
        if (y) *y = q(strings[static_cast<size_t>(index)].m_afPos[1]);
        if (out && cap > 0) {
            strncpy(out, strings[static_cast<size_t>(index)].m_szString, static_cast<size_t>(cap) - 1);
            out[cap - 1] = '\0';
        }
    }
    return count;
}

// THE COLOUR OF ONE DRAWN STRING / ONE DRAWN QUAD, by the panel's registration name
// and its index in emission order. Returns 0 for an unknown panel or index.
//
// The rect and text hooks answer "where" and "what"; neither can answer whether a
// caption is LEGIBLE on the thing behind it, which is a relationship between two
// colours. That question had no reader at all, which is how the Gap Bar drew its gap
// text in the same palette slot as the fill under it -- red on red at high background
// opacity -- while the Notices slabs beside it had been correcting for exactly that
// since captionOnSlabColor landed.
__declspec(dllexport) unsigned long MXBMRP3_Test_HudStringColor(const char* name, int index) {
    const BaseHud* hud = testHudByName(name);
    if (!hud) return 0;
    const auto& strings = hud->getStrings();
    if (index < 0 || index >= static_cast<int>(strings.size())) return 0;
    return strings[static_cast<size_t>(index)].m_ulColor;
}

__declspec(dllexport) unsigned long MXBMRP3_Test_HudQuadColor(const char* name, int index) {
    const BaseHud* hud = testHudByName(name);
    if (!hud) return 0;
    const auto& quads = hud->getQuads();
    if (index < 0 || index >= static_cast<int>(quads.size())) return 0;
    return quads[static_cast<size_t>(index)].m_ulColor;
}

// The plugin's own legibility threshold, so a test asserts against the LIVE value
// rather than freezing 45 into an expectation that rots when it is retuned.
__declspec(dllexport) int MXBMRP3_Test_MinGlyphLumaGap() {
    return static_cast<int>(BaseHud::MIN_GLYPH_LUMA_GAP);
}

// BT.601 luma of a colour, 0..255 -- the quantity the threshold above compares. Exposed
// so the test does not carry a second spelling of the plugin's own luma formula.
__declspec(dllexport) int MXBMRP3_Test_Luma601(unsigned long color) {
    return static_cast<int>(PluginUtils::luma601(color));
}

__declspec(dllexport) int MXBMRP3_Test_HudQuadRects(const char* name, int* out, int cap) {
    const BaseHud* hud = testHudByName(name);
    if (!hud) return 0;
    const auto& quads = hud->getQuads();
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    int i = 0;
    for (const auto& qd : quads) {
        if (out && (i * 4 + 3) < cap) {
            float l = qd.m_aafPos[0][0], r = l, t = qd.m_aafPos[0][1], b = t;
            for (int k = 1; k < 4; k++) {
                l = (std::min)(l, qd.m_aafPos[k][0]);  r = (std::max)(r, qd.m_aafPos[k][0]);
                t = (std::min)(t, qd.m_aafPos[k][1]);  b = (std::max)(b, qd.m_aafPos[k][1]);
            }
            out[i * 4] = q(l); out[i * 4 + 1] = q(t); out[i * 4 + 2] = q(r); out[i * 4 + 3] = q(b);
        }
        i++;
    }
    return i;
}

// THE FILL CUT, which no screenshot and no other hook can answer. finalizeThemedFill
// cuts the frame's centre slice into the strips nothing covers, so that a translucent
// panel carries exactly one fill layer per pixel; the failure it exists to prevent is
// a card drawn on an uncut fill, which is a TONE difference of a few levels rather
// than a geometry difference. MXBMRP3_Test_HudQuadRects sees the quads but cannot say
// which of them is a fill strip and which is a card, and its whole-panel no-overlap
// check is meaningful only for a HUD whose every quad is theme geometry -- which the
// settings panel, the one panel that lays its cards out in COLUMNS and so the one this
// went wrong on, is not.
//
// The covers reported are the ones finalizeThemedFill actually cut against -- the
// band and whole-body card at the centre's own x extents, inner cards at their
// recorded rects plus the live offset -- so the test exercises the real wiring, not
// a re-derivation of it.
//
// Layout: out[0] = strip count, out[1] = cover count, out[2..5] = the centre rect,
// then one (l,t,r,b) per strip and per cover. All quantised x1e6. Returns the number
// of ints the answer needs, so a caller can size its buffer or detect truncation.
__declspec(dllexport) int MXBMRP3_Test_HudFillCut(const char* name, int* out, int cap) {
    const BaseHud* hud = testHudByName(name);
    if (!hud) return 0;
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };

    float cl = 0.0f, ct = 0.0f, cr = 0.0f, cb = 0.0f;
    const bool themed = hud->themedCentreRect(cl, ct, cr, cb);

    // A strip is DEGENERATE when the covers left it no gap -- that is the normal
    // result, not an error, so zero-area strips are dropped rather than reported.
    int strips[64][4];
    int nStrip = 0;
    if (themed) {
        for (int i = 0; i < hud->m_fillCount && nStrip < 64; i++) {
            const int qi = hud->fillStripQuad(i);
            if (qi < 0 || static_cast<size_t>(qi) >= hud->getQuads().size()) continue;
            const SPluginQuad_t& qd = hud->getQuads()[static_cast<size_t>(qi)];
            float l = qd.m_aafPos[0][0], r = l, t = qd.m_aafPos[0][1], b = t;
            for (int k = 1; k < 4; k++) {
                l = (std::min)(l, qd.m_aafPos[k][0]);  r = (std::max)(r, qd.m_aafPos[k][0]);
                t = (std::min)(t, qd.m_aafPos[k][1]);  b = (std::max)(b, qd.m_aafPos[k][1]);
            }
            if (r - l <= 0.0f || b - t <= 0.0f) continue;
            strips[nStrip][0] = q(l); strips[nStrip][1] = q(t);
            strips[nStrip][2] = q(r); strips[nStrip][3] = q(b);
            nStrip++;
        }
    }

    // Same three sources, same coordinates, as finalizeThemedFill's cover pass.
    int covers[70][4];
    int nCover = 0;
    if (themed) {
        // THE BAND'S OWN X, not the centre slice's. It was the centre's while every
        // band sat on the frame's inner boundary; the settings panel's now sits one
        // [panel] padding further in (panelSurfaceInsetX), and reading the centre
        // here reported a band that had not moved -- which is what a test asking
        // "does this panel respect [panel] padding" would have been told.
        if (hud->m_bandValid && nCover < 70) {
            covers[nCover][0] = q(hud->m_bandLeft);  covers[nCover][1] = q(hud->m_bandTop);
            covers[nCover][2] = q(hud->m_bandRight); covers[nCover][3] = q(hud->m_bandBottom);
            nCover++;
        }
        if (hud->m_wholeCardValid && nCover < 70) {
            covers[nCover][0] = q(cl); covers[nCover][1] = q(hud->m_wholeCardTop);
            covers[nCover][2] = q(cr); covers[nCover][3] = q(hud->m_wholeCardBottom);
            nCover++;
        }
        for (const BaseHud::SectionCardSpan& sp : hud->sectionCardSpans()) {
            if (nCover >= 70) break;
            covers[nCover][0] = q(sp.left + hud->getOffsetX());
            covers[nCover][1] = q(sp.top + hud->getOffsetY());
            covers[nCover][2] = q(sp.right + hud->getOffsetX());
            covers[nCover][3] = q(sp.bottom + hud->getOffsetY());
            nCover++;
        }
    }

    const int need = 6 + (nStrip + nCover) * 4;
    if (!out || cap < need) return need;
    out[0] = nStrip; out[1] = nCover;
    out[2] = q(cl); out[3] = q(ct); out[4] = q(cr); out[5] = q(cb);
    int w = 6;
    for (int i = 0; i < nStrip; i++)
        for (int k = 0; k < 4; k++) out[w++] = strips[i][k];
    for (int i = 0; i < nCover; i++)
        for (int k = 0; k < 4; k++) out[w++] = covers[i][k];
    return need;
}

__declspec(dllexport) void MXBMRP3_Test_SetPerformanceElements(unsigned int mask) {
    MXBMRP3_Test_SetPerformanceElementsImpl(mask);
}

// [Display] screenClamping, which keeps a panel inside the window. OFF by default --
// deliberately, since a player may want a HUD half off the edge -- so a test that
// wants to see clamping happen has to ask for it, and one that wants to see the
// opt-out respected has to be able to turn it back off.
__declspec(dllexport) void MXBMRP3_Test_SetScreenClamping(int on) {
    UiConfig::getInstance().setScreenClamping(on != 0);
}

// A HUD's panel edges ON SCREEN -- its bounds PLUS the live position offset --
// quantised x1e6. Same `which` mapping as MXBMRP3_Test_HudPanelRect.
//
// That hook is not enough to ask "is this panel still on the display", and the
// difference is the whole reason this one exists: panelRect() is the panel's own box,
// measured from its layout origin, and a HUD is placed by an offset applied on top of
// it. A theme that widens every panel moves the RIGHT edge only; the width the other
// hook reports grows either way, so it cannot tell a panel that grew inward from one
// that grew off the screen.
__declspec(dllexport) void MXBMRP3_Test_HudScreenEdges(const char* name, int* l, int* t,
                                                      int* r, int* b) {
    const BaseHud* hud = testHudByName(name);
    if (!hud) { if (l) *l = 0; if (t) *t = 0; if (r) *r = 0; if (b) *b = 0; return; }
    float bl = 0, bt = 0, br = 0, bb = 0;
    hud->panelRect(bl, bt, br, bb);
    const float ox = hud->getOffsetX(), oy = hud->getOffsetY();
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (l) *l = q(bl + ox);
    if (t) *t = q(bt + oy);
    if (r) *r = q(br + ox);
    if (b) *b = q(bb + oy);
}

// The settings panel's horizontal margins, in fixed point (x * 1e6).
//
// bgL/bgR are its panel background's two edges; colL/colR the outer edges of its two
// COLUMNS (see SettingsHud::testColumnEdgesX). A settings panel is two columns inside
// one panel, and the only way to see that one of them has broken out of the panel's
// margin is to compare those four numbers -- which is what a person does by eye on a
// screenshot, and what nothing else in the suite could do at all.
__declspec(dllexport) void MXBMRP3_Test_SettingsMarginsX(int* bgL, int* bgR,
                                                        int* colL, int* colR) {
    SettingsHud& hud = HudManager::getInstance().getSettingsHud();
    float l = 0, t = 0, r = 0, b = 0;
    static_cast<const BaseHud&>(hud).panelRect(l, t, r, b);
    float cl = 0.0f, cr = 0.0f;
    hud.testColumnEdgesX(cl, cr);
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (bgL) *bgL = q(l);
    if (bgR) *bgR = q(r);
    if (colL) *colL = q(cl);
    if (colR) *colR = q(cr);
}

// The settings panel's CONTENT anchors, in fixed point (x * 1e6): the label column,
// the control column, and the right edge of a full-width row.
//
// All three must be identical with a theme and without one -- the layout centres the
// content box and hangs the panel off it, so only the panel's outer edges may move.
// MarginsX above cannot see a violation: its two numbers are measured against the
// panel, so the whole content column can walk sideways with the panel and stay
// perfectly symmetric while every control the user is aiming at has moved.
__declspec(dllexport) void MXBMRP3_Test_SettingsContentX(int* labelX, int* controlX,
                                                        int* rowRight) {
    SettingsHud& hud = HudManager::getInstance().getSettingsHud();
    float l = 0.0f, c = 0.0f, r = 0.0f;
    hud.testContentColumnX(l, c, r);
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (labelX)     *labelX     = q(l);
    if (controlX)   *controlX   = q(c);
    if (rowRight)   *rowRight   = q(r);
}

// `[panel] gap` on the synthetic theme — theme_geometry_test turns it to prove
// the settings gutter tracks the same term the vertical seams spend.
// Plant a live gap in the Gap Bar (see GapBarHud::testForceGap) -- the fill's
// extent is the one geometry the layout sweeps cannot reach without it.
__declspec(dllexport) void MXBMRP3_Test_GapBarForceGap(int ms, int valid) {
    HudManager::getInstance().getGapBarHud().testForceGap(ms, valid != 0);
}

__declspec(dllexport) int MXBMRP3_Test_SetThemeGap(float cells) {
    if (!AssetManager::getInstance().testSetThemeGap(cells)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// The settings GUTTER's bounding card edges, fixed point (x * 1e6), plus the
// vertical seam read (contentGapY) in the same units. The gutter — content
// card left minus sidebar card right — must equal the seam for the same
// terms: it was measured short twice (gap-only composition; the unpaid row
// lead-in), and both bugs would have been one failing CHECK here.
__declspec(dllexport) void MXBMRP3_Test_SettingsGutter(int* sidebarR, int* contentL,
                                                       int* seamV) {
    SettingsHud& hud = HudManager::getInstance().getSettingsHud();
    float sr = 0.0f, cl = 0.0f;
    hud.testCardEdgesX(sr, cl);
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (sidebarR) *sidebarR = q(sr);
    if (contentL) *contentL = q(cl);
    if (seamV)    *seamV    = q(hud.contentGapY());
}

// RADAR's per-HUD theme override, alongside the Standings pair above.
//
// Not redundant with them, and the difference is the whole point: StandingsHud's
// FACTORY DEFAULT is an empty override, RadarHud's is THEME_NONE (it opts out of
// theming -- the texture IS the panel). A HUD with a non-empty default is the only
// one that can exercise "set it back to Default and have that stick", because it is
// the only one whose base section carries a value for an absent profile key to fail
// to override. Testing this on Standings alone is what let that bug through.
__declspec(dllexport) void MXBMRP3_Test_RadarTheme(char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string& v = HudManager::getInstance().getRadarHud().getThemeOverride();
    const int n = (static_cast<int>(v.size()) < cap - 1) ? static_cast<int>(v.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = v[i];
    out[n] = '\0';
}

__declspec(dllexport) void MXBMRP3_Test_RadarSetTheme(const char* theme) {
    HudManager::getInstance().getRadarHud().setThemeOverride(
        theme ? std::string(theme) : std::string());
}

__declspec(dllexport) void MXBMRP3_Test_StandingsSetTheme(const char* theme) {
    HudManager::getInstance().getStandingsHud().setThemeOverride(
        theme ? std::string(theme) : std::string());
}

// Reset one named HUD (active profile) to defaults — models a per-tab "Reset".
// keepVisibility mirrors the tab behaviour (single-HUD tabs keep visibility).
__declspec(dllexport) void MXBMRP3_Test_ResetHud(const char* hudName, int keepVisibility) {
    std::vector<std::string> names{ hudName ? std::string(hudName) : std::string() };
    SettingsManager::getInstance().resetHudsToFactoryDefaults(
        HudManager::getInstance(), names, keepVisibility != 0);
}

// Copy the active profile's settings to all other profiles ("Copy to all").
__declspec(dllexport) void MXBMRP3_Test_CopyProfileToAll() {
    SettingsManager::getInstance().applyToAllProfiles(HudManager::getInstance());
}

// Switch the active profile (0=Practice,1=Qualify,2=Race,3=Spectate). Uses the
// same public path as the in-game switch (captures current, applies new, saves).
__declspec(dllexport) void MXBMRP3_Test_SwitchProfile(int idx) {
    if (idx < 0 || idx >= static_cast<int>(ProfileType::COUNT)) return;
    SettingsManager::getInstance().switchProfile(HudManager::getInstance(), static_cast<ProfileType>(idx));
}

// Enable/disable auto-by-session profile switching (the ProfileManager flag the
// Settings UI toggles). Lets a test arm the auto-switch path before driving
// RaceSession/spectate callbacks and asserting the active profile follows.
__declspec(dllexport) void MXBMRP3_Test_SetAutoSwitch(int enabled) {
    ProfileManager::getInstance().setAutoSwitchEnabled(enabled != 0);
}

// Read the active profile index (0=Practice,1=Qualify,2=Race,3=Spectate). The
// active profile isn't in /api/state, so the auto-switch test reads it directly.
__declspec(dllexport) int MXBMRP3_Test_GetActiveProfile() {
    return static_cast<int>(ProfileManager::getInstance().getActiveProfile());
}

// Force a settings save (the reset/profile calls that don't persist rely on this).
__declspec(dllexport) void MXBMRP3_Test_Save() {
    SettingsManager::getInstance().saveSettings(
        HudManager::getInstance(), PluginManager::getInstance().getSavePath());
}

// Mark settings dirty WITHOUT writing — the deferred auto-save path (a HUD drag/toggle).
// The write happens later, on a leave-track flush. Lets a test assert the deferral: after
// this, nothing is on disk until a flush.
__declspec(dllexport) void MXBMRP3_Test_MarkDirty() {
    SettingsManager::getInstance().markDirty();
}

// Flush pending settings to disk if dirty (synchronous atomic write) — models the
// leave-track transition (RunStop/RunDeinit) firing. No-op when clean OR auto-save is off.
__declspec(dllexport) void MXBMRP3_Test_FlushIfDirty() {
    SettingsManager::getInstance().flushIfDirty(HudManager::getInstance());
}

// Unsaved-changes state — drives the settings Save button (lit when dirty, "Saved" when not).
__declspec(dllexport) int MXBMRP3_Test_IsDirty() {
    return SettingsManager::getInstance().isDirty() ? 1 : 0;
}

// Toggle Auto-Save (default on). With it off, leaving the track does NOT auto-flush; the user
// persists via the Save button (MXBMRP3_Test_Save).
__declspec(dllexport) void MXBMRP3_Test_SetAutoSave(int enabled) {
    UiConfig::getInstance().setAutoSave(enabled != 0);
}

// (Re)load settings from an arbitrary save path into live state, via the same
// applier Startup uses. Lets a single-process test perturb an INI on disk and
// pull it into live state (the "set live state" seam), so it can then reset and
// re-save without a second process.
__declspec(dllexport) void MXBMRP3_Test_LoadSettings(const char* savePath) {
    if (!savePath) return;
    SettingsManager::getInstance().loadSettings(HudManager::getInstance(), savePath);
}

// Active settings tab (by display name) - drives the persisted-tab restore test. Set
// selects a tab (no-op if the name is unknown / unavailable on this build); Get copies the
// current tab's name out. Read/written through the same accessors save/load use.
__declspec(dllexport) void MXBMRP3_Test_SetActiveTab(const char* name) {
    HudManager::getInstance().getSettingsHud().setActiveTabByName(name ? name : "");
}

// How far the LAST-BUILT settings tab overran the space reserved for it, in
// thousandths of a row; negative is slack. Drive a Draw first.
//
// The panel measures its TALLEST tab and draws every tab at that height, so this
// should never be positive. A gate reads it anyway (settings_fit_test): the one way
// a measured height can be wrong is a renderer that lays out differently between the
// measure pass and the real one, and nothing else would notice.
__declspec(dllexport) int MXBMRP3_Test_SettingsOverflowRows() {
    const float rows = HudManager::getInstance().getSettingsHud().testOverflowRows();
    return static_cast<int>(rows * 1000.0f + (rows < 0.0f ? -0.5f : 0.5f));
}

// StandingsHud's player-row highlight band, as a span (x, width) in fixed point
// (x * 1e6). Returns 0 when the HUD emitted no band this rebuild.
//
// A full-row band belongs to the CONTENT COLUMN, and saying so needs two numbers no
// other hook reports together: the band's own span, and the card it must sit inside
// (MXBMRP3_Test_HudFillCut's section covers). See standings_row_band_test.
__declspec(dllexport) int MXBMRP3_Test_StandingsRowBand(int* x, int* w) {
    float bx = 0.0f, bw = 0.0f;
    HudManager::getInstance().getStandingsHud().testRowBandX(bx, bw);
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    if (x) *x = q(bx);
    if (w) *w = q(bw);
    return (bw > 0.0f) ? 1 : 0;
}

// The i-th SELECTABLE tab's display name, in tab-list order. Returns 0 past the end,
// which is how a caller enumerates. The panel's height is the tab's own now, so
// "does the settings menu fit the screen" has to be asked of every tab, and a list
// of names in the test would be a list a new tab is not added to.
// Every selectable tab INCLUDING the ones hidden from the sidebar list. See
// SettingsHud::testAnyTabNameAt for why this is not the same enumeration.
__declspec(dllexport) int MXBMRP3_Test_SettingsAnyTabName(int i, char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    const char* name = HudManager::getInstance().getSettingsHud().testAnyTabNameAt(i);
    if (!name) return 0;
    strncpy_s(out, static_cast<size_t>(cap), name, _TRUNCATE);
    return 1;
}

__declspec(dllexport) int MXBMRP3_Test_SettingsTabName(int i, char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    const char* name = HudManager::getInstance().getSettingsHud().testTabNameAt(i);
    if (!name) return 0;
    strncpy_s(out, static_cast<size_t>(cap), name, _TRUNCATE);
    return 1;
}

// Count / click the shared stepped-control regions on the ACTIVE settings tab,
// through the real click path (hit-test -> dispatchRegion -> applySteppedControl).
// The settings-click surface is otherwise reachable only via real OS mouse input;
// this seam makes the converted steppers' step/clamp/wrap/acceleration behavior
// assertable headless. holdRepeats forces the hold-repeat counter (accel tiers:
// <6 -> x1, <16 -> x5, else x10) for the duration of the one click.
__declspec(dllexport) int MXBMRP3_Test_SettingsSteppedCount(int up) {
    return HudManager::getInstance().getSettingsHud().testSteppedRegionCount(up != 0);
}
__declspec(dllexport) int MXBMRP3_Test_SettingsClickStepped(int index, int up, int holdRepeats) {
    return HudManager::getInstance().getSettingsHud().testClickStepped(index, up != 0, holdRepeats) ? 1 : 0;
}

// Cycle-control twin of the stepped seam: count / click the shared
// CYCLE_UP/CYCLE_DOWN regions on the ACTIVE settings tab through the real click
// path (hit-test -> dispatchRegion -> applyCycleControl). No hold tier - cycles
// never accelerate.
// Click the Director tab's "Visible" row through the real click path, and read the
// DirectorWidget's per-surface visibility. Together these pin that the row edits
// the FOCUSED surface: it used to call setVisible() unconditionally, so on the
// companion window it flipped the game flag while the widget stayed on screen and
// the label reported the other surface. Same defect the helmet toggle had.
__declspec(dllexport) int MXBMRP3_Test_ClickDirectorHudVisible() {
    return HudManager::getInstance().getSettingsHud().testClickDirectorHudVisible() ? 1 : 0;
}

__declspec(dllexport) void MXBMRP3_Test_DirectorWidgetVisibility(int* game, int* companion) {
    DirectorWidget* w = HudManager::getInstance().getDirectorWidget();
    if (game)      *game      = (w && w->isVisible()) ? 1 : 0;
    if (companion) *companion = (w && w->getCompanionVisible()) ? 1 : 0;
}

__declspec(dllexport) int MXBMRP3_Test_SettingsCycleCount(int up) {
    return HudManager::getInstance().getSettingsHud().testCycleRegionCount(up != 0);
}
__declspec(dllexport) int MXBMRP3_Test_SettingsClickCycle(int index, int up) {
    return HudManager::getInstance().getSettingsHud().testClickCycle(index, up != 0) ? 1 : 0;
}

// Text signature of the ACTIVE settings tab's emitted click regions (type +
// tooltip id, in order) and string count. A characterization seam: the settings
// panel's rendered output is otherwise invisible below the Wine layer, so this
// is what lets a test assert that a layout refactor changed nothing.
__declspec(dllexport) void MXBMRP3_Test_SettingsRegionSignature(char* out, int cap) {
    HudManager::getInstance().getSettingsHud().testRegionSignature(out, cap);
}

// The ACTIVE rumble config's Bumps light-motor strength. Reads through the same
// getRumbleConfig() resolution the Rumble tab binds to (global, or the current
// bike's profile in per-bike mode), so the stepped-control profile-binding
// guard is assertable headless: a click swallowed after a bike swap leaves BOTH
// the old and the new profile's value unchanged.
__declspec(dllexport) float MXBMRP3_Test_RumbleActiveBumpsLight() {
    return XInputReader::getInstance().getRumbleConfig().suspensionEffect.lightStrength;
}

// Open/close the settings menu (SettingsHud) — mirrors the TOGGLE_SETTINGS hotkey.
// Lets a dump/preview test capture the settings UI without simulating input.
__declspec(dllexport) void MXBMRP3_Test_ShowSettings(int visible) {
    SettingsHud& s = HudManager::getInstance().getSettingsHud();
    if (visible) s.show(); else s.hide();
}

// Open/close the standalone companion window (renders the HUD off-game). Lets a
// driver open the real in-process window and screenshot it under Wine.
__declspec(dllexport) void MXBMRP3_Test_CompanionWindow(int on) {
    CompanionWindow::getInstance().setEnabled(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_GetActiveTab(char* out, int cap) {
    if (!out || cap <= 0) return;
    const char* name = HudManager::getInstance().getSettingsHud().getActiveTabName();
    strncpy(out, name ? name : "", cap - 1);
    out[cap - 1] = '\0';
}

// --- Analytics dry-run capture seam. Analytics's only real effect is a network POST the
// harness can't observe, so these drive the payload build + the sampling gate directly:
// Prime fakes identity/session/host + turns on capture mode (real senders become no-ops,
// so a test build never phones home); AppStarted returns the always-sent app_started body;
// QueueSessionEnd/QueueCustom run the gated paths; SeedCrash runs the (never-gated) crash
// path; DrainPending returns whatever those enqueued (the events that WOULD be sent). ---
__declspec(dllexport) void MXBMRP3_Test_AnalyticsPrime() {
    AnalyticsManager::getInstance().testPrime();
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsSetFullLaunch(int full) {
    AnalyticsManager::getInstance().testSetFullLaunch(full != 0);
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsAppStarted(char* out, int cap) {
    if (!out || cap <= 0) return;
    std::string body = AnalyticsManager::getInstance().testBuildAppStarted();
    strncpy(out, body.c_str(), cap - 1);
    out[cap - 1] = '\0';
}
// Start the real custom-event worker thread (capture mode keeps its sends
// no-ops). Exists so a teardown test can shut down with a LIVE analytics thread
// parked on its condvar — the join that ~AnalyticsManager has to get right.
__declspec(dllexport) int MXBMRP3_Test_AnalyticsStartEventWorker() {
    return AnalyticsManager::getInstance().testStartEventWorker() ? 1 : 0;
}
__declspec(dllexport) int MXBMRP3_Test_AnalyticsEventWorkerRunning() {
    return AnalyticsManager::getInstance().testEventWorkerRunning() ? 1 : 0;
}
// Run the real AnalyticsManager::shutdown() (drain + join). In a shipping build
// PluginManager's orchestrated Shutdown() does this; with GAME_HAS_ANALYTICS off
// it never runs, so a test that wants the join exercised has to ask for it.
__declspec(dllexport) void MXBMRP3_Test_AnalyticsShutdown() {
    AnalyticsManager::getInstance().shutdown();
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsQueueSessionEnd() {
    AnalyticsManager::getInstance().testQueueSessionEnd();
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsQueueCustom(const char* name) {
    AnalyticsManager::getInstance().testQueueCustom(name ? name : "");
}
__declspec(dllexport) void MXBMRP3_Test_AnalyticsSeedCrash(const char* markerPath,
        const char* fault, const char* code) {
    AnalyticsManager::getInstance().testSeedAndReportCrash(
        markerPath ? markerPath : "", fault ? fault : "", code ? code : "");
}
// Join the pending event bodies with '\n' into out; returns the number drained.
__declspec(dllexport) int MXBMRP3_Test_AnalyticsDrainPending(char* out, int cap) {
    std::vector<std::string> events = AnalyticsManager::getInstance().testDrainPending();
    std::string joined;
    for (const auto& e : events) { joined += e; joined += '\n'; }
    if (out && cap > 0) {
        strncpy(out, joined.c_str(), cap - 1);
        out[cap - 1] = '\0';
    }
    return static_cast<int>(events.size());
}

// Update-checker version comparison: <0 if a<b, 0 if equal/unparseable, >0 if a>b.
__declspec(dllexport) int MXBMRP3_Test_CompareVersions(const char* a, const char* b) {
    return UpdateChecker::compareVersions(a ? a : "", b ? b : "");
}

// Publish an UPDATE_AVAILABLE result with no network; "" clears back to the plain
// version state. The only way to reach the update-available UI headlessly -- see
// UpdateChecker::testSetUpdateAvailable for why setDebugMode() is not it. Also
// enables updates, since both the settings footer and the Version widget gate the
// state on isEnabled().
// The two terms the Version widget lays its notification BUTTON ROW out from --
// one text row and the [panel] junction gap -- in fixed point (x * 1e6).
//
// Positions are deliberately NOT here: the caller reads the drawn message and the
// drawn button label out of MXBMRP3_Test_HudStringRows and checks the distance
// between them is at least a row plus a junction. Two strings from the same list
// are the only pair that compare cleanly -- the widget stores its button bounds
// BEFORE the HUD offset while the strings carry it, so mixing the two measures the
// widget's screen position instead of its layout (which is what the first version
// of this hook did, and it read short by exactly the offset).
//
// The terms ride out so the test can state the RULE rather than a pixel count that
// moves with the font, the UI scale and the theme; and they are measured at all
// because a panel's HEIGHT cannot tell the junction from the button box -- a test
// comparing the plain and button states passes on the box's contribution whether
// the junction is spent or not, which is exactly what the first version of that
// test did.
__declspec(dllexport) void MXBMRP3_Test_VersionRowTerms(int* rowH, int* junction) {
    const VersionWidget& v = HudManager::getInstance().getVersionWidget();
    auto q = [](float x) { return static_cast<int>(x * 1e6f + (x < 0 ? -0.5f : 0.5f)); };
    if (rowH) *rowH = q(v.testRowHeight());
    if (junction) *junction = q(v.testJunctionY());
}

__declspec(dllexport) void MXBMRP3_Test_UpdateSetAvailable(const char* latest) {
    UpdateChecker& checker = UpdateChecker::getInstance();
    checker.setEnabled(true);
    checker.testSetUpdateAvailable(latest ? latest : "9.9.9");
    // AND DRIVE THE WIDGET. testSetUpdateAvailable only publishes the checker's
    // state; in the game the notification appears because the check's COMPLETION
    // CALLBACK calls this (settings_manager.cpp), and no callback runs here. Without
    // it the widget stayed in its plain one-row state and a test measuring the
    // button row measured nothing -- which is what this hook's own comment promises
    // it reaches.
    if (latest && *latest) {
        HudManager::getInstance().getVersionWidget().showUpdateNotification();
    }
    HudManager::getInstance().markAllHudsDirty();
}

// --- Auto-director controls (the rider lock invariants). The director only
// evaluates while spectating/replaying and its cadence is wall-clock; these hooks
// expose the enable + rider-lock toggles and the lock state so a test can assert
// the lock's release rules without depending on shot timing. ---
__declspec(dllexport) void MXBMRP3_Test_DirectorSetEnabled(int enabled) {
    DirectorManager::getInstance().setEnabled(enabled != 0);
}
__declspec(dllexport) void MXBMRP3_Test_DirectorToggleLock() {
    DirectorManager::getInstance().toggleLock();
}
__declspec(dllexport) int MXBMRP3_Test_DirectorIsLocked() {
    return DirectorManager::getInstance().isLocked() ? 1 : 0;
}
// The next camera role the rider lock rotates to from `cur` (SpectateHandler::CameraRole
// as int). Deterministic given the enabled-camera config; lets a test assert the cycle
// order + wrap without depending on the wall-clock shot cadence.
__declspec(dllexport) int MXBMRP3_Test_DirectorNextLockedCamera(int cur) {
    return DirectorManager::getInstance().nextLockedCamera(cur);
}

// Set the director's story-follow toggles from a bitmask so a broadcast harness can
// replay one tape under different story configs (all on, all off, battles-only, ...)
// and compare the resulting airtime/camera mix. Bits (LSB first):
//   1=battles 2=incidents 4=fastestLap 8=pace 16=lappers 32=drops.
__declspec(dllexport) void MXBMRP3_Test_DirectorSetStories(int mask) {
    DirectorManager& d = DirectorManager::getInstance();
    d.setFollowBattles((mask & 1) != 0);
    d.setFollowIncidents((mask & 2) != 0);
    d.setFollowFastestLap((mask & 4) != 0);
    d.setFollowPace((mask & 8) != 0);
    d.setFollowLappers((mask & 16) != 0);
    d.setFollowDrops((mask & 32) != 0);
}

// Set the director's shot pacing directly (seconds). maxSec = 0 is "Max shot = Off",
// the forced-rotation switch: the director then cuts only for stories and falls back to
// the broadcaster's own rider. A test drives this instead of clicking the settings tab.
__declspec(dllexport) void MXBMRP3_Test_DirectorSetShotSec(int minSec, int maxSec) {
    DirectorManager& d = DirectorManager::getInstance();
    d.setMaxShotSec(maxSec);   // max first: setMinShotSec would otherwise raise a lower max
    d.setMinShotSec(minSec);
}
// The rider the director treats as "home" - whoever the broadcaster had on camera when it
// was enabled, plus every later manual pick. -1 until one is adopted. Lets a test assert
// the home rider is captured (and re-captured) without inferring it from a cut.
__declspec(dllexport) int MXBMRP3_Test_DirectorHomeSubject() {
    return DirectorManager::getInstance().getHomeSubject();
}
// The rider the director currently intends to follow, regardless of whether it is
// "actively directing" (the /api/state advisory blanks the subject while paused/held,
// which a pacing test needs to see through).
__declspec(dllexport) int MXBMRP3_Test_DirectorSubject() {
    return DirectorManager::getInstance().getCurrentSubject();
}

// --- Broadcast-measurement hook. The director's shot pacing (min/max shot, holds,
// variety cadence) is wall-clock driven, so a naive tape replay - which fires every
// event back-to-back in milliseconds - collapses a whole race into one shot. This lets
// a test inject a simulated clock from each recorded event's timestamp so the pacing
// plays out at the real cadence; every cut the director then makes is logged by cutTo(),
// so the broadcast (cut count, per-rider screen time, camera-angle distribution) is
// reconstructed by parsing the plugin log. ---
__declspec(dllexport) void MXBMRP3_Test_DirectorSetNowMs(long long ms) {
    DirectorManager::testSetNowMs(ms);
}

// Enable/disable the "Director" event-log type. This is a DISPLAY filter only — director
// cuts/state changes are pushed to the event log unconditionally (raw-data contract), so a
// test toggles this to check that emission is independent of the display filter.
__declspec(dllexport) void MXBMRP3_Test_EventLogEnableDirector(int on) {
    HudManager::getInstance().getEventLogHud().setEventTypeEnabled(EventLogType::Director, on != 0);
}

// Make the Event Log HUD visible (default off) for a demo/screenshot.
__declspec(dllexport) void MXBMRP3_Test_EventLogSetVisible(int visible) {
    HudManager::getInstance().getEventLogHud().setVisible(visible != 0);
}

// Return the icon-color-slot override of the most-recent event-log entry whose message
// contains `messageSubstr`: a ColorSlot value (>=0) for an overridden entry (e.g. director
// state-transition tints), -1 for an entry using its per-type default color, or -2 if no
// matching entry exists. Lets a headless test assert the director state->color plumbing
// without the render color leaking into the /api/state web contract.
__declspec(dllexport) int MXBMRP3_Test_EventLogIconColorSlot(const char* messageSubstr) {
    if (!messageSubstr) return -2;
    const auto& log = PluginData::getInstance().getEventLog();
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        if (std::string(it->message).find(messageSubstr) != std::string::npos)
            return it->iconColorSlot;
    }
    return -2;
}

// Put the Event Log into AUTO_HIDE with a given duration, so a test can drive the
// hide-on-timeout path. That path is the one that used to leave click regions behind on an
// invisible HUD (the ON/hidden path returns above the input block and was never affected),
// so it cannot be exercised any other way.
__declspec(dllexport) void MXBMRP3_Test_EventLogSetAutoHide(int enabled, int durationMs) {
    EventLogHud& hud = HudManager::getInstance().getEventLogHud();
    hud.testSetAutoHide(enabled != 0, durationMs);
}

// Whether requestSpectateRider(raceNum) would land — the shared gate behind click-to-
// spectate on Standings / Map / Event Log / Session Charts.
__declspec(dllexport) int MXBMRP3_Test_IsRiderSpectatable(int raceNum) {
    return PluginData::getInstance().isRiderSpectatable(raceNum) ? 1 : 0;
}

// How many Event Log rows currently offer click-to-spectate. See
// EventLogHud::testSpectateRegionCount.
__declspec(dllexport) int MXBMRP3_Test_EventLogSpectateRegionCount() {
    return HudManager::getInstance().getEventLogHud().testSpectateRegionCount();
}

// Session Charts: the sample resolution collectField() settled on (1 = per lap,
// GAME_SECTOR_COUNT = per sector) and the display rider's series length, into out[0]/out[1].
// Also flips ELEM_SECTOR_POINTS on/off first when `sectorPoints` is 0 or 1 (-1 = leave as
// configured), so a test can compare both resolutions over one race.
__declspec(dllexport) void MXBMRP3_Test_ChartSeries(int sectorPoints, int* out) {
    if (!out) return;
    SessionChartsHud& hud = HudManager::getInstance().getSessionChartsHud();
    if (sectorPoints >= 0) {
        uint32_t elems = hud.getEnabledElements();
        if (sectorPoints) elems |=  SessionChartsHud::ELEM_SECTOR_POINTS;
        else              elems &= ~SessionChartsHud::ELEM_SECTOR_POINTS;
        hud.setEnabledElements(elems);
    }
    SessionChartsHud::TestSeries s = hud.testSeries();
    out[0] = s.pointsPerLap;
    out[1] = s.points;
}

// Hide the Notices HUD (the "ALL-TIME PB" etc. flashes) so a demo/screenshot of another
// centered HUD isn't overlapped by a transient notice.
__declspec(dllexport) void MXBMRP3_Test_NoticesSetVisible(int visible) {
    HudManager::getInstance().getNoticesHud().setVisible(visible != 0);
}

// Configure the Timing HUD for a demo/screenshot: show the big time row and a set of
// comparison rows. The (gapEnabled, primaryGap, secondaryMask) args are the pre-redesign
// shape; here they collapse to the union of the enabled gap types as comparison rows.
// Enable a set of Timing comparison rows for tests. The redesigned Timing HUD has no
// primary/secondary distinction (just a flat set of enabled comparisons), but this hook keeps
// the old 3-arg shape so existing call sites don't churn: the three args are OR-folded into one
// enable mask (gapEnabled gates whether primaryGap is included). Effectively an "enable these
// comparison flags" call; the time row is always turned on.
__declspec(dllexport) void MXBMRP3_Test_TimingConfig(int gapEnabled, int primaryGap, int secondaryMask) {
    TimingHud& t = HudManager::getInstance().getTimingHud();
    int mask = secondaryMask | (gapEnabled ? primaryGap : 0);
    const GapTypeFlags all[] = { GAP_TO_PB, GAP_TO_ALLTIME, GAP_TO_IDEAL, GAP_TO_OVERALL, GAP_TO_LASTLAP, GAP_TO_RECORD };
    for (GapTypeFlags f : all) t.setComparisonEnabled(f, (mask & f) != 0);
    t.setTimeEnabled(true);
}

// THE WHAT'S-NEW MARKERS (hud/settings/whats_new.h). Live count, whether a tab is
// tagged, and the two dismissal paths, so a test can drive the rules rather than
// take a screenshot and squint at a coloured band.
//
// Reset clears the DISMISSED SET, not the table: "a player who has seen nothing",
// which is the state the markers are designed for and the one a fresh install has.
__declspec(dllexport) void MXBMRP3_Test_WhatsNewReset() {
    WhatsNew::deserialize("");
}

__declspec(dllexport) int MXBMRP3_Test_WhatsNewLiveCount() {
    int n = 0;
    for (int i = 0; i < WhatsNew::MARKER_COUNT; ++i) {
        if (WhatsNew::isLive(WhatsNew::MARKERS[i])) ++n;
    }
    return n;
}

// By TAB NAME, the same string the sidebar shows and MXBMRP3_Test_SetActiveTab
// takes -- a test naming "Widgets" should not have to know it is tab 14.
__declspec(dllexport) int MXBMRP3_Test_WhatsNewTabTagged(const char* tabName) {
    if (!tabName) return 0;
    const SettingsHud& sh = HudManager::getInstance().getSettingsHud();
    const int t = sh.testTabIndexForName(tabName);
    return (t >= 0 && WhatsNew::tabHasLive(t)) ? 1 : 0;
}

// DOES MARKER `i` POINT AT A ROW THAT ACTUALLY EXISTS? Opens the marker's tab,
// renders it, and looks for a click region carrying the marker's tooltip id.
//
// The failure this catches is SILENT: a marker naming a row id that no row
// registers draws nothing at all -- no band, and no complaint. The tab still gets
// its "New" tag, so the menu looks like the feature was marked while the row it
// was pointing at is unmarked. Nothing else in the build can tell the difference.
__declspec(dllexport) int MXBMRP3_Test_WhatsNewMarkerResolves(int index) {
    if (index < 0 || index >= WhatsNew::MARKER_COUNT) return -1;
    const WhatsNew::Marker& m = WhatsNew::MARKERS[index];
    SettingsHud& sh = HudManager::getInstance().getSettingsHud();
    // show() first: rebuildRenderData is vis-gated, so a hidden panel registers no
    // click regions at all and EVERY marker would read as unresolved -- a uniform
    // failure that says nothing about the table. Left open afterwards on purpose:
    // hide() dismisses the active tab's tag, which is not this hook's business.
    sh.show();
    sh.testClickTab(m.tabId);
    sh.update();
    return sh.testHasRegionWithTooltip(m.rowTooltipId) ? 1 : 0;
}

// The marker's tab, by name, so a failure names the tab rather than an index.
__declspec(dllexport) void MXBMRP3_Test_WhatsNewMarkerName(int index, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (index < 0 || index >= WhatsNew::MARKER_COUNT) return;
    const WhatsNew::Marker& m = WhatsNew::MARKERS[index];
    const SettingsHud& sh = HudManager::getInstance().getSettingsHud();
    snprintf(out, static_cast<size_t>(cap), "%s / %s",
             sh.testTabNameForIndex(m.tabId), m.rowTooltipId);
}

// The footer's About button: one click. Five in a row still start the easter egg,
// which is why this is a click rather than a "go to About" setter.
__declspec(dllexport) void MXBMRP3_Test_ClickAbout() {
    HudManager::getInstance().getSettingsHud().testClickAbout();
}

// Wipe the tag's seen-version in memory, the way whatsNewReset does for markers --
// so a persistence case can clear RAM and prove the value came back from the file.
// Re-announcing the same version cannot do that job: the tag is keyed on the version
// string, so a version already seen stays seen, which is the whole point of it.
__declspec(dllexport) void MXBMRP3_Test_UpdateTagReset() {
    UpdateChecker::getInstance().setUpdateTagSeenVersion("");
}

// The About button's rect in the footer, fixed point (x * 1e6), read from its own
// CLICK REGION rather than re-derived: a test that recomputed the geometry would be
// asserting against its own copy of the layout, not the panel's.
__declspec(dllexport) int MXBMRP3_Test_AboutButtonRect(int* l, int* t, int* r, int* b) {
    return HudManager::getInstance().getSettingsHud()
        .testAboutButtonRect(l, t, r, b) ? 1 : 0;
}

// Is the Updates tab wearing its "Update" tag right now?
__declspec(dllexport) int MXBMRP3_Test_UpdateTagLive() {
    return UpdateChecker::getInstance().shouldShowUpdateTag() ? 1 : 0;
}

__declspec(dllexport) int MXBMRP3_Test_WhatsNewMarkerCount() {
    return WhatsNew::MARKER_COUNT;
}

// The dismissed SET as it would be written to the INI. Live counts cannot see a
// dismissal of something no marker names -- a stray tab key, say -- and that is
// precisely the shape of the constructor's premature TAB_GENERAL dismissal.
__declspec(dllexport) void MXBMRP3_Test_WhatsNewSerialize(char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string s = WhatsNew::serialize();
    snprintf(out, static_cast<size_t>(cap), "%s", s.c_str());
}

// A tab CLICK by name -- the path that dismisses the tab's tag.
__declspec(dllexport) int MXBMRP3_Test_WhatsNewClickTab(const char* tabName) {
    SettingsHud& sh = HudManager::getInstance().getSettingsHud();
    const int t = sh.testTabIndexForName(tabName);
    if (t < 0) return 0;
    sh.testClickTab(t);
    return 1;
}

// The HOVER path, by the row's tooltip id, against whatever tab is open.
__declspec(dllexport) void MXBMRP3_Test_WhatsNewHoverRow(const char* rowTooltipId) {
    // Through SettingsHud, NOT WhatsNew::dismissRow: the real hover path also marks
    // the settings dirty, and that is the half a persistence test has to exercise.
    HudManager::getInstance().getSettingsHud().testHoverDismissRow(rowTooltipId);
}

// The Timing HUD's optional READOUT rows, as a bitmask of ReadoutFlags. They are all
// off by default (this panel is read at a glance mid-corner), so a test that wants one
// has to switch it on -- and the two TEXT rows, Server and Track, are the only rows
// whose value can be too long for the panel, which is what makes them worth driving.
// The Timing HUD's TEXT-readout character budget at the current scale -- the live
// value the panel truncates Server and Track to. Exposed so a test asserts against the
// number the code uses rather than a copy of it.
__declspec(dllexport) int MXBMRP3_Test_TimingTextBudget() {
    return HudManager::getInstance().getTimingHud().readoutTextBudget();
}

__declspec(dllexport) void MXBMRP3_Test_TimingReadouts(unsigned int mask) {
    TimingHud& t = HudManager::getInstance().getTimingHud();
    for (int i = 0; i < READOUT_COUNT; i++) {
        const ReadoutFlags f = READOUT_INFO[i].flag;
        t.setReadoutEnabled(f, (mask & static_cast<unsigned int>(f)) != 0);
    }
}

// The Timing HUD's reference (ms) for a gap type at a given split boundary: the cumulative
// target (S1 / S1+S2 / … ; -1 = full lap), or -1 if unavailable. targetSplit == -999 uses the
// LIVE sector the rider is driving toward (passiveReferenceMs, i.e. currentTargetSplit()).
// The rendered chip text isn't in /api/state, so this exposes the progressive-reference
// selection for a headless test (see timing_hud.cpp).
__declspec(dllexport) int MXBMRP3_Test_TimingReferenceMs(int gapFlag, int targetSplit) {
    TimingHud& t = HudManager::getInstance().getTimingHud();
    GapTypeFlags type = static_cast<GapTypeFlags>(gapFlag);
    return (targetSplit == -999) ? t.passiveReferenceMs(type) : t.cumulativeReferenceMs(type, targetSplit);
}
// The split boundary the Timing HUD's live reference is tracking (currentTargetSplit()):
// 0 = heading to S1, 1 = heading to S2, … ; -1 = timer idle / heading to the finish (full lap).
__declspec(dllexport) int MXBMRP3_Test_TimingTargetSplit() {
    return HudManager::getInstance().getTimingHud().currentTargetSplit();
}
// Whether the Timing HUD's time cell is currently rendering "INVALID" (frozen on a lap flagged
// invalid). Exposed because the rendered text isn't in /api/state. A lap that passed through the
// pits is not flagged invalid, so this stays 0 for a pit out-lap even though the lap is "invalid".
__declspec(dllexport) int MXBMRP3_Test_TimingInvalidShown() {
    return HudManager::getInstance().getTimingHud().showingInvalid() ? 1 : 0;
}
// Whether the Timing HUD is currently holding a frozen official split/lap time (the display
// freeze that follows a split/lap event). Exposed because the freeze state isn't in /api/state;
// it's the signal for the "a completed lap must freeze" regression, including the garage-start
// first-lap case where a stale pit-interrupted flag used to suppress the freeze.
__declspec(dllexport) int MXBMRP3_Test_TimingFrozen() {
    return HudManager::getInstance().getTimingHud().isFrozen() ? 1 : 0;
}
// The Timing panel's rendered geometry (each value ×1e6 as an int, so a headless test can do
// exact integer comparisons). contentTop/contentBot are the section stack's extent measured
// from the panel top, so a test can pin what a row COSTS without also having to know which
// padding vocabulary the panel is built in -- see TimingHud::TestGeometry. Drive a Draw first
// so the bounds reflect the current config.
__declspec(dllexport) void MXBMRP3_Test_TimingGeometry(int* height, int* contentTop,
        int* contentBot, int* fontLarge, int* fontNormal, int* lineLarge, int* lineNormal) {
    TimingHud::TestGeometry g = HudManager::getInstance().getTimingHud().testGeometry();
    auto q = [](float v) { return static_cast<int>(v * 1e6f + 0.5f); };
    if (height)     *height     = q(g.height);
    if (contentTop) *contentTop = q(g.contentTop);
    if (contentBot) *contentBot = q(g.contentBot);
    if (fontLarge)  *fontLarge  = q(g.fontLarge);
    if (fontNormal) *fontNormal = q(g.fontNormal);
    if (lineLarge)  *lineLarge  = q(g.lineLarge);
    if (lineNormal) *lineNormal = q(g.lineNormal);
}

// The display rider's live elapsed lap time (ms), or -1 when there is no valid anchor — the
// placeholder shown before the first S/F crossing and (after the pit-exit fix) after leaving
// the pits until the next S/F. Wall-clock based, so tests assert the PLACEHOLDER condition
// (== -1) or "running" (>= 0), never an exact value.
__declspec(dllexport) int MXBMRP3_Test_ElapsedLapTime() {
    return PluginData::getInstance().getElapsedLapTime();
}

// Whether the display rider's lap timer is in the grid-start grace window: anchored at the
// green flag and not yet reset by the first lap's completion, so an intermediate S/F crossing
// won't zero it. Exposed for the grid-start timing regression (grid->S/F run is counted).
__declspec(dllexport) int MXBMRP3_Test_LapTimerFromRaceStart() {
    return PluginData::getInstance().isLapTimerAnchoredFromRaceStart() ? 1 : 0;
}

// Whether the display rider is in the standing (grid) start grace window - from the green flag,
// through the gate hold, until it clears the first split. Both the wrong-way notice and the
// grid-hazard suppression key on this, so it's the single behavioural signal to assert.
__declspec(dllexport) int MXBMRP3_Test_InGridStartGrace() {
    return PluginData::getInstance().isInGridStartGrace() ? 1 : 0;
}

// Read a rider's computed real-time gap (ms). This is internal plugin state that
// the RaceTrackPosition pipeline computes for the in-game StandingsHud live-gap
// mode; it is NOT emitted in the /api/state JSON, so it's read directly here.
// Returns the leader-relative live gap (0 for the leader), or -1 if unknown.
__declspec(dllexport) int MXBMRP3_Test_GetRealTimeGap(int raceNum) {
    const StandingsData* s = PluginData::getInstance().getStanding(raceNum);
    return s ? s->realTimeGap : -1;
}

// Whether the rider is in the "recently seen in a RaceTrackPosition batch" set
// that feeds liveGapValid. Internal state (the JSON flag ANDs this with other
// conditions, so it can't be observed in isolation there) — used to pin that
// removeRaceEntry() evicts a departed rider so a raceNum reuse doesn't inherit
// a stale "active" bit.
__declspec(dllexport) int MXBMRP3_Test_HasActiveTrackPos(int raceNum) {
    return PluginData::getInstance().hasActiveTrackPos(raceNum) ? 1 : 0;
}

// Blue-flag caches (lazy-rebuilt): pin the O(n^2) lapping/blue-flag detection so it
// can be refactored without changing behavior. Not in /api/state.
__declspec(dllexport) int MXBMRP3_Test_IsRiderBlueFlagged(int raceNum) {
    return PluginData::getInstance().isRiderBlueFlagged(raceNum) ? 1 : 0;
}
__declspec(dllexport) int MXBMRP3_Test_IsRiderLapping(int raceNum) {
    return PluginData::getInstance().isRiderLapping(raceNum) ? 1 : 0;
}
__declspec(dllexport) int MXBMRP3_Test_RiderLappingTarget(int raceNum) {
    return PluginData::getInstance().getRiderLappingTarget(raceNum);
}

// Number of riders in the derived hazard-ahead list (the cached vector NoticesHud
// consumes). Internal state (not in /api/state) — used to pin that removeRaceEntry()
// invalidates the cache: no callbacks arrive while the player sits in menus, so a
// departed rider left in the cached list would linger there indefinitely.
__declspec(dllexport) int MXBMRP3_Test_HazardRaceNumCount() {
    return static_cast<int>(PluginData::getInstance().getHazardRaceNums().size());
}

// Consume the "new all-time PB" notice flag: 1 if it was set (and clears it), else 0.
// Consume-once rather than a plain read so each lap in a test asserts independently —
// in game NoticesHud clears it on a display TIMER, so a plain read would make the
// assertion depend on how much wall time passed between callbacks. The flag is not in
// /api/state, and it is the only observable that distinguishes "stored a PB for this
// bike" from "beat the reference the player is shown" (see PersonalBestUpdate).
__declspec(dllexport) int MXBMRP3_Test_TakeNewAllTimePB() {
    PluginData& pd = PluginData::getInstance();
    if (!pd.hasNewAllTimePB()) return 0;
    pd.clearAllTimePB();
    return 1;
}

// Run the update extract/install pipeline against destDir with an in-memory ZIP,
// bypassing the WinHTTP download. Exercises the real backup → extract → verify →
// rollback path (and the locked-file retry). Returns 1 on success, 0 on failure;
// the error message is copied into errOut. Lets a Wine test hold a handle on the
// target .dlo and assert clean abort + rollback under a lock.
__declspec(dllexport) int MXBMRP3_Test_ExtractAndInstall(const char* destDir,
        const char* zipData, int zipLen, char* errOut, int errCap) {
    std::string err;
    std::vector<char> bytes;
    if (zipData && zipLen > 0) bytes.assign(zipData, zipData + zipLen);
    const bool ok = UpdateDownloader::getInstance().testExtractAndInstall(
        destDir ? destDir : "", bytes, err);
    if (errOut && errCap > 0) {
        strncpy(errOut, err.c_str(), errCap - 1);
        errOut[errCap - 1] = '\0';
    }
    return ok ? 1 : 0;
}

// --- Per-surface HUD decoupling (companion instance) ------------------------
// The companion window carries its OWN on/off + position per HUD, independent of
// the in-game surface (see base_hud.h). That divergence lives in private BaseHud
// members that never reach /api/state, and the settings-INI base section round-
// trips verbatim (so a persistence test can't distinguish "captured from the live
// HUD" from "passed through"). These hooks drive and read the LIVE StandingsHud so
// the runtime semantics (mirror-while-unconfigured, snapshot-on-first-edit, clear-
// reverts-to-mirror) and the real save/load round-trip are asserted directly.
__declspec(dllexport) void MXBMRP3_Test_StandingsSetVisible(int visible) {
    HudManager::getInstance().getStandingsHud().setVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_StandingsSetCompanionVisible(int visible) {
    HudManager::getInstance().getStandingsHud().setCompanionVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_StandingsClearCompanion() {
    HudManager::getInstance().getStandingsHud().clearCompanionState();
}

// TelemetryHud surface visibility + history-buffer depth.
//
// These exist for the PRODUCER side of the visibility invariant, which no other
// hook can reach: the telemetry graphs are drawn from PluginData's history
// buffers, and those are only filled when HudManager::isTelemetryHistoryNeeded()
// says someone is watching. That predicate is a HudManager-internal decision that
// never reaches /api/state, so a black-box test cannot tell "buffers filled" from
// "buffers empty but the HUD is hidden anyway". Depth is the observable that
// distinguishes them. See telemetry_companion_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_TelemetrySetVisible(int visible) {
    HudManager::getInstance().getTelemetryHud().setVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_TelemetrySetCompanionVisible(int visible) {
    HudManager::getInstance().getTelemetryHud().setCompanionVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_TelemetryClearCompanion() {
    HudManager::getInstance().getTelemetryHud().clearCompanionState();
}

// BenchmarkWidget's collection switch vs its visibility, on either surface.
// The bug these exist for: bm.active is the PRODUCER gate for the whole
// instrumentation, and it used to be latched from setVisible() — the game
// toggle — so a widget enabled only on the companion window rendered its tables
// and never filled them. Visibility alone cannot see this: the widget draws
// either way. `active` is the observable that separates "showing data" from
// "showing an empty frame". See benchmark_companion_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_BenchmarkSetVisible(int visible) {
    if (auto* w = HudManager::getInstance().getBenchmarkWidget()) w->setVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_BenchmarkSetCompanionVisible(int visible) {
    if (auto* w = HudManager::getInstance().getBenchmarkWidget()) w->setCompanionVisible(visible != 0);
}

__declspec(dllexport) int MXBMRP3_Test_BenchmarkMetricsActive() {
    return PluginData::getInstance().getBenchmarkMetrics().active ? 1 : 0;
}

// -1 when the widget does not exist (developer mode off), so a test can tell
// "not built" from "built but inactive" rather than reading a false negative.
__declspec(dllexport) int MXBMRP3_Test_BenchmarkExists() {
    return HudManager::getInstance().getBenchmarkWidget() ? 1 : 0;
}

// Samples currently held in the throttle history buffer (every graph buffer is
// appended in the same guarded block, so one is a faithful proxy for "is history
// being accumulated at all").
__declspec(dllexport) int MXBMRP3_Test_TelemetryHistoryDepth() {
    return static_cast<int>(PluginData::getInstance().getHistoryBuffers().throttle.size());
}

__declspec(dllexport) void MXBMRP3_Test_TelemetryClearHistory() {
    PluginData::getInstance().clearHistoryBuffers();
}

// Session Charts HUD: drive visibility and which charts are shown for headless render
// checks / screenshots (the HUD is off by default and its charts are chosen via
// checkboxes in the settings tab). mask is a bitmask of ChartFlags
// (1=Lap 2=Trace 4=Gap 8=Pace; 15=all).
__declspec(dllexport) void MXBMRP3_Test_SessionChartsSetVisible(int visible) {
    HudManager::getInstance().getSessionChartsHud().setVisible(visible != 0);
}

__declspec(dllexport) void MXBMRP3_Test_SessionChartsSetCharts(int mask) {
    HudManager::getInstance().getSessionChartsHud().setEnabledCharts(static_cast<uint32_t>(mask));
}

// Read the StandingsHud's surface state in one call: whether the companion
// instance has diverged (configured), the companion-surface visibility (mirrors
// the game while unconfigured), and the in-game visibility. Any out-pointer may
// be null.
__declspec(dllexport) void MXBMRP3_Test_StandingsCompanionState(
        int* configured, int* companionVisible, int* gameVisible) {
    const BaseHud& hud = HudManager::getInstance().getStandingsHud();
    if (configured)       *configured       = hud.isCompanionConfigured() ? 1 : 0;
    if (companionVisible) *companionVisible = hud.getCompanionVisible() ? 1 : 0;
    if (gameVisible)      *gameVisible      = hud.isVisible() ? 1 : 0;
}

// --- Surface render routing (game vs companion frame) -----------------------
// The riskiest new code is collectSurface + draw()'s per-target routing: the
// game frame is suppressed in COMPANION mode (except with settings open), the
// companion frame filters HUDs by their companion visibility and translates each
// by its (companion - game) offset delta, and the window's X-button falls the
// target back to In-game. None of that is in /api/state, so these hooks set the
// display target and read the collected frames directly.
__declspec(dllexport) void MXBMRP3_Test_SetDisplayTarget(int target) {
    UiConfig::getInstance().setDisplayTarget(static_cast<DisplayTarget>(target));
}

__declspec(dllexport) int MXBMRP3_Test_GetDisplayTarget() {
    return static_cast<int>(UiConfig::getInstance().getDisplayTarget());
}

// Sizes of the last-collected game and companion frames, plus the sum of each
// frame's quad leading-corner X (a cheap fingerprint that shifts when the
// companion offset-delta translation moves a HUD). Any out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_SurfaceFrameStats(
        int* gameQuads, int* companionQuads, double* gameSumX, double* companionSumX) {
    const HudManager& hm = HudManager::getInstance();
    const auto& g = hm.getGameQuads();
    const auto& c = hm.getCompanionQuads();
    if (gameQuads)      *gameQuads      = static_cast<int>(g.size());
    if (companionQuads) *companionQuads = static_cast<int>(c.size());
    if (gameSumX)      { double s = 0; for (const auto& q : g) s += q.m_aafPos[0][0]; *gameSumX = s; }
    if (companionSumX) { double s = 0; for (const auto& q : c) s += q.m_aafPos[0][0]; *companionSumX = s; }
}

// HelmetOverlayHud visibility. The helmet is the one HUD excluded from the
// companion surface entirely (BaseHud::rendersOnCompanion) -- it is a full-screen
// in-game effect, not a panel -- so a test needs to turn it on and confirm the
// companion frame does NOT grow. See companion_decouple_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_HelmetSetVisible(int visible) {
    HelmetOverlayHud& h = HudManager::getInstance().getHelmetOverlayHud();
    h.setVisible(visible != 0);
    // Visibility ALONE emits nothing headlessly. The helmet parts need a discovered
    // texture variant (> 0), and no helmet textures are staged for the harness, so
    // anyHelmetPart stays false and the overlay draws zero quads -- a test would then
    // "prove" the companion is unaffected because NOTHING was affected. The visor
    // tint is a plain untextured quad, so switching it on is what makes the overlay
    // actually render here. Found by the positive control in companion_decouple_test.
    if (visible) h.m_visorMode = HelmetOverlayHud::VISOR_MODE;
}

// Companion-surface visibility for the helmet. Needed to make the exclusion test
// MEAN anything: the companion instance is snapshotted from the game flag on the
// first companion frame, so a test that only sets the game flag leaves the companion
// one false and the frame stays flat for that reason instead of the exclusion --
// which is exactly how the first version of that test passed against a mutant.
__declspec(dllexport) void MXBMRP3_Test_HelmetSetCompanionVisible(int visible) {
    HudManager::getInstance().getHelmetOverlayHud().setCompanionVisible(visible != 0);
}

// The predicate every update() gate reads. Pins that a HUD excluded from the
// companion cannot answer "visible somewhere" off a stale companion flag -- if it
// could, it would rebuild every frame for a surface that never draws it.
__declspec(dllexport) int MXBMRP3_Test_HelmetVisibleAnySurface() {
    return HudManager::getInstance().getHelmetOverlayHud().isVisibleAnySurface() ? 1 : 0;
}

__declspec(dllexport) int MXBMRP3_Test_HelmetVisible() {
    return HudManager::getInstance().getHelmetOverlayHud().isVisible() ? 1 : 0;
}

// Vertical inset of a row's race-number inside its plate, as a fraction of plate
// height. The bug it exists for is invisible in a screenshot: the number was
// centred by the rebuild path and left uncentred by the drag/layout path, so it
// only moved while the HUD was being dragged.
__declspec(dllexport) float MXBMRP3_Test_StandingsPlateInsetY(int row) {
    return HudManager::getInstance().getStandingsHud().testPlateNumberInsetY(row);
}

// Move the standings HUD on the GAME surface. Triggers the layout fast path
// (rebuildLayout) rather than a full rebuild -- which is the whole point: that is
// the path the plate offset was missing from.
__declspec(dllexport) void MXBMRP3_Test_StandingsSetOffset(float x, float y) {
    HudManager::getInstance().getStandingsHud().setPosition(x, y);
}

// Move the StandingsHud on the companion surface (configures its instance), so a
// test can exercise the offset-delta translation independent of visibility.
__declspec(dllexport) void MXBMRP3_Test_StandingsSetCompanionOffset(float x, float y) {
    HudManager::getInstance().getStandingsHud().setCompanionPosition(x, y);
}

// Simulate the user closing the companion window with its X button (sets the
// consumed userClosed flag the game thread checks in draw()).
__declspec(dllexport) void MXBMRP3_Test_CompanionSimulateUserClose() {
    CompanionWindow::getInstance().requestClose();
}

// Force a fake connected controller (with a few buttons/sticks engaged) and show the
// gamepad widget, so the companion-window preview harness can screenshot the gamepad
// layout headless (no real controller). connected=0 clears the override.
__declspec(dllexport) void MXBMRP3_Test_FakeGamepad(int connected) {
    if (connected) {
        XInputData d;
        d.isConnected = true;
        d.leftTrigger = 0.6f; d.rightTrigger = 0.35f;
        d.leftStickX = 0.5f;  d.leftStickY = -0.4f;
        d.rightStickX = -0.3f; d.rightStickY = 0.2f;
        d.buttonA = true; d.buttonY = true; d.dpadUp = true; d.leftShoulder = true;
        d.buttonStart = true;
        XInputReader::getInstance().testForceData(d);
    } else {
        XInputReader::getInstance().testClearForcedData();
    }
    HudManager::getInstance().getGamepadWidget().setVisible(connected != 0);
}

// [Advanced] uiFontSize at runtime, so a test can ask what a HUD does when the user
// changes the base type size -- the one input that reaches every panel. The INI path
// does exactly this (settings_manager_global.cpp), but only at load, and a test that
// has to restart the plugin to change one number cannot compare two states of the
// same HUD.
__declspec(dllexport) void MXBMRP3_Test_SetUiFontSize(float value) {
    layoutSetFontSize(LayoutConfig::getInstance().mutableDefaults(), value);
    HudManager::getInstance().markAllHudsDirty();
}

// THE BOX MODEL'S EIGHT AIR TERMS at runtime, in the same CSS shorthand the
// [Advanced] keys take. Same argument as MXBMRP3_Test_SetUiFontSize: the INI path
// does exactly this but only at load, and comparing two states of the same panel is
// the only way to ask "does this term reach this panel at all".
//
// Which is what it exists for. FOUR OF THE EIGHT were dead on an UNTHEMED panel --
// layoutPanel collapsed the whole title/content box on "is there art to draw a
// border with", and the settings panel gated its content padding the same way --
// and the whole suite was green, because every geometry case either installs a
// theme or asserts a term against itself. `which` is the order the keys are
// written in settings_manager_global.cpp.
__declspec(dllexport) void MXBMRP3_Test_SetBoxTerm(int which, const char* shorthand) {
    if (!shorthand) return;
    LayoutMetrics& L = LayoutConfig::getInstance().mutableDefaults();
    const LayoutMetrics defaults;
    switch (which) {
        case 0: layoutSetBoxSides(L.boxPanelPadding, shorthand, defaults.boxPanelPadding); break;
        case 1: layoutSetBoxSides(L.boxTitleMargin, shorthand, defaults.boxTitleMargin); break;
        case 2: layoutSetBoxSides(L.boxTitlePadding, shorthand, defaults.boxTitlePadding); break;
        case 3: layoutSetBoxSides(L.boxContentMargin, shorthand, defaults.boxContentMargin); break;
        case 4: layoutSetBoxSides(L.boxContentPadding, shorthand, defaults.boxContentPadding); break;
        case 5: layoutSetBoxSides(L.boxButtonMargin, shorthand, defaults.boxButtonMargin); break;
        case 6: layoutSetBoxSides(L.boxButtonPadding, shorthand, defaults.boxButtonPadding); break;
        case 7: layoutSetBoxScalar(L.boxPanelGap, shorthand, defaults.boxPanelGap); break;
        default: return;
    }
    HudManager::getInstance().markAllHudsDirty();
}

// GAMEPAD PACKS. Install a pack with no files (see installSyntheticGamepad), then
// read back both halves of the name-keyed selection: what is STORED and which pack
// is actually in USE. The distinction is the whole point -- an unresolvable name
// must degrade to the shipped default for rendering WITHOUT rewriting what is
// stored, so putting the folder back restores the user's choice.
// `geometryWidth` lets a case prove the widget draws the selected pack's numbers
// rather than the built-in fallback.
__declspec(dllexport) void MXBMRP3_Test_InstallGamepad(const char* name, float geometryWidth) {
    GamepadAsset pad;
    pad.name = name ? name : "synthetic";
    pad.displayName = pad.name;
    pad.geometry.backgroundWidth = geometryWidth;
    // Non-zero so nothing downstream treats the pack as art-less; no case asserts
    // which sprite was drawn, and the emitters never validate an index.
    for (int i = 0; i < GamepadSprite::COUNT; ++i) pad.sprites[i] = i + 1;
    AssetManager::getInstance().installSyntheticGamepad(pad);
}

// PACK SKINS (the `base` key). Three observables, because the feature has three
// halves a black box cannot separate: which pack a stem's FILE resolved from,
// whether the skin INHERITED the base's geometry, and whether its own ini keys
// still override on top. All read the real discovery output -- pack_skin_test.cpp
// stages an actual gamepads/ tree and runs the real scan against it.
__declspec(dllexport) int MXBMRP3_Test_GamepadStemSource(const char* pack, int stem) {
    if (!pack || stem < 0 || stem >= GamepadSprite::COUNT) return -1;
    const GamepadAsset* p = AssetManager::getInstance().getGamepadByName(pack);
    if (!p) return -1;
    return p->spriteFromBase[stem] ? 1 : 0;
}

__declspec(dllexport) float MXBMRP3_Test_GamepadGeomWidth(const char* pack) {
    const GamepadAsset* p = pack ? AssetManager::getInstance().getGamepadByName(pack) : nullptr;
    return p ? p->geometry.backgroundWidth : 0.0f;
}

__declspec(dllexport) int MXBMRP3_Test_PitboardStemSource(const char* pack, int stem) {
    if (!pack || stem < 0 || stem >= PitboardSprite::COUNT) return -1;
    const PitboardAsset* b = AssetManager::getInstance().getPitboardByName(pack);
    if (!b) return -1;
    return b->spriteFromBase[stem] ? 1 : 0;
}

__declspec(dllexport) float MXBMRP3_Test_PitboardPackArtWidth(const char* pack) {
    const PitboardAsset* b = pack ? AssetManager::getInstance().getPitboardByName(pack) : nullptr;
    return b ? b->geometry.artWidth : 0.0f;
}

// Returns the pack's row-text colour as the game's ABGR word, 0 for an unknown
// pack. Pins both halves of [text] color: that it rides the base-inheritance
// geometry copy, and that it parses through parseRgbHex rather than the float
// path (a 32-bit colour word does not survive a float round-trip, so a value
// like #ffffff would come back mangled if it were ever moved to the numeric
// table).
__declspec(dllexport) unsigned int MXBMRP3_Test_PitboardTextColor(const char* pack) {
    const PitboardAsset* b = pack ? AssetManager::getInstance().getPitboardByName(pack) : nullptr;
    return b ? b->geometry.textColor : 0u;
}

// THE STEM TABLE ITSELF, so a fixture that has to stage a complete pack on
// disk stages exactly what discovery requires. pack_skin_test builds real
// gamepads/<name>/ trees; hardcoding the seventeen names there would be a
// second copy of GamepadSprite::kStems, and the CLAUDE.md invariant that keeps
// discovery and registration in step (one table, static_assert'd) would not
// cover it -- add a stem and the fixture silently stages an incomplete base
// pack, which surfaces as a misleading "missing X.tga" cascade pointing at the
// feature instead of the fixture. Integration tests are black-box (they link
// no plugin source, only harness + vendor), so a hook is how they read it.
__declspec(dllexport) int MXBMRP3_Test_GamepadStemCount(void) {
    return static_cast<int>(GamepadSprite::COUNT);
}

__declspec(dllexport) void MXBMRP3_Test_GamepadStemName(int index, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (index < 0 || index >= static_cast<int>(GamepadSprite::COUNT)) return;
    strncpy_s(out, static_cast<size_t>(cap), GamepadSprite::kStems[index], _TRUNCATE);
}

__declspec(dllexport) void MXBMRP3_Test_ClearGamepads(void) {
    AssetManager::getInstance().clearSyntheticGamepads();
}

// PIT BOARD PACKS -- same three-hook shape as the gamepad ones above, for the
// same reason: the selection rule is what is under test, not the file scan.
// `artWidth`/`artHeight` let a case prove the panel takes its shape from the
// SELECTED pack rather than from the constant this replaced.
__declspec(dllexport) void MXBMRP3_Test_InstallPitboard(const char* name, float artW, float artH) {
    PitboardAsset board;
    board.name = name ? name : "synthetic";
    board.displayName = board.name;
    board.geometry.artWidth = artW;
    board.geometry.artHeight = artH;
    for (int i = 0; i < PitboardSprite::COUNT; ++i) board.sprites[i] = i + 1;
    AssetManager::getInstance().installSyntheticPitboard(board);
}

// THE PACK CYCLE, as the settings UI drives it. These exist because the bug they
// pin was UI REACHABILITY -- the widget state was fine, but no control could reach
// it -- which is invisible to any test that only calls the widget's own setters.
__declspec(dllexport) void MXBMRP3_Test_CyclePack(int pitboard, int forward) {
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    if (pitboard) settings.cyclePitboardPack(forward != 0);
    else          settings.cycleGamepadPack(forward != 0);
}

// showBackgroundTexture for either pack-driven HUD: the flag whose only control is
// the Off entry in those cycles.
__declspec(dllexport) int MXBMRP3_Test_PackShowBg(int pitboard) {
    return pitboard ? (HudManager::getInstance().getPitboardHud().getShowBackgroundTexture() ? 1 : 0)
                    : (HudManager::getInstance().getGamepadWidget().getShowBackgroundTexture() ? 1 : 0);
}

__declspec(dllexport) void MXBMRP3_Test_SetPackShowBg(int pitboard, int on) {
    if (pitboard) HudManager::getInstance().getPitboardHud().setShowBackgroundTexture(on != 0);
    else          HudManager::getInstance().getGamepadWidget().setShowBackgroundTexture(on != 0);
}

__declspec(dllexport) void MXBMRP3_Test_ClearPitboards(void) {
    AssetManager::getInstance().clearSyntheticPitboards();
}

__declspec(dllexport) void MXBMRP3_Test_SetPitboardPack(const char* name) {
    HudManager::getInstance().getPitboardHud().setPitboardPack(name ? name : "");
}

__declspec(dllexport) void MXBMRP3_Test_PitboardPackStored(char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string& s = HudManager::getInstance().getPitboardHud().getPitboardPack();
    const int n = (static_cast<int>(s.size()) < cap - 1) ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}

__declspec(dllexport) void MXBMRP3_Test_PitboardPackActive(char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    const PitboardAsset* pack = HudManager::getInstance().getPitboardHud().activePack();
    if (!pack) return;
    const std::string& s = pack->name;
    const int n = (static_cast<int>(s.size()) < cap - 1) ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}

// THE RELOAD_CONFIG HOTKEY'S ASSET HALF, driven directly. Re-reads every theme's
// and every pack's ini without re-discovering sprites -- see
// AssetManager::reloadThemeLayouts().
//
// What this exists to pin is that the reload REACHES all three asset types. It used
// to reach themes and pads only: the board's loop was written out by hand like the
// pad's and simply never written, so RELOAD_CONFIG silently ignored a board's ini
// while README promised it did not, and the sync above it copied the files across.
// Nothing caught it because nothing drove this path.
__declspec(dllexport) void MXBMRP3_Test_ReloadAssetLayouts(void) {
    AssetManager::getInstance().reloadThemeLayouts();
}

// A pack's CURRENT art width, so a test can watch the reload act on it. A synthetic
// pack has no ini on disk, so a reload that reaches it resets the geometry to the
// built-in default -- which is the observable difference between "the loop ran" and
// "the loop is missing", with no filesystem staging required.
__declspec(dllexport) float MXBMRP3_Test_PitboardArtWidth(void) {
    const PitboardAsset* pack = HudManager::getInstance().getPitboardHud().activePack();
    return pack ? pack->geometry.artWidth : 0.0f;
}
__declspec(dllexport) float MXBMRP3_Test_GamepadArtWidth(void) {
    const GamepadAsset* pack = HudManager::getInstance().getGamepadWidget().activePack();
    return pack ? pack->geometry.backgroundWidth : 0.0f;
}

__declspec(dllexport) void MXBMRP3_Test_SetGamepadPack(const char* name) {
    HudManager::getInstance().getGamepadWidget().setGamepadPack(name ? name : "");
}

// The STORED name -- what a save would write back.
__declspec(dllexport) void MXBMRP3_Test_GamepadPackStored(char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string& s = HudManager::getInstance().getGamepadWidget().getGamepadPack();
    const int n = (static_cast<int>(s.size()) < cap - 1) ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}

// The pack actually RESOLVED for rendering; empty when none is installed.
__declspec(dllexport) void MXBMRP3_Test_GamepadPackActive(char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    const GamepadAsset* pack = HudManager::getInstance().getGamepadWidget().activePack();
    if (!pack) return;
    const std::string& s = pack->name;
    const int n = (static_cast<int>(s.size()) < cap - 1) ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}

// Where the gamepad's content sits inside its controller frame, as the fraction of
// the frame (quad[0], the background box) spanned from its top to the LOWEST-RIGHT
// content pixel: out[0] = bottom-most content Y fraction, out[1] = right-most X
// fraction. The buttons are positioned off the interior line height; when that grew
// relative to the fontSize-sized frame (#256), the content slid down/right and these
// fractions jumped. So they're a stable golden signature of the layout: the fix pins
// them, a re-detaching change moves them. Requires a faked connected controller.
__declspec(dllexport) void MXBMRP3_Test_GamepadContentExtent(float* outBottom, float* outRight) {
    if (outBottom) *outBottom = -1.0f;
    if (outRight)  *outRight  = -1.0f;
    const auto& quads = HudManager::getInstance().getGamepadWidget().getQuads();
    if (quads.size() < 2) return;
    auto extent = [](const SPluginQuad_t& q, float& x0, float& y0, float& x1, float& y1) {
        x0 = y0 = 1e9f; x1 = y1 = -1e9f;
        for (int i = 0; i < 4; ++i) {
            float x = q.m_aafPos[i][0], y = q.m_aafPos[i][1];
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    };
    float bx0, by0, bx1, by1;
    extent(quads[0], bx0, by0, bx1, by1);
    float bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0) return;
    float maxY = -1e9f, maxX = -1e9f;
    for (size_t i = 1; i < quads.size(); ++i) {
        float x0, y0, x1, y1;
        extent(quads[i], x0, y0, x1, y1);
        if (y1 > maxY) maxY = y1;
        if (x1 > maxX) maxX = x1;
    }
    if (outBottom) *outBottom = (maxY - by0) / bh;
    if (outRight)  *outRight  = (maxX - bx0) / bw;
}

// Pin the active input surface (0=Game, 1=Companion, -1=off) so the headless preview
// can screenshot surface-scoped rendering (e.g. the settings menu, which only draws on
// the active surface — and the companion window never takes focus, so it's never the
// foreground-derived active surface under Wine).
__declspec(dllexport) void MXBMRP3_Test_ForceActiveSurface(int surface) {
    InputManager::getInstance().testForceActiveSurface(surface);
}

// Comma-joined, sorted list of the section names captureToCache() produces for the
// current live HUDs. settings_sections_test asserts every one of them is actually
// serialized to the INI — guarding against the "third hardcoded list" trap where a
// HUD is captured/applied but missing from serializeSettings' order list (the bug
// that silently dropped FriendsHud). See CLAUDE.md "Adding a New HUD" step 6.
__declspec(dllexport) void MXBMRP3_Test_CapturedSections(char* out, int cap) {
    if (!out || cap <= 0) return;
    const auto names = SettingsManager::getInstance()
        .testCapturedSectionNames(HudManager::getInstance());
    std::string joined;
    for (const auto& n : names) { if (!joined.empty()) joined += ','; joined += n; }
    strncpy(out, joined.c_str(), cap - 1);
    out[cap - 1] = '\0';
}

// --- Map HUD perf isolation --------------------------------------------------
// The map is enabled by default and rebuilds on every rider-position update, so
// it dominates Draw on a full grid. These hooks let the interleaved perf driver
// toggle the map on/off and switch its view mode (rotate-to-player / zoom, which
// defeat the ribbon-quad cache) so the map's per-frame rebuild cost — and the
// cache's effect — can be measured in isolation against a map-off baseline.
__declspec(dllexport) void MXBMRP3_Test_MapSetVisible(int visible) {
    HudManager::getInstance().getMapHud().setVisible(visible != 0);
}
__declspec(dllexport) void MXBMRP3_Test_MapSetRotate(int on) {
    HudManager::getInstance().getMapHud().setRotateToPlayer(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_MapSetZoom(int on) {
    HudManager::getInstance().getMapHud().setZoomEnabled(on != 0);
}
// Legacy preset shim, kept so older drivers/tests keep meaning the same thing:
// 0=AUTO (adaptive, 100%), 1=HIGH (fixed, 200% = 1.0m), 2=LOW (fixed, 60% ≈
// 3.3m). New code uses the percent/adaptive hooks below.
__declspec(dllexport) void MXBMRP3_Test_MapSetDetail(int detail) {
    MapHud& map = HudManager::getInstance().getMapHud();
    switch (detail) {
        case 1:  map.setAdaptiveDetail(false); map.setDetailScale(2.0f);  break;
        case 2:  map.setAdaptiveDetail(false); map.setDetailScale(0.6f);  break;
        default: map.setAdaptiveDetail(true);  map.setDetailScale(1.0f); break;  // old AUTO == 100%, not the new default
    }
}
// Detail scale as the settings row shows it: a percentage (20-200).
__declspec(dllexport) void MXBMRP3_Test_MapSetDetailPct(int pct) {
    HudManager::getInstance().getMapHud().setDetailScale(static_cast<float>(pct) / 100.0f);
}
// Outline control as the combined settings row drives it: 0 = off, else on with
// the rim width at pct/100 (100 = the classic 1.4x pass multiplier).
__declspec(dllexport) void MXBMRP3_Test_MapSetOutline(int pct) {
    MapHud& map = HudManager::getInstance().getMapHud();
    if (pct <= 0) { map.setShowOutline(false); return; }
    map.setShowOutline(true);
    map.setOutlineWidthScale(static_cast<float>(pct) / 100.0f);
}
__declspec(dllexport) void MXBMRP3_Test_MapSetAdaptive(int on) {
    HudManager::getInstance().getMapHud().setAdaptiveDetail(on != 0);
}
// Read back the detail state as percent + 1000*adaptive (e.g. 1100 = adaptive
// 100%, 60 = fixed 60%), so the legacy-INI migration can be asserted headless.
__declspec(dllexport) int MXBMRP3_Test_MapDetailState() {
    const MapHud& map = HudManager::getInstance().getMapHud();
    int pct = static_cast<int>(map.getDetailScale() * 100.0f + 0.5f);
    return pct + (map.getAdaptiveDetail() ? 1000 : 0);
}
// Map render stats: quad count + a position checksum (sum of every quad vertex
// X and Y) + a non-finite flag. The checksum is a cheap geometry fingerprint used
// to assert the world-ribbon cache is transparent (identical quads across mode
// round-trips) and that no quad carries NaN/Inf (the degenerate-track divide-by-
// zero). anyNonFinite/sums may be null.
__declspec(dllexport) int MXBMRP3_Test_MapQuadStats(double* sumX, double* sumY, int* anyNonFinite) {
    const auto& quads = HudManager::getInstance().getMapHud().getQuads();
    double sx = 0, sy = 0; int bad = 0;
    for (const auto& q : quads) {
        for (int i = 0; i < 4; ++i) {
            float x = q.m_aafPos[i][0], y = q.m_aafPos[i][1];
            if (!std::isfinite(x) || !std::isfinite(y)) bad = 1;
            sx += x; sy += y;
        }
    }
    if (sumX) *sumX = sx;
    if (sumY) *sumY = sy;
    if (anyNonFinite) *anyNonFinite = bad;
    return static_cast<int>(quads.size());
}
// Sum-of-SQUARES variant of the checksum above. The plain sum is blind to ribbon
// WIDTH changes (left/right edges move symmetrically, center ± w*perp, so the
// width term cancels); the squared sum keeps it (2c^2 + 2w^2), so tests can
// assert that a width-only change actually moved the vertices.
__declspec(dllexport) int MXBMRP3_Test_MapQuadSumSq(double* sumSqX, double* sumSqY) {
    const auto& quads = HudManager::getInstance().getMapHud().getQuads();
    double sx2 = 0, sy2 = 0;
    for (const auto& q : quads) {
        for (int i = 0; i < 4; ++i) {
            double x = q.m_aafPos[i][0], y = q.m_aafPos[i][1];
            sx2 += x * x; sy2 += y * y;
        }
    }
    if (sumSqX) *sumSqX = sx2;
    if (sumSqY) *sumSqY = sy2;
    return static_cast<int>(quads.size());
}
// Read + reset the accumulated per-phase rebuild time (microseconds), rebuild
// count (return value), and ribbon-cache hit/miss counts. Attributes the map's
// per-frame cost to bounds/layout vs ribbon vs markers vs riders.
__declspec(dllexport) long long MXBMRP3_Test_MapProfile(
        double* boundsUs, double* ribbonUs, double* markersUs, double* ridersUs,
        long long* ribbonHits, long long* ribbonMiss) {
    double b = 0, r = 0, m = 0, ri = 0; long long c = 0, hits = 0, miss = 0;
    mapHudReadProfile(b, r, m, ri, c, hits, miss);
    if (boundsUs) *boundsUs = b;
    if (ribbonUs) *ribbonUs = r;
    if (markersUs) *markersUs = m;
    if (ridersUs) *ridersUs = ri;
    if (ribbonHits) *ribbonHits = hits;
    if (ribbonMiss) *ribbonMiss = miss;
    return c;
}

// Toggle the developer BenchmarkWidget (always created; normally gated behind
// developer mode in the UI). on=1 activates the built-in profiler (per-callback +
// per-HUD rebuild timing) and resets its counters; on=0 hides it, which exports a
// timestamped report to <savePath>/mxbmrp3/benchmarks/. Lets the headless bench
// driver capture a whole-plugin timing breakdown using the plugin's own component.
__declspec(dllexport) void MXBMRP3_Test_BenchmarkWidget(int on) {
    BenchmarkWidget* bw = HudManager::getInstance().getBenchmarkWidget();
    if (!bw) return;
    // setVisible() runs the transition synchronously via BaseHud::onVisibilityChanged,
    // which is what bench_driver depends on: it does Benchmark(0) then Shutdown() with
    // no frame between, and without a synchronous edge no report is written and
    // run_perf.sh fails its analyzer contract check with no BENCH line to parse.
    bw->setVisible(on != 0);
}

// Force every HUD/widget visible (or hidden) so the benchmark driver can profile
// the plugin with EVERYTHING enabled, not just the default-on HUDs.
__declspec(dllexport) void MXBMRP3_Test_ShowAllHuds(int on) {
    HudManager::getInstance().testSetAllHudsVisible(on != 0);
}

// --- RENDER PROBE (see tools/probetheme/README.md) -------------------
// The probe injects synthetic quads for the ENGINE to draw, so its cost is
// invisible to every in-plugin timer by construction -- there is nothing here to
// assert about speed. What CAN go wrong is the probe emitting the wrong primitive
// and the report saying otherwise, which turns a measurement session into a
// confidently wrong conclusion. These two expose exactly that.
// Lowest/highest textured sprite in the last frame handed to the game, plus the
// count of sprite-0 (untextured) quads.
__declspec(dllexport) void MXBMRP3_Test_FrameSpriteSpan(int* outMin, int* outMax,
                                                        int* outUntextured) {
    int lo = 0, hi = 0, flat = 0;
    HudManager::getInstance().testLastFrameSpriteSpan(lo, hi, flat);
    if (outMin) *outMin = lo;
    if (outMax) *outMax = hi;
    if (outUntextured) *outUntextured = flat;
}
__declspec(dllexport) int MXBMRP3_Test_RegisteredSpriteCount() {
    return HudManager::getInstance().registeredSpriteCount();
}
// Stand up a sprite table without assets on disk -- see testInstallSpriteTable.
__declspec(dllexport) void MXBMRP3_Test_InstallSpriteTable(int count) {
    HudManager::getInstance().testInstallSpriteTable(count);
}
// Read back the live probe settings, so a test can assert the sweep put them back.
__declspec(dllexport) void MXBMRP3_Test_GetRenderProbe(int* n, int* type, int* fs, int* sprite) {
    UiConfig& ui = UiConfig::getInstance();
    if (n)      *n = ui.getRenderProbeQuads();
    if (type)   *type = ui.getRenderProbeType();
    if (fs)     *fs = ui.getRenderProbeFullscreen() ? 1 : 0;
    if (sprite) *sprite = ui.getRenderProbeSprite();
}
// The automatic sweep (core/render_probe_sweep.h). start/abort are one action in
// game (one hotkey toggles), but split here so a test can drive each edge.
__declspec(dllexport) void MXBMRP3_Test_ProbeSweepStart() {
    RenderProbeSweep::getInstance().start();
}
__declspec(dllexport) void MXBMRP3_Test_ProbeSweepAbort() {
    RenderProbeSweep::getInstance().abort();
}
__declspec(dllexport) int MXBMRP3_Test_ProbeSweepRunning() {
    return RenderProbeSweep::getInstance().isRunning() ? 1 : 0;
}
// The probe's text length, which the sweep also steps and so must also restore.
__declspec(dllexport) void MXBMRP3_Test_SetRenderProbeTextChars(int n) {
    UiConfig::getInstance().setRenderProbeTextChars(n);
}
// The sweep's report for a SYNTHETIC result set (per-quad costs supplied, no
// 40s sweep) — the seam that makes the DERIVED block's arithmetic assertable.
// Front truncation is fine here: the derived block sits mid-report and the
// buffer callers pass outgrows the whole report anyway.
__declspec(dllexport) void MXBMRP3_Test_ProbeSweepReport(double fillUs, double alpha0Us,
                                                         double degenUs,
                                                         char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string s = RenderProbeSweep::getInstance().testReportSynthetic(
        fillUs, alpha0Us, degenUs);
    const int n = static_cast<int>(s.size()) < cap - 1 ? static_cast<int>(s.size()) : cap - 1;
    for (int i = 0; i < n; ++i) out[i] = s[i];
    out[n] = '\0';
}
__declspec(dllexport) int MXBMRP3_Test_GetRenderProbeTextChars() {
    return UiConfig::getInstance().getRenderProbeTextChars();
}
// A named panel's background opacity. At 0 the whole background chain is skipped
// (see BaseHud::backgroundIsInvisible), which is the behaviour under test.
__declspec(dllexport) int MXBMRP3_Test_SetHudOpacity(const char* name, float opacity) {
    // Matched on the REGISTRATION NAME, like every other name-addressed hook here.
    // It used to match on the TEXTURE BASE NAME, and the note here warned against
    // MXBMRP3_Test_PanelName because that preferred the ICON name -- "two naming
    // schemes for the same panel is how a test ends up silently matching nothing and
    // passing". The warning was right and the fix was to delete the second scheme:
    // PanelName now answers with the registration name too, so there is one
    // vocabulary. It also makes the elements with NO texture stem (Version, the
    // settings panel, the director widget) addressable here for the first time.
    if (!name) return 0;
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (!hud || std::strcmp(hud->getHarnessId(), name) != 0) continue;
        hud->setBackgroundOpacity(opacity);
        hud->setDataDirty();
        return 1;
    }
    return 0;
}

// Reposition a named panel. Marks it LAYOUT-dirty, not data-dirty, so the next
// update() takes the layout fast path (updateBackgroundQuadPosition) rather than a
// full rebuild -- which is the path that rewrites the recorded background span.
__declspec(dllexport) int MXBMRP3_Test_SetHudOffset(const char* name, float x, float y) {
    if (!name) return 0;   // registration name, as above
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (!hud || std::strcmp(hud->getHarnessId(), name) != 0) continue;
        hud->setPosition(x, y);
        return 1;
    }
    return 0;
}

// The LARGEST quad a named panel emits, as area x 1e6.
//
// For the zero-opacity drag case, where the corruption is geometric rather than
// countable: with the background skipped, m_bgQuadFirst points at the first CONTENT
// quad, and the layout fast path would rewrite THAT to the full panel rect on a
// reposition. The quad count is unchanged -- one quad simply becomes panel-sized --
// so only a size measure can see it. A pure translation preserves every area, which
// makes "unchanged after a move" the exact assertion.
__declspec(dllexport) int MXBMRP3_Test_HudMaxQuadArea(const char* name) {
    if (!name) return -1;   // registration name, as above
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (!hud || std::strcmp(hud->getHarnessId(), name) != 0) continue;
        double best = 0.0;
        for (const SPluginQuad_t& q : hud->getQuads()) {
            float minX = q.m_aafPos[0][0], maxX = minX;
            float minY = q.m_aafPos[0][1], maxY = minY;
            for (int c = 1; c < 4; ++c) {
                minX = (std::min)(minX, q.m_aafPos[c][0]);
                maxX = (std::max)(maxX, q.m_aafPos[c][0]);
                minY = (std::min)(minY, q.m_aafPos[c][1]);
                maxY = (std::max)(maxY, q.m_aafPos[c][1]);
            }
            best = (std::max)(best, static_cast<double>(maxX - minX) * (maxY - minY));
        }
        return static_cast<int>(best * 1e6);
    }
    return -1;
}

// Developer mode, which reveals the Performance tab's Developer section. The settings
// panel measures EVERY tab to size itself, so a section that only exists in developer
// mode is measured only in developer mode -- and is invisible to any test that does
// not turn it on.
__declspec(dllexport) void MXBMRP3_Test_SetDeveloperMode(int on) {
    SettingsManager::getInstance().setDeveloperMode(on != 0);
    HudManager::getInstance().markAllHudsDirty();
}

// The global drop-shadow setting, which doubles the string count when on.
__declspec(dllexport) void MXBMRP3_Test_SetDropShadow(int on) {
    UiConfig::getInstance().setDropShadow(on != 0);
    HudManager::getInstance().markAllHudsDirty();
}

// How many HUDs are currently registered with the benchmark profiler. Registration
// happens once, in HudManager::initialize(); anything that wipes it leaves the whole
// per-HUD footprint table silently empty (recordHudRebuild() bounds-checks against this
// count), which is exactly the bug benchmark_registry_test.cpp pins. Also exposes the
// callback registry count, whose slots alias across a wipe rather than going empty.
__declspec(dllexport) int MXBMRP3_Test_BenchmarkHudCount() {
    return PluginData::getInstance().getBenchmarkMetrics().hudCount;
}
__declspec(dllexport) int MXBMRP3_Test_BenchmarkCallbackCount() {
    return PluginData::getInstance().getBenchmarkMetrics().callbackCount;
}
// How many panels SHOULD carry a benchmark slot: every registered HUD except the
// BenchmarkWidget, which excludes itself. Paired with BenchmarkHudCount so a test
// can compare two live numbers instead of a hardcoded one that rots on every new HUD.
__declspec(dllexport) int MXBMRP3_Test_ProfilableHudCount() {
    return HudManager::getInstance().profilableHudCount();
}
// The BenchmarkWidget's snapshot-array capacity and the count it actually stored.
// A count ABOVE the capacity is the out-of-bounds read that corrupted the exported
// report; the pair is exposed so a test can assert the invariant directly.
__declspec(dllexport) int MXBMRP3_Test_BenchmarkSnapshotCapacity() {
    return BenchmarkWidget::snapshotCapacity();
}
__declspec(dllexport) int MXBMRP3_Test_BenchmarkSnapshotCount() {
    BenchmarkWidget* w = HudManager::getInstance().getBenchmarkWidget();
    return w ? w->snapshotCount() : -1;
}

// A named panel's stint rebuild count, or -1 if no panel carries that name.
//
// The question this answers is "did THIS panel rebuild on THIS change", which is
// the one a dirty-flag subscription is a claim about, and which nothing else here
// could ask: the snapshot shows computed state, so a panel that quietly stops
// updating still reports the right numbers through it. The counter only moves while
// the profiler is collecting (bm.active), so a caller must switch the benchmark
// widget on first.
//
// The name is the profiler's own -- the same string the report's per-HUD table
// prints, and the one MXBMRP3_Test_PanelName answers with.
__declspec(dllexport) int MXBMRP3_Test_HudRebuildCount(const char* name) {
    if (!name) return -1;
    const BenchmarkMetrics& bm = PluginData::getInstance().getBenchmarkMetrics();
    for (int i = 0; i < bm.hudCount; ++i) {
        if (std::strcmp(bm.huds[i].name, name) == 0) return bm.huds[i].stintRebuildCount;
    }
    return -1;
}

// Crank the heavy HUDs' individual settings to maximum (all columns/rows/events,
// max row counts, long names, highest map detail) for worst-case profiling.
__declspec(dllexport) void MXBMRP3_Test_MaxHudSettings() {
    SettingsManager::getInstance().testMaxAllHudSettings(HudManager::getInstance());
}

// Read + reset the accumulated per-phase StandingsHud::rebuildRenderData() time
// (microseconds): setup / format / name+anim / layout / render; return value is
// the rebuild count. Attributes the standings rebuild cost for the perf probe.
__declspec(dllexport) long long MXBMRP3_Test_StandingsProfile(
        double* setupUs, double* formatUs, double* nameAnimUs, double* layoutUs, double* renderUs) {
    double se = 0, fo = 0, na = 0, la = 0, re = 0; long long c = 0;
    standingsReadProfile(se, fo, na, la, re, c);
    if (setupUs) *setupUs = se;
    if (formatUs) *formatUs = fo;
    if (nameAnimUs) *nameAnimUs = na;
    if (layoutUs) *layoutUs = la;
    if (renderUs) *renderUs = re;
    return c;
}

// Sub-phase of render: total us spent resolving the TRACKED-column status icon
// per rider since last read (the target of option-1 status caching).
__declspec(dllexport) double MXBMRP3_Test_StandingsTrackedUs() {
    return standingsReadTrackedUs();
}

// Experimental plugin worker thread: turn it on AFTER Startup (the flag is normally
// read once at init) and start the worker, so a test can drive callbacks through the
// off-thread offload path. Flush() blocks until the worker has drained everything
// queued so far, so the test can then read a deterministic snapshot(). See
// core/plugin_thread.{h,cpp} and tests/integration/tests/plugin_thread_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_PluginThreadEnable() {
    UiConfig::getInstance().setPluginThread(true);
    PluginThread::getInstance().start();
}
__declspec(dllexport) int MXBMRP3_Test_PluginThreadEnabled() {
    return PluginThread::getInstance().enabled() ? 1 : 0;
}
// Set ONLY the [Advanced] flag (as a live INI reload would) without starting/stopping
// the worker — so a test can exercise the game-thread reconcileEnabled() switch that a
// RELOAD_CONFIG hotkey triggers, by then driving a draw().
__declspec(dllexport) void MXBMRP3_Test_SetPluginThreadFlag(int on) {
    UiConfig::getInstance().setPluginThread(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_PluginThreadFlush() {
    PluginThread::getInstance().flush();
}
// Fault injection: kill the worker with an exception that escapes threadMain(),
// so the abort self-heal (inline fallback + reconcileEnabled join/drain/latch)
// can be asserted. See plugin_thread_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_PluginThreadAbortWorker() {
    PluginThread::getInstance().testAbortWorker();
}
// Fault injection: make the worker take batches off the queue and discard them
// unrun, so flush()'s bounded wait (and the abandoned sentinel's lifetime) can be
// asserted deterministically. See plugin_thread_flush_test.cpp.
__declspec(dllexport) void MXBMRP3_Test_PluginThreadSwallowBatches(int on) {
    PluginThread::getInstance().testSwallowBatches(on != 0);
}
__declspec(dllexport) void MXBMRP3_Test_PluginThreadStop() {
    // Clear the flag too, so the game-thread reconcileEnabled() (in handleDraw) doesn't
    // immediately restart the worker on the next draw.
    UiConfig::getInstance().setPluginThread(false);
    PluginThread::getInstance().stop();
}
// Inject an artificial per-frame stall into the render build (produceFrame), to
// demonstrate the game-thread isolation: this cost is paid inside Draw in sync mode
// but on the worker in plugin-thread mode.
__declspec(dllexport) void MXBMRP3_Test_SetProduceDelayMs(int ms) {
    HudManager::testSetProduceDelayMs(ms);
}
// Render-load probe: emit `quads` extra synthetic primitives each frame. type:
// 0=solid-fill quad, 1=sprite (textured) quad, 2=text string. fullscreen!=0 makes
// the quads full-screen (fill-rate) vs tiny (submit). sprite: 0=cycle every
// registered sprite (a texture switch per quad), k=pin sprite k (sampling, no
// switching) -- the pair that separates those two costs. Lets a driver verify the
// emission plumbing and an in-game sweep measure the engine's per-primitive cost.
// See tools/probetheme/README.md for the run matrix.
__declspec(dllexport) void MXBMRP3_Test_SetRenderProbe(int quads, int type, int fullscreen,
                                                       int sprite) {
    UiConfig::getInstance().setRenderProbeQuads(quads);
    UiConfig::getInstance().setRenderProbeType(type);
    UiConfig::getInstance().setRenderProbeFullscreen(fullscreen != 0);
    UiConfig::getInstance().setRenderProbeSprite(sprite);
}
// XInput I/O thread: stop it so a test can inspect the rumble command setVibration
// posts without the worker draining it; drive index/vibration; read the pending post.
// Proves the send policy + quantization survive moving XInputSetState off-thread.
__declspec(dllexport) void MXBMRP3_Test_XInputStopIo() {
    XInputReader::getInstance().stopIoThread();
}
__declspec(dllexport) void MXBMRP3_Test_XInputSetIndex(int idx) {
    XInputReader::getInstance().setControllerIndex(idx);
}
__declspec(dllexport) void MXBMRP3_Test_XInputVibrate(float left, float right) {
    XInputReader::getInstance().setVibration(left, right);
}
__declspec(dllexport) int MXBMRP3_Test_XInputConsumePending(int* left8, int* right8, int* idx) {
    int a = 0, b = 0, c = 0;
    bool has = XInputReader::getInstance().testConsumePendingRumble(a, b, c);
    if (left8) *left8 = a;
    if (right8) *right8 = b;
    if (idx) *idx = c;
    return has ? 1 : 0;
}
// --- Rumble effect math seam. updateRumbleFromTelemetry() runs on every real
// RunTelemetry (the handler derives the spike/slip inputs from the raw frame),
// but its outputs — the per-channel contributions and the combined motor values,
// i.e. the numbers users tune in the Rumble tab — are in-game-only (the rumble
// graph + the motor feed), never in /api/state. These read them back, and flip
// the per-bike-profile mode / reload the profile JSON without a plugin restart,
// so rumble_effect_test can pin the math and the JSON load/fallback. The send
// POLICY stays pinned separately by xinput_thread_test. ---
__declspec(dllexport) void MXBMRP3_Test_RumbleSetPerBike(int on) {
    XInputReader::getInstance().getGlobalRumbleConfig().usePerBikeEffects = on != 0;
}
// Master enable. Off (the default), updateRumbleFromTelemetry still computes
// every per-channel value (the graph stays live) but feeds the motors 0 — so a
// test asserting the COMBINED heavy/light values must switch it on.
__declspec(dllexport) void MXBMRP3_Test_RumbleSetEnabled(int on) {
    XInputReader::getInstance().getGlobalRumbleConfig().enabled = on != 0;
}
__declspec(dllexport) void MXBMRP3_Test_RumbleLoadProfiles(const char* savePath) {
    RumbleProfileManager::getInstance().load(savePath ? savePath : "");
}
__declspec(dllexport) int MXBMRP3_Test_RumbleHasProfile() {
    return RumbleProfileManager::getInstance().hasProfileForCurrentBike() ? 1 : 0;
}
__declspec(dllexport) void MXBMRP3_Test_RumbleChannels(
        float* heavy, float* light, float* susp, float* suspRear, float* spin,
        float* lock, float* lockRear, float* wheelie, float* rpm, float* slide,
        float* surface, float* steer) {
    const XInputReader& xi = XInputReader::getInstance();
    if (heavy)    *heavy    = xi.getLastHeavyMotor();
    if (light)    *light    = xi.getLastLightMotor();
    if (susp)     *susp     = xi.getLastSuspensionRumble();
    if (suspRear) *suspRear = xi.getLastSuspensionRumbleRear();
    if (spin)     *spin     = xi.getLastWheelspinRumble();
    if (lock)     *lock     = xi.getLastLockupRumble();
    if (lockRear) *lockRear = xi.getLastLockupRumbleRear();
    if (wheelie)  *wheelie  = xi.getLastWheelieRumble();
    if (rpm)      *rpm      = xi.getLastRpmRumble();
    if (slide)    *slide    = xi.getLastSlideRumble();
    if (surface)  *surface  = xi.getLastSurfaceRumble();
    if (steer)    *steer    = xi.getLastSteerRumble();
}
// Read the live PerformanceHud metrics (fps / plugin ms / plugin %), to assert they
// stay live in plugin-thread mode (the worker publishes them, not DrawHandler).
__declspec(dllexport) void MXBMRP3_Test_GetDebugMetrics(float* fps, float* pluginMs, float* pct) {
    const auto& m = PluginData::getInstance().getDebugMetrics();
    if (fps) *fps = m.currentFps;
    if (pluginMs) *pluginMs = m.pluginTimeMs;
    if (pct) *pct = m.pluginPercent;
}

#if GAME_HAS_FMX
// --- FMX trick-detection seam. FmxManager's whole state machine runs on the wall
// clock (dt integration, the 0.5s airborne/ground debounces, the 0.75s landing
// grace, the 2s chain window), so back-to-back headless callbacks give dt≈0 and
// nothing ever advances. The injectable clock (Fmx::clockNow) lets a test step
// simulated time with each telemetry frame; this hook sets it (µs; -1 restores
// the real clock). ---
__declspec(dllexport) void MXBMRP3_Test_FmxSetNowUs(long long us) {
    Fmx::testSetNowUs(us);
}

// Read the FMX score/chain/active-trick state in one call. The FMX score is
// in-game-only (never in /api/state), so detection results are read directly.
// lastTrickType is the most recent trick banked into the chain — or, once the
// chain has completed/failed (which moves the chain into the end animation),
// the final type snapshotted there. Any out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_FmxState(int* sessionScore, int* tricksCompleted,
        int* tricksFailed, int* chainCount, int* chainScore,
        int* activeState, int* activeType, int* lastTrickType) {
    const FmxManager& fmx = FmxManager::getInstance();
    const Fmx::FmxScore& score = fmx.getScore();
    if (sessionScore)    *sessionScore    = score.sessionScore;
    if (tricksCompleted) *tricksCompleted = score.tricksCompleted;
    if (tricksFailed)    *tricksFailed    = score.tricksFailed;
    if (chainCount)      *chainCount      = score.chainCount;
    if (chainScore)      *chainScore      = score.chainScore;
    if (activeState)     *activeState     = static_cast<int>(fmx.getActiveTrick().state);
    if (activeType)      *activeType      = static_cast<int>(fmx.getActiveTrick().type);
    if (lastTrickType) {
        const auto& chain = fmx.getChainTricks();
        *lastTrickType = static_cast<int>(chain.empty()
            ? fmx.getChainEndAnimation().finalType
            : chain.back().type);
    }
}
#endif

// --- Stats odometer seam. Distance integrates speed over the WALL-CLOCK gap
// between telemetry calls, so the odometer test injects the clock (µs; -1
// restores the real one) to make each tick's dt — and the expected distance —
// exact. ---
__declspec(dllexport) void MXBMRP3_Test_StatsSetNowUs(long long us) {
    StatsManager::testSetNowUs(us);
}

// Read the live odometer state: the current bike's odometer + the session trip
// (both meters), plus the ~100m dirty-coalescing internals (distance accumulated
// since the last dirty mark, and the dirty flag itself) — neither observable
// through the stats file, because a save only ever happens off-track. Any
// out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_StatsOdometerState(double* bikeOdometer,
        double* sessionTrip, double* unsavedDistance, int* dirty) {
    const StatsManager& sm = StatsManager::getInstance();
    if (bikeOdometer)    *bikeOdometer    = sm.getOdometerForCurrentBike();
    if (sessionTrip)     *sessionTrip     = sm.getSessionTripDistance();
    if (unsavedDistance) *unsavedDistance = sm.testUnsavedDistance();
    if (dirty)           *dirty           = sm.testIsDirty() ? 1 : 0;
}

// Force a stats save (the same save() the RunStop/RunDeinit leave-track flush
// calls; a no-op when clean). Lets a test establish a known-clean baseline
// before asserting the dirty-coalescing behaviour.
// The crash widget's streaming tally: read it, and drive the SAME reset entry
// point the Reset button and the hotkey both call, so a test exercises the
// widget's path rather than reaching past it into StatsManager.
__declspec(dllexport) int MXBMRP3_Test_CrashTally(int doReset) {
    if (doReset) HudManager::getInstance().getCrashWidget().resetCounter();
    return StatsManager::getInstance().getCrashTally();
}

__declspec(dllexport) void MXBMRP3_Test_StatsSave() {
    StatsManager::getInstance().save();
}

#if GAME_HAS_RECORDS_PROVIDER
// --- Records fetch/parse seam. The records fetch is user/auto-triggered network
// I/O whose response parsing was previously only testable live in-game. These
// hooks (a) run a canned response body through the REAL parse path and read the
// parsed records back (records_parse_test), and (b) arm a stubbed fetch worker
// (sleep + canned response, no network) so a test can hold a fetch in flight
// and pin the join contract: HudManager::clear() joins the fetch thread BEFORE
// nulling the cached HUD pointers the worker touches on completion (TimingHud). ---
//
// Parse `json` as `provider` (0=CBR, 1=MXB_RANKED) through the real parse path.
// Returns the parsed record count, or -1 on a parse error.
__declspec(dllexport) int MXBMRP3_Test_RecordsParse(int provider, const char* json) {
    RecordsHud& hud = HudManager::getInstance().getRecordsHud();
    if (!hud.testParseResponse(provider, json ? json : "")) return -1;
    return hud.testRecordCount();
}
// Current parsed-record count (readable independent of the last parse result).
__declspec(dllexport) int MXBMRP3_Test_RecordsCount() {
    return HudManager::getInstance().getRecordsHud().testRecordCount();
}
// Copy one parsed record out (strings truncated to the caller's caps; any
// out-pointer may be null). Returns 1 if index is valid, 0 otherwise.
__declspec(dllexport) int MXBMRP3_Test_RecordsGet(int index,
        char* rider, int riderCap, char* bike, int bikeCap,
        int* laptime, int* s1, int* s2, int* s3, char* date, int dateCap) {
    RecordsHud::RecordEntry e;
    if (!HudManager::getInstance().getRecordsHud().testGetRecord(index, e)) return 0;
    auto copy = [](char* dst, int cap, const char* src) {
        if (!dst || cap <= 0) return;
        strncpy(dst, src, cap - 1);
        dst[cap - 1] = '\0';
    };
    copy(rider, riderCap, e.rider);
    copy(bike, bikeCap, e.bike);
    copy(date, dateCap, e.date);
    if (laptime) *laptime = e.laptime;
    if (s1) *s1 = e.sector1;
    if (s2) *s2 = e.sector2;
    if (s3) *s3 = e.sector3;
    return 1;
}
// Arm/disarm the fetch-worker stub (delayMs < 0 disarms): the worker sleeps,
// then completes with `response` through the normal parse/notify path.
__declspec(dllexport) void MXBMRP3_Test_RecordsSetFetchStub(int delayMs, const char* response) {
    RecordsHud::testSetFetchStub(delayMs, response ? response : "");
}
// Start a real fetch (same cooldown/state gate as the Compare button).
// Returns 1 if a fetch is now in flight, 0 if the gate refused it.
__declspec(dllexport) int MXBMRP3_Test_RecordsStartFetch() {
    RecordsHud& hud = HudManager::getInstance().getRecordsHud();
    hud.testStartFetch();
    return hud.testFetchState() == static_cast<int>(RecordsHud::FetchState::FETCHING) ? 1 : 0;
}
// Fetch state as int (0=IDLE, 1=FETCHING, 2=SUCCESS, 3=FETCH_ERROR).
__declspec(dllexport) int MXBMRP3_Test_RecordsFetchState() {
    return HudManager::getInstance().getRecordsHud().testFetchState();
}
#endif

#if GAME_HAS_RECORDER
// Callback-tape recorder: open a tape at an explicit path and finalize it. Lets a
// test record the live callback stream it drives, then replay the produced tape
// back (round-trip) to prove the in-plugin recorder writes a harness-readable tape.
// startRecording bypasses the [Recorder] enabled gate (beginSessionRecording is the
// gated entry point); the replayer skips the Startup/Shutdown events, so no
// recordStartup is needed here.
__declspec(dllexport) int MXBMRP3_Test_StartRecording(const char* path) {
    return EventRecorder::getInstance().startRecording(path ? path : "") ? 1 : 0;
}
__declspec(dllexport) void MXBMRP3_Test_StopRecording() {
    EventRecorder::getInstance().stopRecording();
}
#endif

// Crash-backtrace frame resolver, formatted exactly like a dashboard stack
// frame ("module+0xoffset"). Pins the null-frame contract: address 0 resolves
// to the canonical "unknown+0x0" and is NEVER attributed to the host EXE via
// GetModuleHandleExA's NULL-lpModuleName rule (which produced the
// "mxbikes.exe+0xfffffffec0000000" mislabels on the v1.27.7.44 dashboard —
// 0 minus the game's 0x140000000 base, wrapped). Also usable for sanity checks
// on real addresses (an in-module address must name that module).
__declspec(dllexport) void MXBMRP3_Test_ResolveFrame(unsigned long long addr,
                                                     char* out, int cap) {
    if (!out || cap <= 0) return;
    char mod[64];
    unsigned long long off = 0;
    CrashHandler::resolveModuleOffset(reinterpret_cast<void*>(addr),
                                      mod, sizeof(mod), &off);
    snprintf(out, static_cast<size_t>(cap), "%s+0x%llx", mod, off);
}

// --- Display-rider telemetry -------------------------------------------------
// The live bike/input telemetry PluginData holds for the display rider. Not in
// the /api/state snapshot (the overlay has no use for it), so this is the only
// way a headless test can assert what RaceVehicleData — the sole telemetry
// source while spectating or in a replay — actually landed. Any out-pointer may
// be null.
__declspec(dllexport) void MXBMRP3_Test_BikeTelemetry(float* speedometer, int* gear,
                                                      int* rpm, float* throttle,
                                                      float* frontBrake, float* roll,
                                                      int* valid) {
    const BikeTelemetryData& bike = PluginData::getInstance().getBikeTelemetry();
    const InputTelemetryData& in = PluginData::getInstance().getInputTelemetry();
    if (speedometer) *speedometer = bike.speedometer;
    if (gear)        *gear        = bike.gear;
    if (rpm)         *rpm         = bike.rpm;
    if (roll)        *roll        = bike.roll;
    if (valid)       *valid       = bike.isValid ? 1 : 0;
    if (throttle)    *throttle    = in.throttle;
    if (frontBrake)  *frontBrake  = in.frontBrake;
}

// --- Current-lap splits ------------------------------------------------------
// A rider's live current-lap split accumulators (-1 = not crossed yet). These
// drive the in-game timing/split display only and never reach /api/state, so
// without this hook a headless test cannot see them at all — which is what let
// run_split_test's "nothing happened" assertion pass a mutation that made
// RunSplit start recording splits. Returns 1 if the rider has current-lap data.
__declspec(dllexport) int MXBMRP3_Test_CurrentLapSplits(int raceNum, int* lapNum,
                                                        int* s1, int* s2, int* s3) {
    const CurrentLapData* d = PluginData::getInstance().getCurrentLapData(raceNum);
    if (lapNum) *lapNum = d ? d->lapNum : -1;
    if (s1) *s1 = d ? d->split1 : -1;
    if (s2) *s2 = d ? d->split2 : -1;
    if (s3) *s3 = d ? d->split3 : -1;
    return d ? 1 : 0;
}

// --- Spectate camera control -------------------------------------------------
// Post a camera-role request exactly as the director does, so a test can drive
// the real SpectateCameras callback and observe the index it selects. The role
// int is a SpectateHandler::CameraRole (== Cameras::Role).
__declspec(dllexport) void MXBMRP3_Test_RequestSpectateCamera(int role) {
    SpectateHandler::getInstance().requestSpectateCamera(
        static_cast<SpectateHandler::CameraRole>(role));
}

// Whether the broadcaster is on a hand-flown camera (Orbit / Free / Free-Roam).
// The director pauses entirely while this is set, so it is the observable that
// makes the manual-camera detection in SpectateCameras assertable.
__declspec(dllexport) int MXBMRP3_Test_ManualCameraActive() {
    return SpectateHandler::getInstance().isManualCameraActive() ? 1 : 0;
}

// Drop the per-session camera tracking so the next SpectateCameras call
// re-resolves the manual flag from scratch (the handler short-circuits when
// neither the selection nor the camera count changed).
__declspec(dllexport) void MXBMRP3_Test_ResetCameraTracking() {
    SpectateHandler::getInstance().resetCameraTracking();
}

} // extern "C"

#endif // MXBMRP3_TEST_BUILD
