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
constexpr int kClickRegionTypeCount = 182;

}  // namespace

TEST_CASE("settings General tab: click regions survive the layout-helper conversion") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_layout\\");

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

    // GOLDEN, captured from the build BEFORE the hand-rolled control blocks were
    // routed through addCycleControl. Equality here is what proves the conversion
    // emitted the same regions, of the same types, in the same order.
    //
    // Re-blessing this is a DELIBERATE act: it means the panel's click surface
    // changed. Print the new value (the MESSAGE below) and read the diff before
    // pasting it in — a changed count or a reordered pair is exactly the bug this
    // exists to catch, not noise to silence.
    static const char* kGolden =
        "83:general;83:appearance;83:hotkeys;83:riders;85:-;83:rumble;94:-;83:helmet;140:-;83:director;71:-;83:updates;78:-;79:-;14:-;83:standings;14:-;83:map;14:-;83:radar;14:-;83:lap_log;14:-;83:ideal_lap;14:-;83:session_charts;14:-;83:telemetry;14:-;83:records;14:-;83:pitboard;14:-;83:session;14:-;83:timing;14:-;83:gap_bar;14:-;83:notices;14:-;83:event_log;14:-;83:friends;14:-;83:fmx;14:-;83:stats;14:-;83:performance;82:-;83:widgets;133:general.pb_scope;64:-;64:-;133:general.controller;87:-;86:-;133:general.auto_save;72:-;72:-;133:general.grid_snap;66:-;66:-;133:general.screen_clamp;67:-;67:-;133:general.steam_friends;133:general.web_server;75:-;75:-;133:general.web_port;76:-;77:-;133:general.auto_switch;80:-;80:-;133:general.copy_profile;10:-;9:-;11:-;133:general.reset_profile;12:-;133:general.reset_all;13:-;7:-;179:-;180:-;181:-;84:-;8:-;132:-;typecount=182;strings=123";

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
