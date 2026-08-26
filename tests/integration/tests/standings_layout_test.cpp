// ============================================================================
// tests/integration/tests/standings_layout_test.cpp
// StandingsHud places row text through TWO paths: rebuildRenderData (a full
// rebuild) and rebuildLayout (the drag fast path, which repositions existing
// primitives without rebuilding them). Anything that adjusts a column's placement
// has to go through both, or the HUD renders one way at rest and another way while
// it is being moved.
//
// This file pins that agreement for the race-number plate, which is the only
// column drawn inside a tight box and so the only one where a few pixels show.
//
// It also pins that the number is actually CENTRED on its plate, which this file
// did not do and should have. The nudge that centred it first went into the rebuild
// path alone (centred at rest, jumped high mid-drag), so the test written then asked
// only whether the two paths AGREE. When glyph centring later moved into addString(),
// the number received the offset twice and sat on the plate's bottom edge -- both
// paths agreeing about it perfectly, and this file green throughout. An agreement
// test needs a correctness test beside it.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cmath>

// The race number stays centred in its plate when the HUD is MOVED.
//
// StandingsHud has two paths that place row text: rebuildRenderData (full rebuild)
// and rebuildLayout (the drag fast path, which repositions existing primitives
// without rebuilding them). The plate number needs a vertical nudge that the other
// columns do not -- it is the only column drawn inside a tight box -- and putting
// that nudge in the rebuild path alone left the drag path placing it at the plain
// row baseline. The number was centred at rest and jumped high while dragging.
//
// A screenshot cannot catch this: both frames are individually plausible. The
// property is that the two paths AGREE, so the inset is read after each.
// getColumnTextY() is the shared helper both now call, mirroring getColumnTextX().
TEST_CASE("standings: the plate number stays centred when the HUD is moved") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasPlateHooks(), "plate hooks not exported (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\standings_plate_move\\");
    host.session(/*session=*/1, /*numLaps=*/10);

    for (int n : {10, 22, 7, 3}) {
        char nm[8];
        snprintf(nm, sizeof(nm), "R%d", n);
        host.addEntry(n, nm);
    }
    // Classify, not just addEntry: an unclassified grid renders PLACEHOLDER rows,
    // and a placeholder draws no plate at all — so every inset would read NOT_FOUND
    // and the case would be vacuous. The REQUIRE below is what caught exactly that.
    host.classify(1, 60000, {
        { .num = 10, .best = 92000, .laps = 2 },
        { .num = 22, .best = 93000, .laps = 2, .gap = 1200 },
        { .num = 7,  .best = 94000, .laps = 2, .gap = 2400 },
        { .num = 3,  .best = 95000, .laps = 2, .gap = 3600 },
    });
    host.stSetVisible(true);
    host.stSetOffset(0.0f, 0.0f);
    host.draw();

    const float atRest = host.stPlateInsetY(0);
    REQUIRE_MESSAGE(atRest > PluginHost::kPlateInsetNotFound + 1.0f,
                    "no plate found on row 0 — the scene has no number plate, so this "
                    "case cannot discriminate");

    // Move the HUD. This runs rebuildLayout, NOT rebuildRenderData: the primitives
    // are repositioned in place, which is where the offset used to be dropped.
    host.stSetOffset(0.10f, 0.05f);
    host.draw();
    const float afterMove = host.stPlateInsetY(0);

    // Absolute tolerance, not doctest::Approx: the inset sits near zero, where a
    // RELATIVE epsilon collapses to almost nothing and the check would fail on
    // ordinary float noise instead of on the bug.
    CHECK_MESSAGE(std::fabs(afterMove - atRest) < 0.01f,
                  "the race number moved inside its plate when the HUD was dragged — "
                  "the layout fast path is placing it at the row baseline again "
                  "instead of going through getColumnTextY()");

    // And back, so a one-way fluke cannot pass.
    host.stSetOffset(0.0f, 0.0f);
    host.draw();
    CHECK_MESSAGE(std::fabs(host.stPlateInsetY(0) - atRest) < 0.01f,
                  "the race number did not return to its original inset after the HUD "
                  "was moved back");

    host.shutdown();
}

// The race number is centred on its plate -- the property the agreement case above
// cannot see, since two paths can agree on a wrong placement.
//
// The tolerance is 2% of plate height, ~0.4px on the shipped 19px plate. It is tight
// on purpose: the failure this pins is a WHOLE half-row of offset (+0.092, ~9% of the
// plate, the number sitting on its bottom edge), and the one before that was the same
// magnitude negative. Anything that lands between those is a genuine regression too.
TEST_CASE("standings: the race number is centred on its plate") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasPlateHooks(), "plate hooks not exported (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\standings_plate_centre\\");
    host.session(/*session=*/1, /*numLaps=*/10);

    for (int n : {10, 22, 7, 3}) {
        char nm[8];
        snprintf(nm, sizeof(nm), "R%d", n);
        host.addEntry(n, nm);
    }
    host.classify(1, 60000, {
        { .num = 10, .best = 92000, .laps = 2 },
        { .num = 22, .best = 93000, .laps = 2, .gap = 1200 },
        { .num = 7,  .best = 94000, .laps = 2, .gap = 2400 },
        { .num = 3,  .best = 95000, .laps = 2, .gap = 3600 },
    });
    host.stSetVisible(true);
    host.stSetOffset(0.0f, 0.0f);
    host.draw();

    // Every plate row, not just the first: a per-row term (an animation offset, a
    // header/row-index mixup) would leave row 0 right and the rest wrong.
    for (int row = 0; row < 4; ++row) {
        const float off = host.stPlateInsetY(row);
        REQUIRE_MESSAGE(off > PluginHost::kPlateInsetNotFound + 1.0f,
                        "no plate found on row " << row << " -- the scene cannot "
                        "discriminate");
        CHECK_MESSAGE(std::fabs(off) < 0.02f,
                      "race number off-centre on its plate by " << (off * 100.0f)
                      << "% of plate height (row " << row << "); positive is low. A "
                      "value near +9% means the row-centring offset is being applied "
                      "twice, near -9% that it is not being applied at all.");
    }

    host.shutdown();
}
