// ============================================================================
// tests/unit/gauge_square_test.cpp
// A gauge's dial stays CIRCULAR while its box lands on the cell lattice.
//
// Restored after the box-model rework removed it
// in 8bf6610. The one-line spec there ("a gauge's box staying square once the
// theme's borders are added to its padding") reads as though the BOX is what
// must stay square; the code says the opposite, and tacho_widget.cpp states it
// at the call: "The BOX lands on the lattice, the DIAL keeps its size --
// stretching a circle to whole cells on both axes is an ellipse." So the box is
// free to be oblong, and the dial is what must not move. The cases below pin the
// code's version.
//
// SCOPE, stated because it is smaller than the original: the gauges have NOT yet
// migrated onto panel_box.h (tacho/speedo still call addBackgroundQuad directly),
// so the half of this that needs a themed panel -- that the dial is drawn at its
// own size rather than stretched to the fitted box, once a theme's borders widen
// the padding -- belongs to the integration row that is still out per the spec.
// What is here is the pure arithmetic underneath it, which does not wait.
//
// KNOWN LIMITATION, so nobody reads more into a green run than it earns:
// dialFor() below REPLICATES the two lines tacho_widget/speedo_widget use rather
// than calling them, because those live inside a BaseHud-derived Draw() that a
// pure unit TU cannot link. So this pins the RELATIONSHIP, not the widgets --
// change the formula in both widgets and these cases still pass. Closing that
// needs the dial sizing extracted to a pure header (the blue_flag_detect.h /
// proximity_tuning.h pattern) or the integration row that is still out; until
// then the widget side is guarded only by the ellipse being obvious on screen.
// ============================================================================
#include "doctest.h"

#include <cmath>

#include "core/layout_metrics.h"
#include "core/plugin_constants.h"

namespace {

LayoutMetrics derived() { LayoutMetrics m; m.derive(); return m; }

// The dial's normalized dimensions, exactly as tacho_widget/speedo_widget
// compute them from their shared DIAL_SIZE base.
struct Dial { float w, h; };
Dial dialFor(float base, float scale) {
    const float size = base * scale;
    return { size / PluginConstants::UI_ASPECT_RATIO, size };
}

}  // namespace

TEST_CASE("gauge: the dial is circular in PIXELS at every scale") {
    // Normalized coordinates are not isotropic: equal w and h would draw an
    // ellipse 16:9 wide. Dividing the width by the aspect is what makes the
    // drawn dial round, and it has to hold for any user scale, not just 1.0.
    for (float scale : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.25f}) {
        const Dial d = dialFor(0.15f, scale);
        CHECK(d.w * PluginConstants::UI_ASPECT_RATIO == doctest::Approx(d.h));
    }
}

TEST_CASE("gauge: the lattice fit never shrinks the box below the dial") {
    // fitPanelToGrid is BaseHud's two-liner over these: ceil both axes onto the
    // lattice, then centre the content in the slack. If a ceil could round DOWN,
    // the centring padding would go negative and the dial would overhang its own
    // box -- so this is the property the centring depends on.
    const LayoutMetrics L = derived();

    for (float scale : {0.5f, 1.0f, 1.7f, 2.0f}) {
        const Dial d = dialFor(0.15f, scale);
        const float boxW = L.ceilX(d.w);
        const float boxH = L.ceilY(d.h);

        CHECK(boxW >= d.w);
        CHECK(boxH >= d.h);

        // ...so the centring slack is non-negative on both axes.
        CHECK((boxW - d.w) * 0.5f >= 0.0f);
        CHECK((boxH - d.h) * 0.5f >= 0.0f);
    }
}

TEST_CASE("gauge: the fitted BOX is free to be oblong -- that is the design") {
    // The counter-case, written down so a future change that "fixes" the box
    // back to square has to delete an assertion that says why it isn't.
    //
    // The grid is 10.56 x 12.672px at 1080p, so a circle's bounding box cannot
    // be whole cells on both axes AND stay square. The resolution is that the
    // box quantises and the dial does not.
    const LayoutMetrics L = derived();
    const Dial d = dialFor(0.15f, 1.0f);

    const float boxW = L.ceilX(d.w);
    const float boxH = L.ceilY(d.h);

    // The box, in pixels, is NOT square...
    const float boxAspect = (boxW * PluginConstants::UI_ASPECT_RATIO) / boxH;
    CHECK(boxAspect != doctest::Approx(1.0f));

    // ...while the dial inside it still is.
    CHECK(d.w * PluginConstants::UI_ASPECT_RATIO == doctest::Approx(d.h));
}

TEST_CASE("gauge: stretching the dial to its fitted box is the ellipse bug") {
    // The regression this file exists for. Drawing the dial at the BOX's
    // dimensions instead of its own is the one-character change that turns every
    // gauge into an ellipse, and it is invisible in any assertion about position.
    const LayoutMetrics L = derived();
    const Dial d = dialFor(0.15f, 1.0f);

    const float stretchedW = L.ceilX(d.w);
    const float stretchedH = L.ceilY(d.h);
    const float stretchedAspect =
        (stretchedW * PluginConstants::UI_ASPECT_RATIO) / stretchedH;

    // Measurably off round -- not a rounding whisker.
    CHECK(std::fabs(stretchedAspect - 1.0f) > 0.01f);
}
