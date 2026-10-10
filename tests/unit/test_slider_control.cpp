// ============================================================================
// tests/unit/test_slider_control.cpp
// The settings slider's two halves of the value <-> track mapping
// (hud/settings/settings_controls.h): where a value draws its knob, and which
// value a click or drag at a point on the track sets. A drag past either end
// must clamp, and a whole-number control must never be handed a fraction.
// ============================================================================
#include "doctest.h"
#include "hud/settings/settings_controls.h"

TEST_CASE("SliderControl: a position on the track snaps to the step and stays in range") {
    SliderControl scale;
    scale.lo = 0.1f; scale.hi = 3.0f; scale.step = 0.01f;
    CHECK(scale.valueAt(0.0f) == doctest::Approx(0.1f));
    CHECK(scale.valueAt(1.0f) == doctest::Approx(3.0f));
    CHECK(scale.valueAt(-0.5f) == doctest::Approx(0.1f));   // dragged past the left end
    CHECK(scale.valueAt(1.5f) == doctest::Approx(3.0f));    // ...and the right
    CHECK(scale.valueAt(0.5f) == doctest::Approx(1.55f));

    SliderControl rows;   // a whole-number count
    rows.lo = 3.0f; rows.hi = 30.0f; rows.step = 1.0f;
    const float v = rows.valueAt(0.37f);
    CHECK(v == doctest::Approx(static_cast<float>(static_cast<int>(v + 0.5f))));
}

TEST_CASE("SliderControl: a value draws at its fraction, and maps back to itself") {
    SliderControl opacity;   // lo/hi/step default to 0..1 in hundredths
    CHECK(opacity.fractionOf(0.0f) == doctest::Approx(0.0f));
    CHECK(opacity.fractionOf(0.25f) == doctest::Approx(0.25f));
    CHECK(opacity.fractionOf(2.0f) == doctest::Approx(1.0f));   // out of range clamps
    for (int i = 0; i <= 100; ++i) {
        const float value = static_cast<float>(i) / 100.0f;
        CHECK(opacity.valueAt(opacity.fractionOf(value)) == doctest::Approx(value));
    }

    SliderControl empty;   // a degenerate range draws at the left end
    empty.lo = 1.0f; empty.hi = 1.0f;
    CHECK(empty.fractionOf(1.0f) == doctest::Approx(0.0f));
}
