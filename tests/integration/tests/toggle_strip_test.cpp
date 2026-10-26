// ============================================================================
// tests/integration/tests/toggle_strip_test.cpp
// The Toggles widget (hud/toggle_strip_widget.h): the Settings icon over every
// on/off switch in the settings sidebar, Global then Profile, as one strip shown
// with the cursor.
//
// What it must not get wrong, each a case below:
//  - It mirrors the sidebar: the same tabs in the same order, both sections, the
//    More groups' included, under Settings -- all but Updates. A hand-kept list would lose the next
//    HUD; the strip reads SettingsHud::tabToggles, which walks the tab registry.
//  - A click is the sidebar checkbox's click: it switches the HUD on the surface,
//    and switching it back restores it.
//  - Its Widgets icon switches the widgets off, but not the strip itself -- it
//    carries the way back, so it is exempt from the Widgets master like the
//    settings button.
//  - It shows only while the cursor does: idle mouse, no strip -- except a
//    reveal (entering the track), which shows the Settings icon alone.
//  - The idle cursor does not hide while it rests on the strip: a player reading
//    the icons would lose them from under the pointer.
//  - Settings opens and closes the menu; the Global Director switch is the old
//    camera button, on track too, and like it shows the director's state while
//    spectating: waiting, rider lock, off each in a colour of its own (the strip
//    used to show only green/red), named on hover.
//  - The hovered icon's name is bare text right-aligned against the strip, with
//    no plate behind it: hovering adds a string, never a quad. At the screen's
//    left edge, where it would not fit, it flips to the strip's right side.
//  - Resting the cursor on a switched-off HUD's icon draws that HUD as a dimmed
//    sample without switching it on (nothing saves it, its flag stays off), and
//    only until the cursor moves to another icon; passing over an icon shows
//    nothing; a HUD already on is no sample; a feature switch samples what it
//    shows (Achievements: the toast); a click on a sampled icon switches it on,
//    and a click that switches one off shows no preview until the cursor leaves.
//  - It replaced the settings button: a player who had hidden that button gets
//    the strip hidden on upgrade, in every profile it was hidden in.
//  - The caret under Settings folds the switches away, leaving Settings and the
//    caret -- the old single button -- and unfolds them. Expanded on a fresh
//    install; once folded it stays folded in every profile and across a reload.
//  - Unfolding fades the switches in as Motion's part and settles to the frame
//    Motion Off draws; with Motion Off it is that frame at once.
//  - Horizontal, it is the same strip along X: Settings first (left), the caret
//    after it, every icon on one line, a click still the sidebar's click, the
//    fold shortening it along its axis, and the hovered name under the icon.
//  - Dragged, only the pointer docks it: at the top or bottom edge it turns
//    horizontal flat against that edge, at a side edge vertical, with the
//    grabbed icon under the pointer; a corner and the middle keep the shape, so
//    a drag along an edge or across the screen never turns it twice. The shape
//    is saved per profile, a file without it (fresh install, upgrade) is
//    vertical, and a profile switch never carries it over.
//    A drag on the companion window docks it there too: one shape for both
//    windows, but only the dragged copy moves.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

const char* kSave = "Z:\\tmp\\mxbmrp3-tests\\toggle_strip\\";
const char* kFoldSave = "Z:\\tmp\\mxbmrp3-tests\\toggle_strip_fold\\";

// Wake the cursor away from the strip (screen centre) and let the strip build.
void wake(PluginHost& host) {
    host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
    host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
    host.draw();
}

// The row whose tab is `name`, or -1.
int rowNamed(PluginHost& host, const char* name) {
    const int n = host.toggleStripRow(0);
    for (int i = 0; i < n; ++i) {
        std::string s;
        host.toggleStripRow(i, nullptr, nullptr, &s);
        if (s == name) return i;
    }
    return -1;
}

void clickRow(PluginHost& host, int row) {
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.toggleStripRow(row, &x, &y) > row);
    host.clickAt(x, y);
}

}  // namespace

TEST_CASE("toggle strip: mirrors the sidebar's switches, both sections, More included") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    wake(host);
    const int n = host.toggleStripRow(0);
    REQUIRE(n > 10);
    std::string settings, last;
    host.toggleStripRow(0, nullptr, nullptr, &settings);
    host.toggleStripRow(n - 1, nullptr, nullptr, &last);
    CHECK(settings == "Settings");             // Settings first...
    const int rumble = rowNamed(host, "Rumble");
    const int director = rowNamed(host, "Director");
    const int widgets = rowNamed(host, "Widgets");
    CHECK(rumble > 0);                         // ...then the Global switches...
    CHECK(director > rumble);                  // (sidebar order)
    CHECK(widgets > director);                 // ...then the Profile section's
    CHECK(rowNamed(host, "Dash") == widgets + 1);
    CHECK(rowNamed(host, "Map") == widgets + 2);
    CHECK(last == "Charts");       // the More group's last tab: More is inline
    CHECK(rowNamed(host, "General") < 0);      // no toggle on the row: not in the strip
    CHECK(rowNamed(host, "Appearance") < 0);
    CHECK(rowNamed(host, "Updates") < 0);      // a setting, not a switch to flip mid-session
    host.injectMouse(false);
}

TEST_CASE("toggle strip: a click switches the HUD, a second click switches it back") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    const char* standings = PluginHost::hudName(PluginHost::HUD_STANDINGS);
    REQUIRE(host.setHudVisible(standings, true));
    wake(host);
    const int row = rowNamed(host, "Standings");
    REQUIRE(row >= 0);

    clickRow(host, row);
    CHECK(host.hudVisible(standings) == 0);
    clickRow(host, row);
    CHECK(host.hudVisible(standings) == 1);
    // Back on: the cases share a save dir, and the next one loads this one's INI.
    REQUIRE(host.setHudVisibleBoth("toggle_strip_widget", true));
    host.injectMouse(false);
}

TEST_CASE("toggle strip: its Widgets switch hides the widgets but not the strip") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasScreenEdges()),
                    "build without the hooks");
    host.setWidgetsEnabled(true);
    wake(host);
    const int row = rowNamed(host, "Widgets");
    REQUIRE(row >= 0);

    auto widgetsOn = [&]() {
        bool on = false;
        host.toggleStripRow(row, nullptr, nullptr, nullptr, &on);
        return on;
    };
    REQUIRE(widgetsOn());
    clickRow(host, row);
    host.draw();
    CHECK_FALSE(widgetsOn());
    // The strip is still there to switch them back on...
    REQUIRE(host.toggleStripRow(0) > row);
    clickRow(host, row);
    host.draw();
    CHECK(widgetsOn());
    host.injectMouse(false);
}

TEST_CASE("toggle strip: shown only while the cursor is") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    wake(host);
    REQUIRE(host.toggleStripRow(0) > 0);
    // No mouse: InputManager reports no cursor to show (the same answer its idle
    // timeout gives), and the strip goes with it. Any reveal a Draw gap started
    // is ended first, so the case does not race its 4 s.
    host.injectMouse(false);
    host.toggleStripReveal(0);
    host.draw(); host.draw();
    CHECK(host.toggleStripRow(0) == 0);
    // Entering the track reveals the controls alone, not the switches: the way
    // into the menu, findable without moving the mouse.
    host.toggleStripReveal(4000);
    host.draw();
    REQUIRE(host.toggleStripRow(0) == 1);
    std::string first;
    host.toggleStripRow(0, nullptr, nullptr, &first);
    CHECK(first == "Settings");
    host.toggleStripReveal(0);
    host.draw();
    CHECK(host.toggleStripRow(0) == 0);
    wake(host);
    CHECK(host.toggleStripRow(0) > 1);
    host.injectMouse(false);
}


TEST_CASE("toggle strip: the idle cursor stays while it rests on the strip") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    wake(host);
    const int row = rowNamed(host, "Standings");
    REQUIRE(row >= 0);
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.toggleStripRow(row, &x, &y) > row);
    // On a row: past the timeout, the cursor and the whole strip stay.
    host.injectMouse(true, x, y, 0); host.draw(); host.draw();
    REQUIRE(host.toggleStripHoverRow() == row);
    host.cursorIdle();
    host.draw(); host.draw();
    CHECK(host.toggleStripRow(0) > 1);
    CHECK(host.toggleStripHoverRow() == row);
    // Off the strip, the same timeout hides both (a reveal a Draw gap started is
    // ended first, or the Settings icon stays for its 4 s).
    host.injectMouse(true, 0.5f, 0.5f, 0); host.draw(); host.draw();
    CHECK(host.toggleStripHoverRow() == -1);
    host.toggleStripReveal(0);
    host.cursorIdle();
    host.draw(); host.draw();
    CHECK(host.toggleStripRow(0) == 0);
    host.injectMouse(false);
}
TEST_CASE("toggle strip: hovering an icon marks its row") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    wake(host);
    CHECK(host.toggleStripHoverRow() == -1);
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.toggleStripRow(2, &x, &y) > 2);
    const char* hud = "toggle_strip_widget";
    const size_t quadsBefore = host.hudQuadRects(hud).size();
    host.injectMouse(true, x, y, 0); host.draw(); host.draw();
    CHECK(host.toggleStripHoverRow() == 2);

    // The name: no plate behind it, its right edge just left of the strip.
    const std::vector<PluginHost::QuadRect> quads = host.hudQuadRects(hud);
    CHECK(quads.size() == quadsBefore);
    if (host.hasStringRows() && !quads.empty()) {
        double stripLeft = quads[0].l;
        for (const PluginHost::QuadRect& q : quads) stripLeft = q.l < stripLeft ? q.l : stripLeft;
        std::string name;
        REQUIRE(host.toggleStripRow(2, nullptr, nullptr, &name) > 2);
        bool found = false;
        for (const PluginHost::StringRow& r : host.hudStringRows(hud)) {
            if (r.text != name) continue;
            found = true;
            CHECK(r.x < stripLeft);
            CHECK(r.x > stripLeft - 0.03);   // within a cell or so: hugs the strip
        }
        CHECK(found);
    }
    host.injectMouse(false);
}

TEST_CASE("toggle strip: hovering a switched-off HUD's icon shows it as a sample") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasHoverSample() &&
                     host.hasSurfaceRouting()), "build without the hooks");
    const char* standings = PluginHost::hudName(PluginHost::HUD_STANDINGS);
    const char* map = PluginHost::hudName(PluginHost::HUD_MAP);
    REQUIRE(host.setHudVisible(standings, false));
    REQUIRE(host.setHudVisible(map, true));
    wake(host);
    const int sRow = rowNamed(host, "Standings");
    const int mRow = rowNamed(host, "Map");
    const int setRow = rowNamed(host, "Settings");
    REQUIRE((sRow >= 0 && mRow >= 0 && setRow >= 0));
    // A rest on the icon: past ToggleStripWidget::HOVER_PREVIEW_DELAY_MS (250).
    auto hover = [&](int row) {
        float x = 0.0f, y = 0.0f;
        REQUIRE(host.toggleStripRow(row, &x, &y) > row);
        host.injectMouse(true, x, y, 0); host.draw(); host.draw();
        Sleep(300);
        host.draw(); host.draw();
    };
    auto frameQuads = [&] { return host.surfaceFrameStats().gameQuads; };

    hover(setRow);
    const int before = frameQuads();
    const double alphaOff = host.gameFrameAlphaSum();
    CHECK(host.hudSampled(standings) == 0);

    // Passing over it shows nothing yet.
    {
        float x = 0.0f, y = 0.0f;
        REQUIRE(host.toggleStripRow(sRow, &x, &y) > sRow);
        host.injectMouse(true, x, y, 0); host.draw(); host.draw();
        CHECK(host.hudSampled(standings) == 0);
    }
    // Resting on the off HUD's icon: drawn, but still switched off.
    hover(sRow);
    CHECK(host.hudSampled(standings) == 1);
    CHECK(host.hudVisible(standings) == 0);
    CHECK(frameQuads() > before);
    const double alphaSample = host.gameFrameAlphaSum();

    // Dimmed: its quads carry half the alpha they do switched on.
    REQUIRE(host.setHudVisible(standings, true));
    hover(setRow);
    const double alphaOn = host.gameFrameAlphaSum();
    REQUIRE(host.setHudVisible(standings, false));
    REQUIRE(alphaOn > alphaOff);
    CHECK((alphaSample - alphaOff) / (alphaOn - alphaOff) == doctest::Approx(0.5).epsilon(0.05));

    // Drawn is not switched on: a save with a sample up does not count it as
    // tried toward Tyre Kicker (ExplorationStats::observeSettings).
    const int lRow = rowNamed(host, "Lap Log");
    REQUIRE(lRow >= 0);
    const double tried = host.achievementValue("tyre_kicker");
    hover(lRow);
    REQUIRE(host.hudSampled("lap_log_hud") == 1);
    host.save();
    CHECK(host.achievementValue("tyre_kicker") == doctest::Approx(tried));

    // A HUD that is on is no sample; moving on takes the last one down.
    hover(mRow);
    CHECK(host.hudSampled(map) == 0);
    CHECK(host.hudSampled(standings) == 0);
    hover(setRow);
    CHECK(frameQuads() == before);

    // A HUD that is empty until something happens previews a sample of it:
    // FMX, with no trick landed, a sample chain.
    const int fRow = rowNamed(host, "FMX");
    if (fRow >= 0 && host.hasStringRows()) {   // FMX is MX Bikes and GP Bikes only
        hover(fRow);
        REQUIRE(host.hudSampled("fmx_hud") == 1);
        bool sampleChain = false;
        for (const PluginHost::StringRow& r : host.hudStringRows("fmx_hud")) sampleChain |= r.text == "Backflip";
        CHECK(sampleChain);
    }

    // A feature switch samples what it shows: Achievements, with toasts off, the toast.
    const int aRow = rowNamed(host, "Achievements");
    REQUIRE(aRow >= 0);
    {
        bool on = true;
        host.toggleStripRow(aRow, nullptr, nullptr, nullptr, &on);
        if (on) { clickRow(host, aRow); host.draw(); host.draw(); }
        hover(setRow);   // off the icon: a click there holds its preview back
        hover(aRow);
        CHECK(host.hudSampled("achievement_widget") == 1);
        CHECK(frameQuads() > before);   // the sample card, though toasts are off
        clickRow(host, aRow);   // back on, for the cases after this one
        host.draw();
    }

    // A click on the sampled icon switches it on for real.
    hover(sRow);
    clickRow(host, sRow);
    host.draw();
    CHECK(host.hudVisible(standings) == 1);
    CHECK(host.hudSampled(standings) == 0);
    // Switched off from the strip, it is gone, not faint, until the cursor
    // leaves the icon; resting on it afresh previews it again.
    clickRow(host, sRow);
    host.draw(); Sleep(300); host.draw(); host.draw();
    CHECK(host.hudVisible(standings) == 0);
    CHECK(host.hudSampled(standings) == 0);
    hover(setRow);
    hover(sRow);
    CHECK(host.hudSampled(standings) == 1);
    REQUIRE(host.setHudVisible(standings, true));
    host.injectMouse(false);
}

TEST_CASE("toggle strip: at the left screen edge the name flips to the right") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasStringRows()),
                    "build without the hooks");
    const char* hud = "toggle_strip_widget";
    REQUIRE(host.setHudOffset(hud, 0.0f, 0.05f));
    wake(host);
    float x = 0.0f, y = 0.0f;
    std::string name;
    REQUIRE(host.toggleStripRow(2, &x, &y, &name) > 2);
    host.injectMouse(true, x, y, 0); host.draw(); host.draw();
    REQUIRE(host.toggleStripHoverRow() == 2);

    const std::vector<PluginHost::QuadRect> quads = host.hudQuadRects(hud);
    REQUIRE(!quads.empty());
    double stripRight = quads[0].r;
    for (const PluginHost::QuadRect& q : quads) stripRight = q.r > stripRight ? q.r : stripRight;
    bool found = false;
    for (const PluginHost::StringRow& r : host.hudStringRows(hud)) {
        if (r.text != name) continue;
        found = true;
        CHECK(r.x > stripRight);
        CHECK(r.x < stripRight + 0.03);   // within a cell or so: hugs the strip
    }
    CHECK(found);
    host.injectMouse(false);
}

TEST_CASE("toggle strip: switched off, it draws nothing and takes no click") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    const char* standings = PluginHost::hudName(PluginHost::HUD_STANDINGS);
    REQUIRE(host.setHudVisible(standings, true));
    wake(host);
    const int row = rowNamed(host, "Standings");
    REQUIRE(row >= 0);
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.toggleStripRow(row, &x, &y) > row);

    REQUIRE(host.setHudVisibleBoth("toggle_strip_widget", false));
    wake(host);
    CHECK(host.toggleStripRow(0) == 0);
    host.clickAt(x, y);   // where the Standings icon was
    CHECK(host.hudVisible(standings) == 1);
    // Back on: the cases share a save dir, and the next one loads this one's INI.
    REQUIRE(host.setHudVisibleBoth("toggle_strip_widget", true));
    host.injectMouse(false);
}

TEST_CASE("toggle strip: a click on the companion window switches the companion's HUD") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasForceSurface()),
                    "build without the hooks");
    host.stSetCompanionVisible(true);
    const char* standings = PluginHost::hudName(PluginHost::HUD_STANDINGS);
    REQUIRE(host.setHudVisible(standings, true));
    host.forceActiveSurface(1);
    wake(host);
    const int row = rowNamed(host, "Standings");
    REQUIRE(row >= 0);

    clickRow(host, row);
    const PluginHost::SurfaceState s = host.stCompanionState();
    CHECK(s.companionVisible == 0);   // the surface clicked on...
    CHECK(s.gameVisible == 1);        // ...and only that one
    host.forceActiveSurface(0);
    host.injectMouse(false);
}

TEST_CASE("toggle strip: its Settings icon opens and closes the menu") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    host.showSettings(false);
    wake(host);
    const int row = rowNamed(host, "Settings");
    REQUIRE(row == 0);
    clickRow(host, row);
    CHECK(host.settingsVisible());
    host.draw();
    clickRow(host, row);
    CHECK_FALSE(host.settingsVisible());
    host.injectMouse(false);
}

TEST_CASE("toggle strip: its Director switch is the old camera button, on track too") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    host.directorSetEnabled(false);
    host.injectMouse(true, 0.45f, 0.5f, 0); host.drawWithState(0);
    host.injectMouse(true, 0.5f, 0.5f, 0); host.drawWithState(0);
    const int row = rowNamed(host, "Director");
    REQUIRE(row > 0);
    bool on = true;
    host.toggleStripRow(row, nullptr, nullptr, nullptr, &on);
    CHECK_FALSE(on);
    clickRow(host, row);
    CHECK(host.directorEnabled());
    host.drawWithState(0);
    host.toggleStripRow(row, nullptr, nullptr, nullptr, &on);
    CHECK(on);
    clickRow(host, row);
    CHECK_FALSE(host.directorEnabled());
    host.injectMouse(false);
}

// The colour of the strip's switch at row `row`, as text: the harness has no
// icons folder, so the case runs with UI icons off, where each switch is the
// first two letters of its name in the icon's colour.
unsigned long rowTextColor(PluginHost& host, int row, const char* text) {
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.toggleStripRow(row, &x, &y) > row);
    const char* hud = "toggle_strip_widget";
    const std::vector<PluginHost::StringRow> rows = host.hudStringRows(hud);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text == text && std::abs(rows[i].y - y) < 0.03)
            return host.stringColor(hud, static_cast<int>(i));
    }
    FAIL("no '" << text << "' at the row");
    return 0;
}

TEST_CASE("toggle strip: the Director icon shows the director's state while spectating") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasStringRows() &&
                     host.hasInkHooks()), "build without the hooks");
    host.writeSettingsFile(kSave, "[Settings]\nversion=11\n\n[Display]\ntitleIcons=0\n");
    host.loadSettings(kSave);
    host.directorSetEnabled(true);
    host.injectMouse(true, 0.45f, 0.5f, 0); host.drawWithState(0);
    host.injectMouse(true, 0.5f, 0.5f, 0); host.drawWithState(0);
    const int row = rowNamed(host, "Director");
    REQUIRE(row > 0);
    const unsigned long onTrack = rowTextColor(host, row, "Di");   // on: plain green

    // Spectating with nobody to follow yet: waiting.
    host.drawWithState(1); host.drawWithState(1);
    const unsigned long waiting = rowTextColor(host, row, "Di");
    std::string name;
    host.toggleStripRow(row, nullptr, nullptr, &name);
    CHECK(name == "Director: waiting");
    CHECK(waiting != onTrack);

    // The rider lock: its own colour, and named.
    host.directorToggleLock();
    host.drawWithState(1); host.drawWithState(1);
    const unsigned long locked = rowTextColor(host, row, "Di");
    host.toggleStripRow(row, nullptr, nullptr, &name);
    CHECK(name == "Director: locked");
    CHECK(locked != onTrack);
    CHECK(locked != waiting);
    host.directorToggleLock();

    // Off while spectating: red, the plain name.
    host.directorSetEnabled(false);
    host.drawWithState(1); host.drawWithState(1);
    const unsigned long off = rowTextColor(host, row, "Di");
    host.toggleStripRow(row, nullptr, nullptr, &name);
    CHECK(name == "Director");
    CHECK(off != onTrack);
    CHECK(off != waiting);
    CHECK(off != locked);

    // Back on track: green again, whatever the director was doing.
    host.directorSetEnabled(true);
    host.drawWithState(0); host.drawWithState(0);
    CHECK(rowTextColor(host, row, "Di") == onTrack);
    host.toggleStripRow(row, nullptr, nullptr, &name);
    CHECK(name == "Director");
    host.directorSetEnabled(false);
    host.injectMouse(false);
}

// Last: it rewrites the shared save dir's INI.
TEST_CASE("toggle strip: a hidden settings button hides the strip on upgrade") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    // A file from before the strip: the button's section, no strip section.
    host.writeSettingsFile(kSave, "[Settings]\nversion=10\n\n[SettingsButtonWidget]\nvisible=0\n");
    host.loadSettings(kSave);
    wake(host);
    CHECK(host.toggleStripRow(0) == 0);

    host.writeSettingsFile(kSave, "[Settings]\nversion=10\n\n[SettingsButtonWidget]\nvisible=1\n");
    host.loadSettings(kSave);
    wake(host);
    CHECK(host.toggleStripRow(0) > 2);

    // Each surface carries its own switch: a gear hidden in-game but kept on the
    // companion window (or the reverse) keeps that split.
    host.writeSettingsFile(kSave, "[Settings]\nversion=10\n\n[SettingsButtonWidget]\nvisible=0\n"
                                  "companionConfigured=1\ncompanionVisible=1\n");
    host.loadSettings(kSave);
    PluginHost::SurfaceState st = host.toggleStripSurfaces();
    CHECK(st.gameVisible == 0);
    CHECK(st.configured == 1);
    CHECK(st.companionVisible == 1);
    host.writeSettingsFile(kSave, "[Settings]\nversion=10\n\n[SettingsButtonWidget]\nvisible=1\n"
                                  "companionConfigured=1\ncompanionVisible=0\n");
    host.loadSettings(kSave);
    st = host.toggleStripSurfaces();
    CHECK(st.gameVisible == 1);
    CHECK(st.configured == 1);
    CHECK(st.companionVisible == 0);

    // The strip's own section wins once there is one: the first save ends the
    // migration.
    host.writeSettingsFile(kSave, "[Settings]\nversion=10\n\n[SettingsButtonWidget]\nvisible=0\n"
                                  "\n[ToggleStripWidget]\nvisible=1\n");
    host.loadSettings(kSave);
    wake(host);
    CHECK(host.toggleStripRow(0) > 2);
    host.injectMouse(false);
}

// A button hidden in some profiles only, one of them not visited before the
// first save. That save drops the button's sections (nothing reads them any
// more), so the carry-over must already sit in every profile's settings by then:
// done when a profile was applied, the unvisited one came back with the strip on.
TEST_CASE("toggle strip: a button hidden in a profile not visited before the first save stays hidden") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE(host.hasToggleStrip(), "build without the hooks");
    host.writeSettingsFile(kSave, "[Settings]\nversion=10\n\n[Profiles]\nactiveProfile=0\n\n"
                                  "[SettingsButtonWidget]\nvisible=1\n\n"
                                  "[SettingsButtonWidget:Race]\nvisible=0\n\n"
                                  "[SettingsButtonWidget:Spectate]\nvisible=0\n");
    host.loadSettings(kSave);
    REQUIRE(host.activeProfile() == 0);
    CHECK(host.toggleStripSurfaces().gameVisible == 1);   // Practice: shown
    host.switchProfile(2);                                // Race
    CHECK(host.toggleStripSurfaces().gameVisible == 0);
    REQUIRE(host.save());                                 // Spectate still unvisited
    const std::string saved = ini::readFile(std::string(kSave) + "mxbmrp3\\mxbmrp3_settings.ini");
    REQUIRE(saved.find("[ToggleStripWidget]") != std::string::npos);
    REQUIRE(saved.find("[SettingsButtonWidget") == std::string::npos);   // the button is gone

    host.loadSettings(kSave);                             // what that save wrote
    host.switchProfile(3);                                // Spectate, first visit
    CHECK_MESSAGE(host.toggleStripSurfaces().gameVisible == 0, "Spectate's hidden button was lost");
    host.switchProfile(1);
    CHECK(host.toggleStripSurfaces().gameVisible == 1);   // Qualify never hid it
    host.switchProfile(0);
}

TEST_CASE("toggle strip: the caret folds the switches away in every profile, and unfolds them") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kFoldSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    host.writeSettingsFile(kFoldSave, "[Settings]\nversion=11\n");   // a fresh install's file
    host.loadSettings(kFoldSave);
    wake(host);
    REQUIRE(host.toggleStripRow(0) > 10);                  // fresh install: expanded
    REQUIRE(rowNamed(host, "Collapse") == 1);              // right under Settings

    clickRow(host, 1);
    host.draw();
    CHECK(host.toggleStripRow(0) == 2);                    // Settings + the caret
    std::string name;
    host.toggleStripRow(1, nullptr, nullptr, &name);
    CHECK(name == "Expand");
    host.switchProfile(2);                                 // one choice for every profile
    wake(host);
    CHECK(host.toggleStripRow(0) == 2);
    REQUIRE(host.save());
    const std::string ini = std::string(kFoldSave) + "mxbmrp3\\mxbmrp3_settings.ini";
    CHECK(ini::readFile(ini).find("togglesCollapsed=1") != std::string::npos);
    host.loadSettings(kFoldSave);                          // survives a reload
    wake(host);
    CHECK(host.toggleStripRow(0) == 2);

    clickRow(host, 1);
    host.draw();
    CHECK(host.toggleStripRow(0) > 10);
    REQUIRE(host.save());
    CHECK(ini::readFile(ini).find("togglesCollapsed=0") != std::string::npos);
    host.switchProfile(0);
    host.injectMouse(false);
}

TEST_CASE("toggle strip: unfolding fades the switches in with Motion, at once with Motion Off") {
    constexpr int OFF = 0, NORMAL = 2;
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kFoldSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasMotion()),
                    "build without the hooks");
    host.writeSettingsFile(kFoldSave, "[Settings]\nversion=11\n");
    host.loadSettings(kFoldSave);
    host.showAllHuds(false);
    host.showSettings(false);
    long long now = 1000000000ll;
    host.setMotion(OFF);
    host.setMotionNowUs(now);
    wake(host);
    float x = 0.0f, y = 0.0f;
    REQUIRE(rowNamed(host, "Collapse") == 1);
    REQUIRE(host.toggleStripRow(1, &x, &y) > 10);
    // Each frame on Motion's clock, the cursor resting on the caret throughout.
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) {
            now += 16000;
            host.setMotionNowUs(now);
            host.injectMouse(true, x, y, 0);
            host.draw();
        }
        return std::make_pair(host.gameFrameHash(), host.lastGameQuads());
    };

    host.clickAt(x, y);                                    // fold
    frames(30);
    host.clickAt(x, y);                                    // unfold, Motion Off
    const auto off = frames(1);
    CHECK(frames(30) == off);                              // nothing left to settle: instant
    REQUIRE(host.toggleStripRow(0) > 10);

    host.clickAt(x, y);                                    // fold
    host.setMotion(NORMAL);
    frames(30);
    host.clickAt(x, y);                                    // unfold, Motion on
    const auto fading = frames(1);
    CHECK(fading.second == off.second);                    // every switch drawn...
    CHECK(fading.first != off.first);                      // ...still fading in
    CHECK(frames(30) == off);                              // and settled to the Off frame

    host.setMotion(OFF);
    host.injectMouse(false);
}

TEST_CASE("toggle strip: horizontal, the same strip along X, Settings first") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasToggleStripShape() &&
                     host.hasStringRows()), "build without the hooks");
    const char* hud = "toggle_strip_widget";
    REQUIRE(host.toggleStripHorizontal(1) == 1);
    REQUIRE(host.setHudOffset(hud, 0.1f, 0.01f));
    wake(host);
    const int n = host.toggleStripRow(0);
    REQUIRE(n > 10);
    std::string name;
    host.toggleStripRow(0, nullptr, nullptr, &name);
    CHECK(name == "Settings");
    CHECK(rowNamed(host, "Collapse") == 1);
    float x0 = 0.0f, y0 = 0.0f;
    host.toggleStripRow(0, &x0, &y0);
    float prevX = x0;
    for (int i = 1; i < n; ++i) {
        float x = 0.0f, y = 0.0f;
        host.toggleStripRow(i, &x, &y);
        CHECK(std::fabs(y - y0) < 1e-4);   // one line
        CHECK(x > prevX);                  // left to right
        prevX = x;
    }

    // A click is still the sidebar's click.
    const int row = rowNamed(host, "Standings");
    REQUIRE(row > 1);
    bool before = false, after = false;
    host.toggleStripRow(row, nullptr, nullptr, nullptr, &before);
    clickRow(host, row);
    host.draw();
    host.toggleStripRow(row, nullptr, nullptr, nullptr, &after);
    CHECK(after != before);
    clickRow(host, row);
    host.draw();

    // The hovered name: under the icon, centred on it, no plate.
    float x = 0.0f, y = 0.0f;
    host.toggleStripRow(row, &x, &y, &name);
    host.injectMouse(true, x, y, 0); host.draw(); host.draw();
    REQUIRE(host.toggleStripHoverRow() == row);
    const std::vector<PluginHost::QuadRect> quads = host.hudQuadRects(hud);
    REQUIRE(!quads.empty());
    double stripBottom = quads[0].b;
    for (const PluginHost::QuadRect& q : quads) stripBottom = q.b > stripBottom ? q.b : stripBottom;
    bool found = false;
    for (const PluginHost::StringRow& r : host.hudStringRows(hud)) {
        if (r.text != name) continue;
        found = true;
        CHECK(r.y > stripBottom);
        CHECK(r.y < stripBottom + 0.04);   // within a cell or so: hugs the strip
        CHECK(r.x < x);                    // it starts left of the icon's centre...
        CHECK(r.x > x - 0.1);              // ...about half a short name away
    }
    CHECK(found);

    // Folded, it shortens along X and keeps Settings where it was.
    const int caret = rowNamed(host, "Collapse");
    clickRow(host, caret);
    host.draw();
    CHECK(host.toggleStripRow(0) == 2);
    float fx = 0.0f, fy = 0.0f;
    host.toggleStripRow(0, &fx, &fy);
    CHECK(std::fabs(fx - x0) < 1e-4);
    CHECK(std::fabs(fy - y0) < 1e-4);
    clickRow(host, 1);
    host.draw();
    CHECK(host.toggleStripRow(0) == n);
    host.toggleStripHorizontal(0);
    host.injectMouse(false);
}

TEST_CASE("toggle strip: the pointer docks it at an edge, the grabbed icon under it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasToggleStripShape()),
                    "build without the hooks");
    host.writeSettingsFile(kSave, "[Settings]\nversion=11\n");   // a fresh install's file
    host.loadSettings(kSave);
    // The dock decides only the shape: where it sits is the drag's, so it reaches
    // every edge (it once held one cell in, the last grid unit unreachable
    // whatever clamp and snap said).
    wake(host);
    REQUIRE(host.toggleStripHorizontal() == 0);                    // fresh install: vertical
    const PluginHost::LayoutCells cells = host.layoutCells();
    const int grab = 4;
    // The strip's drawn box.
    auto box = [&]() {
        const std::vector<PluginHost::QuadRect> q = host.hudQuadRects("toggle_strip_widget");
        PluginHost::QuadRect r = q.empty() ? PluginHost::QuadRect{} : q[0];
        for (const PluginHost::QuadRect& e : q) {
            if (e.r - e.l < 1e-6) continue;
            r.l = std::min(r.l, e.l); r.t = std::min(r.t, e.t);
            r.r = std::max(r.r, e.r); r.b = std::max(r.b, e.b);
        }
        return r;
    };
    float gx = 0.0f, gy = 0.0f;
    REQUIRE(host.toggleStripRow(grab, &gx, &gy) > grab);

    // Press on an icon, then walk the pointer, counting turns.
    int turns = 0;
    int shape = host.toggleStripHorizontal();
    host.injectMouse(true, gx, gy, 0); host.draw();
    host.injectMouse(true, gx, gy, 2); host.draw();
    auto walk = [&](float x1, float y1, int steps) {
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            host.injectMouse(true, gx + (x1 - gx) * t, gy + (y1 - gy) * t, 2);
            host.draw();
            const int now = host.toggleStripHorizontal();
            if (now != shape) { ++turns; shape = now; }
        }
        gx = x1; gy = y1;
    };
    // The grabbed icon is under the pointer: along the strip to within the snap
    // grid, across it within the strip (docked flat, the pointer can be nearer
    // the edge than the icon's centre).
    auto underPointer = [&]() {
        float x = 0.0f, y = 0.0f;
        host.toggleStripRow(grab, &x, &y);
        const bool horizontal = host.toggleStripHorizontal() == 1;
        const double along = horizontal ? std::fabs(x - gx) : std::fabs(y - gy);
        const double across = horizontal ? std::fabs(y - gy) : std::fabs(x - gx);
        INFO("icon " << x << "," << y << " pointer " << gx << "," << gy);
        CHECK(along <= (horizontal ? cells.cellW : cells.cellH) + 1e-4);
        CHECK(across <= 0.03);
        return along <= (horizontal ? cells.cellW : cells.cellH) + 1e-4 && across <= 0.03;
    };

    walk(0.5f, 0.5f, 8);                       // across the middle: no turn
    CHECK(turns == 0);
    CHECK(underPointer());
    walk(0.5f, 0.005f, 8);                     // the top edge: horizontal, flat on it
    CHECK(turns == 1);
    CHECK(shape == 1);
    CHECK(underPointer());
    {
        float x = 0.0f, y = 0.0f;
        host.toggleStripRow(0, &x, &y);
        CHECK(y < 0.03f);                      // against the top edge
        CHECK(box().t < 0.5 * cells.cellH);    // at the edge, not held a cell in
    }
    walk(0.3f, 0.005f, 8);                     // along it: still horizontal
    CHECK(turns == 1);
    CHECK(underPointer());
    walk(0.003f, 0.005f, 6);                   // into the corner: keeps the shape
    CHECK(turns == 1);
    walk(0.003f, 0.5f, 10);                    // down the left edge: vertical, flat on it
    CHECK(turns == 2);
    CHECK(shape == 0);
    CHECK(underPointer());
    {
        float x = 0.0f, y = 0.0f;
        host.toggleStripRow(0, &x, &y);
        CHECK(x < 0.02f);                      // against the left edge
        CHECK(box().l < 0.5 * cells.cellW);    // at the edge
    }
    walk(0.5f, 0.995f, 12);                    // to the bottom edge: horizontal
    CHECK(turns == 3);
    CHECK(shape == 1);
    CHECK(underPointer());
    CHECK(box().b > 1.0 - 0.5 * cells.cellH);   // at the edge
    walk(0.997f, 0.5f, 12);                    // the right edge: a column again...
    CHECK(turns == 4);
    CHECK(shape == 0);
    CHECK(box().r > 1.0 - 0.5 * cells.cellW);   // ...at the edge
    walk(0.5f, 0.995f, 12);                    // and back to the bottom
    CHECK(turns == 5);
    // Screen clamp keeps it on screen, flush on the edge it is docked at.
    REQUIRE(host.hasScreenEdges());
    host.setScreenClamping(true);
    walk(0.6f, 0.999f, 6);
    CHECK(turns == 5);
    CHECK(std::fabs(box().b - 1.0) < 1e-4);
    host.setScreenClamping(false);
    host.injectMouse(true, gx, gy, 0); host.draw();   // drop

    // Saved per profile with the position; another profile keeps its own.
    REQUIRE(host.save());
    const std::string iniPath = std::string(kSave) + "mxbmrp3\\mxbmrp3_settings.ini";
    const std::string text = ini::readFile(iniPath);
    CHECK(text.find("horizontal=1") != std::string::npos);
    host.loadSettings(kSave);                  // survives a reload
    wake(host);
    CHECK(host.toggleStripHorizontal() == 1);
    host.switchProfile(2);
    wake(host);
    CHECK(host.toggleStripHorizontal() == 0);
    host.switchProfile(0);
    wake(host);
    CHECK(host.toggleStripHorizontal() == 1);

    // A file from before it docked has no shape: vertical, whatever was drawn.
    host.writeSettingsFile(kSave, "[Settings]\nversion=11\n\n[ToggleStripWidget]\nvisible=1\n");
    host.loadSettings(kSave);
    wake(host);
    CHECK(host.toggleStripHorizontal() == 0);
    host.injectMouse(false);
}

TEST_CASE("toggle strip: a drag on the companion window docks it there, moving only that copy") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSave);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip() && host.hasToggleStripShape() &&
                     host.hasForceSurface()), "build without the hooks");
    host.writeSettingsFile(kSave, "[Settings]\nversion=11\n");
    host.loadSettings(kSave);
    host.stSetCompanionVisible(true);
    // The game copy where a fresh install has it: a column at the top right
    // (the DLL stays loaded between cases, so the last case's drag is still on it).
    REQUIRE(host.toggleStripHorizontal(0) == 0);
    REQUIRE(host.setHudOffset("toggle_strip_widget", 0.97f, 0.01f));
    float gameX = 0.0f, gameY = 0.0f;
    wake(host);
    REQUIRE(host.toggleStripRow(0, &gameX, &gameY) > 4);
    host.forceActiveSurface(1);
    wake(host);
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.toggleStripRow(4, &x, &y) > 4);
    host.injectMouse(true, x, y, 0); host.draw();
    host.injectMouse(true, x, y, 2); host.draw();
    for (int i = 1; i <= 8; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        host.injectMouse(true, x + (0.5f - x) * t, y + (0.995f - y) * t, 2);
        host.draw();
    }
    CHECK(host.toggleStripHorizontal() == 1);     // one shape for both windows
    float cx = 0.0f, cy = 0.0f;
    host.toggleStripRow(4, &cx, &cy);             // the companion copy, under the pointer
    CHECK(std::fabs(cx - 0.5f) < 0.02f);
    CHECK(cy > 0.95f);
    host.injectMouse(true, 0.5f, 0.995f, 0); host.draw();
    host.forceActiveSurface(0);
    wake(host);
    float gx = 0.0f, gy = 0.0f;
    host.toggleStripRow(0, &gx, &gy);
    CHECK(std::fabs(gy - gameY) < 0.02f);         // the game copy stayed at the top
    // ...turned into a row there, but pulled back on screen, its last icon too.
    const int n = host.toggleStripRow(0);
    REQUIRE(n > 4);
    float lx = 0.0f, ly = 0.0f;
    host.toggleStripRow(n - 1, &lx, &ly);
    CHECK(lx < 1.0f);
    CHECK(lx > gx);
    host.toggleStripHorizontal(0);
    host.injectMouse(false);
}
