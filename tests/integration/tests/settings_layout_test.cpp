// ============================================================================
// tests/integration/tests/settings_layout_test.cpp
// Characterization guard for the settings panel's emitted CLICK REGIONS.
//
// WHY THIS EXISTS. The settings menu is the one large render surface with no
// automated coverage: its output is quads and strings, and its click regions
// never reach /api/state. That made every layout edit a "looks right in-game"
// change — and region ORDER and TYPE are behaviour, because a click is resolved
// by hit-testing them in emission order. A converted control that emits its two
// arrow regions in the wrong order, or drops the row's tooltip region, is a
// silently broken control that still renders perfectly.
//
// It was written to pin the General tab across the conversion of its
// hand-rolled `< value >` blocks (PB Scope / Controller / Steam / Analytics) onto
// the shared SettingsLayoutContext::addCycleControl helper — captured against the
// PRE-conversion build, so it pins the original behaviour rather than describing
// the new code. That is the same technique blueflag_test.cpp used across the
// blue-flag refactor.
//
// WHAT IT ASSERTS. Two layers, deliberately. The first pins STRUCTURE (each
// control's tooltip row exists exactly once) and survives unrelated additions to
// the tab. The second is an exact GOLDEN of the whole emitted sequence, captured
// from the pre-conversion build: that is what actually proved the conversion
// changed nothing, and it catches a reordered or dropped region that the
// structural checks would miss. The golden legitimately changes whenever a
// control is added to this tab — re-bless it by reading the diff, not reflexively.
//
// WHAT IT DOES NOT ASSERT — read this before trusting a green run. The
// signature is region TYPE + tooltip id + string count, in emission order. It
// is not GEOMETRY: a region's x/y/width/height are absent, so a control whose
// click box moved or resized still matches a passing golden. That is a real gap
// for this conversion in particular, which changed how the value column's
// x-advance is computed (`cw * VALUE_WIDTH` -> `calculateMonospaceTextWidth`);
// those are arithmetically equivalent, verified by hand, but the test is not
// what verified it. Extending the signature to carry coordinates would close
// the gap at the cost of a golden that churns on every spacing tweak — hence
// the split, stated rather than assumed.
//
// Build-gated controls: Discord and Analytics are compiled out of
// MXBMRP3_TEST_BUILD (see game_config.h), and Steam's runtime is absent under
// Wine so its row renders DISABLED (label + arrows muted, no click regions) —
// which is itself the interesting case, since `enabled=false` is the path that
// must emit the tooltip row but no arrows.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> splitRegions(const std::string& sig) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t sep = sig.find(';', start);
        if (sep == std::string::npos) break;
        out.push_back(sig.substr(start, sep - start));
        start = sep + 1;
    }
    return out;
}

int countTooltipRows(const std::vector<std::string>& regions, const char* tooltipId) {
    const std::string want = std::string(":") + tooltipId;
    return static_cast<int>(std::count_if(regions.begin(), regions.end(),
        [&](const std::string& r) {
            return r.size() > want.size() && r.compare(r.size() - want.size(), want.size(), want) == 0;
        }));
}

// ClickRegion::Type's cardinality at the time the golden below was captured.
// Bumped deliberately, together with a re-captured golden.
//
// 182 -> 186: panel themes added THEME_PREV/THEME_NEXT (the Appearance tab's
// global theme cycle) and HUD_THEME_DOWN/UP (the per-HUD override). Re-blessed
// only after checking the diff was a PURE ordinal shift -- all 66 changed
// segments moved by exactly +4, every label identical, no pair added, removed
// or reordered. That check is the whole point of this guard; a golden that
// differs any other way is a real change to the panel's click surface.
//
// 190 -> 192: the Spotter tab added SPOTTER_ENABLED_TOGGLE and
// SPOTTER_SUBTITLES_TOGGLE. Golden diff checked the same way: one new
// "91:spotter" tab row after director, the trailing themed-control ordinals
// shifted by exactly +2, and strings dropped by one (the sidebar's "Global"
// header label was removed to pay for the new tab row — see the
// TAB_SECTION_GLOBAL branch in settings_hud_render.cpp).
//
// 192 -> 194: spotter cue packs added SPOTTER_PACK_PREV/NEXT (the Voice pack
// cycle). Same pure +2 trailing-ordinal shift, no row changes on this tab.
// strings 120 -> 119: the web server's three-state helper note is gone. It said one
// of "enable to serve a live overlay" / the address / "port may be in use", and the
// row only existed in some of those -- so everything below it jumped as you toggled
// the server. The address survives as a clickable link, which is the only part of it
// worth a row.
//
// 197 -> 198, strings 122 -> 120: the Reset section became TWO BUTTONS (Profile /
// Everything, each arming on the first click and performing on the second) instead of
// two radio rows plus a shared Reset button -- four rows for two outcomes became one.
// The two RESET_*_CHECKBOX region types survive as the buttons themselves, so they
// keep their ordinals; what left the signature is the pair of tooltip rows the radios
// carried. OPEN_LINK_OVERLAY is APPENDED (the web server's address is a link now, not
// a sentence), so again only the count moved.
//
// strings 124 -> 122: the Reset row became ONE string ("Reset current profile")
// instead of three placed at fixed 6- and 9-character advances, which left a hole
// mid-sentence for any profile name shorter than its column ("Reset   Qualify
// profile"). No region ordinals moved; only the string count.
//
// 196 -> 197: the render-probe sweep's button (PROBE_SWEEP), APPENDED to the enum
// rather than filed with its relatives, so no ordinal below moved and the golden
// itself is unchanged -- only this count.
//
// 198 -> 200: GAUGES_PACK_UP/DOWN, filed with their relatives (next to the pad's
// and the board's pack cycles) rather than appended -- so unlike PROBE_SWEEP
// above, every ordinal at or past them shifted by two and the golden below moved
// with them. RE-BLESSED on exactly that basis and no other: 66 of the 91 entries
// gained 2, the other 25 (all below the insertion point) are byte-identical,
// every LABEL is unchanged, and `strings` did not move -- so the General tab's
// click surface is the same surface, renumbered. A diff with any other shape
// would have been a real change to this tab, which is what the guard is for.
//
// 200 -> 201: DIRECT_GL_TOGGLE (named OVERLAY_RENDERER_TOGGLE when it landed,
// before the renderer it toggles was renamed), APPENDED like PROBE_SWEEP, so no
// ordinal moved -- only this count.
//
// The golden itself then gained ONE row when that control landed on the GENERAL
// tab (it started on Appearance) as a TOGGLE rather than a two-value cycler:
// `143:general.direct_gl;200:-;200:-` after screen_clamp, and strings
// 123 -> 128. RE-BLESSED on exactly that basis: the inserted triple is
// byte-identical in SHAPE to the screen_clamp and grid_snap rows beside it
// (tooltip row + the control's two regions), every other entry is unchanged and
// nothing was reordered -- what a control added to the end of an existing
// section should look like, and what made the diff readable instead of a wall
// of shifted ordinals.
//
// 210 -> 206: Standings' "Rows to show" / "Top positions" became data-driven
// addSteppedControl rows (as on the Charts tab), so their dedicated pairs
// ROW_COUNT_UP/DOWN and STANDINGS_TOP_COUNT_UP/DOWN left the enum. Every one of
// the 72 golden entries at or past them shifted by exactly -4, every label is
// unchanged and `strings` did not move -- a pure renumbering, re-blessed on
// that basis.
//
// 206 -> 204: the Hotkeys tab's HOTKEY_KEYBOARD_CLEAR / HOTKEY_CONTROLLER_CLEAR
// left the enum (two bindings share a row now, and a right-click on a field
// clears it). Every ordinal past them dropped by exactly 2, every label is
// unchanged and `strings` did not move -- a pure renumbering, re-blessed on
// that basis.
//
// The golden then LOST the Help & Community footer (its two link regions,
// `193:-;194:-` after reset_all, and five strings: heading, two labels, two
// URLs) when the docs and discussion links moved to the About page. Nothing
// else moved -- re-blessed on that basis.
//
// SLIDER, DROPDOWN and DROPDOWN_OPTION were then appended before COUNT (the
// settings slider and dropdown). Appended, so no ordinal moved; the General
// tab draws neither, so only the count changed.
//
// The Helmet tab's 22 arrow-pair types then left the enum (its rows are shared
// slider and dropdown descriptors now). Every ordinal past them dropped by
// exactly 22, every label and `strings` are unchanged -- a pure renumbering.
constexpr int kClickRegionTypeCount = 119;

}  // namespace

TEST_CASE("settings General tab: click regions survive the layout-helper conversion") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout\\");
    // MARKERS RESET FIRST. The sidebar draws a "New" tag per tagged tab, so the
    // string count below depends on how many are undismissed -- and dismissals
    // persist to the INI, which survives between runs of this test. Without this
    // the golden would pass on a clean machine and fail on one that had run the
    // suite before, which is the worst kind of golden. See hud/settings/whats_new.h.
    if (host.hasWhatsNew()) host.whatsNewReset();

    host.showSettings(true);
    host.setActiveTab("General");
    host.draw();

    const std::string sig = host.regionSignature();
    REQUIRE_MESSAGE(!sig.empty(), "region signature hook missing or tab rendered nothing");
    MESSAGE("General tab signature: " << sig);

    const auto regions = splitRegions(sig);
    REQUIRE(regions.size() > 5);

    // Every converted control keeps exactly ONE row-wide tooltip region. A
    // conversion that forgets to pass tooltipId silently loses the row's hover
    // help; one that passes it twice double-registers the hover target.
    CHECK(countTooltipRows(regions, "general.pb_scope") == 1);
    // Controller is a list now: its arrows and dropdown box carry the id too, so
    // count the ROW region alone (the type PB Scope's single region has).
    const auto pbRow = std::find_if(regions.begin(), regions.end(),
        [](const std::string& r) { return r.find(":general.pb_scope") != std::string::npos; });
    REQUIRE(pbRow != regions.end());
    const std::string rowType = pbRow->substr(0, pbRow->find(':'));
    CHECK(std::count(regions.begin(), regions.end(), rowType + ":general.controller") == 1);
    // The Profile/Everything reset pair: their tooltips existed but the pair
    // helper took no id, so neither was ever shown.
    CHECK(sig.find("general.reset_profile") != std::string::npos);
    CHECK(sig.find("general.reset_all") != std::string::npos);

    // The tab's own tooltip and at least one control row must be present, i.e.
    // the tab actually rendered its content and not just a frame.
    CHECK(sig.find("general.pb_scope") != std::string::npos);

    // The panel emits strings as well as regions; a tab that rendered no strings
    // means the content area was skipped entirely.
    CHECK(sig.find("strings=") != std::string::npos);
    CHECK(sig.find("strings=0") == std::string::npos);
    // Explicit teardown through the orchestrated Shutdown export. ~PluginHost
    // now does this too (it used to be a bare FreeLibrary, which is what made
    // the unload-without-Shutdown() path reachable from a test), so this is
    // belt-and-braces — shutdown() is idempotent. Kept because it tears down
    // while the test's own scope is still intact rather than during
    // destruction, which keeps a teardown failure attributable to this case.
    host.shutdown();
}

TEST_CASE("settings General tab: the emitted region sequence is unchanged") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout2\\");

    host.showSettings(true);
    host.setActiveTab("General");
    host.draw();

    const std::string sig = host.regionSignature();
    REQUIRE(!sig.empty());

    // Ordinal-shift guard, asserted BEFORE the golden. The golden encodes region
    // types as raw ClickRegion::Type ordinals, and that enum is unnumbered and
    // grouped by topic — so a control inserted next to its relatives shifts every
    // later value and rewrites the whole golden, for a change that never touched
    // this tab. Checking the cardinality first turns that into ONE readable line.
    // If this fails and the golden does too, the golden is almost certainly fine:
    // re-capture it, don't go looking for a layout bug.
    // REQUIRE, not CHECK: on a CHECK the case continues into the golden compare
    // and prints the wall of shifted ordinals anyway — the exact outcome this
    // guard exists to replace with one readable line.
    REQUIRE_MESSAGE(sig.find("typecount=" + std::to_string(kClickRegionTypeCount)) != std::string::npos,
                    "ClickRegion::Type gained or lost a value, so every region ordinal below shifted; "
                    "re-capture the golden and update kClickRegionTypeCount");

    // GOLDEN. Equality here is what proves a refactor emitted the same regions, of
    // the same types, in the same order.
    //
    // Re-blessing this is a DELIBERATE act: it means the panel's click surface
    // changed. Print the new value (the MESSAGE below) and read the diff before
    // pasting it in — a changed count or a reordered pair is exactly the bug this
    // exists to catch, not noise to silence.
    //
    // RE-BLESSED once, and this is the diff that justified it: types 11
    // (COPY_BUTTON) and 7 (RESET_BUTTON) LEFT the General tab's region list. Both
    // are disabled in this test's state -- no copy target is selected and neither
    // reset checkbox is ticked -- and addActionButton registers no region for a
    // disabled button. That was the stricter of the two policies the five button
    // sites used before they were unified (Check Now suppressed the region; Copy
    // and Reset pushed one anyway and re-checked the condition in their handlers).
    // Both buttons still RENDER greyed, which is why `strings` was unchanged then:
    // only their clickability went, which is what disabled means.
    //
    // RE-BLESSED again, 119 -> 120 strings, and the diff is one string with no
    // region: the "Beta" status tag drawn after the Spotter row's label. It shows on
    // EVERY tab because the sidebar is drawn on every tab, which is why the General
    // tab's signature moved for a change to the Spotter row. No region, because the
    // tag is not clickable -- the row it sits on already is.
    //
    // AND AGAIN, 120 -> 125: the five "New" tags, same shape as the Beta one -- one
    // string each, no region, drawn on every tab because the sidebar is. The count
    // is the number of TAGGED TABS (Appearance, Pitboard, Widgets, Timing, Hotkeys),
    // so it moves when the marker table is curated for a release; the whatsNewReset()
    // above is what keeps it from ALSO moving with whatever this machine dismissed on
    // a previous run.
    // RENAMED, 2026-09-01: the "Overlay Renderer" row now drives the in-context GL
    // backend and is called "Direct GL Rendering", so its tooltip id moved from
    // general.overlay_renderer to general.direct_gl. The diff is EXACTLY that one
    // token: typecount stays 201, strings stays 128, and the row still emits label
    // + two regions. Which is the interesting part - the control ALSO changed from
    // a toggle to a status cycler (it has to show Off/On/Failed, because the
    // backend can stand down on its own and a boolean cannot say so), and that
    // conversion turned out to be structurally free. A golden that moved by one
    // string is the evidence for that claim.
    // MOVED, 2026-08-25: the Ko-fi link left this tab for the About page, taking
    // region 195 (OPEN_LINK_KOFI) and its two strings with it -- 125 -> 123. That the
    // diff was exactly one region and exactly two strings is what says the move
    // touched nothing else on the tab.
    // ADDED, 2026-09-03: an Achievements row joined the global group after Spotter,
    // with a master-toggle checkbox in front of it ("189:-;93:achievements" --
    // ACHIEVEMENTS_TOASTS_TOGGLE, then the tab region). Three new ClickRegion
    // types (the toggle and the list's two pager arrows) took the typecount
    // 201 -> 204 and shifted every ordinal after them by three. The Stats tab
    // stayed where it was in the profile group ("14:-;93:stats" after FMX): it
    // was folded into Achievements for a day and put back, so 128 -> 130 strings
    // is the Achievements row's label and its checkbox (no badge, see the registry
    // row) on top of the unchanged profile group. 130 -> 124 at 1.30: the six
    // 1.29 "New" tags retired with their release, and 1.30's one marker sits on
    // the tab that draws no tag. 204 -> 205 with ACHIEVEMENTS_PRESTIGE, which
    // shifted the three types after it (the two GL-confirm regions and the two
    // easter-egg ones) by one -- a change that never touched this tab, which is
    // exactly the shape the typecount guard above exists to name in one line.
    // 205 -> 206 with OPEN_LINK_GITHUB, APPENDED for the About page's thanks
    // links: no ordinal before it moved, so only the count changes here.
    // 206 -> 207 with TWITCH_CHANNEL_EDIT, APPENDED (no ordinal moved), and a
    // Twitch row joined the global group after Director: "14:-;93:twitch" -- a
    // plain HUD_TOGGLE checkbox (its backing HUD is the chat), then the tab
    // region. strings 124 -> 126 is that row's label and checkbox icon.
    // 207 -> 208 with TWITCH_ENABLED_TOGGLE, APPENDED: only the count moves.
    // 208 -> 210 with YOUTUBE_CHANNEL_EDIT/YOUTUBE_ENABLED_TOGGLE, APPENDED: only the count moves.
    // The chat tab's region token follows its tooltip id: "93:twitch" became
    // "93:stream_chat" when the tab became Stream Chat for YouTube (only the
    // name moved).
    // strings 126 -> 127: the Twitch tab's "New" tag (its 1.30 What's New
    // marker), drawn on the sidebar row until the tab is opened.
    // strings 127 -> 129 at 1.31: the tab became "Stream Chat", whose name fills
    // the sidebar and so draws no tag (-1), and the Standings, Lap Log and Gap
    // Bar rows gained one each for their new rows' markers (+3).
    // strings 129 -> 130: the Pitboard row's tag, for its new Freeze row.
    // The reset pair's two regions (12, 13) now carry their tooltip ids:
    // "12:-;13:-" became "12:general.reset_profile;13:general.reset_all".
    // strings 130 -> 132 and "55:delta_trace;14:-" after the Gap Bar: the Delta
    // Trace tab's sidebar row (its name, and the "[ ]" its checkbox draws with no
    // icons staged).
    // REORDERED with the More groups: each section lists its tabs most-used first
    // and closes with a "More" header -- one "55:-" region, a TAB region carrying no
    // tooltip id, so hovering it can't dismiss its first tab's news -- over the rest.
    // Only the active tab's group draws its rows, so with General open both Mores
    // are closed: Hotkeys, Riders, Director, Spotter and Stream Chat leave the
    // global group, and ten HUD rows leave the profile group. strings 132 -> 108
    // is those rows' labels and icons, less the headers' names, carets and tags.
    // Stream Chat moved above Global's More ("55:stream_chat" after Updates):
    // it carries what's-new markers, and the More header shows its on-count in
    // place of a "New" tag (whats_new_test pins that no marked tab sits in More).
    // strings 108 -> 111: Stream Chat's row, and each More's count drawn apart from its name.
    // strings 111 -> 109: the More carets have no text stand-in any more (no icons
    // are staged here, so each closed caret used to draw ">").
    // Each More row opens the More page now and carries its "more" tooltip id:
    // both "55:-" became "55:more". The rows draw the same strings as before.
    // Global's More group is gone and its four tabs are sidebar rows again, in
    // their old order: "55:hotkeys"/"55:riders" after Appearance, "55:director"
    // after Helmet, "55:spotter" after Stream Chat; each row's label and icon add
    // strings, Global's More row (name and count) removes two: strings 109 -> 114.
    // Preferences' five switches went side by side and the Controller row, whose
    // value is a device name, stays full width BELOW them: "72:general.controller;
    // 93:-;92:-" moved from after the PB scope arrows to after Direct GL's.
    // General's Reference (a dropdown: arrows 4/3, box 116) and Freeze (a
    // slider: arrows 2/1, track 115) follow PB scope: strings 111 -> 119.
    // At 1.32 the 1.31 tags (Standings, Lap Log, Gap Bar, Pitboard) gave way to
    // General, Appearance, Hotkeys, Map and Gap Bar's; Delta Trace and Stream
    // Chat band their rows instead (tabCanTag): strings 119 -> 120.
    // The Timing tab's Gap (1.32, #439) tags the Timing sidebar row: 120 -> 121.
    // ClickRegion::Type gained MAP_RANGE_ADAPTIVE_TOGGLE (appended): typecount 118 -> 119.
    // The RPM widget (1.32) tags the Widgets sidebar row: strings 121 -> 122.
    static const char* kGolden =
        "52:general;52:appearance;52:hotkeys;52:riders;54:-;52:rumble;61:-;52:helmet;77:-;52:director;10:-;52"
        ":stream_chat;78:-;52:spotter;94:-;52:achievements;40:-;52:updates;47:-;48:-;51:-;52:widgets;10:-;52:"
        "map;10:-;52:notices;10:-;52:standings;10:-;52:friends;10:-;52:timing;10:-;52:lap_log;10:-;52:gap_bar"
        ";10:-;52:delta_trace;10:-;52:pitboard;52:more;72:general.pb_scope;34:-;34:-;72:general.reference;4:g"
        "eneral.reference;3:general.reference;116:general.reference;72:general.freeze;2:general.freeze;1:gene"
        "ral.freeze;115:general.freeze;72:general.auto_save;41:-;41:-;72:general.grid_snap;35:-;35:-;72:gener"
        "al.screen_clamp;36:-;36:-;72:general.direct_gl;109:-;109:-;72:general.controller;4:general.controlle"
        "r;3:general.controller;116:general.controller;72:general.steam_friends;72:general.web_server;44:-;44"
        ":-;72:general.web_port;45:-;46:-;72:general.auto_switch;49:-;49:-;72:general.copy_profile;4:general."
        "copy_profile;3:general.copy_profile;116:general.copy_profile;8:general.reset_profile;9:general.reset"
        "_all;53:-;6:-;71:-;typecount=119;strings=122";

    MESSAGE("General tab signature: " << sig);
    CHECK(sig == kGolden);
    // Explicit teardown through the orchestrated Shutdown export. ~PluginHost
    // now does this too (it used to be a bare FreeLibrary, which is what made
    // the unload-without-Shutdown() path reachable from a test), so this is
    // belt-and-braces — shutdown() is idempotent. Kept because it tears down
    // while the test's own scope is still intact rather than during
    // destruction, which keeps a teardown failure attributable to this case.
    host.shutdown();
}

// A row whose setting is moot is DISABLED, not drawn with an "Off" value: no
// arrows, and the same grey label and value every tab uses. The Helmet tab's
// Tint opacity and Tint color passed enabled=true with the "Off" styling while
// the visor was off, so they kept a white label and pink arrows that changed a
// setting with no effect - the one tab whose greyed-out rows looked different.
// The tint rows are the last two on the tab, so their regions (two arrows each,
// plus Tint opacity's slider and Tint color's dropdown box) are the only ones the
// visor mode can add or remove.
TEST_CASE("settings Helmet tab: the tint rows lose their arrows while the visor is off") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_layout_helmet\\";
    auto tokens = [](const std::string& sig) {
        return static_cast<int>(std::count(sig.begin(), sig.end(), ';'));
    };
    auto helmetSig = [&](int visorMode) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(saveWin);
        {
            std::filesystem::create_directories(std::string(saveWin) + "mxbmrp3");
            std::ofstream ini(std::string(saveWin) + "mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
            REQUIRE(ini.is_open());
            ini << "[Settings]\nversion=6\n\n[HelmetOverlay]\nvisorMode=" << visorMode << "\n";
        }
        host.loadSettings(saveWin);
        host.showSettings(true);
        host.setActiveTab("Helmet");
        host.draw();
        std::string sig = host.regionSignature();
        host.shutdown();
        return sig;
    };

    const std::string off = helmetSig(0);
    const std::string goggles = helmetSig(1);
    REQUIRE(!off.empty());
    REQUIRE(!goggles.empty());
    // Two rows, three regions each.
    CHECK_MESSAGE(tokens(goggles) - tokens(off) == 6,
                  "visor off should drop exactly the tint rows' six regions");
}

// A "MORE" GROUP IS ONE SIDEBAR ROW THAT OPENS A PAGE (renderTabMore). The
// per-element section ends in a More group (s_tabRegistry); its tabs are never
// sidebar rows, whichever tab is open, so the sidebar's height no longer depends
// on what is open. The global section has none: every global tab is a row. A tab
// region is "52:<tooltip id>" and a More page row "72:<tab id>", so where a tab
// is listed reads straight off the signature.
TEST_CASE("settings sidebar: the More row opens a page of its tabs") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_groups\\");
    host.showSettings(true);

    auto has = [](const std::string& sig, const char* token) {
        return sig.find(std::string(token) + ";") != std::string::npos;
    };
    auto count = [](const std::string& sig, const char* token) {
        int n = 0;
        for (size_t at = sig.find(token); at != std::string::npos; at = sig.find(token, at + 1)) ++n;
        return n;
    };

    host.setActiveTab("Map");
    host.draw();
    const std::string top = host.regionSignature();
    REQUIRE(!top.empty());
    CHECK(has(top, "52:map"));            // the most-used tabs: always listed
    CHECK(has(top, "52:general"));
    CHECK(has(top, "52:riders"));         // ...and every global tab
    CHECK(has(top, "52:spotter"));
    CHECK(count(top, "52:more;") == 1);   // one More, the per-element section's
    CHECK_FALSE(has(top, "52:radar"));    // ...and none of its tabs

    // Opened by any route, a More tab still isn't a sidebar row.
    host.setActiveTab("Telemetry");
    host.draw();
    CHECK_FALSE(has(host.regionSignature(), "52:telemetry"));

    // The More row opens the page of its tabs.
    float mx = 0.0f, my = 0.0f;
    REQUIRE(host.settingsRegionCenter("more", &mx, &my));
    host.clickAt(mx, my);
    host.draw();
    CHECK(host.activeTab() == "More");
    const std::string page = host.regionSignature();
    CHECK(has(page, "72:radar"));
    CHECK(has(page, "72:telemetry"));
    CHECK(has(page, "72:session_charts"));
    CHECK_FALSE(has(page, "72:riders"));
    CHECK(count(page, "52:more;") == 1);   // the sidebar is the same height

    // A row's name opens its tab...
    float tx = 0.0f, ty = 0.0f;
    REQUIRE(host.settingsRegionCenter("telemetry", &tx, &ty));
    host.clickAt(tx, ty);
    host.draw();
    CHECK(host.activeTab() == "Telemetry");
    // ...which gains the Back button under its content: a TAB region like the
    // sidebar row, the same click. A tab outside More has none (see Map above).
    CHECK(count(host.regionSignature(), "52:more;") == 1);
    CHECK(count(host.regionSignature(), "52:more.back;") == 1);

    host.clickAbout();
    host.draw();
    CHECK_FALSE(has(host.regionSignature(), "72:radar"));
}

// A More page row's icon switches its HUD, as a sidebar icon does, and the name
// beside it opens the tab. The icon fills the row's first 4 characters
// (settings_tab_more.cpp); the character width and the label column come from the
// Appearance tab's two colour cells, as in the test below -- every tab shares the
// font and the label column.
TEST_CASE("settings More page: a row's icon switches its HUD") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_more_toggle\\");
    host.showSettings(true);
    host.setActiveTab("Appearance");
    host.draw();
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    REQUIRE(host.settingsRegionCenter("appearance.color_primary", &lx, &ly));
    REQUIRE(host.settingsRegionCenter("appearance.color_accent", &rx, &ry));
    const float cw = (rx - lx) / 30.5f;
    const float labelX = lx - 15.25f * cw;

    host.setActiveTab("More");   // Profile's: no More row clicked yet
    host.draw();
    REQUIRE(host.hudVisible("radar_hud") == 0);
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("radar", &x, &y));
    host.clickAt(labelX + 2.0f * cw, y);    // its icon
    CHECK(host.hudVisible("radar_hud") == 1);
    CHECK(host.activeTab() == "More");      // the icon doesn't open the tab
    host.draw();
    host.clickAt(labelX + 2.0f * cw, y);
    CHECK(host.hudVisible("radar_hud") == 0);
    host.draw();
    host.clickAt(labelX + 8.0f * cw, y);    // its name
    CHECK(host.activeTab() == "Radar");
    CHECK(host.hudVisible("radar_hud") == 0);
    host.shutdown();
}

// THE APPEARANCE TAB'S COLOURS ARE TWO COLUMNS OF FIVE, side by side
// (SettingsLayoutContext::beginColumns). Pins what a two-column row can get wrong
// and a one-column one cannot: the pairs sharing a row, the cells emitted in slot
// order (left column, then right), and each cell's arrows reaching its OWN slot -- a
// payload stamped onto the wrong regions, or a right cell whose arrows sit under
// the left cell's tooltip region, still renders perfectly.
//
// The arrow positions are derived from the two cells' tooltip-region centres: the
// regions meet mid-gap, so both are (cell + gap/2) = 30.5 characters wide and their
// centres are that far apart, which gives the character width without a hook. A
// cell is 29 characters, the next starts 32 in, and a cell's "<" starts 12
// characters into it and its " >" 27.
TEST_CASE("settings Appearance tab: the colours are two columns of five") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_colors\\");
    host.showSettings(true);
    host.setActiveTab("Appearance");
    host.draw();

    const char* left[]  = { "primary", "secondary", "tertiary", "muted", "background" };
    const char* right[] = { "accent", "positive", "neutral", "warning", "negative" };
    float lx[5], ly[5], rx[5], ry[5];
    for (int i = 0; i < 5; ++i) {
        const std::string l = std::string("appearance.color_") + left[i];
        const std::string r = std::string("appearance.color_") + right[i];
        REQUIRE(host.settingsRegionCenter(l.c_str(), &lx[i], &ly[i]));
        REQUIRE(host.settingsRegionCenter(r.c_str(), &rx[i], &ry[i]));
        CHECK(ly[i] == doctest::Approx(ry[i]));   // a pair shares its row
        CHECK(rx[i] > lx[i]);                     // ...the right cell to the right
        if (i > 0) {
            CHECK(ly[i] > ly[i - 1]);             // each column runs down
            CHECK(lx[i] == doctest::Approx(lx[0]));
            CHECK(rx[i] == doctest::Approx(rx[0]));
        }
    }

    // Emission order is the slot order it always was: the left column, then the right.
    const std::vector<std::string> regions = splitRegions(host.regionSignature());
    auto indexOf = [&](const char* slot) {
        const std::string want = std::string(":appearance.color_") + slot;
        for (size_t i = 0; i < regions.size(); ++i) {
            const std::string& r = regions[i];
            if (r.size() > want.size() && r.compare(r.size() - want.size(), want.size(), want) == 0)
                return static_cast<int>(i);
        }
        return -1;
    };
    int prev = -1;
    for (const char* slot : { "primary", "secondary", "tertiary", "muted", "background",
                              "accent", "positive", "neutral", "warning", "negative" }) {
        const int at = indexOf(slot);
        CHECK_MESSAGE(at > prev, slot);
        prev = at;
    }

    // Each cell's arrows cycle its own slot. ColorSlot ordinals: PRIMARY 0,
    // POSITIVE 5, NEGATIVE 8, ACCENT 9.
    // Each cell's region is its own 29 characters, 32 apart (a 3-character gap).
    const float cw = (rx[0] - lx[0]) / 32.0f;
    const float leftCellX = lx[0] - 14.5f * cw;
    const float rightCellX = leftCellX + 32.0f * cw;
    REQUIRE(host.colorOverridden(9) == 0);
    host.clickAt(rightCellX + 12.5f * cw, ry[0]);     // Accent's "<"
    CHECK(host.colorOverridden(9) == 1);
    CHECK(host.colorOverridden(0) == 0);
    REQUIRE(host.colorOverridden(5) == 0);
    host.clickAt(leftCellX + 28.5f * cw, ly[4]);      // Background's ">"
    CHECK(host.colorOverridden(4) == 1);
    CHECK(host.colorOverridden(8) == 0);              // not Negative, its row-mate
    host.clickAt(rightCellX + 28.5f * cw, ry[4]);     // Negative's ">"
    CHECK(host.colorOverridden(8) == 1);
    CHECK(host.colorOverridden(5) == 0);
    host.shutdown();
}

// EVERY HUD TAB OPENS WITH ITS APPEARANCE BLOCK IN TWO COLUMNS
// (addStandardHudControls through beginColumns): Visible, Title and Theme down
// the left, Opacity and Scale down the right, emitted in that order. A run that
// forgot to rewind would put Opacity under Theme; one that never advanced would
// stack every cell on one row.
TEST_CASE("settings HUD tab: the Appearance block is two columns") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_block\\");
    host.showSettings(true);
    host.setActiveTab("Standings");
    host.draw();

    float vx = 0.0f, vy = 0.0f, tx = 0.0f, ty = 0.0f, hx = 0.0f, hy = 0.0f;
    float ox = 0.0f, oy = 0.0f, sx = 0.0f, sy = 0.0f;
    REQUIRE(host.settingsRegionCenter("common.visible", &vx, &vy));
    REQUIRE(host.settingsRegionCenter("common.title", &tx, &ty));
    // Theme when themes are installed, Texture otherwise (none are staged here).
    REQUIRE((host.settingsRegionCenter("common.theme", &hx, &hy) ||
             host.settingsRegionCenter("common.texture", &hx, &hy)));
    REQUIRE(host.settingsRegionCenter("common.opacity", &ox, &oy));
    REQUIRE(host.settingsRegionCenter("common.scale", &sx, &sy));
    CHECK(vx == doctest::Approx(tx));
    CHECK(vx == doctest::Approx(hx));
    CHECK(ty > vy);
    CHECK(hy > ty);
    CHECK(ox > vx);                           // the right column...
    CHECK(oy == doctest::Approx(vy));         // ...from the block's top
    CHECK(sy == doctest::Approx(ty));
    CHECK(sx == doctest::Approx(ox));

    // The next section starts under the taller column, not under the last cell.
    float rx = 0.0f, ry = 0.0f;
    REQUIRE(host.settingsRegionCenter("standings.rows", &rx, &ry));
    CHECK(ry > hy);
    host.shutdown();
}

// A bounded number draws as a slider under its value (SettingsLayoutContext::
// addSliderTrack): a press on the track sets the value at that point, a drag
// follows the cursor, and a drag past either end stops at that end. Read back
// from the value the row draws, which is what the player sees.
TEST_CASE("settings slider: press and drag set the value, clamped at the ends") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_slider\\");
    host.showSettings(true);
    host.setActiveTab("Map");
    host.draw();

    // The value string drawn on the slider's row: inside the track's span, and
    // the nearest one whose top is above the track's centre line (the next row's
    // top is below it).
    auto valueOn = [&](float centreY, float x0, float w) {
        std::string best;
        double bestGap = 1.0;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.x < x0 - 0.001 || r.x >= x0 + w || r.y > centreY) continue;
            if (centreY - r.y < bestGap) { bestGap = centreY - r.y; best = r.text; }
        }
        return best;
    };

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsSliderSpan("common.opacity", &x0, &y, &w));
    REQUIRE(w > 0.0f);
    host.leftDragAt(x0 + w * 0.1f, y, x0 + w * 0.5f, y);
    host.draw();
    CHECK(valueOn(y, x0, w).rfind("50%", 0) == 0);

    host.leftDragAt(x0 + w * 0.5f, y, x0 - w, y);   // past the left end
    host.draw();
    CHECK(valueOn(y, x0, w).rfind("0%", 0) == 0);

    REQUIRE(host.settingsSliderSpan("common.scale", &x0, &y, &w));
    host.leftDragAt(x0 + w * 0.05f, y, x0 + w * 2.0f, y);   // past the right end
    host.draw();
    INFO("scale reads " << valueOn(y, x0, w));
    CHECK(valueOn(y, x0, w).rfind("300%", 0) == 0);

    // A wrapping stepper (a duration that runs round to Off) has no ends, so no track.
    host.setActiveTab("Notices");
    host.draw();
    float nx = 0.0f, ny = 0.0f, nw = 0.0f;
    CHECK_FALSE(host.settingsSliderSpan("notices.duration", &nx, &ny, &nw));
    host.shutdown();
}

// UI scale resizes the settings panel itself, so applied live the track slid
// out from under a held cursor and the knob drifted away from it. A drag only
// moves the value shown (SteppedControl::dragSet); the release applies it.
TEST_CASE("settings slider: UI scale applies on release, not while held") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_uiscale\\");
    host.showSettings(true);
    host.setActiveTab("Appearance");
    host.draw();
    REQUIRE(host.hudScales("settings_hud").drawn == 1000);

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsSliderSpan("appearance.ui_scale", &x0, &y, &w));
    auto rowSays = [&](const char* text) {
        for (const auto& r : host.hudStringRows("settings_hud")) {
            // The value is padded to the slider's width: match its start.
            if (r.text.rfind(text, 0) == 0 && r.x >= x0 - 0.001 && r.x < x0 + w) return true;
        }
        return false;
    };
    host.injectMouse(true, x0 + w * 0.5f, y, 0); host.draw();
    host.injectMouse(true, x0 + w * 0.5f, y, 1); host.draw();
    host.injectMouse(true, x0 + w * 0.75f, y, 1); host.draw();
    host.draw();
    CHECK(rowSays("125%"));                                  // the row follows the drag...
    CHECK(host.hudScales("settings_hud").drawn == 1000);     // ...the panel does not
    host.injectMouse(true, x0 + w * 0.75f, y, 0); host.draw();
    host.draw();
    CHECK(host.hudScales("settings_hud").drawn == 1250);
    host.shutdown();
}

// A dragged panel sits at its offset on screen, while the regions stay in build
// space: the slider once took the track's build-space left edge as the cursor's
// origin, so with the menu moved right by a track's width every press read as
// the right end (100%, 300%) and a drag stayed pinned there.
TEST_CASE("settings slider: the value follows the cursor on a moved panel") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_slider_moved\\");
    host.showSettings(true);
    host.setActiveTab("Map");
    host.draw();

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsSliderSpan("common.opacity", &x0, &y, &w));
    const auto before = host.hudScreenEdges("settings_hud");
    const float dx = w * 1.5f;
    REQUIRE(host.setHudOffset("settings_hud", dx, 0.0f));
    host.draw();
    const auto moved = host.hudScreenEdges("settings_hud");
    const float shift = static_cast<float>(moved.l - before.l) / 1e6f;
    REQUIRE(shift > w);   // really moved by more than a track

    auto valueOn = [&](float centreY) {   // the strings are on screen, moved too
        std::string best;
        double bestGap = 1.0;
        const float sx = x0 + shift;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.x < sx - 0.001 || r.x >= sx + w || r.y > centreY) continue;
            if (centreY - r.y < bestGap) { bestGap = centreY - r.y; best = r.text; }
        }
        return best;
    };
    host.leftDragAt(x0 + shift + w * 0.1f, y, x0 + shift + w * 0.5f, y);
    host.draw();
    INFO("opacity reads " << valueOn(y));
    CHECK(valueOn(y).rfind("50%", 0) == 0);
    host.shutdown();
}

// A dropdown's caret sits in the cell its row's right arrow region also spans,
// and hit-testing takes the first region pushed: a click on the caret stepped the
// value (and repeated while held) instead of opening the list.
TEST_CASE("settings dropdown: a click on the caret opens the list") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_caret\\");
    host.showSettings(true);
    host.setActiveTab("Map");
    host.draw();

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsDropdownSpan("map.colorize", &x0, &y, &w));
    auto brandBelow = [&]() {
        int n = 0;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.y > y && r.x >= x0 - 0.001 && r.x < x0 + w && r.text.rfind("Brand", 0) == 0) ++n;
        }
        return n;
    };
    REQUIRE(brandBelow() == 0);
    host.clickAt(x0 + w * 0.97f, y);   // the caret, at the box's right end
    host.draw();
    CHECK(brandBelow() == 1);
    host.shutdown();
}

// A range whose Off is stored as 0 but sits one step below its low end (the
// Director's Max shot) puts Off at the slider's left end: a drag there turns it
// off, and a drag back lands on a real number again, never 1..4 seconds.
TEST_CASE("settings slider: Off at the left end of an Off-below range") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_offbelow\\");
    host.directorSetEnabled(true);
    host.showSettings(true);
    host.setActiveTab("Director");
    host.draw();

    auto valueOn = [&](float centreY, float x0, float w) {
        std::string best;
        double bestGap = 1.0;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.x < x0 - 0.001 || r.x >= x0 + w || r.y > centreY) continue;
            if (centreY - r.y < bestGap) { bestGap = centreY - r.y; best = r.text; }
        }
        return best;
    };

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsSliderSpan("director.max_shot", &x0, &y, &w));
    host.leftDragAt(x0 + w * 0.5f, y, x0 + w * 2.0f, y);   // past the right end
    host.draw();
    CHECK(valueOn(y, x0, w).rfind("40s", 0) == 0);

    host.leftDragAt(x0 + w * 0.5f, y, x0 - w, y);   // past the left end
    host.draw();
    CHECK(valueOn(y, x0, w).rfind("Off", 0) == 0);

    host.leftDragAt(x0 + w * 0.5f, y, x0 + w * 0.03f, y);   // just inside it
    host.draw();
    CHECK(valueOn(y, x0, w).rfind("5s", 0) == 0);
    host.shutdown();
}

// A cycle of three or more named states draws as a dropdown (SettingsLayout-
// Context::addDropdownBox): a click on the box opens the list under it, a click
// on an entry picks it and closes the list, and a click anywhere else closes it
// and does nothing else. The list draws over the rows below, so the text it
// covers must be gone -- the game draws every string after every quad.
// Unthemed, each section gets a faint tint box reaching a little past its rows.
// It must not reach across the gutter: the sidebar's tints and the content's met
// edge to edge, so the two columns read as one block, where a theme's cards keep a
// gap. A quad straddling the gutter's midline is one that closes it -- only the
// panel's own background may span both columns.
TEST_CASE("settings unthemed: the section tints keep the sidebar gutter open") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasQuadRects());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_gutter\\");
    host.showSettings(true);
    host.setActiveTab("Map");
    host.draw();
    const auto g = host.settingsGutter();
    REQUIRE(g.contentCardLeft > g.sidebarCardRight);
    const double mid = (g.sidebarCardRight + g.contentCardLeft) / 2.0;
    const double colW = g.contentCardLeft - g.sidebarCardRight;
    const auto e = host.hudScreenEdges(PluginHost::HUD_SETTINGS);
    const double panelW = (e.r - e.l) / 1e6;
    REQUIRE(panelW > 0.0);
    int tints = 0, straddling = 0;
    for (const auto& q : host.hudQuadRects("settings_hud")) {
        const double w = q.r - q.l;
        if (w <= colW * 4.0) continue;           // icons, carets, sliders: nowhere near
        const bool reachesGutter = (q.r > g.sidebarCardRight && q.r < g.contentCardLeft) ||
                                   (q.l > g.sidebarCardRight && q.l < g.contentCardLeft);
        if (reachesGutter) ++tints;              // a tint edge inside the gutter, as designed
        if (q.l < mid && q.r > mid && w < panelW * 0.9) ++straddling;   // not the frame or band
    }
    CHECK(tints > 0);                            // the case is looking at the tints at all
    CHECK(straddling == 0);
}

// Every list of three or more values names its values, so it draws as a dropdown. A
// list that does not was built around the shared descriptor rather than through it,
// which is how the hand-rolled cyclers this sweep replaced looked; the case keeps a
// new one from slipping back in on any tab.
TEST_CASE("settings dropdown: no tab has a 3+ value list without names") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_unnamed\\");
    host.showSettings(true);
    const auto tabs = host.settingsAllTabNames();
    REQUIRE(tabs.size() > 20);
    for (const std::string& tab : tabs) {
        host.setActiveTab(tab.c_str());
        host.draw();
        INFO("tab: " << tab);
        CHECK(host.unnamedLists() == 0);
    }
}

TEST_CASE("settings dropdown: opens, picks an entry, and closes on a click outside") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_dropdown\\");
    host.showSettings(true);
    host.setActiveTab("Map");
    host.draw();

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsDropdownSpan("map.colorize", &x0, &y, &w));
    // Strings in the box's column below it: what the list will cover.
    auto below = [&](const char* text) {
        int n = 0;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.y > y && r.x >= x0 - 0.001 && r.x < x0 + w && r.text.rfind(text, 0) == 0) ++n;
        }
        return n;
    };
    REQUIRE(below("100%") == 1);      // Marker scale's value, the row under it
    CHECK(below("Brand") == 0);

    host.clickAt(x0 + w * 0.5f, y);   // open
    host.draw();
    CHECK(below("Brand") == 1);       // the list...
    CHECK(below("100%") == 0);        // ...hides the row it covers

    // Pick "Brand", the list's second entry, one row under the first.
    float bx = 0.0f, by = 0.0f;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.y > y && r.text == "Brand") { bx = static_cast<float>(r.x); by = static_cast<float>(r.y); }
    }
    REQUIRE(by > 0.0f);
    host.clickAt(bx + 0.005f, by + 0.005f);
    host.draw();
    CHECK(below("Brand") == 0);       // closed
    CHECK(below("100%") == 1);
    // The box now reads Brand (padded to its field): on its own row, top above its centre.
    bool picked = false;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.text.rfind("Brand", 0) == 0 && r.y <= y && y - r.y < (by - y) * 0.5 &&
            r.x >= x0 - 0.001 && r.x < x0 + w) picked = true;
    }
    CHECK(picked);

    // Open again and HOVER an entry that is not the current one: the entry
    // itself lights up. The entries' regions once went in front of the rest, so
    // the hover index named a sidebar row instead and lit that.
    host.clickAt(x0 + w * 0.5f, y);
    host.draw();
    REQUIRE(below("Uniform") == 1);
    float ux = 0.0f, uy = 0.0f;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.y > y && r.text == "Uniform") { ux = static_cast<float>(r.x); uy = static_cast<float>(r.y); }
    }
    REQUIRE(uy > 0.0f);
    auto entryLit = [&]() {   // a quad spanning the entry's line, starting left of its text
        for (const auto& q : host.hudQuadRects("settings_hud")) {
            if (q.l <= ux && q.l > ux - 0.01 && std::fabs(q.t - uy) < 0.002 && q.r > ux + 0.03 &&
                q.b - q.t < (by - y)) return true;
        }
        return false;
    };
    CHECK_FALSE(entryLit());
    host.injectMouse(true, ux + 0.005f, uy + 0.005f, 0);
    host.draw();
    host.draw();
    CHECK(entryLit());

    // Click outside: closed, and nothing under the click fired.
    const std::string tab = host.activeTab();
    host.clickAt(0.01f, 0.99f);
    host.draw();
    CHECK(below("Uniform") == 0);
    CHECK(host.activeTab() == tab);
    host.shutdown();
}

// An icon list (CycleControl::spriteOf - the map's Marker icon, a couple of
// hundred icons) opens as a grid of the icons themselves, with a caption row
// naming the hovered one; a click on a cell picks it. Names alone would wrap
// into more columns than the panel is wide.
TEST_CASE("settings dropdown: an icon list opens as a grid and picks by cell") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    REQUIRE(host.hasQuadRects());

    // The harness discovery tree has no icons, so stage the SHIPPED set through
    // the plugin's user-asset sync (the twitch_chat_test path), and take every
    // copy back out afterwards: the discovery tree outlives this test, and later
    // tests count what the menu draws without icons.
    static const char* const kSave = "Z:\\tmp\\mxbmrp3-tests\\settings_layout_icon_grid\\";
    const std::string save = kSave;
    // From the defaults: a save left by an earlier run has cell 20 picked already.
    std::filesystem::remove_all(kSave);
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\icons").c_str(), nullptr);
    struct Staged {
        std::vector<std::string> files;
        ~Staged() { for (const std::string& f : files) DeleteFileA(f.c_str()); }
    } staged;
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA(MXB_REPO_DATA_DIR "/icons/*.tga", &fd);
    REQUIRE(find != INVALID_HANDLE_VALUE);
    do {
        const std::string name = fd.cFileName;
        const std::string dst = save + "mxbmrp3\\icons\\" + name;
        if (CopyFileA((std::string(MXB_REPO_DATA_DIR "/icons/") + name).c_str(), dst.c_str(), FALSE)) {
            staged.files.push_back(dst);
            staged.files.push_back("plugins\\mxbmrp3_data\\icons\\" + name);
        }
    } while (FindNextFileA(find, &fd));
    FindClose(find);
    REQUIRE(staged.files.size() > 100);
    host.startup(kSave);
    host.showSettings(true);
    host.setActiveTab("Map");
    host.draw();

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsDropdownSpan("map.rider_shape", &x0, &y, &w));
    // Icon cells: square on screen (16:9 aspect) and small.
    auto icons = [&]() {
        std::vector<PluginHost::QuadRect> out;
        for (const auto& q : host.hudQuadRects("settings_hud")) {
            const double qw = q.r - q.l, qh = q.b - q.t;
            if (qh > 0.0 && qh < 0.05 && std::fabs(qh - qw * 16.0 / 9.0) < qh * 0.03) out.push_back(q);
        }
        return out;
    };
    const auto closed = icons();
    const size_t closedIcons = closed.size();
    host.clickAt(x0 + w * 0.5f, y);
    host.draw();
    // The grid's cells: the icons that were not there with the list closed.
    std::vector<PluginHost::QuadRect> grid;
    for (const auto& q : icons()) {
        bool before = false;
        for (const auto& c : closed) {
            if (std::fabs(c.l - q.l) < 1e-5 && std::fabs(c.t - q.t) < 1e-5) before = true;
        }
        if (!before) grid.push_back(q);
    }
    REQUIRE(grid.size() > 50);

    // The caption: the string just under the lowest cell (blanked strings keep
    // their place with no text, so skip those).
    double gridBottom = 0.0, gridLeft = 1.0;
    for (const auto& q : grid) { gridBottom = std::max(gridBottom, q.b); gridLeft = std::min(gridLeft, q.l); }
    auto caption = [&]() {
        std::string best;
        double bestY = 1.0;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (!r.text.empty() && r.y >= gridBottom && r.y < bestY && r.x >= gridLeft - 0.02) {
                best = r.text;
                bestY = r.y;
            }
        }
        return best;
    };
    const std::string current = caption();
    REQUIRE_FALSE(current.empty());

    // Hover the 20th cell: the caption names it, then a click picks it.
    const auto& cell = grid[20];
    const float cx = static_cast<float>((cell.l + cell.r) * 0.5), cy = static_cast<float>((cell.t + cell.b) * 0.5);
    host.injectMouse(true, cx, cy, 0);
    host.draw();
    host.draw();
    const std::string hovered = caption();
    REQUIRE_FALSE(hovered.empty());
    CHECK(hovered != current);

    host.clickAt(cx, cy);
    host.draw();
    CHECK(icons().size() == closedIcons);   // closed
    bool picked = false;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.x >= x0 - 0.001 && r.x < x0 + w && std::fabs(r.y - y) < 0.02 &&
            r.text.rfind(hovered.substr(0, 6), 0) == 0) picked = true;
    }
    INFO("hovered " << hovered);
    CHECK(picked);
    host.shutdown();
}

// A colour slot's list is ColorConfig's own ring: Default, then the palette. A
// palette entry PINS the slot (setColor), and Default -- a real entry, not just
// where the arrows wrap to -- UN-pins it, so the slot follows the theme again.
// Picking Default by setting the theme's colour would leave it pinned.
TEST_CASE("settings dropdown: a colour list pins a palette entry and Default un-pins") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_colour_list\\");
    host.showSettings(true);
    host.setActiveTab("Appearance");
    host.draw();
    REQUIRE(host.colorOverridden(0) == 0);   // PRIMARY, untouched

    float x0 = 0.0f, y = 0.0f, w = 0.0f;
    REQUIRE(host.settingsDropdownSpan("appearance.color_primary", &x0, &y, &w));
    // An entry of the open list: off the box's own row.
    auto pick = [&](const char* name) {
        host.clickAt(x0 + w * 0.5f, y);   // open
        host.draw();
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.text == name && std::fabs(r.y - y) > 0.01) {
                host.clickAt(static_cast<float>(r.x) + 0.005f, static_cast<float>(r.y) + 0.005f);
                host.draw();
                return true;
            }
        }
        return false;
    };
    REQUIRE(pick("Crimson"));
    CHECK(host.colorOverridden(0) == 1);
    REQUIRE(pick("Default"));
    CHECK(host.colorOverridden(0) == 0);
    host.shutdown();
}

// A tracked rider's cell carries two swatch dropdowns ahead of its name: its
// colour, then its icon, each opening its own list. Picking a colour recolours
// the rider's swatch.
TEST_CASE("settings dropdown: a tracked rider's colour and icon dropdowns") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInjectedMouse());
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasInkHooks());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_rider_lists\\");
    REQUIRE(host.trackRider("Zed Rider"));
    host.showSettings(true);
    host.setActiveTab("Riders");
    host.draw();

    float cx0 = 0.0f, cy = 0.0f, cw = 0.0f, ix0 = 0.0f, iy = 0.0f, iw = 0.0f;
    REQUIRE(host.settingsDropdownSpan("riders.color", &cx0, &cy, &cw));
    REQUIRE(host.settingsDropdownSpan("riders.icon", &ix0, &iy, &iw));
    CHECK(cy == doctest::Approx(iy));
    CHECK(cx0 + cw < ix0);   // colour first, then icon
    float nx = 0.0f;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.text.rfind("Zed Rider", 0) == 0) nx = static_cast<float>(r.x);
    }
    CHECK(ix0 + iw < nx);    // both ahead of the name

    // The colour swatch: the last quad inside the colour box on its line.
    auto swatchColor = [&]() -> unsigned long {
        const auto quads = host.hudQuadRects("settings_hud");
        unsigned long c = 0;
        for (size_t i = 0; i < quads.size(); ++i) {
            const auto& q = quads[i];
            if (q.l >= cx0 - 0.001 && q.r <= cx0 + cw * 0.6 && q.t <= cy && q.b >= cy) {
                c = host.quadColor("settings_hud", static_cast<int>(i));
            }
        }
        return c;
    };
    const unsigned long before = swatchColor();
    REQUIRE(before != 0);
    auto listShows = [&](const char* name) {
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (r.text == name) return true;
        }
        return false;
    };

    host.clickAt(cx0 + cw * 0.5f, cy);   // the colour list
    host.draw();
    REQUIRE(listShows("Crimson"));
    float px = 0.0f, py = 0.0f;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.text == "Crimson") { px = static_cast<float>(r.x); py = static_cast<float>(r.y); }
    }
    host.clickAt(px + 0.005f, py + 0.005f);
    host.draw();
    CHECK_FALSE(listShows("Crimson"));
    const unsigned long after = swatchColor();
    CHECK(after != 0);
    CHECK(after != before);

    host.clickAt(ix0 + iw * 0.5f, iy);   // the icon list, not the colours
    host.draw();
    CHECK_FALSE(listShows("Crimson"));
    host.shutdown();
}

// A tab opened from a More page carries its Back button UNDER ITS CONTENT, where
// Updates puts Check Now, not in the footer: above the footer's About button, and
// one click takes the player back to the page.
TEST_CASE("settings More tabs: Back sits under the content and returns to the page") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_back\\");
    REQUIRE(host.hasAboutRect());
    host.showSettings(true);
    host.setActiveTab("Stats");
    host.draw();

    float bx = 0.0f, by = 0.0f;
    REQUIRE(host.settingsRegionCenter("more.back", &bx, &by));
    int l = 0, t = 0, r = 0, b = 0;
    REQUIRE(host.aboutButtonRect(l, t, r, b));
    CHECK(by * 1e6f < static_cast<float>(t));   // in the content, above the footer row

    host.clickAt(bx, by);
    CHECK(host.activeTab() == "More");

    // A sidebar tab has none.
    host.setActiveTab("Map");
    host.draw();
    float x = 0.0f, y = 0.0f;
    CHECK_FALSE(host.settingsRegionCenter("more.back", &x, &y));
    host.shutdown();
}

// Every HUD's toggle has a row on the Hotkeys tab -- they used to be INI-only
// past the first nine for want of height -- Delta Trace's new one included.
TEST_CASE("settings Hotkeys: every HUD toggle has a row") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout_hotkey_rows\\");
    host.showSettings(true);
    host.setActiveTab("Hotkeys");
    host.draw();
    // In the sidebar's order (its tabs, the More page's, then the global ones),
    // which is also the README's: read down the left column, then the right.
    static const char* const kRows[] = {
        "hotkeys.settings", "hotkeys.map", "hotkeys.notices", "hotkeys.standings",
        "hotkeys.timing", "hotkeys.lap_log", "hotkeys.gap_bar", "hotkeys.delta_trace",
        "hotkeys.pitboard", "hotkeys.radar", "hotkeys.ideal_lap", "hotkeys.session",
        "hotkeys.performance", "hotkeys.stats", "hotkeys.telemetry", "hotkeys.event_log",
        "hotkeys.session_charts", "hotkeys.rumble", "hotkeys.helmet", "hotkeys.stream_chat",
    };
    float prevX = -1.0f, prevY = -1.0f;
    for (const char* id : kRows) {
        float x = 0.0f, y = 0.0f;
        CHECK_MESSAGE(host.settingsRegionCenter(id, &x, &y), id << " has no row");
        const bool after = x > prevX + 0.01f || (std::fabs(x - prevX) <= 0.01f && y > prevY);
        CHECK_MESSAGE(after, id << " is out of the sidebar's order");
        prevX = x; prevY = y;
    }
    CHECK(host.settingsOverflowRows() <= 0.0);
    host.shutdown();
}
