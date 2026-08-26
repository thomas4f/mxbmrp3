// ============================================================================
// tests/integration/tests/theme_panel_padding_test.cpp
// `[panel] padding-x/-y` -- the gap between a panel's border and its first glyph.
//
// WHAT THIS KEY IS. It was the last unreachable number in the panel box model: with
// every border at 0 and every card switch off, a panel still held its content two grid
// cells in on each axis, and nothing in any file could change that. The layout ini that
// once owned it was deleted (across every theme a user would pick, the number of layout
// keys set was zero), so it had become a compile-time constant that no theme could
// reach and that the mock could only report as a mystery.
//
// WHY THIS IS NOT ONE OF THE BOX-MODEL TESTS. Those were removed while the model is
// reworked because they pinned exact numbers that were
// deliberately in flux. Nothing here pins a number. Every case is RELATIVE -- unset
// equals the built-in, more is more, and below the border nothing happens -- so it
// survives the model changing around it, which is exactly the property that makes it
// worth having now rather than after.
//
// THE DEAD ZONE IS GONE, and the third case pins its absence — the reversal of what
// this case originally pinned, made deliberately. Under the old max-model composition
// (contentPaddingX() = max(base, frame + inner)) every padding value below the border's
// clearance rendered identically, a knob dead in part of its range that the box-model
// mock had to flag. A plan-based panel (core/panel_box.h; Standings is the one measured
// here) composes ADDITIVELY — content sits at border + padding — so each cell of
// padding moves the content column by exactly one cell, at any border size. The third
// case asserts that: equal, strictly positive steps under a [frame] 4 border.
//
// WHAT SURVIVES FROM THE OLD CONTRACT is the CLEARANCE INVARIANT, asserted across the
// whole registry (which still includes the annotated own-geometry holdouts on the
// legacy accessors): paddingV >= the panel's own frame border, whatever the key says.
// Additive composition satisfies it by construction (border + padding >= border); the
// legacy max/ceil form satisfied it by clamping. Content must never be drawn on the
// frame, under either composition — that is the property the key must not be able to
// break, and it is exactly what MXBMRP3_Test_PanelPadY reports both terms for.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

// The vertical padding every registered panel reports, summed. One number standing in
// for the whole sweep: this case is about whether a KEY reaches the panels at all, and
// which panel it reached is what the (removed) per-panel sweep was for.
double totalPaddingV(PluginHost& host) {
    double sum = 0.0;
    for (const auto& p : host.panelPadY()) sum += p.paddingV;
    return sum;
}

// The content column: the leftmost glyph a HUD draws.
//
// STANDINGS, not one of the centre-stack panels: those are a fixed character count wide
// and share one padding so they line up with each other (centerStackPaddingX), so their
// column does not track this key at all -- which is what the first draft of this case
// measured, reading the same 0.44 at every setting.
double leftmostGlyph(PluginHost& host, PluginHost::HudId id) {
    double x = 1e9;
    for (const auto& r : host.hudStringRows(id))
        if (!r.text.empty()) x = (std::min)(x, r.x);
    return x;
}

}  // namespace

TEST_CASE("[panel] padding-x/-y reaches the panels, and says so when it cannot") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_panel_pad\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE_MESSAGE(host.hasPanelPaddingKey(),
                    "MXBMRP3_Test_SetThemePanelPadding not exported (test build?)");
    REQUIRE(host.hasPanelPadY());
    REQUIRE(host.hasStringRows());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 1, .gap = 0 } });
    REQUIRE(host.setHudTitle("timing_hud", true));

    // Install a theme and optionally name the padding. A negative cell count is the
    // sentinel for "this axis follows the built-in", which is what a theme that never
    // mentions the key has.
    auto apply = [&](const std::string& tag, float frame, float x, float y) {
        host.installTheme(tag.c_str(), frame, /*cardBorder=*/1.0f,
                          /*titleBand=*/1, /*card=*/1);
        REQUIRE(host.setThemePanelPadding(x, y));
        host.draw();
    };

    // 1. UNSET FOLLOWS THE BUILT-IN, and is indistinguishable from naming its value.
    //
    // This is what the -1 sentinel buys over a copied default: the key is absent for
    // every shipped theme, so "absent" has to render exactly as it did before the key
    // existed. A default of 2.0f alongside the built-in would pass this today and stop
    // passing the moment somebody retuned the built-in -- which is the whole reason the
    // sentinel is there rather than a copy.
    // THE BUILT-IN'S VALUE, which this case has to spell to compare against and
    // which is therefore the one number in it that can rot -- exactly the failure
    // the paragraph above describes, and which this line committed: it said 2.0
    // while LayoutMetrics::boxPanelPadding was retuned to 1, so "unset renders as
    // its built-in" was being asserted against the wrong built-in. Keep it here,
    // named, so a retune fails ONE readable line rather than a bare literal.
    // TWO built-ins, one per axis, because the tree still has two panel paddings:
    // the box model's boxPanelPadding (which the measured HUDs use horizontally)
    // and the legacy panelPadding{X,Y}Cells basePaddingY still reads. They were
    // one number until boxPanelPadding was retuned to 1 and the legacy pair could
    // not follow (it pads in a unit that does not land on the lattice at 1 --
    // see layout_metrics.h). Spelled here rather than hidden behind one constant
    // so the split is visible until those panels are ported.
    constexpr float kBuiltInPadX = 1.0f;   // = LayoutMetrics::boxPanelPadding
    constexpr float kBuiltInPadY = 2.0f;   // = LayoutMetrics::panelPaddingYCells
    apply("pp_unset", 0.0f, -1.0f, -1.0f);
    const double unsetV = totalPaddingV(host);
    const double unsetX = leftmostGlyph(host, PluginHost::HUD_STANDINGS);
    apply("pp_spelled", 0.0f, kBuiltInPadX, kBuiltInPadY);
    const double spelledV = totalPaddingV(host);
    const double spelledX = leftmostGlyph(host, PluginHost::HUD_STANDINGS);
    INFO("unset paddingV " << unsetV << " against " << spelledV << " spelled at "
         << kBuiltInPadY << " cells");
    CHECK(std::abs(unsetV - spelledV) < 1e-6);
    CHECK(std::abs(unsetX - spelledX) < 1e-6);

    // 2. MORE IS MORE, on both axes, with the borders at zero so nothing else competes.
    //    Asserted as strictly-increasing across a sweep rather than against a computed
    //    number: the arithmetic is what the (removed) padding sweep pinned, and pinning
    //    it here would just be the same frozen number in a new file.
    double prevV = spelledV, prevX = spelledX;
    for (float cells : { 3.0f, 4.0f, 6.0f }) {
        apply("pp_up" + std::to_string(static_cast<int>(cells)), 0.0f, cells, cells);
        const double v = totalPaddingV(host), x = leftmostGlyph(host, PluginHost::HUD_STANDINGS);
        INFO("padding " << cells << " cells: paddingV " << v << " (was " << prevV
             << "), content column " << x << " (was " << prevX << ")");
        CHECK(v > prevV);
        CHECK(x > prevX);
        prevV = v; prevX = x;
    }

    // 3. UNDER A THICK BORDER the key stays LIVE — the additive model has no dead
    //    zone. Read at [frame] 4 with a 1-cell card, where the old max-model made
    //    0, 1 and 2 cells render identically: each added cell now moves the plan-based
    //    content column by exactly one cell (equal, strictly positive steps — the
    //    engine's flip property, measured through the real DLL).
    apply("pp_add0", 4.0f, 0.0f, 0.0f);
    const double add0X = leftmostGlyph(host, PluginHost::HUD_STANDINGS);
    apply("pp_add1", 4.0f, 1.0f, 1.0f);
    const double add1X = leftmostGlyph(host, PluginHost::HUD_STANDINGS);
    apply("pp_add2", 4.0f, 2.0f, 2.0f);
    const double add2X = leftmostGlyph(host, PluginHost::HUD_STANDINGS);
    INFO("[frame] 4: column at padding 0/1/2 = " << add0X << " / " << add1X
         << " / " << add2X);
    CHECK(add1X > add0X + 1e-6);
    CHECK(std::abs((add2X - add1X) - (add1X - add0X)) < 1e-6);
    // The clearance invariant, across the whole registry (including the
    // own-geometry holdouts still on the legacy accessors): whatever the key
    // says, every panel clears its own frame — the one thing the key must not
    // be able to break, under either composition.
    for (float cells : { 0.0f, 1.0f, 2.0f }) {
        apply("pp_clr" + std::to_string(static_cast<int>(cells)), 4.0f, cells, cells);
        INFO("[frame] 4, padding " << cells << " cells");
        for (const auto& p : host.panelPadY()) {
            INFO("  " << p.name << ": paddingV " << p.paddingV
                 << " against frame border " << p.frameMarginY);
            CHECK(p.paddingV >= p.frameMarginY - 1e-6);
        }
    }

    host.setHudTitle("timing_hud", false);
    host.clearTheme();
}

TEST_CASE("a panel's vertical padding lands on whole cells, so both its ends can spend it") {
    // WHAT THIS UNDERWRITES. panelContentY() places content at dim.paddingV and
    // panelHeight() spends paddingV at both ends, so content is centred in its box BY
    // CONSTRUCTION -- but only if paddingV is the same quantity at both, which it now is,
    // and only if it lands on the grid, which is what this asserts.
    //
    // The bug it replaced: the TOP spent frameBorderY + cardBorderY (a raw sum) while the
    // bottom spent paddingV, which is that sum with its shortfall CEILED. The bottom
    // exceeded the top by the ceil remainder and content sat high in its card -- 6.3px at
    // [card] 1, 8.4 at 2, 10.6 at 3, then 0.0 at 4, because a card border is 0.833 of a
    // row-cell per unit while the ceil only ever adds whole ones. A sawtooth, which is why
    // the amount read as arbitrary.
    //
    // TWO PROPERTIES, both of which the unified inset needs and neither of which is a
    // frozen number:
    //   paddingV >= frameMarginY   content can never be drawn on the frame. This is the
    //                              clearance the first case already guards; repeated over
    //                              the CARD sweep, which it does not cover.
    //   paddingV is whole cells    so top + content + bottom lands on the lattice and
    //                              panelHeight()'s outer ceil has nothing left to round --
    //                              which is what stops the slack reappearing at the bottom
    //                              and undoing the symmetry.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\panel_pad_cells\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasPanelPadY());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 1, .gap = 0 } });

    // One cell of screen height, read from the plugin's LIVE lattice -- this used
    // to be the hardcoded shipped value, with a note wishing for exactly this hook.
    REQUIRE(host.hasLayoutCells());
    const double kCellH = host.layoutCells().cellH;

    for (float frame : { 0.0f, 1.0f, 2.0f, 4.0f }) {
        for (float card : { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f }) {
            host.installTheme(("pc" + std::to_string(static_cast<int>(frame))
                                    + std::to_string(static_cast<int>(card))).c_str(),
                              frame, card, /*titleBand=*/1, /*card=*/1);
            host.draw();
            for (const auto& p : host.panelPadY()) {
                INFO("[frame] " << frame << " [card] " << card << "  " << p.name
                     << ": paddingV " << p.paddingV << ", frame border " << p.frameMarginY
                     << ", in cells " << (p.paddingV / kCellH));
                CHECK(p.paddingV >= p.frameMarginY - 1e-6);
                const double cells = p.paddingV / kCellH;
                CHECK(std::abs(cells - std::round(cells)) < 0.01);
            }
        }
    }
    host.clearTheme();
}
