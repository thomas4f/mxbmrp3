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
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
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
constexpr int kClickRegionTypeCount = 198;

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
    CHECK(countTooltipRows(regions, "general.controller") == 1);

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
    // MOVED, 2026-08-25: the Ko-fi link left this tab for the About page, taking
    // region 195 (OPEN_LINK_KOFI) and its two strings with it -- 125 -> 123. That the
    // diff was exactly one region and exactly two strings is what says the move
    // touched nothing else on the tab.
    static const char* kGolden =
        "91:general;91:appearance;91:hotkeys;91:riders;93:-;91:rumble;102:-;91:helmet;148:-;91:director;1"
        "59:-;91:spotter;79:-;91:updates;86:-;87:-;14:-;91:standings;14:-;91:map;14:-;91:radar;14:-;91:la"
        "p_log;14:-;91:ideal_lap;14:-;91:session_charts;14:-;91:telemetry;14:-;91:records;14:-;91:pitboar"
        "d;14:-;91:session;14:-;91:timing;14:-;91:gap_bar;14:-;91:notices;14:-;91:event_log;14:-;91:frien"
        "ds;14:-;91:fmx;14:-;91:stats;14:-;91:performance;90:-;91:widgets;141:general.pb_scope;68:-;68:-;"
        "141:general.controller;95:-;94:-;141:general.auto_save;80:-;80:-;141:general.grid_snap;74:-;74:-"
        ";141:general.screen_clamp;75:-;75:-;141:general.steam_friends;141:general.web_server;83:-;83:-;1"
        "41:general.web_port;84:-;85:-;141:general.auto_switch;88:-;88:-;141:general.copy_profile;10:-;9:"
        "-;12:-;13:-;193:-;194:-;92:-;8:-;140:-;typecount=198;strings=123";

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
